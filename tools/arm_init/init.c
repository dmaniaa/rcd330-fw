#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("mdev", "/dev", "tmpfs", 0, "mode=0755");
    mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
    mount("usbfs", "/proc/bus/usb", "usbfs", 0, NULL);

    mkdir("/dev/pts", 0755);
    mkdir("/dev/input", 0755);
    mkdir("/dev/shm", 0777);
    mkdir("/var", 0755);
    mkdir("/var/run", 0755);
    mkdir("/tmp/var", 0755);
    mkdir("/tmp/var/run", 0755);

    mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
    mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3));
    mknod("/dev/zero", S_IFCHR | 0666, makedev(1, 5));
    mknod("/dev/mem", S_IFCHR | 0666, makedev(1, 1));
    mknod("/dev/tty", S_IFCHR | 0666, makedev(5, 0));
    mknod("/dev/fb0", S_IFCHR | 0666, makedev(29, 0));
    mknod("/dev/fb1", S_IFCHR | 0666, makedev(29, 1));
    mknod("/dev/i2c-0", S_IFCHR | 0666, makedev(89, 0));
    mknod("/dev/i2c-1", S_IFCHR | 0666, makedev(89, 1));
    mknod("/dev/i2c-2", S_IFCHR | 0666, makedev(89, 2));
    mknod("/dev/i2c-3", S_IFCHR | 0666, makedev(89, 3));
    mknod("/dev/spidev0.0", S_IFCHR | 0666, makedev(153, 0));
    mknod("/dev/spidev1.0", S_IFCHR | 0666, makedev(153, 0));
    mknod("/dev/tuner_knob", S_IFCHR | 0666, makedev(265, 0));
    mknod("/dev/v850srq", S_IFCHR | 0666, makedev(200, 0));
    mknod("/dev/event0", S_IFCHR | 0666, makedev(13, 64));
    mknod("/dev/event1", S_IFCHR | 0666, makedev(13, 65));
    mknod("/dev/input/event0", S_IFCHR | 0666, makedev(13, 64));
    mknod("/dev/input/event1", S_IFCHR | 0666, makedev(13, 65));

    int fd = open("/dev/console", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
    }

    /* Include /opt and . in PATH so unmodified start_carplay.sh ("source sourceme.sh") resolves */
    setenv("PATH", "/opt:/usr/sbin:/usr/bin:/sbin:/bin:.", 1);
    setenv("LD_PRELOAD", "/opt/lib/libhal_gpio_shim.so", 1);

    printf("\n=== RCD 330 /sbin/init starting (/etc/rc.d/rcS) ===\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "/bin/sh", "/etc/rc.d/rcS", NULL };
        execv("/bin/sh", argv);
        _exit(127);
    }

    /* Wait for /etc/rc.d/rcS then spawn interactive shell loop */
    if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
    }

    while (1) {
        pid_t sh = fork();
        if (sh == 0) {
            char *argv[] = { "/bin/sh", "-i", NULL };
            execv("/bin/sh", argv);
            _exit(127);
        }
        if (sh > 0) {
            int st = 0;
            waitpid(sh, &st, 0);
        } else {
            sleep(1);
        }
    }
    return 0;
}
