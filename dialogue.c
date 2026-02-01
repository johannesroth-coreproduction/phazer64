#include "dialogue.h"

#include "audio.h"
#include "csv_helper.h"
#include "font_helper.h"
#include "frame_time.h"
#include "game_objects/gp_camera.h"
#include "game_objects/gp_state.h"
#include "game_objects/tractor_beam.h"
#include "math_helper.h"
#include "rdpq.h"
#include "resource_helper.h"
#include "rng.h"
#include "rspq.h"
#include "tilemap.h"
#include "ui.h"

#include <ctype.h>
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdragon.h"

static const char *kSpeakerNames[DIALOGUE_SPEAKER_COUNT] = {"boy", "rhino", "alien"};

/* Rendering constants (values provided for overscan = 0; overscan applied at runtime) */
/* Text rectangle offsets from the box origin (exact coordinates for text rendering) */
#define DIALOGUE_TEXT_OFFSET_X_LEFT 73
#define DIALOGUE_TEXT_OFFSET_Y 11
#define DIALOGUE_TEXT_OFFSET_X_RIGHT 11
#define DIALOGUE_TEXT_RECT_W 214
#define DIALOGUE_TEXT_RECT_H 50

/* Box/portrait sprite placement (absolute origin for box; portrait offsets from box origin) */
#define DIALOGUE_BOX_SPRITE_OFFSET_X UI_DESIGNER_PADDING
#define DIALOGUE_BOX_SPRITE_OFFSET_Y UI_DESIGNER_PADDING
#define DIALOGUE_PORTRAIT_LEFT_X 5
#define DIALOGUE_PORTRAIT_LEFT_Y 5
#define DIALOGUE_PORTRAIT_RIGHT_X 234
#define DIALOGUE_PORTRAIT_RIGHT_Y DIALOGUE_PORTRAIT_LEFT_Y

#define DIALOGUE_LINE_HEIGHT 10 /* Approximate line height for FONT_NORMAL */
#define DIALOGUE_DEFAULT_CHAR_RATE 45.0f
#define DIALOGUE_MIN_CHAR_RATE 5.0f
#define DIALOGUE_MAX_CHAR_RATE 240.0f
#define DIALOGUE_PUNCTUATION_PAUSE_SECONDS 0.3f

/* Punctuation characters that trigger a pause in the typewriter effect */
#define DIALOGUE_PUNCT_PERIOD '.'
#define DIALOGUE_PUNCT_COMMA ','
#define DIALOGUE_PUNCT_EXCLAMATION '!'
#define DIALOGUE_PUNCT_QUESTION '?'
#define DIALOGUE_PUNCT_SEMICOLON ';'

#define DIALOGUE_INSET_ANIMATION_DURATION 0.25f /* Duration in seconds for inset animation */

typedef struct dialogue_entry
{
    dialogue_speaker_t speaker;
    dialogue_position_t position;
    char *variant;       /* Portrait variant (NULL for default) */
    char *text_raw;      /* Original text string */
    char **pages;        /* Wrapped pages (null-terminated strings) */
    uint16_t page_count; /* Number of pages */
} dialogue_entry_t;

typedef struct dialogue_state
{
    dialogue_entry_t *entries;
    uint16_t entry_count;
    uint16_t entry_index;
    uint16_t page_index;
    uint32_t visible_chars;
    float char_accum;
    float char_rate_base;
    float punctuation_pause_accum;
    bool page_complete;
    bool active;
    bool advance_pressed_on_current_page;
} dialogue_state_t;

static dialogue_state_t s_state = {0};

/* Portrait mapping structure */
typedef struct portrait_entry
{
    dialogue_speaker_t speaker;
    const char *variant; /* NULL or empty string for default */
    sprite_t *sprite;
} portrait_entry_t;

/* Sprites */
static sprite_t *s_box_l = NULL;
static sprite_t *s_box_r = NULL;

/* Sound effects */
static wav64_t *s_sfxType = NULL;

/* Portrait mapping table - easily extensible */
#define PORTRAIT_MAP_SIZE 16
static portrait_entry_t s_portrait_map[PORTRAIT_MAP_SIZE] = {0};
static uint16_t s_portrait_map_count = 0;

/* Inset interpolation state */
static float s_fInsetCurrent = 0.0f;
static float s_fInsetAnimTimer = 0.0f;

/* Ease-in cubic: slow start, fast end */
static float ease_in_cubic(float t)
{
    return t * t * t;
}

/* Register a portrait sprite for a speaker and variant */
static bool register_portrait(dialogue_speaker_t _eSpeaker, const char *_pVariant, sprite_t *_pSprite)
{
    if (s_portrait_map_count >= PORTRAIT_MAP_SIZE)
    {
        debugf("dialogue: portrait map full, cannot register more portraits\n");
        return false;
    }

    s_portrait_map[s_portrait_map_count].speaker = _eSpeaker;
    s_portrait_map[s_portrait_map_count].variant = _pVariant;
    s_portrait_map[s_portrait_map_count].sprite = _pSprite;
    s_portrait_map_count++;
    return true;
}

/* Get portrait sprite for a speaker and variant (variant can be NULL for default) */
static sprite_t *get_portrait(dialogue_speaker_t _eSpeaker, const char *_pVariant)
{
    /* First, try to find exact match (speaker + variant) */
    if (_pVariant && _pVariant[0] != '\0')
    {
        for (uint16_t i = 0; i < s_portrait_map_count; ++i)
        {
            if (s_portrait_map[i].speaker == _eSpeaker && s_portrait_map[i].variant && strcmp(s_portrait_map[i].variant, _pVariant) == 0)
            {
                return s_portrait_map[i].sprite;
            }
        }
    }

    /* Fallback to default (variant is NULL or empty) */
    for (uint16_t i = 0; i < s_portrait_map_count; ++i)
    {
        if (s_portrait_map[i].speaker == _eSpeaker && (!s_portrait_map[i].variant || s_portrait_map[i].variant[0] == '\0'))
        {
            return s_portrait_map[i].sprite;
        }
    }

    return NULL;
}

/* Load and register a portrait variant - dynamically constructs sprite path using kSpeakerNames */
/* Usage: load_portrait_variant(DIALOGUE_SPEAKER_BOY, "sad") */
static void load_portrait_variant(dialogue_speaker_t _eSpeaker, const char *_pVariant)
{
    if (_eSpeaker >= DIALOGUE_SPEAKER_COUNT || !_pVariant || _pVariant[0] == '\0')
        return;

    /* Dynamically construct path using lowercase speaker name from kSpeakerNames */
    char path[128];
    snprintf(path, sizeof(path), "rom:/portrait_%s_%s_00.sprite", kSpeakerNames[_eSpeaker], _pVariant);

    sprite_t *pSprite = sprite_load(path);
    if (pSprite)
    {
        register_portrait(_eSpeaker, _pVariant, pSprite);
    }
}

/* Parse speaker name, optionally extracting variant (e.g., "boy_sad" -> speaker=boy, variant="sad") */
static dialogue_speaker_t parse_speaker(const char *_pToken, char **_ppVariant, bool *_pbValid)
{
    if (!_pToken)
    {
        if (_pbValid)
            *_pbValid = false;
        if (_ppVariant)
            *_ppVariant = NULL;
        return DIALOGUE_SPEAKER_BOY;
    }

    /* Case-insensitive match */
    const char *pToken = _pToken;
    char szLower[32] = {0};
    size_t uLen = strlen(pToken);
    if (uLen >= sizeof(szLower))
        uLen = sizeof(szLower) - 1;
    for (size_t i = 0; i < uLen; ++i)
        szLower[i] = (char)tolower((unsigned char)pToken[i]);
    szLower[uLen] = '\0';

    /* Check for variant (speaker_variant format) */
    const char *pUnderscore = strchr(szLower, '_');
    char szSpeakerOnly[32] = {0};
    const char *pVariantStr = NULL;

    if (pUnderscore)
    {
        /* Extract speaker name (before underscore) */
        size_t uSpeakerLen = (size_t)(pUnderscore - szLower);
        if (uSpeakerLen >= sizeof(szSpeakerOnly))
            uSpeakerLen = sizeof(szSpeakerOnly) - 1;
        memcpy(szSpeakerOnly, szLower, uSpeakerLen);
        szSpeakerOnly[uSpeakerLen] = '\0';

        /* Extract variant (after underscore) */
        pVariantStr = pUnderscore + 1;
        if (pVariantStr[0] == '\0')
            pVariantStr = NULL;
    }
    else
    {
        /* No variant, use full string as speaker name */
        size_t uCopyLen = strlen(szLower);
        if (uCopyLen >= sizeof(szSpeakerOnly))
            uCopyLen = sizeof(szSpeakerOnly) - 1;
        memcpy(szSpeakerOnly, szLower, uCopyLen);
        szSpeakerOnly[uCopyLen] = '\0';
    }

    dialogue_speaker_t eResult = DIALOGUE_SPEAKER_BOY;
    bool bValid = false;
    for (int i = 0; i < DIALOGUE_SPEAKER_COUNT; ++i)
    {
        if (strcmp(szSpeakerOnly, kSpeakerNames[i]) == 0)
        {
            eResult = (dialogue_speaker_t)i;
            bValid = true;
            break;
        }
    }

    /* Store variant if requested */
    if (_ppVariant && pVariantStr && pVariantStr[0] != '\0')
    {
        size_t uVariantLen = strlen(pVariantStr);
        char *pVariantCopy = malloc(uVariantLen + 1);
        if (pVariantCopy)
        {
            memcpy(pVariantCopy, pVariantStr, uVariantLen + 1);
            CACHE_FLUSH_DATA(pVariantCopy, uVariantLen + 1);
            *_ppVariant = pVariantCopy;
        }
        else
        {
            *_ppVariant = NULL;
        }
    }
    else if (_ppVariant)
    {
        *_ppVariant = NULL;
    }

    if (_pbValid)
        *_pbValid = bValid;
    return eResult;
}

static void free_entry(dialogue_entry_t *_pEntry)
{
    if (!_pEntry)
        return;
    if (_pEntry->text_raw)
    {
        free(_pEntry->text_raw);
        _pEntry->text_raw = NULL;
    }
    if (_pEntry->variant)
    {
        free(_pEntry->variant);
        _pEntry->variant = NULL;
    }
    if (_pEntry->pages)
    {
        for (uint16_t i = 0; i < _pEntry->page_count; ++i)
        {
            free(_pEntry->pages[i]);
        }
        free(_pEntry->pages);
        _pEntry->pages = NULL;
    }
    _pEntry->page_count = 0;
}

static void clear_entries(void)
{
    if (s_state.entries)
    {
        // HACK - DOES THIS EVEN MAKE SENSE?
        /* Ensure RSP is idle before freeing memory that might be referenced by queued commands. */
        rspq_wait();
        for (uint16_t i = 0; i < s_state.entry_count; ++i)
            free_entry(&s_state.entries[i]);
        free(s_state.entries);
        s_state.entries = NULL;
    }

    s_state.entry_count = 0;
    s_state.entry_index = 0;
    s_state.page_index = 0;
    s_state.visible_chars = 0;
    s_state.char_accum = 0.0f;
    s_state.punctuation_pause_accum = 0.0f;
    s_state.page_complete = false;
    s_state.advance_pressed_on_current_page = false;
}

static const char *get_data_folder(void)
{
    const char *pFolder = gp_state_get_current_folder();
    if (pFolder && pFolder[0])
        return pFolder;
    /* Fallback for SPACE / non-tilemap scenes */
    return "space";
}

static bool ensure_array_capacity(void **_ppArray, uint16_t *_pCapacity, uint16_t _uNeeded, size_t _uElemSize, bool _bZeroNew)
{
    if (_uNeeded <= *_pCapacity)
        return true;

    uint16_t uNewCapacity = (*_pCapacity == 0) ? 8 : (uint16_t)(*_pCapacity * 2);
    if (uNewCapacity < _uNeeded)
        uNewCapacity = _uNeeded;

    size_t uOldBytes = (size_t)(*_pCapacity) * _uElemSize;
    size_t uNewBytes = (size_t)uNewCapacity * _uElemSize;

    void *pNew = realloc(*_ppArray, uNewBytes);
    if (!pNew)
        return false;

    if (_bZeroNew && uNewBytes > uOldBytes)
        memset((uint8_t *)pNew + uOldBytes, 0, uNewBytes - uOldBytes);

    *_ppArray = pNew;
    *_pCapacity = uNewCapacity;
    return true;
}

static bool push_line(char ***_ppLines, uint16_t *_pLineCount, uint16_t *_pLineCapacity, const char *_pText)
{
    if (!ensure_array_capacity((void **)_ppLines, _pLineCapacity, (uint16_t)(*_pLineCount + 1), sizeof(char *), false))
        return false;

    size_t uLen = strlen(_pText);
    char *pCopy = malloc(uLen + 1);
    if (!pCopy)
        return false;
    memcpy(pCopy, _pText, uLen + 1);

    (*_ppLines)[*_pLineCount] = pCopy;
    (*_pLineCount)++;
    return true;
}

static float measure_width(const char *_pText)
{
    return font_helper_get_text_width(FONT_NORMAL, _pText ? _pText : "");
}

static bool flush_line_if_needed(char ***_ppLines, uint16_t *_pLineCount, uint16_t *_pLineCapacity, char *_pLineBuf)
{
    if (_pLineBuf[0] == '\0')
        return true;
    bool bOk = push_line(_ppLines, _pLineCount, _pLineCapacity, _pLineBuf);
    _pLineBuf[0] = '\0';
    return bOk;
}

static bool wrap_text_into_pages(const char *_pText, dialogue_entry_t *_pEntry)
{
    if (!_pText || !_pEntry)
        return false;

    int iPad = ui_get_overscan_padding();
    float fScaleX = (float)(SCREEN_W - iPad * 2) / (float)SCREEN_W;
    float fScaleY = (float)(SCREEN_H - iPad * 2) / (float)SCREEN_H;
    int iMaxWidth = (int)(DIALOGUE_TEXT_RECT_W * fScaleX);
    int iMaxHeight = (int)(DIALOGUE_TEXT_RECT_H * fScaleY) - UI_FONT_Y_OFFSET;
    if (iMaxWidth < 1)
        iMaxWidth = 1;
    if (iMaxHeight < DIALOGUE_LINE_HEIGHT)
        iMaxHeight = DIALOGUE_LINE_HEIGHT;

    const int iMaxLines = iMaxHeight / DIALOGUE_LINE_HEIGHT;
    const int iLinesPerPage = (iMaxLines > 0) ? iMaxLines : 1;

    /* Collect wrapped lines */
    char **lines = NULL;
    uint16_t uLineCount = 0;
    uint16_t uLineCapacity = 0;

    char linebuf[512] = {0};

    const char *p = _pText;
    while (*p)
    {
        if (*p == '\n')
        {
            if (!flush_line_if_needed((char ***)&lines, &uLineCount, &uLineCapacity, linebuf))
                goto error;
            p++;
            continue;
        }

        /* Extract word */
        char word[256] = {0};
        size_t wlen = 0;
        while (*p && !isspace((unsigned char)*p))
        {
            if (wlen + 1 < sizeof(word))
                word[wlen] = *p;
            wlen++;
            p++;
        }
        word[(wlen < sizeof(word)) ? wlen : sizeof(word) - 1] = '\0';

        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p) && *p != '\n')
            p++;

        bool line_empty = (linebuf[0] == '\0');
        char testbuf[512];
        snprintf(testbuf, sizeof(testbuf), "%s%s%s", linebuf, line_empty ? "" : " ", word);

        float test_width = measure_width(testbuf);
        if (test_width <= (float)iMaxWidth || line_empty)
        {
            /* Fits current line */
            if (!line_empty)
                strncat(linebuf, " ", sizeof(linebuf) - strlen(linebuf) - 1);
            strncat(linebuf, word, sizeof(linebuf) - strlen(linebuf) - 1);
            continue;
        }

        /* Doesn't fit, flush current line */
        if (!flush_line_if_needed((char ***)&lines, &uLineCount, &uLineCapacity, linebuf))
            goto error;

        /* If the word itself is too long, split it hard */
        float word_width = measure_width(word);
        if (word_width > (float)iMaxWidth)
        {
            size_t start = 0;
            while (start < wlen)
            {
                size_t take = 1;
                char chunk[256] = {0};
                for (; start + take <= wlen; ++take)
                {
                    size_t copy_len = take;
                    if (copy_len >= sizeof(chunk))
                        copy_len = sizeof(chunk) - 1;
                    memcpy(chunk, word + start, copy_len);
                    chunk[copy_len] = '\0';
                    if (measure_width(chunk) > (float)iMaxWidth)
                    {
                        if (copy_len > 1)
                            chunk[copy_len - 1] = '\0';
                        break;
                    }
                }
                /* Copy chunk into line buffer safely */
                linebuf[0] = '\0';
                strncat(linebuf, chunk, sizeof(linebuf) - 1);
                if (!flush_line_if_needed((char ***)&lines, &uLineCount, &uLineCapacity, linebuf))
                    goto error;
                start += strlen(chunk);
            }
        }
        else
        {
            /* Start new line with this word */
            linebuf[0] = '\0';
            strncat(linebuf, word, sizeof(linebuf) - 1);
        }
    }

    if (!flush_line_if_needed((char ***)&lines, &uLineCount, &uLineCapacity, linebuf))
        goto error;

    if (uLineCount == 0)
    {
        /* Ensure at least one empty line/page */
        if (!push_line((char ***)&lines, &uLineCount, &uLineCapacity, ""))
            goto error;
    }

    /* Build pages */
    uint16_t uPageCount = (uint16_t)((uLineCount + iLinesPerPage - 1) / iLinesPerPage);
    _pEntry->pages = calloc(uPageCount, sizeof(char *));
    if (!_pEntry->pages)
        goto error;
    _pEntry->page_count = uPageCount;

    for (uint16_t uPageIndex = 0; uPageIndex < uPageCount; ++uPageIndex)
    {
        uint16_t uStartLine = (uint16_t)(uPageIndex * iLinesPerPage);
        uint16_t uEndLine = uStartLine + iLinesPerPage;
        if (uEndLine > uLineCount)
            uEndLine = uLineCount;

        /* Compute combined length */
        size_t total_len = 0;
        for (uint16_t i = uStartLine; i < uEndLine; ++i)
            total_len += strlen(lines[i]) + 1; /* + newline or terminator */

        char *page_text = malloc(total_len + 1);
        if (!page_text)
            goto error;

        page_text[0] = '\0';
        for (uint16_t i = uStartLine; i < uEndLine; ++i)
        {
            strcat(page_text, lines[i]);
            if (i + 1 < uEndLine)
                strcat(page_text, "\n");
        }

        /* Flush cache after CPU writes to string (safety: rdpq_text_printf may DMA read) */
        CACHE_FLUSH_DATA(page_text, total_len + 1);

        _pEntry->pages[uPageIndex] = page_text;
    }

    /* Free temp lines */
    for (uint16_t i = 0; i < uLineCount; ++i)
        free(lines[i]);
    free(lines);

    return true;

error:
    if (lines)
    {
        for (uint16_t i = 0; i < uLineCount; ++i)
            free(lines[i]);
        free(lines);
    }
    return false;
}

static bool parse_line_to_entry(const char *_pLine, dialogue_entry_t *_pOutEntry)
{
    if (!_pLine || !_pOutEntry)
        return false;

    /* Find first and second commas to isolate speaker, position, text */
    const char *pFirst = strchr(_pLine, ',');
    if (!pFirst)
        return false;
    const char *pSecond = strchr(pFirst + 1, ',');
    if (!pSecond)
        return false;

    /* speaker (may include variant, e.g., "boy_sad") */
    size_t uSpeakerLen = (size_t)(pFirst - _pLine);
    char szSpeaker[32] = {0};
    if (uSpeakerLen >= sizeof(szSpeaker))
        uSpeakerLen = sizeof(szSpeaker) - 1;
    memcpy(szSpeaker, _pLine, uSpeakerLen);
    szSpeaker[uSpeakerLen] = '\0';

    char *pVariant = NULL;
    bool bSpeakerValid = true;
    dialogue_speaker_t eSpeaker = parse_speaker(szSpeaker, &pVariant, &bSpeakerValid);
    if (!bSpeakerValid)
    {
        if (pVariant)
            free(pVariant);
        return false;
    }

    /* position */
    const char *pPosToken = pFirst + 1;
    size_t uPosLen = (size_t)(pSecond - pPosToken);
    char szPos[16] = {0};
    if (uPosLen >= sizeof(szPos))
        uPosLen = sizeof(szPos) - 1;
    memcpy(szPos, pPosToken, uPosLen);
    szPos[uPosLen] = '\0';

    int iPos = 0;
    if (!csv_helper_parse_int(szPos, &iPos))
        return false;
    dialogue_position_t ePosition = (iPos == 0) ? DIALOGUE_POSITION_TOP : DIALOGUE_POSITION_BOTTOM;

    /* text (rest of line) */
    const char *pTextToken = pSecond + 1;
    if (!pTextToken || pTextToken[0] == '\0')
        return false;

    size_t uTextLen = strlen(pTextToken);
    char *pTextCopy = malloc(uTextLen + 1);
    if (!pTextCopy)
        return false;
    memcpy(pTextCopy, pTextToken, uTextLen + 1);

    /* Flush cache after CPU write to string (safety: may be DMA'd later) */
    CACHE_FLUSH_DATA(pTextCopy, uTextLen + 1);

    _pOutEntry->speaker = eSpeaker;
    _pOutEntry->position = ePosition;
    _pOutEntry->variant = pVariant;
    _pOutEntry->text_raw = pTextCopy;
    _pOutEntry->pages = NULL;
    _pOutEntry->page_count = 0;

    return true;
}

static bool parse_csv_buffer(char *_pBuffer, dialogue_entry_t **_ppOutEntries, uint16_t *_pOutCount)
{
    if (!_pBuffer || !_ppOutEntries || !_pOutCount)
        return false;

    uint16_t uCapacity = 0;
    uint16_t uCount = 0;
    dialogue_entry_t *pEntries = NULL;

    char *pSave = NULL;
    char *pLine = strtok_r(_pBuffer, "\n", &pSave);
    while (pLine)
    {
        csv_helper_strip_eol(pLine);
        if (pLine[0] != '\0')
        {
            if (!ensure_array_capacity((void **)&pEntries, &uCapacity, (uint16_t)(uCount + 1), sizeof(dialogue_entry_t), true))
                goto error;

            if (parse_line_to_entry(pLine, &pEntries[uCount]))
            {
                if (!wrap_text_into_pages(pEntries[uCount].text_raw, &pEntries[uCount]))
                    goto error;
                uCount++;
            }
        }

        pLine = strtok_r(NULL, "\n", &pSave);
    }

    if (uCount == 0)
        goto error;

    *_ppOutEntries = pEntries;
    *_pOutCount = uCount;
    return true;

error:
    if (pEntries)
    {
        for (uint16_t i = 0; i < uCount; ++i)
            free_entry(&pEntries[i]);
        free(pEntries);
    }
    return false;
}

static void reset_state_for_start(void)
{
    s_state.entry_index = 0;
    s_state.page_index = 0;
    s_state.visible_chars = 0;
    s_state.char_accum = 0.0f;
    s_state.punctuation_pause_accum = 0.0f;
    s_state.page_complete = false;
    s_state.advance_pressed_on_current_page = false;
}

bool dialogue_init(void)
{
    if (!s_box_l)
        s_box_l = sprite_load("rom:/hud_dialogue_box_l_00.sprite");
    if (!s_box_r)
        s_box_r = sprite_load("rom:/hud_dialogue_box_r_00.sprite");

    /* Load typewriter sound effect */
    if (!s_sfxType)
        s_sfxType = wav64_load("rom:/ui_type.wav64", &(wav64_loadparms_t){.streaming_mode = 0});

    /* Clear portrait map */
    s_portrait_map_count = 0;
    memset(s_portrait_map, 0, sizeof(s_portrait_map));

    /* Load default portraits for each speaker */
    for (int i = 0; i < DIALOGUE_SPEAKER_COUNT; ++i)
    {
        char path[64];
        snprintf(path, sizeof(path), "rom:/portrait_%s_00.sprite", kSpeakerNames[i]);
        sprite_t *pSprite = sprite_load(path);
        if (pSprite)
        {
            register_portrait((dialogue_speaker_t)i, NULL, pSprite);
        }
    }

    /* Load variant portraits - easily extensible by adding more entries here */
    /* Usage: load_portrait_variant(DIALOGUE_SPEAKER_BOY, "sad") */
    load_portrait_variant(DIALOGUE_SPEAKER_BOY, "sad");
    load_portrait_variant(DIALOGUE_SPEAKER_BOY, "angry");
    load_portrait_variant(DIALOGUE_SPEAKER_BOY, "worried");
    load_portrait_variant(DIALOGUE_SPEAKER_ALIEN, "surprise");
    load_portrait_variant(DIALOGUE_SPEAKER_RHINO, "surprise");

    s_state.char_rate_base = DIALOGUE_DEFAULT_CHAR_RATE;
    return true;
}

void dialogue_free(void)
{
    clear_entries();
    s_state.active = false;
    s_fInsetCurrent = 0.0f;
    s_fInsetAnimTimer = 0.0f;

    SAFE_FREE_SPRITE(s_box_l);
    SAFE_FREE_SPRITE(s_box_r);
    SAFE_CLOSE_WAV64(s_sfxType);

    /* Free all portrait sprites */
    for (uint16_t i = 0; i < s_portrait_map_count; ++i)
    {
        SAFE_FREE_SPRITE(s_portrait_map[i].sprite);
    }
    s_portrait_map_count = 0;
}

bool dialogue_start(const char *_pszCsvFilename)
{
    clear_entries();

    if (!_pszCsvFilename || _pszCsvFilename[0] == '\0')
        return false;

    const char *pFolder = get_data_folder();
    char szPath[128];
    snprintf(szPath, sizeof(szPath), "rom:/%s/%s.csv", pFolder, _pszCsvFilename);

    char *pFileData = NULL;
    size_t uFileSize = 0;
    if (!csv_helper_load_file(szPath, &pFileData, &uFileSize))
    {
        debugf("dialogue_start: failed to load %s\n", szPath);
        return false;
    }

    bool bOk = parse_csv_buffer(pFileData, &s_state.entries, &s_state.entry_count);
    free(pFileData);
    if (!bOk)
    {
        debugf("dialogue_start: failed to parse %s\n", szPath);
        return false;
    }

    reset_state_for_start();
    s_state.active = true;

    /* Disengage tractor beam when dialogue starts */
    tractor_beam_disengage();

    /* Reset inset animation so box animates in from off-screen */
    s_fInsetCurrent = 0.0f;
    s_fInsetAnimTimer = 0.0f;

    /* Set music speed to minimum when dialogue starts */
    if (gp_state_get() == SPACE)
    {
        audio_update_music_speed(AUDIO_SPEED_MIN);
    }

    return true;
}

static dialogue_entry_t *current_entry(void)
{
    if (!s_state.entries || s_state.entry_index >= s_state.entry_count)
        return NULL;
    return &s_state.entries[s_state.entry_index];
}

static const char *current_page_text(dialogue_entry_t *_pEntry)
{
    if (!_pEntry || s_state.page_index >= _pEntry->page_count)
        return NULL;
    return _pEntry->pages[s_state.page_index];
}

static void advance_page_or_entry(void)
{
    dialogue_entry_t *pEntry = current_entry();
    if (!pEntry)
    {
        s_state.active = false;
        return;
    }

    if (s_state.page_index + 1 < pEntry->page_count)
    {
        s_state.page_index++;
    }
    else if (s_state.entry_index + 1 < s_state.entry_count)
    {
        s_state.entry_index++;
        s_state.page_index = 0;
    }
    else
    {
        /* Dialogue completed - enter transition out mode */
        /* Don't clear entries yet, let inset animation reverse */
        s_state.active = false;
        return;
    }

    s_state.visible_chars = 0;
    s_state.char_accum = 0.0f;
    s_state.punctuation_pause_accum = 0.0f;
    s_state.page_complete = false;
    s_state.advance_pressed_on_current_page = false;
}

void dialogue_update(bool _bAdvancePressed, bool _bAdvanceDown)
{
    float fDelta = frame_time_delta_seconds();

    dialogue_entry_t *pEntry = current_entry();
    const char *pPageText = pEntry ? current_page_text(pEntry) : NULL;

    /* Calculate target inset: 0 if inactive/invalid, otherwise from current entry */
    float fTargetInset = 0.0f;
    bool bInsetTop = true;

    if (s_state.active && pEntry && pPageText)
    {
        /* Calculate target inset from current entry: UI_DESIGNER_PADDING + box sprite height */
        /* Both box sprites have the same height, so just use s_box_l */
        sprite_t *pBoxSprite = s_box_l;
        int iInsetHeight = UI_DESIGNER_PADDING + (pBoxSprite ? pBoxSprite->height : DIALOGUE_TEXT_RECT_H);
        fTargetInset = (float)iInsetHeight;
        bInsetTop = (pEntry->position == DIALOGUE_POSITION_TOP);
    }

    /* Check if target changed - start new animation */
    static float s_fLastTargetInset = -1.0f;
    static float s_fDisappearStartValue = 0.0f; /* Captured when dialogue starts disappearing */

    if (fabsf(fTargetInset - s_fLastTargetInset) > 0.5f)
    {
        /* Target changed - capture start value for disappearing animation */
        if (fTargetInset < 0.5f && s_fLastTargetInset > 0.5f)
        {
            /* Dialogue is disappearing - capture current value as start */
            s_fDisappearStartValue = s_fInsetCurrent;
        }
        s_fLastTargetInset = fTargetInset;
        s_fInsetAnimTimer = 0.0f;
    }

    /* Update animation if current != target (for visual box animation) */
    if (fabsf(s_fInsetCurrent - fTargetInset) > 0.01f)
    {
        s_fInsetAnimTimer += fDelta;

        /* Calculate progress (0 to 1) */
        float fProgress = s_fInsetAnimTimer / DIALOGUE_INSET_ANIMATION_DURATION;
        if (fProgress >= 1.0f)
        {
            s_fInsetCurrent = fTargetInset;
        }
        else
        {
            /* Apply ease-in cubic curve */
            float fEased = ease_in_cubic(fProgress);

            /* Determine start value: dialogue always starts from 0 (appearing) or from captured value (disappearing) */
            float fStartValue = (fTargetInset > 0.5f) ? 0.0f : s_fDisappearStartValue;

            /* Interpolate from start to target */
            s_fInsetCurrent = fStartValue + (fTargetInset - fStartValue) * fEased;
        }
    }
    else
    {
        /* Close enough - snap to target */
        s_fInsetCurrent = fTargetInset;
    }

    /* Update camera with target value immediately (no animation for camera) */
    gp_camera_set_dialogue_inset((int)fTargetInset, bInsetTop);

    /* Check if we're in transition out mode: inactive but entries still exist */
    bool bInTransitionOut = (!s_state.active && s_state.entries != NULL);

    /* If transition out animation has completed (reached 0), clear entries */
    if (bInTransitionOut && fabsf(s_fInsetCurrent) < 0.01f)
    {
        clear_entries();
        return;
    }

    /* Early return if dialogue is inactive and not in transition out */
    if (!s_state.active && !bInTransitionOut)
        return;

    if (!pEntry || !pPageText)
    {
        /* Entry is invalid - deactivate */
        s_state.active = false;
        clear_entries();
        return;
    }

    /* Only start typing when inset animation has reached its target */
    /* Check if we're at the target inset (box has fully animated in) */
    float fTargetInsetForTyping = (float)(UI_DESIGNER_PADDING + (s_box_l ? s_box_l->height : DIALOGUE_TEXT_RECT_H));
    bool bInsetAnimationComplete = (fabsf(s_fInsetCurrent - fTargetInsetForTyping) < 0.01f);

    if (!bInsetAnimationComplete)
    {
        /* Don't start typing yet - wait for animation to complete */
        return;
    }

    size_t uPageLen = strlen(pPageText);

    /* Check for page advance FIRST (before setting fast-forward flag) */
    if (_bAdvancePressed && s_state.page_complete)
    {
        advance_page_or_entry();
        return;
    }

    /* Mark that A has been pressed on current page (required before fast-forward can activate) */
    /* Only set this if we're NOT advancing (already checked above) */
    if (_bAdvancePressed)
    {
        s_state.advance_pressed_on_current_page = true;
    }

    /* Fast-reveal: only if A has been pressed on current page */
    bool bBoost = s_state.advance_pressed_on_current_page && (_bAdvanceDown || _bAdvancePressed) && (s_state.visible_chars > 0);
    const float fCharRate = bBoost ? DIALOGUE_MAX_CHAR_RATE : s_state.char_rate_base;

    /* Check for punctuation pause (skip if fast-forwarding) */
    if (!bBoost && s_state.visible_chars > 0 && s_state.visible_chars <= uPageLen)
    {
        char cPrev = pPageText[s_state.visible_chars - 1];
        if (cPrev == DIALOGUE_PUNCT_PERIOD || cPrev == DIALOGUE_PUNCT_COMMA || cPrev == DIALOGUE_PUNCT_EXCLAMATION || cPrev == DIALOGUE_PUNCT_QUESTION ||
            cPrev == DIALOGUE_PUNCT_SEMICOLON)
        {
            /* Pause at punctuation */
            float fDelta = frame_time_delta_seconds();
            s_state.punctuation_pause_accum += fDelta;
            if (s_state.punctuation_pause_accum < DIALOGUE_PUNCTUATION_PAUSE_SECONDS)
            {
                return; /* Still pausing */
            }
            s_state.punctuation_pause_accum = 0.0f;
        }
        else
        {
            s_state.punctuation_pause_accum = 0.0f;
        }
    }

    /* Typewriter progression (fDelta already calculated above) */
    s_state.char_accum += fDelta * fCharRate;
    uint32_t add = (uint32_t)s_state.char_accum;
    if (add > 0)
    {
        s_state.char_accum -= (float)add;

        /* Process characters one at a time to handle escape sequences */
        while (add > 0 && s_state.visible_chars < uPageLen)
        {
            /* If we encounter an escape sequence (^ followed by 2 chars), skip all 3 at once */
            if (pPageText[s_state.visible_chars] == '^' && s_state.visible_chars + 2 < uPageLen)
            {
                /* Skip entire escape sequence (^ + 2 chars = 3 chars total) */
                s_state.visible_chars += 3;
                add--; /* Consume one "character slot" from the accumulator */
                continue;
            }

            /* Normal character advancement */
            s_state.visible_chars++;
            add--;

            /* Play typewriter sound for each new character (skip spaces and escape sequences) */
            if (s_sfxType && pEntry)
            {
                char cNewChar = pPageText[s_state.visible_chars - 1];
                /* Only play sound for visible characters (not spaces or control chars) */
                if (cNewChar != ' ' && cNewChar != '\n' && cNewChar != '\t')
                {
                    /* Get speaker-specific base frequency multiplier */
                    float fSpeakerBaseMult = 1.0f; /* Default to normal */
                    switch (pEntry->speaker)
                    {
                    case DIALOGUE_SPEAKER_BOY:
                        fSpeakerBaseMult = rngf(0.95f, 1.05f);
                        break;
                    case DIALOGUE_SPEAKER_ALIEN:
                        fSpeakerBaseMult = rngf(1.2f, 1.4f);
                        break;
                    case DIALOGUE_SPEAKER_RHINO:
                        fSpeakerBaseMult = rngf(0.7f, 0.8f);
                        break;
                    default:
                        fSpeakerBaseMult = 1.0f;
                        break;
                    }

                    /* Apply speaker-specific base frequency with additional random variation */
                    float fBaseFreq = AUDIO_BITRATE * 0.5f;    /* Base frequency (half sample rate) */
                    float fFreqVariation = rngf(0.95f, 1.05f); /* Small random variation: 95% to 105% */
                    float fFreq = fBaseFreq * fSpeakerBaseMult * fFreqVariation;

                    wav64_play(s_sfxType, MIXER_CHANNEL_USER_INTERFACE);
                    mixer_ch_set_freq(MIXER_CHANNEL_USER_INTERFACE, fFreq);
                }
            }
        }

        if (s_state.visible_chars >= uPageLen)
        {
            s_state.visible_chars = (uint32_t)uPageLen;
            s_state.page_complete = true;
        }
    }
}

bool dialogue_is_active(void)
{
    /* Return true if active, or if in transition out mode (inactive but entries still exist) */
    return s_state.active || s_state.entries != NULL;
}

int dialogue_get_current_entry_index(void)
{
    if (!dialogue_is_active())
        return -1;
    return (int)s_state.entry_index;
}

void dialogue_render(void)
{
    /* Check if we're in transition out mode: inactive but entries still exist */
    bool bInTransitionOut = (!s_state.active && s_state.entries != NULL);

    if (!s_state.active && !bInTransitionOut)
        return;

    dialogue_entry_t *pEntry = current_entry();
    const char *pPageText = pEntry ? current_page_text(pEntry) : NULL;
    if (!pEntry || !pPageText)
        return;

    /* Determine which box sprite to use (for portrait positioning) */
    const bool bPortraitLeft = (pEntry->speaker == DIALOGUE_SPEAKER_BOY);
    sprite_t *pBoxSprite = bPortraitLeft ? s_box_l : s_box_r;

    int iPad = ui_get_overscan_padding();
    float fScaleX = (float)(SCREEN_W - iPad * 2) / (float)SCREEN_W;
    float fScaleY = (float)(SCREEN_H - iPad * 2) / (float)SCREEN_H;

    int iBoxX = iPad + (int)(DIALOGUE_BOX_SPRITE_OFFSET_X * fScaleX);
    int iBoxY;
    int iBoxH = pBoxSprite ? (int)(pBoxSprite->height * fScaleY) : (int)(DIALOGUE_TEXT_RECT_H * fScaleY);

    /* Calculate base Y position */
    bool bIsTop = (pEntry->position == DIALOGUE_POSITION_TOP);
    int iBaseY;
    if (bIsTop)
    {
        iBaseY = iPad + (int)(DIALOGUE_BOX_SPRITE_OFFSET_Y * fScaleY);
    }
    else
    {
        iBaseY = SCREEN_H - iPad - (int)(DIALOGUE_BOX_SPRITE_OFFSET_Y * fScaleY) - iBoxH;
    }

    /* Apply animated inset offset: slide box in/out based on interpolation */
    /* Calculate target inset for this entry (use unscaled height to match update function) */
    float fTargetInset = (float)(UI_DESIGNER_PADDING + (pBoxSprite ? pBoxSprite->height : DIALOGUE_TEXT_RECT_H));

    /* Calculate animation progress: 0 = fully off-screen, 1 = fully on-screen */
    /* During entry: progress goes from 0.0 to 1.0 */
    /* During exit: progress goes from 1.0 (fully on-screen) to 0.0 (fully off-screen) */
    float fInsetProgress = 0.0f;
    if (fTargetInset > 0.5f)
    {
        fInsetProgress = s_fInsetCurrent / fTargetInset;
        fInsetProgress = clampf_01(fInsetProgress);
    }

    /* Calculate box Y position with animation: box slides from off-screen when progress < 1 */
    /* Top box: starts at Y = -iBoxH (fully above screen, bottom edge at Y=0) */
    /* Bottom box: starts at Y = SCREEN_H (fully below screen, top edge at Y=SCREEN_H) */
    if (bIsTop)
    {
        /* Top box: slides down from above */
        /* When progress=0: box at Y = -iBoxH (fully above screen) */
        /* When progress=1: box at iBaseY (final position) */
        /* Direct interpolation: iBoxY = -iBoxH + (iBaseY - (-iBoxH)) * progress */
        iBoxY = (int)(-(float)iBoxH + ((float)iBaseY + (float)iBoxH) * fInsetProgress);
    }
    else
    {
        /* Bottom box: slides up from below */
        /* When progress=0: box at Y = SCREEN_H (fully below screen) */
        /* When progress=1: box at iBaseY (final position) */
        /* Direct interpolation: iBoxY = SCREEN_H + (iBaseY - SCREEN_H) * progress */
        iBoxY = (int)((float)SCREEN_H + ((float)iBaseY - (float)SCREEN_H) * fInsetProgress);
    }

    /* Draw speaker first (use standard mode when scaling, copy mode when 1:1) */
    /* Get portrait using variant from entry (NULL if no variant specified) */
    sprite_t *pPortrait = get_portrait(pEntry->speaker, pEntry->variant);
    if (pPortrait)
    {
        int iPortraitX = bPortraitLeft ? (iBoxX + (int)(DIALOGUE_PORTRAIT_LEFT_X * fScaleX)) : (iBoxX + (int)(DIALOGUE_PORTRAIT_RIGHT_X * fScaleX));
        int iPortraitY = (pEntry->position == DIALOGUE_POSITION_TOP) ? (iBoxY + (int)(DIALOGUE_PORTRAIT_LEFT_Y * fScaleY)) : (iBoxY + (int)(DIALOGUE_PORTRAIT_LEFT_Y * fScaleY));
        rdpq_blitparms_t parms = {0};
        parms.scale_x = fScaleX;
        parms.scale_y = fScaleY;

        /* Use standard mode when scaling is applied (avoids RDPQ validation warning) */
        if (fScaleX != 1.0f || fScaleY != 1.0f)
        {
            rdpq_set_mode_standard();
        }
        else
        {
            rdpq_set_mode_copy(false);
        }

        rdpq_sprite_blit(pPortrait, iPortraitX, iPortraitY, &parms);
    }

    if (pBoxSprite)
    {
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_blitparms_t box_parms = {0};
        box_parms.scale_x = fScaleX;
        box_parms.scale_y = fScaleY;
        rdpq_sprite_blit(pBoxSprite, iBoxX, iBoxY, &box_parms);
    }

    /* Text area (adjust width/height for overscan padding) */
    int iTextX = iBoxX + (int)((bPortraitLeft ? DIALOGUE_TEXT_OFFSET_X_LEFT : DIALOGUE_TEXT_OFFSET_X_RIGHT) * fScaleX);
    int iTextY = iBoxY + (int)(DIALOGUE_TEXT_OFFSET_Y * fScaleY) + UI_FONT_Y_OFFSET;

    /* Clamp visible chars to page length */
    size_t uPageLen = strlen(pPageText);
    uint32_t uVisible = s_state.visible_chars;
    if (uVisible > uPageLen)
        uVisible = (uint32_t)uPageLen;

    /* Apply reduced width/height for overscan when wrapping was computed */
    int iWrapWidth = (int)(DIALOGUE_TEXT_RECT_W * fScaleX);
    int iWrapHeight = (int)(DIALOGUE_TEXT_RECT_H * fScaleY);
    if (iWrapWidth < 1)
        iWrapWidth = 1;
    if (iWrapHeight < DIALOGUE_LINE_HEIGHT)
        iWrapHeight = DIALOGUE_LINE_HEIGHT;

    /* Render text */
    rdpq_text_printf(NULL, FONT_NORMAL, iTextX, iTextY, "%.*s", (int)uVisible, pPageText);
}
