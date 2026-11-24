// File name is ours; stock name unknown.
//
// libOSAL.so.1 (/opt/lib) entry points used by opt/hmi. libOSAL ships no
// header; these prototypes follow its exported C symbols and the decompiled
// bodies in docs/decompiled/hal_osal/libOSAL/functions/.
#ifndef RCD330_HMI_SERVICE_OSAL_OSAL_API_H
#define RCD330_HMI_SERVICE_OSAL_OSAL_API_H

#ifdef __cplusplus
extern "C" {
#endif

// log_simple (libOSAL 0x00017414): prints fmt to stdout when the per-process
// /pss1/config/<proc>.conf enables [OPTION] ENABLE and [OUTPUT] <tag>.
void log_simple(const char *tag, const char *fmt, ...);

// printlog (libOSAL 0x0001aafc): appends "[time][level][proc][tag] in file
// line n func: msg" to the SD-card log when log saving is enabled.
void printlog(int level, const char *tag, const char *file, const char *func, int line,
              const char *fmt, ...);

// ini_getl (libOSAL 0x00018714, minIni): Section/Key as long (decimal, or hex
// with 0x); def_value when the file, section or key is missing.
long ini_getl(const char *section, const char *key, long def_value, const char *filename);

#ifdef __cplusplus
}
#endif

#endif // RCD330_HMI_SERVICE_OSAL_OSAL_API_H
