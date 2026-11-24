/*
 * LD_PRELOAD shim for RCD 330 on QEMU:
 * 1. Emulates /sys/class/gpio entries in /tmp/gpio_emu so libHAL.so GPIO calls
 *    succeed without EBUSY from kernel-claimed CS pins.
 * 2. Bridges SPI IPCL transfers to rcd330-vip MMIO (0x021fc000), uses the
 *    real kernel /dev/v850srq device (GPIO2_8 IRQ 0x128) when available, and
 *    wakes SIGIO cleanly on outbound tx_msg_id frames, VIP RX queue, and
 *    single-shot ACK completion without leaving libHAL stuck in ACK state.
 * 3. Virtualizes /dev/ttymxc4 for opt/bt (no physical ALPS BT chip in QEMU)
 *    so SDMA RX DMA does not time out and BT_VER from /pss1/stdplus.ini is
 *    reported to opt/pwr_agent via VMF(0x0B, 0x04) [0x04, hi, lo].
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t g_shim_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_v850srq_fd = -1;
static int g_v850srq_is_real = 0;
static int g_spidev_fd = -1;
static int g_srq_thread_started = 0;
static int g_bt_ver_thread_started = 0;
static volatile uint32_t *g_vip_mmio = NULL;
static uint8_t g_rx_stash[512];
static uint16_t g_rx_stash_len = 0;
static uint8_t g_tx_hdr[2];
static uint16_t g_tx_expect = 0;
static int g_expect_ack = 0;
static int g_need_ack_sigio = 0;
static int g_tx_pending = 0;
static int g_tx_msg_id = -1;

int shmget(key_t key, size_t size, int shmflg) {
    static int (*real_shmget)(key_t, size_t, int) = NULL;
    if (!real_shmget) real_shmget = dlsym(RTLD_NEXT, "shmget");
    int r = real_shmget(key, size, shmflg);
    if (r < 0 && errno == ENOENT && !(shmflg & IPC_CREAT)) {
        r = real_shmget(key, size, shmflg | IPC_CREAT);
    }
    return r;
}

int msgget(key_t key, int msgflg) {
    static int (*real_msgget)(key_t, int) = NULL;
    if (!real_msgget) real_msgget = dlsym(RTLD_NEXT, "msgget");
    int r = real_msgget(key, msgflg);
    if (r < 0 && errno == EEXIST && (msgflg & IPC_EXCL)) {
        r = real_msgget(key, msgflg & ~IPC_EXCL);
    }
    /* Do not prematurely create Android Auto SysV queues (100/300) while AAP_Application polls for aap_service */
    if (r < 0 && errno == ENOENT && !(msgflg & IPC_CREAT) &&
        key != 100 && key != 300) {
        r = real_msgget(key, msgflg | IPC_CREAT | 0666);
    }
    if (r >= 0 && msgflg == 0xfad && __atomic_load_n(&g_tx_msg_id, __ATOMIC_ACQUIRE) < 0) {
        /* First msgget(..., 0xfad) in libHAL spi_ipcl_init is tx_msg_id */
        __atomic_store_n(&g_tx_msg_id, r, __ATOMIC_RELEASE);
    }
    return r;
}

int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg) {
    static int (*real_msgsnd)(int, const void *, size_t, int) = NULL;
    if (!real_msgsnd) real_msgsnd = dlsym(RTLD_NEXT, "msgsnd");
    int r = real_msgsnd(msqid, msgp, msgsz, msgflg);
    if (r == 0 && msgsz == 0x404) {
        /* libHAL spi_ipcl_write enqueues 0x404-byte IPCL TX frames onto tx_msg_id */
        if (__atomic_load_n(&g_tx_msg_id, __ATOMIC_ACQUIRE) < 0) {
            __atomic_store_n(&g_tx_msg_id, msqid, __ATOMIC_RELEASE);
        }
        if (msqid == __atomic_load_n(&g_tx_msg_id, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&g_tx_pending, 1, __ATOMIC_RELEASE);
        }
    }
    return r;
}

static const char *map_gpio_path(const char *path, char *buf, size_t sz) {
    if (path && strncmp(path, "/sys/class/gpio/", 16) == 0) {
        const char *rest = path + 16;
        mkdir("/tmp/gpio_emu", 0777);
        /* Flatten slashes so /sys/class/gpio/gpio83/value -> /tmp/gpio_emu/gpio83_value */
        char flat[96];
        size_t j = 0;
        for (size_t i = 0; rest[i] && j < sizeof(flat) - 1; i++) {
            flat[j++] = (rest[i] == '/') ? '_' : rest[i];
        }
        flat[j] = '\0';
        snprintf(buf, sz, "/tmp/gpio_emu/%s", flat);
        return buf;
    }
    return path;
}

struct qemu_spi_ioc_transfer {
    uint64_t tx_buf;
    uint64_t rx_buf;
    uint32_t len;
    uint32_t speed_hz;
    uint16_t delay_usecs;
    uint8_t bits_per_word;
    uint8_t cs_change;
    uint32_t pad;
};

static volatile uint32_t *get_vip_mmio(int (*real_open)(const char *, int, ...)) {
    volatile uint32_t *cur = __atomic_load_n(&g_vip_mmio, __ATOMIC_ACQUIRE);
    if (cur) return cur;
    pthread_mutex_lock(&g_shim_lock);
    cur = __atomic_load_n(&g_vip_mmio, __ATOMIC_ACQUIRE);
    if (!cur) {
        int mfd = real_open("/dev/mem", O_RDWR | O_SYNC);
        if (mfd >= 0) {
            void *p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0x021fc000);
            close(mfd);
            if (p != MAP_FAILED) {
                cur = (volatile uint32_t *)p;
                __atomic_store_n(&g_vip_mmio, cur, __ATOMIC_RELEASE);
            }
        }
    }
    pthread_mutex_unlock(&g_shim_lock);
    return cur;
}

static void *v850srq_sigio_thread(void *arg) {
    int (*real_open)(const char *, int, ...) = (int (*)(const char *, int, ...))arg;
    volatile uint32_t *mmio = get_vip_mmio(real_open);
    for (;;) {
        usleep(20000);
        int fire = 0;

        /* 1. Single-shot ACK completion (libHAL DAT_0001936c: 1 -> 0) */
        if (__atomic_exchange_n(&g_need_ack_sigio, 0, __ATOMIC_ACQ_REL) > 0) {
            fire = 1;
        }
        /* 2. Pending VIP -> SoC RX frame in rcd330-vip queue */
        if (!fire && mmio && (mmio[0x18 / 4] & 1u)) {
            fire = 1;
        }
        /* 3. Pending outbound SoC -> VIP TX frame in libHAL tx_msg_id */
        if (!fire && __atomic_exchange_n(&g_tx_pending, 0, __ATOMIC_ACQ_REL)) {
            fire = 1;
        }
        if (!fire) {
            int qid = __atomic_load_n(&g_tx_msg_id, __ATOMIC_ACQUIRE);
            if (qid >= 0) {
                struct msqid_ds ds;
                if (msgctl(qid, IPC_STAT, &ds) == 0 && ds.msg_qnum > 0) {
                    fire = 1;
                }
            }
        }
        if (fire) {
            kill(getpid(), SIGIO);
        }
    }
    return NULL;
}

/* 16-byte 32-bit ARM Linux struct input_event */
struct arm_input_event {
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

static int g_event0_wfd = -1;
static int g_event0_thread_started = 0;

static void *event0_poll_thread(void *arg) {
    int (*real_open)(const char *, int, ...) = (int (*)(const char *, int, ...))arg;
    volatile uint32_t *mmio = get_vip_mmio(real_open);
    if (!mmio) return NULL;
    for (;;) {
        uint32_t st = mmio[0x18 / 4];
        if (st & 2u) {
            uint32_t ev = mmio[0x20 / 4];
            while (ev & 0x80000000u) {
                struct arm_input_event ie;
                memset(&ie, 0, sizeof(ie));
                ie.type = 1; /* EV_KEY */
                ie.code = (uint16_t)((ev >> 8) & 0xffffu);
                ie.value = (int32_t)(ev & 0xffu);
                int wfd = __atomic_load_n(&g_event0_wfd, __ATOMIC_ACQUIRE);
                if (wfd >= 0) {
                    ssize_t w = write(wfd, &ie, sizeof(ie));
                    (void)w;
                }
                ev = mmio[0x20 / 4];
            }
        }
        usleep(20000);
    }
    return NULL;
}

static uint16_t read_bt_ver_from_ini(void) {
    FILE *fp = fopen("/pss1/stdplus.ini", "r");
    if (!fp) fp = fopen("/opt/stdplus.ini", "r");
    uint16_t ver = 0xC210u;
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            char *p = strstr(line, "BT_VER");
            if (p) {
                char *eq = strchr(p, '=');
                if (eq) {
                    eq++;
                    while (*eq == ' ' || *eq == '\t') eq++;
                    unsigned long parsed = strtoul(eq, NULL, 16);
                    if (parsed > 0 && parsed <= 0xFFFFul) {
                        ver = (uint16_t)parsed;
                    }
                    break;
                }
            }
        }
        fclose(fp);
    }
    return ver;
}

static void *bt_version_announce_thread(void *arg) {
    (void)arg;
    typedef int (*fn_conn_t)(const char *, int, int, int, int);
    typedef int (*fn_send_t)(int, const void *);
    typedef int (*fn_disc_t)(int);
    fn_conn_t fn_conn = (fn_conn_t)dlsym(RTLD_DEFAULT, "nw_vmf_connect");
    fn_send_t fn_send = (fn_send_t)dlsym(RTLD_DEFAULT, "nw_vmf_send_basic");
    fn_disc_t fn_disc = (fn_disc_t)dlsym(RTLD_DEFAULT, "nw_vmf_disconnect");
    if (!fn_conn || !fn_send) {
        return NULL;
    }

    uint16_t bt_ver = read_bt_ver_from_ini();
    uint8_t msg[64];
    memset(msg, 0, sizeof(msg));
    msg[0] = 0x02;
    msg[0x14] = 0x0b; /* POWER_AGENT_IN */
    msg[0x15] = 0x04; /* Sub 4, case 4: BT version */
    msg[0x18] = 3;
    msg[0x19] = 0;
    msg[0x1a] = 0x04;
    msg[0x1b] = (uint8_t)((bt_ver >> 8) & 0xffu);
    msg[0x1c] = (uint8_t)(bt_ver & 0xffu);

    /* Wait briefly for opt/pwr_agent and opt/hmi to subscribe, then announce BT_VER */
    for (int attempt = 0; attempt < 5; attempt++) {
        sleep(2);
        int fd = fn_conn("bt_ver_emu", 0, 0, 0x1e, 0x280);
        if (fd >= 1) {
            fn_send(fd, msg);
            if (fn_disc) fn_disc(fd);
            /* Re-announce once more after HMI finishes subscribing to 0x0c:0x0a */
            if (attempt >= 1) {
                break;
            }
        }
    }
    return NULL;
}

static int open_virtual_bt_uart(void) {
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pthread_mutex_lock(&g_shim_lock);
    if (!g_bt_ver_thread_started) {
        g_bt_ver_thread_started = 1;
        pthread_t th;
        if (pthread_create(&th, NULL, bt_version_announce_thread, NULL) == 0) {
            pthread_detach(th);
        }
    }
    pthread_mutex_unlock(&g_shim_lock);
    /* Keep write-end open so read() blocks cleanly instead of spinning on EOF */
    (void)pfd[1];
    return pfd[0];
}

static int open_virtual_event0(int (*real_open)(const char *, int, ...)) {
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pthread_mutex_lock(&g_shim_lock);
    int old_wfd = __atomic_exchange_n(&g_event0_wfd, pfd[1], __ATOMIC_ACQ_REL);
    if (old_wfd >= 0) {
        close(old_wfd);
    }
    if (!g_event0_thread_started) {
        g_event0_thread_started = 1;
        pthread_t th;
        if (pthread_create(&th, NULL, event0_poll_thread, (void *)real_open) == 0) {
            pthread_detach(th);
        }
    }
    pthread_mutex_unlock(&g_shim_lock);
    fprintf(stderr, "hal_shim: virtual /dev/event0 pipe -> fd=%d\n", pfd[0]);
    return pfd[0];
}

static int open_v850srq(int (*real_open)(const char *, int, ...), int flags) {
    int fd = real_open("/dev/v850srq", flags);
    if (fd >= 0) {
        g_v850srq_fd = fd;
        g_v850srq_is_real = 1;
        return fd;
    }
    int pfd[2];
    if (pipe(pfd) == 0) {
        g_v850srq_fd = pfd[0];
        g_v850srq_is_real = 0;
        return pfd[0];
    }
    return -1;
}

int ioctl(int fd, unsigned long req, ...) {
    static int (*real_ioctl)(int, unsigned long, ...) = NULL;
    static int (*real_open)(const char *, int, ...) = NULL;
    if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    /* Intercept SPI_IOC_MESSAGE(N) on spidev */
    if (fd >= 0 && fd == g_spidev_fd && ((req & 0xff00ffffUL) == 0x40006b00UL) && arg) {
        volatile uint32_t *mmio = get_vip_mmio(real_open);
        if (mmio) {
            struct qemu_spi_ioc_transfer *tr = (struct qemu_spi_ioc_transfer *)arg;
            const uint8_t *tx = (const uint8_t *)(uintptr_t)tr->tx_buf;
            uint8_t *rx = (uint8_t *)(uintptr_t)tr->rx_buf;

            if (tr->len == 2) {
                g_tx_hdr[0] = tx ? tx[0] : 0;
                g_tx_hdr[1] = tx ? tx[1] : 0;
                g_tx_expect = ((uint16_t)g_tx_hdr[0] << 8) | g_tx_hdr[1];
                g_expect_ack = (g_tx_expect == 0x0005 || g_tx_expect == 0x0500);
                if (rx) {
                    rx[0] = 0;
                    rx[1] = 0;
                    if (g_expect_ack) {
                        rx[0] = 0x00;
                        rx[1] = 0x05;
                        g_rx_stash_len = 0;
                    } else {
                        uint32_t n = mmio[0x10 / 4];
                        if (n >= 2 && n <= sizeof(g_rx_stash)) {
                            for (uint32_t i = 0; i < n; i++) {
                                g_rx_stash[i] = (uint8_t)mmio[0x14 / 4];
                            }
                            g_rx_stash_len = (uint16_t)n;
                            rx[0] = g_rx_stash[0];
                            rx[1] = g_rx_stash[1];
                        } else {
                            g_rx_stash_len = 0;
                        }
                        if (g_tx_expect == 0 && g_rx_stash_len == 0) {
                            /* Empty newframe in libHAL (0x13854:141) still transitions DAT_0001936c -> 1 */
                            __atomic_store_n(&g_need_ack_sigio, 1, __ATOMIC_RELEASE);
                        }
                    }
                }
            } else {
                if (g_expect_ack) {
                    /* Completing ACK transitions libHAL DAT_0001936c: 1 -> 0 */
                    g_expect_ack = 0;
                    __atomic_store_n(&g_need_ack_sigio, 0, __ATOMIC_RELEASE);
                    if (rx && tr->len > 0) {
                        memset(rx, 0, tr->len);
                        if (tr->len >= 3) {
                            rx[0] = 0x01; rx[1] = 0x23; rx[2] = 0x48;
                        }
                    }
                } else {
                    if (tx && g_tx_expect >= 6 && g_tx_expect <= (uint32_t)tr->len + 2) {
                        mmio[0x00 / 4] = 0;
                        mmio[0x04 / 4] = g_tx_hdr[0];
                        mmio[0x04 / 4] = g_tx_hdr[1];
                        for (uint32_t i = 0; i < g_tx_expect - 2; i++) {
                            mmio[0x04 / 4] = tx[i];
                        }
                        mmio[0x08 / 4] = 1;
                    } else if (tx && tr->len >= 6) {
                        uint16_t flen = ((uint16_t)tx[0] << 8) | tx[1];
                        if (flen >= 6 && flen <= tr->len) {
                            mmio[0x00 / 4] = 0;
                            for (uint16_t i = 0; i < flen; i++) {
                                mmio[0x04 / 4] = tx[i];
                            }
                            mmio[0x08 / 4] = 1;
                        }
                    }
                    if (rx && tr->len > 0) {
                        memset(rx, 0, tr->len);
                        if (g_rx_stash_len > 2) {
                            uint16_t rem = g_rx_stash_len - 2;
                            uint16_t cp = (tr->len < rem) ? (uint16_t)tr->len : rem;
                            memcpy(rx, &g_rx_stash[2], cp);
                            g_rx_stash_len = 0;
                        }
                    }
                    /* Exactly 1 ACK SIGIO returns libHAL DAT_0001936c from 1 -> 0 */
                    __atomic_store_n(&g_need_ack_sigio, 1, __ATOMIC_RELEASE);
                }
                g_tx_expect = 0;
            }
            return (int)tr->len;
        }
    }
    return real_ioctl(fd, req, arg);
}

int fcntl(int fd, int cmd, ...) {
    static int (*real_fcntl)(int, int, ...) = NULL;
    if (!real_fcntl) real_fcntl = dlsym(RTLD_NEXT, "fcntl");
    va_list ap;
    va_start(ap, cmd);
    unsigned long arg = va_arg(ap, unsigned long);
    va_end(ap);
    if (fd >= 0 && fd == g_v850srq_fd && cmd == F_SETFL && (arg & O_ASYNC)) {
        static int (*real_open)(const char *, int, ...) = NULL;
        if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
        pthread_mutex_lock(&g_shim_lock);
        if (!g_srq_thread_started) {
            g_srq_thread_started = 1;
            pthread_t th;
            if (pthread_create(&th, NULL, v850srq_sigio_thread, (void *)real_open) == 0) {
                pthread_detach(th);
            }
        }
        pthread_mutex_unlock(&g_shim_lock);
        if (g_v850srq_is_real) {
            return real_fcntl(fd, cmd, arg);
        }
        return 0;
    }
    if (fd >= 0 && fd == g_v850srq_fd && !g_v850srq_is_real) {
        return 0;
    }
    return real_fcntl(fd, cmd, arg);
}

int close(int fd) {
    static int (*real_close)(int) = NULL;
    if (!real_close) real_close = dlsym(RTLD_NEXT, "close");
    if (fd >= 0) {
        if (fd == g_v850srq_fd) {
            g_v850srq_fd = -1;
            g_v850srq_is_real = 0;
        }
        if (fd == g_spidev_fd) {
            g_spidev_fd = -1;
        }
    }
    return real_close(fd);
}

static int open_gpio_emu(int (*real_open_fn)(const char *, int, ...), const char *mapped_path) {
    int fd = real_open_fn(mapped_path, O_RDWR | O_CREAT, 0666);
    if (fd >= 0 && strstr(mapped_path, "gpio44_value")) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size == 0) {
            ssize_t w = write(fd, "1\n", 2);
            (void)w;
            lseek(fd, 0, SEEK_SET);
        }
    }
    return fd;
}

int open(const char *pathname, int flags, ...) {
    static int (*real_open)(const char *, int, ...) = NULL;
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    char buf[128];
    const char *p = map_gpio_path(pathname, buf, sizeof(buf));
    if (p != pathname) {
        return open_gpio_emu(real_open, p);
    }
    if (strcmp(pathname, "/dev/event0") == 0) {
        return open_virtual_event0(real_open);
    }
    if (strcmp(pathname, "/dev/v850srq") == 0) {
        return open_v850srq(real_open, flags);
    }
    if (strcmp(pathname, "/dev/ttymxc4") == 0) {
        return open_virtual_bt_uart();
    }
    int fd;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        int mode = va_arg(ap, int);
        va_end(ap);
        fd = real_open(pathname, flags, mode);
    } else {
        fd = real_open(pathname, flags);
    }
    if ((strcmp(pathname, "/dev/spidev0.0") == 0 || strcmp(pathname, "/dev/spidev1.0") == 0) && fd >= 0) {
        fprintf(stderr, "hal_shim: opened %s -> fd=%d\n", pathname, fd);
        g_spidev_fd = fd;
    }
    return fd;
}

int open64(const char *pathname, int flags, ...) {
    static int (*real_open64)(const char *, int, ...) = NULL;
    if (!real_open64) real_open64 = dlsym(RTLD_NEXT, "open64");
    char buf[128];
    const char *p = map_gpio_path(pathname, buf, sizeof(buf));
    if (p != pathname) {
        return open_gpio_emu(real_open64, p);
    }
    if (strcmp(pathname, "/dev/event0") == 0) {
        return open_virtual_event0(real_open64);
    }
    if (strcmp(pathname, "/dev/v850srq") == 0) {
        return open_v850srq(real_open64, flags);
    }
    if (strcmp(pathname, "/dev/ttymxc4") == 0) {
        return open_virtual_bt_uart();
    }
    int fd;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        int mode = va_arg(ap, int);
        va_end(ap);
        fd = real_open64(pathname, flags, mode);
    } else {
        fd = real_open64(pathname, flags);
    }
    if ((strcmp(pathname, "/dev/spidev0.0") == 0 || strcmp(pathname, "/dev/spidev1.0") == 0) && fd >= 0) {
        fprintf(stderr, "hal_shim: opened %s -> fd=%d\n", pathname, fd);
        g_spidev_fd = fd;
    }
    return fd;
}

int access(const char *pathname, int mode) {
    static int (*real_access)(const char *, int) = NULL;
    if (!real_access) real_access = dlsym(RTLD_NEXT, "access");
    if (strncmp(pathname, "/sys/class/gpio/", 16) == 0) {
        return 0;
    }
    return real_access(pathname, mode);
}

void log_simple(const char *tag, const char *fmt, unsigned long a1, unsigned long a2, unsigned long a3, unsigned long a4) {
    static void (*real_log)(const char *, const char *, unsigned long, unsigned long, unsigned long, unsigned long) = NULL;
    if (!real_log) real_log = dlsym(RTLD_NEXT, "log_simple");
    if (fmt && strstr(fmt, "==== HMI ====") &&
        !strstr(fmt, "BTsubsystemNotify") &&
        !strstr(fmt, "PA_OUT_DEV_STS") &&
        !strstr(fmt, "PA_OUT_VRM_INTO") &&
        !strstr(fmt, "PA_OUT_WAKE_UP_REASON") &&
        !strstr(fmt, "PA_OUT_MODE_CHG")) {
        fprintf(stderr, fmt, a1, a2, a3, a4);
        fputc('\n', stderr);
    }
    if (real_log) real_log(tag, fmt, a1, a2, a3, a4);
}
