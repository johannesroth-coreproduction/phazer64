#include "tv_helper.h"
#include "vi.h"

void tv_activate_pal60(void)
{
    vi_set_timing_preset(&VI_TIMING_PAL60);
    // vi_debug_dump(1);
}

void tv_revert_to_pal50(void)
{
    vi_set_timing_preset(&VI_TIMING_PAL);
    // vi_debug_dump(1);
}
