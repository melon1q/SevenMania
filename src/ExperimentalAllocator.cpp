// One definition per executable. Do not redirect CRT/COM/Windows allocations:
// their ownership contracts continue to use their original allocators.
#ifdef BMS_USE_MIMALLOC
#ifdef BMS_PERF_DIAGNOSTICS
#include <mimalloc.h>
#include "PerformanceTrace.h"
inline void* countedMiNew(size_t n){if(perf::detailed){++perf::allocations.calls;perf::allocations.bytes+=n;}return mi_new(n);}
inline void* countedMiNewNoThrow(size_t n){if(perf::detailed){++perf::allocations.calls;perf::allocations.bytes+=n;}return mi_new_nothrow(n);}
inline void countedMiFree(void* p){if(perf::detailed && p)++perf::allocations.frees;mi_free(p);}
#define mi_new(n) countedMiNew(n)
#define mi_new_nothrow(n) countedMiNewNoThrow(n)
#define mi_free(p) countedMiFree(p)
#endif
#include <mimalloc-new-delete.h>
#ifdef BMS_PERF_DIAGNOSTICS
#undef mi_new
#undef mi_new_nothrow
#undef mi_free
#endif
#endif
