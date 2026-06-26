#ifndef DLA_DEBUG_LOG_H
#define DLA_DEBUG_LOG_H

#include <psp2/kernel/clib.h>

#ifndef DLA_DEBUG_LOGS
#if defined(DEBUG) && DEBUG
#define DLA_DEBUG_LOGS 1
#else
#define DLA_DEBUG_LOGS 0
#endif
#endif

#if DLA_DEBUG_LOGS
#define DLA_DEBUG_PRINTF(...) sceClibPrintf(__VA_ARGS__)
#else
#define DLA_DEBUG_PRINTF(...) ((void)0)
#endif

// Keep real error messages available in release builds. These are rare and
// useful when something genuinely prevents startup, save, or asset loading.
#define DLA_ERROR_PRINTF(...) sceClibPrintf(__VA_ARGS__)

#endif // DLA_DEBUG_LOG_H
