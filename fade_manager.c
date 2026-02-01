#include "fade_manager.h"
#include "frame_time.h"
#include "n64sys.h"
#include "ui.h"

static float s_current_alpha = 0.0f;
static float s_target_alpha = 0.0f;
static float s_fade_start_time = 0.0f;
static float s_fade_start_alpha = 0.0f;
static bool s_has_rendered_final = true; /* Track if we've rendered the final value */
static uint8_t s_fade_color_r = 0;
static uint8_t s_fade_color_g = 0;
static uint8_t s_fade_color_b = 0;
static int s_fade_from_black_delay_counter = 0; /* Frames to wait at full black before fading */

void fade_manager_start(eFadeType _type)
{
    if (_type == FROM_BLACK && s_current_alpha == 0.0f)
    {
        /* If fading from black and currently transparent, start from black (255) */
        s_fade_start_alpha = 255.0f;
        s_current_alpha = 255.0f;                                       /* Set immediately so it starts from black */
        s_fade_from_black_delay_counter = FADE_FROM_BLACK_DELAY_FRAMES; /* Wait at full black */
    }
    else
    {
        s_fade_start_alpha = s_current_alpha;
        s_fade_from_black_delay_counter = 0; /* No delay for other fade types */
    }
    s_target_alpha = (_type == TO_BLACK) ? 255.0f : 0.0f;
    s_fade_start_time = (float)get_ticks_ms() / 1000.0f;
    s_has_rendered_final = false; /* Haven't rendered the final value yet */
}

void fade_manager_update(void)
{
    /* If we're waiting at full black before fading from black, decrement counter */
    if (s_fade_from_black_delay_counter > 0)
    {
        s_fade_from_black_delay_counter--;
        /* Update start time when delay ends so fade duration is correct */
        if (s_fade_from_black_delay_counter == 0)
        {
            s_fade_start_time = (float)get_ticks_ms() / 1000.0f;
        }
        return; /* Stay at full black during delay */
    }

    if (s_current_alpha != s_target_alpha)
    {
        float current_time = (float)get_ticks_ms() / 1000.0f;
        float elapsed = current_time - s_fade_start_time;
        float progress = elapsed / FADE_DURATION;

        if (progress >= 1.0f)
        {
            /* Clamp to exact target value to ensure we reach full black/transparent */
            s_current_alpha = s_target_alpha;
        }
        else
        {
            s_current_alpha = s_fade_start_alpha + (s_target_alpha - s_fade_start_alpha) * progress;
        }
    }
}

void fade_manager_render(void)
{
    if (s_current_alpha > 0.0f)
    {
        ui_draw_overlay_alpha_rgb((uint8_t)s_current_alpha, s_fade_color_r, s_fade_color_g, s_fade_color_b);
    }

    /* Mark that we've rendered the final value once we reach it */
    /* We only mark it as rendered when we've actually reached the target value */
    if (!s_has_rendered_final && s_current_alpha == s_target_alpha)
    {
        s_has_rendered_final = true;
    }
}

bool fade_manager_is_busy(void)
{
    /* Only consider not busy when we've rendered the final value */
    /* s_has_rendered_final is only true when we've reached the target AND rendered it */
    return !s_has_rendered_final;
}

bool fade_manager_is_opaque(void)
{
    /* Check if screen is fully opaque (fully black, alpha == 255) */
    return s_current_alpha >= 255.0f;
}

void fade_manager_stop(void)
{
    s_current_alpha = 0.0f;
    s_target_alpha = 0.0f;
    s_fade_from_black_delay_counter = 0;
    s_has_rendered_final = true; /* Consider it rendered when stopped */
}

void fade_manager_set_color(uint8_t _r, uint8_t _g, uint8_t _b)
{
    s_fade_color_r = _r;
    s_fade_color_g = _g;
    s_fade_color_b = _b;
}
