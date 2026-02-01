#pragma once

#include <stdint.h>

/* Profiler sections. Some are reserved for core timings, others for ad-hoc use. */
enum eProfilerSection
{
    PROF_SECTION_BOOT = 0,
    PROF_SECTION_FRAME,
    PROF_SECTION_UPDATE,
    PROF_SECTION_RENDER,
    PROF_SECTION_AUDIO,
    PROF_SECTION_USER0,
    PROF_SECTION_USER1,
    PROF_SECTION_USER2,
    PROF_SECTION_MAX
};

#ifdef PROFILER_ENABLED

void profiler_init(void);
void profiler_mark_boot_done(void);
void profiler_frame_begin(void);
void profiler_frame_end(float _fFps);
void profiler_section_begin(enum eProfilerSection _eSection);
void profiler_section_end(enum eProfilerSection _eSection);

/* Convenience macros so game code never needs #ifdef PROFILER_ENABLED. */
#define PROF_INIT() profiler_init()
#define PROF_BOOT_DONE() profiler_mark_boot_done()
#define PROF_FRAME_BEGIN() profiler_frame_begin()
#define PROF_FRAME_END(_fFps) profiler_frame_end(_fFps)
#define PROF_SECTION_BEGIN(_sec) profiler_section_begin(_sec)
#define PROF_SECTION_END(_sec) profiler_section_end(_sec)

#else /* !PROFILER_ENABLED */

/* When disabled, everything compiles down to no-ops. */
#define PROF_INIT() ((void)0)
#define PROF_BOOT_DONE() ((void)0)
#define PROF_FRAME_BEGIN() ((void)0)
#define PROF_FRAME_END(_fFps) ((void)0)
#define PROF_SECTION_BEGIN(_sec) ((void)0)
#define PROF_SECTION_END(_sec) ((void)0)

#endif /* PROFILER_ENABLED */
