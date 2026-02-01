#include "bootup_logos.h"
#include "fade_manager.h"
#include "graphics.h"
#include "libdragon.h"
#include "n64sys.h"
#include "rdpq.h"
#include "rdpq_mode.h"
#include "resource_helper.h"
#include "sprite.h"
#include "ui.h"
#include <math.h>
#include <stdbool.h>

/* Configurable defines */
#define BOOTUP_ANIMATION_DURATION 0.75f
#define BOOTUP_WAIT_AFTER_ANIMATION 1.0f
#define BOOTUP_COREPROD_ANIMATION_DELAY 0.1f

/* Libdragon logo defines */
#define BOOTUP_LIBDRAGON_ROTATION_START_DEG 120.0f
#define BOOTUP_LIBDRAGON_ROTATION_END_DEG 0.0f
#define BOOTUP_LIBDRAGON_CIRCLE_START_X_OFFSET -10.0f
#define BOOTUP_LIBDRAGON_SPRITES_X_OFFSET 15
#define BOOTUP_LIBDRAGON_SPRITES_Y_OFFSET 7

/* Coreprod logo defines */
#define BOOTUP_COREPROD_CIRCLE_X_OFFSET -45
#define BOOTUP_COREPROD_CIRCLE_Y_OFFSET 17
#define BOOTUP_COREPROD_CIRCLE_TARGET_SCALE 2.0f

/* Logo state */
typedef enum
{
    LOGO_STATE_LIBDRAGON_ANIMATION,
    LOGO_STATE_LIBDRAGON_WAIT,
    LOGO_STATE_LIBDRAGON_FADE_TO_BLACK,
    LOGO_STATE_COREPROD_FADE_FROM_BLACK,
    LOGO_STATE_COREPROD_ANIMATION,
    LOGO_STATE_COREPROD_WAIT,
    LOGO_STATE_COREPROD_FADE_TO_BLACK,
    LOGO_STATE_DONE
} eLogoState;

/* Internal state */
static sprite_t *s_pLibdragonTextSprite = NULL;
static sprite_t *s_pLibdragonCircleSprite = NULL;
static sprite_t *s_pCoreprodTextSprite = NULL;
static sprite_t *s_pCoreprodCircleSprite = NULL;

static eLogoState s_eLogoState = LOGO_STATE_LIBDRAGON_ANIMATION;
static float s_fAnimationStartTime = 0.0f;
static float s_fWaitStartTime = 0.0f;
static float s_fCoreprodAnimationDelayStartTime = 0.0f;

/* Libdragon animation state */
static float s_fLibdragonRotationAngle = 0.0f;

/* Coreprod animation state */
static float s_fCoreprodScale = 1.0f;

static bool s_bInitialized = false;

/* Ease-out function: quick start, slow end */
static float ease_out_cubic(float t)
{
    float f = 1.0f - t;
    return 1.0f - (f * f * f);
}

void bootup_logos_init(void)
{
    if (s_bInitialized)
        return;

    /* Load libdragon sprites */
    s_pLibdragonTextSprite = sprite_load("rom:/logo_libdragon_text_00.sprite");
    s_pLibdragonCircleSprite = sprite_load("rom:/logo_libdragon_circle_00.sprite");

    /* Load coreprod sprites */
    s_pCoreprodTextSprite = sprite_load("rom:/logo_coreprod_00.sprite");
    s_pCoreprodCircleSprite = sprite_load("rom:/logo_coreprod_circle_00.sprite");

    /* Initialize libdragon animation state */
    s_fLibdragonRotationAngle = BOOTUP_LIBDRAGON_ROTATION_START_DEG * (M_PI / 180.0f);
    s_fAnimationStartTime = (float)get_ticks_ms() / 1000.0f;
    s_fWaitStartTime = 0.0f;
    s_eLogoState = LOGO_STATE_LIBDRAGON_ANIMATION;

    /* Initialize coreprod animation state */
    s_fCoreprodScale = 1.0f;

    /* Start fade from black */
    fade_manager_start(FROM_BLACK);

    s_bInitialized = true;
}

void bootup_logos_update(void)
{
    if (!s_bInitialized)
        return;

    /* Update fade manager */
    fade_manager_update();

    float current_time = (float)get_ticks_ms() / 1000.0f;

    switch (s_eLogoState)
    {
    case LOGO_STATE_LIBDRAGON_ANIMATION:
    {
        /* Update libdragon rotation animation */
        float elapsed = current_time - s_fAnimationStartTime;
        float progress = elapsed / BOOTUP_ANIMATION_DURATION;

        if (progress >= 1.0f)
        {
            progress = 1.0f;
            s_eLogoState = LOGO_STATE_LIBDRAGON_WAIT;
            s_fWaitStartTime = current_time;
        }
        else
        {
            /* Apply ease-out */
            float eased = ease_out_cubic(progress);

            /* Interpolate rotation from start to end */
            float startRad = BOOTUP_LIBDRAGON_ROTATION_START_DEG * (M_PI / 180.0f);
            float endRad = BOOTUP_LIBDRAGON_ROTATION_END_DEG * (M_PI / 180.0f);
            s_fLibdragonRotationAngle = startRad + (endRad - startRad) * eased;
        }
        break;
    }

    case LOGO_STATE_LIBDRAGON_WAIT:
    {
        float elapsed = current_time - s_fWaitStartTime;
        if (elapsed >= BOOTUP_WAIT_AFTER_ANIMATION)
        {
            s_eLogoState = LOGO_STATE_LIBDRAGON_FADE_TO_BLACK;
            fade_manager_set_color(255, 255, 255); /* Switch to white before fading */
            fade_manager_start(TO_BLACK);
        }
        break;
    }

    case LOGO_STATE_LIBDRAGON_FADE_TO_BLACK:
    {
        if (!fade_manager_is_busy())
        {
            /* Fade to black complete, switch to coreprod */
            s_eLogoState = LOGO_STATE_COREPROD_FADE_FROM_BLACK;
            fade_manager_start(FROM_BLACK);
            s_fAnimationStartTime = current_time;
            s_fCoreprodScale = 1.0f;
        }
        break;
    }

    case LOGO_STATE_COREPROD_FADE_FROM_BLACK:
    {
        if (!fade_manager_is_busy())
        {
            /* Fade from black complete, start scale animation (with delay) */
            fade_manager_set_color(0, 0, 0); /* Switch back to black after coreprod logo is shown */
            s_eLogoState = LOGO_STATE_COREPROD_ANIMATION;
            s_fCoreprodAnimationDelayStartTime = current_time;
            s_fCoreprodScale = 1.0f;
        }
        break;
    }

    case LOGO_STATE_COREPROD_ANIMATION:
    {
        /* Check if delay has passed */
        float delayElapsed = current_time - s_fCoreprodAnimationDelayStartTime;
        if (delayElapsed < BOOTUP_COREPROD_ANIMATION_DELAY)
        {
            /* Still in delay - keep scale at 1.0 */
            s_fCoreprodScale = 1.0f;
            break;
        }

        /* Delay complete - start scale animation */
        float animationElapsed = delayElapsed - BOOTUP_COREPROD_ANIMATION_DELAY;
        float progress = animationElapsed / BOOTUP_ANIMATION_DURATION;

        if (progress >= 1.0f)
        {
            /* Animation complete - set scale to exact target */
            s_fCoreprodScale = BOOTUP_COREPROD_CIRCLE_TARGET_SCALE;
            s_eLogoState = LOGO_STATE_COREPROD_WAIT;
            s_fWaitStartTime = current_time;
        }
        else
        {
            /* Apply ease-out */
            float eased = ease_out_cubic(progress);

            /* Interpolate scale from 1.0 to target scale */
            s_fCoreprodScale = 1.0f + (BOOTUP_COREPROD_CIRCLE_TARGET_SCALE - 1.0f) * eased;
        }
        break;
    }

    case LOGO_STATE_COREPROD_WAIT:
    {
        float elapsed = current_time - s_fWaitStartTime;
        if (elapsed >= BOOTUP_WAIT_AFTER_ANIMATION)
        {
            s_eLogoState = LOGO_STATE_COREPROD_FADE_TO_BLACK;
            fade_manager_start(TO_BLACK);
        }
        break;
    }

    case LOGO_STATE_COREPROD_FADE_TO_BLACK:
    {
        if (!fade_manager_is_busy())
        {
            /* Unload sprites when bootup is complete */
            SAFE_FREE_SPRITE(s_pLibdragonTextSprite);
            SAFE_FREE_SPRITE(s_pLibdragonCircleSprite);
            SAFE_FREE_SPRITE(s_pCoreprodTextSprite);
            SAFE_FREE_SPRITE(s_pCoreprodCircleSprite);
            s_eLogoState = LOGO_STATE_DONE;
        }
        break;
    }

    case LOGO_STATE_DONE:
        break;
    }
}

void bootup_logos_render(void)
{
    if (!s_bInitialized)
        return;

    /* Calculate center position */
    int iCenterX = SCREEN_W / 2;
    int iCenterY = SCREEN_H / 2;

    /* Render based on current logo state */
    if (s_eLogoState == LOGO_STATE_LIBDRAGON_ANIMATION || s_eLogoState == LOGO_STATE_LIBDRAGON_WAIT || s_eLogoState == LOGO_STATE_LIBDRAGON_FADE_TO_BLACK)
    {
        /* Render libdragon logo */
        if (!s_pLibdragonTextSprite || !s_pLibdragonCircleSprite)
            return;

        int iTextWidth = s_pLibdragonTextSprite->width;
        int iTextHeight = s_pLibdragonTextSprite->height;
        int iCircleWidth = s_pLibdragonCircleSprite->width;
        int iCircleHeight = s_pLibdragonCircleSprite->height;

        /* Map rotation angle directly to X offset (start offset at start angle, 0 at end angle) */
        float fRotationDeg = s_fLibdragonRotationAngle * (180.0f / M_PI);
        float fXOffset = BOOTUP_LIBDRAGON_CIRCLE_START_X_OFFSET *
                         (1.0f - (fRotationDeg - BOOTUP_LIBDRAGON_ROTATION_START_DEG) / (BOOTUP_LIBDRAGON_ROTATION_END_DEG - BOOTUP_LIBDRAGON_ROTATION_START_DEG));
        int iCircleX = iCenterX - iCircleWidth / 2 + 18 + (int)fXOffset + BOOTUP_LIBDRAGON_SPRITES_X_OFFSET;
        int iCircleY = iCenterY - iCircleHeight / 2 + 45 + BOOTUP_LIBDRAGON_SPRITES_Y_OFFSET;

        /* Set up multiply blending mode */
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_dithering(DITHER_BAYER_INVBAYER);

        /* Render circle sprite centered with rotation */
        rdpq_sprite_blit(s_pLibdragonCircleSprite, iCircleX, iCircleY, &(rdpq_blitparms_t){.cx = 66, .cy = 57, .theta = s_fLibdragonRotationAngle});

        /* Render text sprite centered */
        rdpq_sprite_blit(s_pLibdragonTextSprite,
                         iCenterX - iTextWidth / 2.0f + BOOTUP_LIBDRAGON_SPRITES_X_OFFSET,
                         iCenterY - iTextHeight / 2.0f + BOOTUP_LIBDRAGON_SPRITES_Y_OFFSET,
                         NULL);
    }
    else if (s_eLogoState == LOGO_STATE_COREPROD_FADE_FROM_BLACK || s_eLogoState == LOGO_STATE_COREPROD_ANIMATION || s_eLogoState == LOGO_STATE_COREPROD_WAIT ||
             s_eLogoState == LOGO_STATE_COREPROD_FADE_TO_BLACK)
    {
        /* Render coreprod logo */
        if (!s_pCoreprodTextSprite || !s_pCoreprodCircleSprite)
            return;

        int iTextWidth = s_pCoreprodTextSprite->width;
        int iTextHeight = s_pCoreprodTextSprite->height;
        int iCircleWidth = s_pCoreprodCircleSprite->width;
        int iCircleHeight = s_pCoreprodCircleSprite->height;

        /* Map scale directly to alpha (scale 1.0 -> alpha 255 opaque, scale target -> alpha 0 transparent) */
        float fScaleProgress = (s_fCoreprodScale - 1.0f) / (BOOTUP_COREPROD_CIRCLE_TARGET_SCALE - 1.0f);
        if (fScaleProgress < 0.0f)
            fScaleProgress = 0.0f;
        if (fScaleProgress > 1.0f)
            fScaleProgress = 1.0f;
        float fAlphaProgress = 1.0f - fScaleProgress;
        /* Ensure alpha reaches exactly 0 when scale reaches target */
        if (s_fCoreprodScale >= BOOTUP_COREPROD_CIRCLE_TARGET_SCALE)
            fAlphaProgress = 0.0f;
        uint8_t uCircleAlpha = (uint8_t)(fAlphaProgress * 255.0f);

        /* Set up multiply blending mode */
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);

        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);

        /* Render text sprite centered */
        rdpq_sprite_blit(s_pCoreprodTextSprite, iCenterX - iTextWidth / 2.0f, iCenterY - iTextHeight / 2.0f, NULL);

        if (uCircleAlpha > 0)
        {
            /* Render circle sprite with scale and alpha animation */
            /* Circle offset on X and Y compared to full logo coords */
            int iCircleX = iCenterX - iCircleWidth / 2 + BOOTUP_COREPROD_CIRCLE_X_OFFSET;
            int iCircleY = iCenterY - iCircleHeight / 2 + BOOTUP_COREPROD_CIRCLE_Y_OFFSET;

            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT); /* output = TEX0 * PRIM (RGB and A) */
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);   /* normal alpha blending using combiner alpha */
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_mode_dithering(DITHER_NONE_INVBAYER);
            rdpq_set_prim_color(RGBA32(255, 255, 255, uCircleAlpha)); /* white with animated alpha */
            rdpq_sprite_blit(s_pCoreprodCircleSprite,
                             iCircleX,
                             iCircleY,
                             &(rdpq_blitparms_t){.cx = iCircleWidth / 2, .cy = iCircleHeight / 2, .scale_x = s_fCoreprodScale, .scale_y = s_fCoreprodScale});
        }
    }
}

bool bootup_logos_is_done(void)
{
    return s_bInitialized && s_eLogoState == LOGO_STATE_DONE;
}
