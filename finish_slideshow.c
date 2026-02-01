#include "finish_slideshow.h"
#include "audio.h"
#include "credits.h"
#include "fade_manager.h"
#include "font_helper.h"
#include "frame_time.h"
#include "joypad.h"
#include "rdpq_mode.h"
#include "rdpq_sprite.h"
#include "rdpq_text.h"
#include "resource_helper.h"
#include "stick_normalizer.h"
#include "ui.h"
#include <stdbool.h>
#include <string.h>

/* Slide definition structure */
typedef struct
{
    const char *sprite_path; /* Sprite path (NULL if text-only) */
    const char **text_lines; /* NULL-terminated array of text lines (NULL if no text) */
    int text_line_count;     /* Number of text lines */
} slide_def_t;

/* State machine */
typedef enum
{
    SLIDESHOW_STATE_IDLE,
    SLIDESHOW_STATE_FADING_FROM_BLACK,
    SLIDESHOW_STATE_SHOWING_SLIDE,
    SLIDESHOW_STATE_FADING_TO_BLACK
} slideshow_state_t;

/* Constants */
#define MENU_CREDITS_Y_OFFSET -30

/* Slide definitions */
static const slide_def_t s_slides[] = {{.sprite_path = "rom:/credits_screen_00.sprite", .text_lines = NULL, .text_line_count = 0}, /* Credits - handled specially */
                                       {.sprite_path = "rom:/qr_screen_00.sprite", .text_lines = NULL, .text_line_count = 0}};

#define SLIDE_COUNT (sizeof(s_slides) / sizeof(s_slides[0]))

/* Assets */
static sprite_t *s_pSlideSprites[SLIDE_COUNT] = {NULL};
static sprite_t *s_pBtnCRight = NULL;
static sprite_t *s_pBtnCLeft = NULL;
static wav64_t *s_pSoundConfirm = NULL;
static wav64_t *s_pSoundCancel = NULL;

/* State */
static bool s_bActive = false;
static slideshow_state_t s_state = SLIDESHOW_STATE_IDLE;
static int s_iCurrentSlide = 0;
static bool s_bNavigatingForward = true; /* Track navigation direction */

/* Button state tracking for edge detection */
static bool s_bPrevA = false;
static bool s_bPrevB = false;
static bool s_bPrevZ = false;
static bool s_bPrevLeftHeld = false;
static bool s_bPrevRightHeld = false;

/* Helper: Render sprite fullscreen (reused from stick_calibration pattern) */
static void render_sprite_fullscreen(sprite_t *pSprite)
{
    if (pSprite)
    {
        struct vec2i vSafeSize = ui_get_safe_area_size();
        float fScaleW = (float)vSafeSize.iX / (float)pSprite->width;
        float fScaleH = (float)vSafeSize.iY / (float)pSprite->height;
        float fScale = (fScaleW < fScaleH) ? fScaleW : fScaleH;

        int iScaledW = (int)(pSprite->width * fScale);
        int iScaledH = (int)(pSprite->height * fScale);

        /* Center within safe area (overscan-aware) */
        int iSafeOffsetX = (SCREEN_W - vSafeSize.iX) / 2;
        int iSafeOffsetY = (SCREEN_H - vSafeSize.iY) / 2;
        int iSpriteX = iSafeOffsetX + (vSafeSize.iX - iScaledW) / 2;
        int iSpriteY = iSafeOffsetY + (vSafeSize.iY - iScaledH) / 2;

        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_sprite_blit(pSprite, iSpriteX, iSpriteY, &(rdpq_blitparms_t){.scale_x = fScale, .scale_y = fScale});
    }
}

/* Helper: Check if button was just pressed (edge detection) */
static bool button_pressed(bool bCurrent, bool *pPrev)
{
    bool bPressed = bCurrent && !(*pPrev);
    *pPrev = bCurrent;
    return bPressed;
}

/* Helper: Navigate to next slide */
static void navigate_next(void)
{
    if (s_iCurrentSlide < SLIDE_COUNT - 1)
    {
        if (s_pSoundConfirm)
        {
            wav64_play(s_pSoundConfirm, MIXER_CHANNEL_USER_INTERFACE);
        }
        s_bNavigatingForward = true;
        fade_manager_start(TO_BLACK);
        s_state = SLIDESHOW_STATE_FADING_TO_BLACK;
    }
}

/* Helper: Navigate to previous slide */
static void navigate_previous(void)
{
    if (s_iCurrentSlide > 0)
    {
        if (s_pSoundCancel)
        {
            wav64_play(s_pSoundCancel, MIXER_CHANNEL_USER_INTERFACE);
        }
        s_bNavigatingForward = false;
        fade_manager_start(TO_BLACK);
        s_state = SLIDESHOW_STATE_FADING_TO_BLACK;
    }
}

void finish_slideshow_init(void)
{
    /* Load slide sprites */
    for (int i = 0; i < SLIDE_COUNT; i++)
    {
        if (s_slides[i].sprite_path)
        {
            s_pSlideSprites[i] = sprite_load(s_slides[i].sprite_path);
        }
    }

    /* Load button sprites */
    s_pBtnCRight = sprite_load("rom:/btn_c_right_00.sprite");
    s_pBtnCLeft = sprite_load("rom:/btn_c_left_00.sprite");

    /* Load sound effects */
    s_pSoundConfirm = wav64_load("rom:/btn_confirm.wav64", &(wav64_loadparms_t){.streaming_mode = 0});
    s_pSoundCancel = wav64_load("rom:/btn_cancel.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Stop all audio channels except music */
    audio_stop_all_except_music();

    /* Start music */
    audio_play_music(MUSIC_STARTSCREEN, NULL);

    /* Initialize state */
    s_bActive = true;
    s_iCurrentSlide = 0;
    s_state = SLIDESHOW_STATE_FADING_FROM_BLACK;

    /* Reset credits scroll when starting slideshow */
    credits_reset();

    /* Reset button states */
    s_bPrevA = false;
    s_bPrevB = false;
    s_bPrevZ = false;
    s_bPrevLeftHeld = false;
    s_bPrevRightHeld = false;

    /* Screen should already be black from SA_FADE_TO_BLACK in script.
     * When fade_manager_start(FROM_BLACK) is called and already at black (alpha=255),
     * it will use current alpha (255) as start and fade to 0 (transparent).
     * However, we need to ensure the fade actually starts properly.
     * If not already at black, force it first. */
    if (!fade_manager_is_opaque())
    {
        /* Not at black yet - force to black first (shouldn't happen, but safety check) */
        fade_manager_start(TO_BLACK);
        /* State will remain FADING_FROM_BLACK, and update() will handle starting the fade from black once opaque */
    }
    else
    {
        /* Already at black - start fading from black immediately.
         * fade_manager_start(FROM_BLACK) when already at black will:
         * - Set s_fade_start_alpha = s_current_alpha (255)
         * - Set s_target_alpha = 0 (transparent)
         * - Start the fade */
        fade_manager_start(FROM_BLACK);
    }
}

void finish_slideshow_close(void)
{
    /* Free sprites */
    for (int i = 0; i < SLIDE_COUNT; i++)
    {
        SAFE_FREE_SPRITE(s_pSlideSprites[i]);
    }
    SAFE_FREE_SPRITE(s_pBtnCRight);
    SAFE_FREE_SPRITE(s_pBtnCLeft);
    SAFE_CLOSE_WAV64(s_pSoundConfirm);
    SAFE_CLOSE_WAV64(s_pSoundCancel);

    s_bActive = false;
    s_state = SLIDESHOW_STATE_IDLE;
}

void finish_slideshow_update(const joypad_inputs_t *inputs)
{
    if (!s_bActive)
        return;

    mixer_ch_set_freq(MIXER_CHANNEL_MUSIC, AUDIO_BITRATE);

    /* Handle state transitions */
    switch (s_state)
    {
    case SLIDESHOW_STATE_FADING_FROM_BLACK:
    {
        /* If we were waiting for screen to become black, start fade from black once it's ready */
        if (fade_manager_is_opaque() && !fade_manager_is_busy())
        {
            /* At black but no fade active - start fading from black */
            fade_manager_start(FROM_BLACK);
        }

        /* Check if fade from black is complete (not busy and not opaque = fully transparent) */
        if (!fade_manager_is_busy() && !fade_manager_is_opaque())
        {
            s_state = SLIDESHOW_STATE_SHOWING_SLIDE;
        }
    }
    break;

    case SLIDESHOW_STATE_FADING_TO_BLACK:
        if (!fade_manager_is_busy() && fade_manager_is_opaque())
        {
            /* Change slide while fully black based on navigation direction */
            if (s_bNavigatingForward && s_iCurrentSlide < SLIDE_COUNT - 1)
            {
                s_iCurrentSlide++;
                /* Reset scroll when changing slides */
                if (s_iCurrentSlide == 0)
                {
                    credits_reset();
                }
            }
            else if (!s_bNavigatingForward && s_iCurrentSlide > 0)
            {
                s_iCurrentSlide--;
                /* Reset scroll when changing slides */
                if (s_iCurrentSlide == 0)
                {
                    credits_reset();
                }
            }

            /* Start fading from black */
            fade_manager_start(FROM_BLACK);
            s_state = SLIDESHOW_STATE_FADING_FROM_BLACK;
        }
        break;

    case SLIDESHOW_STATE_SHOWING_SLIDE:
        /* Update credits scroll if on credits slide */
        if (s_iCurrentSlide == 0)
        {
            bool bAllowInput = !fade_manager_is_busy();
            credits_update(inputs, bAllowInput);
        }

        /* Only process navigation if not fading */
        if (!fade_manager_is_busy())
        {
            /* Check for button presses (A, Z, B) */
            bool bAdvance = button_pressed(inputs->btn.a, &s_bPrevA) || button_pressed(inputs->btn.z, &s_bPrevZ);
            bool bGoBack = button_pressed(inputs->btn.b, &s_bPrevB);

            /* Check horizontal navigation (combines d-pad, C buttons, and stick) */
            int8_t iStickX = stick_normalizer_get_x();
            bool bLeftHeld = inputs->btn.d_left || inputs->btn.c_left || iStickX < -STICK_DEADZONE_MENU;
            bool bRightHeld = inputs->btn.d_right || inputs->btn.c_right || iStickX > STICK_DEADZONE_MENU;
            bool bLeftPressed = bLeftHeld && !s_bPrevLeftHeld;
            bool bRightPressed = bRightHeld && !s_bPrevRightHeld;

            s_bPrevLeftHeld = bLeftHeld;
            s_bPrevRightHeld = bRightHeld;

            /* Handle navigation */
            if (bAdvance || bRightPressed)
            {
                navigate_next();
            }
            else if (bGoBack || bLeftPressed)
            {
                navigate_previous();
            }
        }
        break;

    case SLIDESHOW_STATE_IDLE:
    default:
        break;
    }
}

void finish_slideshow_render(void)
{
    if (!s_bActive)
        return;

    /* Render full black overlay to hide game */
    ui_draw_darkening_overlay_alpha(255);

    /* Render current slide sprite */
    if (s_iCurrentSlide >= 0 && s_iCurrentSlide < SLIDE_COUNT)
    {
        if (s_pSlideSprites[s_iCurrentSlide])
        {
            render_sprite_fullscreen(s_pSlideSprites[s_iCurrentSlide]);
        }

        /* Render text overlay - credits slide (slide 0) uses scrolling credits */
        if (s_iCurrentSlide == 0)
        {
            int iStartY = SCREEN_H / 2 + MENU_CREDITS_Y_OFFSET;
            credits_render(iStartY);
        }
        /* Other slides can have text_lines if needed in the future */
    }

    /* Render button prompts */
    rdpq_set_mode_copy(false);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(1);

    /* Show right arrow if not on last slide */
    if (s_iCurrentSlide < SLIDE_COUNT - 1 && s_pBtnCRight)
    {
        struct vec2i vPos = ui_get_pos_bottom_right_sprite(s_pBtnCRight);
        rdpq_sprite_blit(s_pBtnCRight, vPos.iX, vPos.iY, NULL);
    }

    /* Show left arrow if not on first slide */
    if (s_iCurrentSlide > 0 && s_pBtnCLeft)
    {
        struct vec2i vPos = ui_get_pos_bottom_left_sprite(s_pBtnCLeft);
        rdpq_sprite_blit(s_pBtnCLeft, vPos.iX, vPos.iY, NULL);
    }
}

bool finish_slideshow_is_active(void)
{
    return s_bActive;
}
