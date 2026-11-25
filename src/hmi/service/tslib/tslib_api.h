// File name is ours; stock name unknown.
//
// Device libts-1.0.so.0 (Visteon tslib fork) entry points used by
// VMultiTouch. The fork's struct ts_sample is 24 bytes, not upstream's 20:
// VMultiTouch steps samples by 0x18 (0x00023b14 / 0x00023cac) and reads the
// contact id at +0x15 (0x00023b14) and +0x15 again in 0x00023dbc.
#ifndef RCD330_HMI_SERVICE_TSLIB_TSLIB_API_H
#define RCD330_HMI_SERVICE_TSLIB_TSLIB_API_H

#include <stdint.h>

struct tsdev;

struct ts_sample {
    int32_t x;               // +0x00
    int32_t y;               // +0x04
    uint32_t pressure;       // +0x08
    int32_t tv_sec;          // +0x0c (ARM32 struct timeval; int32_t keeps 64-bit hosts at 24 bytes)
    int32_t tv_usec;         // +0x10
    unsigned char reserved;  // +0x14
    unsigned char id;        // +0x15 contact id
    unsigned char pad[2];    // +0x16..0x17
};

// C++98 static assert.
typedef char ts_sample_must_be_24_bytes[sizeof(ts_sample) == 24 ? 1 : -1];

extern "C" {
struct tsdev* ts_open(const char* dev_name, int nonblock);
int ts_config(struct tsdev* ts);
int ts_read(struct tsdev* ts, struct ts_sample* samp, int nr);
int ts_close(struct tsdev* ts);
}

#endif  // RCD330_HMI_SERVICE_TSLIB_TSLIB_API_H
