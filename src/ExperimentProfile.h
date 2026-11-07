#pragma once
#ifdef BMS_USE_TRACY
#include <tracy/Tracy.hpp>
#define BMS_TRACE_ZONE(name) ZoneScopedN(name)
#define BMS_TRACE_FRAME() FrameMark
#define BMS_TRACE_THREAD(name) tracy::SetThreadName(name)
#else
#define BMS_TRACE_ZONE(name) ((void)0)
#define BMS_TRACE_FRAME() ((void)0)
#define BMS_TRACE_THREAD(name) ((void)0)
#endif
