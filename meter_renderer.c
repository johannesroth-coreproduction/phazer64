#include "meter_renderer.h"

#include "math_helper.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_tex.h"
#include "resource_helper.h"

/* HUD sprites shared by all meters (same visuals as UFO turbo meter) */
static sprite_t *s_pHudFrame = NULL;
static sprite_t *s_pHudFill = NULL;
static sprite_t *s_pHudFillCap = NULL;
static rdpq_texparms_t s_fillTexParms = {0};

/* Simple ref-count so multiple systems can use the shared sprites safely. */
static int s_iRefCount = 0;

void meter_renderer_init(void)
{
    if (s_iRefCount == 0)
    {
        /* Load HUD sprites (same assets that were previously in ufo_turbo) */
        s_pHudFrame = sprite_load("rom:/hud_turbo_frame_00.sprite");
        s_pHudFill = sprite_load("rom:/hud_turbo_fill_00.sprite");
        s_pHudFillCap = sprite_load("rom:/hud_turbo_fill_cap_00.sprite");

        /* Setup texture parameters for Y-wrapped fill */
        if (s_pHudFill)
        {
            s_fillTexParms = (rdpq_texparms_t){
                .s = {.repeats = 1.0f, .mirror = MIRROR_NONE},
                .t = {.repeats = REPEAT_INFINITE, .mirror = MIRROR_NONE},
            };
        }
    }

    s_iRefCount++;
}

void meter_renderer_free(void)
{
    if (s_iRefCount <= 0)
        return;

    s_iRefCount--;
    if (s_iRefCount > 0)
        return;

    SAFE_FREE_SPRITE(s_pHudFrame);
    SAFE_FREE_SPRITE(s_pHudFill);
    SAFE_FREE_SPRITE(s_pHudFillCap);
}

struct vec2i meter_renderer_get_frame_size(void)
{
    struct vec2i vSize = {0, 0};
    if (s_pHudFrame)
    {
        vSize.iX = s_pHudFrame->width;
        vSize.iY = s_pHudFrame->height;
    }
    return vSize;
}

void meter_renderer_render(struct vec2i _vFramePos, float _fValue01, color_t _uColor)
{
    if (!s_pHudFrame || !s_pHudFill || !s_pHudFillCap)
        return;

    /* Clamp value to [0,1] */
    float fValue = clampf_01(_fValue01);

    /* Fill area coordinates inside frame: (5,45) to (10,5) for 100% fuel
     * Frame coordinates are relative to frame sprite
     * Frame is 1px narrower on X and 16px shorter on Y */
    int iFillLeft = 5;
    int iFillRight = 10;
    int iFillBottom = 50;                     /* Bottom of fill area (lowest Y) - adjusted for 16px shorter frame */
    int iFillTop = 5;                         /* Top of fill area (highest Y) */
    int iFillHeight = iFillBottom - iFillTop; /* 40 pixels for 100% */
    int iCapHeight = 3;

    /* Calculate current fill height */
    int iCurrentFillHeight = (int)((float)iFillHeight * fValue + 0.5f);

    /* Calculate if we need to draw the fill rectangle or just the cap */
    bool bDrawFillRect = (iCurrentFillHeight + 1 > iCapHeight);
    bool bDrawCap = (iCurrentFillHeight > 0);

    if (bDrawFillRect)
    {
        /* Draw fill rectangle with Y-wrapping
         * Top position excludes cap height to avoid overdraw */
        int iFillRectTop = _vFramePos.iY + iFillTop + (iFillHeight - iCurrentFillHeight) + iCapHeight;
        int iFillRectBottom = _vFramePos.iY + iFillBottom + 1; /* +1 to include bottom pixel */
        int iFillRectLeft = _vFramePos.iX + iFillLeft;
        int iFillRectRight = _vFramePos.iX + iFillRight + 1; /* +1 to include right pixel */

        /* Upload fill texture with Y-wrapping */
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(0);
        rdpq_mode_filter(FILTER_POINT);
        /* Tint fill by using prim color with texture alpha */
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_set_prim_color(_uColor);
        rdpq_sprite_upload(TILE0, s_pHudFill, &s_fillTexParms);

        /* Draw textured rectangle with Y-wrapping using non-scaled command
         * Texture coordinates S,T are in texel units (rdpq handles the fixed-point scaling) */
        int iRectHeight = iFillRectBottom - iFillRectTop;
        int iTexHeight = s_pHudFill->height;

        /* Calculate initial T coordinate. We want the bottom of the texture to align with the bottom of the rect.
         * Since the texture wraps, we can just use negative offsets relative to the full height. */
        float fT0_unwrapped = (float)iTexHeight - (float)iRectHeight;

        /* Keep T0 positive to be safe, though wrapping should handle it */
        while (fT0_unwrapped < 0.0f)
            fT0_unwrapped += (float)iTexHeight;

        float fT0 = fT0_unwrapped - 0.5f;
        float fT1 = fT0 + (float)iRectHeight;
        float fS1 = 16.0f; /* Use 11px width for content (texture is 1px narrower) */

        rdpq_texture_rectangle_scaled(TILE0, iFillRectLeft, iFillRectTop, iFillRectRight, iFillRectBottom, 0, fT0, fS1, fT1);
    }

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_filter(FILTER_POINT);

    if (bDrawCap)
    {
        /* Draw cap at the top of the fill area.
         * Cap position: top of fill area moves down as value decreases.
         * When value is 0, cap is at bottom (invisible behind frame). */
        int iFillHeight = 50 - 5;
        int iCapY = _vFramePos.iY + iFillTop + (iFillHeight - iCurrentFillHeight);

        /* Tint cap using the same color (texture alpha, flat color) */
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_set_prim_color(_uColor);
        rdpq_sprite_blit(s_pHudFillCap, _vFramePos.iX + iFillLeft, iCapY, NULL);
    }

    /* Draw frame without tint (original sprite colors) */
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_sprite_blit(s_pHudFrame, _vFramePos.iX, _vFramePos.iY, NULL);
}
