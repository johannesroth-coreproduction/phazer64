#include "profiler.h"

#ifdef PROFILER_ENABLED

#include "libdragon.h"
#include <limits.h>
#include <stddef.h>

#define PROFILER_TARGET_FPS 60.0f
#define PROFILER_REPORT_FRAMES 60
#define PROFILER_BUDGET_MS (1000.0f / PROFILER_TARGET_FPS)

struct ProfSectionStats
{
    uint64_t uTotalTicks;
    uint32_t uMinTicks;
    uint32_t uMaxTicks;
    uint32_t uLastTicks;
    uint64_t uOpenTicks;
    uint32_t uCallCount;
    int bActive;
};

static struct ProfSectionStats m_aProfilerSections[PROF_SECTION_MAX];

static uint64_t m_uBootStartTicks;
static uint64_t m_uBootTicks;
static int m_bBootDone = 0;

static uint64_t m_uFrameStartTicks;
static uint64_t m_uFrameTotalTicks;
static uint64_t m_uFrameMinTicks;
static uint64_t m_uFrameMaxTicks;

static uint64_t m_uFrameStartSystemTicks;
static uint64_t m_uFrameTotalSystemTicks;
static uint64_t m_uFrameMinSystemTicks;
static uint64_t m_uFrameMaxSystemTicks;

static int m_iFramesInBatch;
static float m_fFpsSum;

#ifdef SHOW_DETAILS
static const char *m_aSectionNames[PROF_SECTION_MAX] = {"BOOT", "FRAME", "UPDATE", "RENDER", "AUDIO", "USER0", "USER1", "USER2"};
#endif

static void profiler_reset_sections(void)
{
    for (int iIndex = 0; iIndex < PROF_SECTION_MAX; ++iIndex)
    {
        struct ProfSectionStats *pProfSection = &m_aProfilerSections[iIndex];
        pProfSection->uTotalTicks = 0;
        pProfSection->uMinTicks = UINT32_MAX;
        pProfSection->uMaxTicks = 0;
        pProfSection->uLastTicks = 0;
        pProfSection->uOpenTicks = 0;
        pProfSection->uCallCount = 0;
        pProfSection->bActive = 0;
    }

    m_uFrameTotalTicks = 0;
    m_uFrameMinTicks = UINT64_MAX;
    m_uFrameMaxTicks = 0;
    m_uFrameTotalSystemTicks = 0;
    m_uFrameMinSystemTicks = UINT64_MAX;
    m_uFrameMaxSystemTicks = 0;
    m_iFramesInBatch = 0;
    m_fFpsSum = 0.0f;
}

void profiler_init(void)
{
    m_uBootStartTicks = get_user_ticks();
    m_uBootTicks = 0;
    m_bBootDone = 0;

    profiler_reset_sections();

    debugf("[PROFILE] Profiler initialized\n");
}

void profiler_mark_boot_done(void)
{
    if (m_bBootDone)
        return;

    uint32_t uNowTicks = (uint32_t)get_user_ticks();
    uint32_t uStartTicks = (uint32_t)m_uBootStartTicks;
    uint64_t uDelta = (uint64_t)(uNowTicks - uStartTicks);

    m_uBootTicks = uDelta;
    m_bBootDone = 1;

    float fBootMs = (float)TIMER_MICROS_LL(uDelta) / 1000.0f;

    debugf("[PROFILE] Boot time: %.3f ms\n", fBootMs);
}

void profiler_frame_begin(void)
{
    m_uFrameStartTicks = get_user_ticks();
    m_uFrameStartSystemTicks = get_system_ticks();
}

static void profiler_accumulate_section(enum eProfilerSection _eSection, uint64_t _uTicks)
{
    if (_eSection < 0 || _eSection >= PROF_SECTION_MAX)
        return;

    struct ProfSectionStats *pProfSection = &m_aProfilerSections[_eSection];

    pProfSection->uLastTicks = (uint32_t)_uTicks;
    pProfSection->uTotalTicks += _uTicks;
    pProfSection->uCallCount++;

    uint32_t uTicks32 = (uint32_t)_uTicks;
    if (uTicks32 < pProfSection->uMinTicks)
        pProfSection->uMinTicks = uTicks32;
    if (uTicks32 > pProfSection->uMaxTicks)
        pProfSection->uMaxTicks = uTicks32;
}

void profiler_section_begin(enum eProfilerSection _eSection)
{
    /* BOOT and FRAME are managed internally. */
    if (_eSection <= PROF_SECTION_FRAME || _eSection >= PROF_SECTION_MAX)
        return;

    struct ProfSectionStats *pProfSection = &m_aProfilerSections[_eSection];
    if (pProfSection->bActive)
        return;

    pProfSection->bActive = 1;
    pProfSection->uOpenTicks = get_user_ticks();
}

void profiler_section_end(enum eProfilerSection _eSection)
{
    if (_eSection <= PROF_SECTION_FRAME || _eSection >= PROF_SECTION_MAX)
        return;

    struct ProfSectionStats *pProfSection = &m_aProfilerSections[_eSection];
    if (!pProfSection->bActive)
        return;

    uint32_t uNowTicks = (uint32_t)get_user_ticks();
    uint32_t uOpenTicks = (uint32_t)pProfSection->uOpenTicks;
    uint64_t uDelta = (uint64_t)(uNowTicks - uOpenTicks);

    pProfSection->bActive = 0;
    pProfSection->uOpenTicks = 0;

    profiler_accumulate_section(_eSection, uDelta);
}

static void profiler_print_report(void)
{
    if (m_iFramesInBatch <= 0)
        return;

    float fFpsAvg = m_fFpsSum / (float)m_iFramesInBatch;
    float fBudgetMs = PROFILER_BUDGET_MS;

    /* Core section stats (per frame). */
    struct ProfSectionStats *pUpdate = &m_aProfilerSections[PROF_SECTION_UPDATE];
    struct ProfSectionStats *pRender = &m_aProfilerSections[PROF_SECTION_RENDER];
    struct ProfSectionStats *pAudio = &m_aProfilerSections[PROF_SECTION_AUDIO];

    float fUpdateAvgMs = 0.0f, fUpdatePct = 0.0f;
    float fRenderAvgMs = 0.0f, fRenderPct = 0.0f;
    float fAudioAvgMs = 0.0f, fAudioPct = 0.0f;
    float fSystemAvgMs = 0.0f, fSystemPct = 0.0f;
#ifdef SHOW_DETAILS
    /* Frame timing (CPU side). */
    uint64_t uAvgFrameTicks = m_uFrameTotalTicks / (uint64_t)m_iFramesInBatch;
    float fFrameAvgMs = (float)TIMER_MICROS_LL(uAvgFrameTicks) / 1000.0f;
    float fFrameMinMs = (float)TIMER_MICROS_LL(m_uFrameMinTicks) / 1000.0f;
    float fFrameMaxMs = (float)TIMER_MICROS_LL(m_uFrameMaxTicks) / 1000.0f;

    /* System timing. */
    float fSystemMinMs = 0.0f;
    float fSystemMaxMs = 0.0f;

    float fUpdateMinMs = 0.0f, fUpdateMaxMs = 0.0f;
    float fRenderMinMs = 0.0f, fRenderMaxMs = 0.0f;
    float fAudioMinMs = 0.0f, fAudioMaxMs = 0.0f;
#endif

    if (m_iFramesInBatch > 0)
    {
        if (pUpdate->uCallCount > 0)
        {
            uint64_t uAvgTicks = pUpdate->uTotalTicks / (uint64_t)m_iFramesInBatch;
            fUpdateAvgMs = (float)TIMER_MICROS_LL(uAvgTicks) / 1000.0f;
            fUpdatePct = (fUpdateAvgMs / fBudgetMs) * 100.0f;
#ifdef SHOW_DETAILS
            fUpdateMinMs = (float)TIMER_MICROS_LL(pUpdate->uMinTicks) / 1000.0f;
            fUpdateMaxMs = (float)TIMER_MICROS_LL(pUpdate->uMaxTicks) / 1000.0f;
#endif
        }

        if (pRender->uCallCount > 0)
        {
            uint64_t uAvgTicks = pRender->uTotalTicks / (uint64_t)m_iFramesInBatch;
            fRenderAvgMs = (float)TIMER_MICROS_LL(uAvgTicks) / 1000.0f;
            fRenderPct = (fRenderAvgMs / fBudgetMs) * 100.0f;
#ifdef SHOW_DETAILS
            fRenderMinMs = (float)TIMER_MICROS_LL(pRender->uMinTicks) / 1000.0f;
            fRenderMaxMs = (float)TIMER_MICROS_LL(pRender->uMaxTicks) / 1000.0f;
#endif
        }

        if (pAudio->uCallCount > 0)
        {
            uint64_t uAvgTicks = pAudio->uTotalTicks / (uint64_t)m_iFramesInBatch;
            fAudioAvgMs = (float)TIMER_MICROS_LL(uAvgTicks) / 1000.0f;
            fAudioPct = (fAudioAvgMs / fBudgetMs) * 100.0f;
#ifdef SHOW_DETAILS
            fAudioMinMs = (float)TIMER_MICROS_LL(pAudio->uMinTicks) / 1000.0f;
            fAudioMaxMs = (float)TIMER_MICROS_LL(pAudio->uMaxTicks) / 1000.0f;
#endif
        }

        /* Calculate system/idle time percentage. */
        uint64_t uAvgSystemTicks = m_uFrameTotalSystemTicks / (uint64_t)m_iFramesInBatch;
        fSystemAvgMs = (float)TIMER_MICROS_LL(uAvgSystemTicks) / 1000.0f;
        fSystemPct = (fSystemAvgMs / fBudgetMs) * 100.0f;
#ifdef SHOW_DETAILS
        fSystemMinMs = (float)TIMER_MICROS_LL(m_uFrameMinSystemTicks) / 1000.0f;
        fSystemMaxMs = (float)TIMER_MICROS_LL(m_uFrameMaxSystemTicks) / 1000.0f;
#endif
    }

    /* Heap statistics (for M:% and optional detail line). */
    heap_stats_t stats;
    sys_get_heap_stats(&stats);

    size_t uHeapTotal = (size_t)stats.total;
    size_t uHeapUsed = (size_t)stats.used;

    float fMemPct = 0.0f;
    if (uHeapTotal > 0)
        fMemPct = ((float)uHeapUsed * 100.0f) / (float)uHeapTotal;

#ifdef SHOW_DETAILS
    size_t uHeapFree = uHeapTotal - uHeapUsed;
#endif

    float fTotalPct = fUpdatePct + fRenderPct + fAudioPct;

    /* High-level overview line.
     * Example:
     * [PROFILE] FPS: 59.3   U: 03.3%    R: 76.9%    A: 17.7%     M: 06.6%    I: 02.1%
     */
    debugf("[PROFILE] FPS: %.1f\tT: %04.1f%%\tM: %04.1f%%\t\tU: %04.1f%%\tR: %04.1f%%\tA: %04.1f%%\tI: %04.1f%%\n",
           fFpsAvg,
           fTotalPct,
           fMemPct,
           fUpdatePct,
           fRenderPct,
           fAudioPct,
           fSystemPct);

#ifdef SHOW_DETAILS
    /* Frame summary. */
    debugf("[PROFILE] FRAMES:\t%07.3f\t(%07.3f\t|\t%07.3f)\n", fFrameAvgMs, fFrameMinMs, fFrameMaxMs);

    /* System time detail. */
    debugf("[PROFILE] SYSTEM:\t%07.3f\t(%07.3f\t|\t%07.3f)\n", fSystemAvgMs, fSystemMinMs, fSystemMaxMs);

    /* Core section details. */
    debugf("[PROFILE] UPDATE:\t%07.3f\t(%07.3f\t|\t%07.3f)\n", fUpdateAvgMs, fUpdateMinMs, fUpdateMaxMs);
    debugf("[PROFILE] RENDER:\t%07.3f\t(%07.3f\t|\t%07.3f)\n", fRenderAvgMs, fRenderMinMs, fRenderMaxMs);

    /* AUDIO: show per-call stats, because mixer_poll is not called every frame. */
    float fAudioAvgMsCall = 0.0f;
    float fAudioMinMsCall = fAudioMinMs;
    float fAudioMaxMsCall = fAudioMaxMs;

    if (pAudio->uCallCount > 0)
    {
        uint64_t uAvgTicksPerCall = pAudio->uTotalTicks / (uint64_t)pAudio->uCallCount;
        fAudioAvgMsCall = (float)TIMER_MICROS_LL(uAvgTicksPerCall) / 1000.0f;
    }

    debugf("[PROFILE] AUDIO:\t%07.3f\t(%07.3f\t|\t%07.3f)\tcalls=%lu\n", fAudioAvgMsCall, fAudioMinMsCall, fAudioMaxMsCall, (unsigned long)pAudio->uCallCount);

    /* User sections: per-call stats, only if used. */
    enum eProfilerSection aUserSections[] = {PROF_SECTION_USER0, PROF_SECTION_USER1, PROF_SECTION_USER2};

    int iNumUserSections = (int)(sizeof(aUserSections) / sizeof(aUserSections[0]));

    for (int iIndex = 0; iIndex < iNumUserSections; ++iIndex)
    {
        enum eProfilerSection eSection = aUserSections[iIndex];
        struct ProfSectionStats *pProfSection = &m_aProfilerSections[eSection];

        if (pProfSection->uCallCount == 0)
            continue;

        uint64_t uAvgTicksPerFrame = pProfSection->uTotalTicks / (uint64_t)m_iFramesInBatch;
        float fAvgMs = (float)TIMER_MICROS_LL(uAvgTicksPerFrame) / 1000.0f;
        float fMinMs = (float)TIMER_MICROS_LL(pProfSection->uMinTicks) / 1000.0f;
        float fMaxMs = (float)TIMER_MICROS_LL(pProfSection->uMaxTicks) / 1000.0f;

        debugf("[PROFILE] %-6s:\t%07.3f\t(%07.3f\t|\t%07.3f)\tcalls=%lu\n", m_aSectionNames[eSection], fAvgMs, fMinMs, fMaxMs, (unsigned long)pProfSection->uCallCount);
    }

    /* Heap detail line: used / total (free) in KB. */
    unsigned long uKbUsed = (unsigned long)(uHeapUsed / 1024UL);
    unsigned long uKbTotal = (unsigned long)(uHeapTotal / 1024UL);
    unsigned long uKbFree = (unsigned long)(uHeapFree / 1024UL);

    debugf("[PROFILE] HEAP:\t%lu / %lu\t(%lu)\n", uKbUsed, uKbTotal, uKbFree);
#endif
}

void profiler_frame_end(float _fFps)
{
    uint32_t uNowTicks = (uint32_t)get_user_ticks();
    uint32_t uStartTicks = (uint32_t)m_uFrameStartTicks;
    uint64_t uDelta = (uint64_t)(uNowTicks - uStartTicks);

    /* Accumulate FRAME section too. */
    profiler_accumulate_section(PROF_SECTION_FRAME, uDelta);

    m_uFrameTotalTicks += uDelta;
    if (uDelta < m_uFrameMinTicks)
        m_uFrameMinTicks = uDelta;
    if (uDelta > m_uFrameMaxTicks)
        m_uFrameMaxTicks = uDelta;

    /* Track system time. */
    uint32_t uNowSystemTicks = (uint32_t)get_system_ticks();
    uint32_t uStartSystemTicks = (uint32_t)m_uFrameStartSystemTicks;
    uint64_t uSystemDelta = (uint64_t)(uNowSystemTicks - uStartSystemTicks);

    m_uFrameTotalSystemTicks += uSystemDelta;
    if (uSystemDelta < m_uFrameMinSystemTicks)
        m_uFrameMinSystemTicks = uSystemDelta;
    if (uSystemDelta > m_uFrameMaxSystemTicks)
        m_uFrameMaxSystemTicks = uSystemDelta;

    m_fFpsSum += _fFps;
    m_iFramesInBatch++;

    if (m_iFramesInBatch >= PROFILER_REPORT_FRAMES)
    {
        profiler_print_report();
        profiler_reset_sections();
    }
}

#endif /* PROFILER_ENABLED */