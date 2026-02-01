#include "frame_time.h"

/* Cached per-frame timing values. Defaults to 60fps. */
static float s_fDeltaSeconds = 1.0f / 60.0f;
static float s_fFrameMul = 1.0f;

void frame_time_set(float _fDeltaSeconds)
{
    float fDelta = (_fDeltaSeconds > 0.0f) ? _fDeltaSeconds : 0.0001f;
    float fMul = fDelta * 60.0f;
    if (fMul < 0.0001f)
        fMul = 0.0001f;

    s_fDeltaSeconds = fDelta;
    s_fFrameMul = fMul;
}

float frame_time_delta_seconds(void)
{
    return s_fDeltaSeconds;
}

float frame_time_mul(void)
{
    return s_fFrameMul;
}
