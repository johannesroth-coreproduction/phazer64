#include "stick_normalizer.h"
#include "save.h"
#include <stdbool.h>

/* Calibration values: min/max for each axis */
static int8_t s_iMinX = -STICK_NORMALIZED_MAX;
static int8_t s_iMaxX = STICK_NORMALIZED_MAX;
static int8_t s_iMinY = -STICK_NORMALIZED_MAX;
static int8_t s_iMaxY = STICK_NORMALIZED_MAX;

/* Current normalized values */
static int8_t s_iNormalizedX = 0;
static int8_t s_iNormalizedY = 0;

/* Helper function to clamp value to range */
static int8_t clamp_i8(int value, int8_t min_val, int8_t max_val)
{
    if (value < min_val)
        return min_val;
    if (value > max_val)
        return max_val;
    return (int8_t)value;
}

/* Helper function to normalize a single axis */
static int8_t normalize_axis(int8_t raw_value, int8_t min_cal, int8_t max_cal)
{
    /* Handle edge case: if calibration range is invalid, pass through raw value */
    if (max_cal <= 0 || min_cal >= 0 || max_cal <= -min_cal - 10)
    {
        /* Invalid calibration, clamp raw value to normalized range */
        return clamp_i8(raw_value, -STICK_NORMALIZED_MAX, STICK_NORMALIZED_MAX);
    }

    /* Determine which side of neutral we're on */
    if (raw_value >= 0)
    {
        /* Positive side: scale [0, max_cal] to [0, STICK_NORMALIZED_MAX] */
        if (max_cal <= 0)
            return 0;

        /* Scale: normalized = raw * (STICK_NORMALIZED_MAX / max_cal) */
        int normalized = (raw_value * STICK_NORMALIZED_MAX) / max_cal;
        return clamp_i8(normalized, 0, STICK_NORMALIZED_MAX);
    }
    else
    {
        /* Negative side: scale [min_cal, 0] to [-STICK_NORMALIZED_MAX, 0] */
        if (min_cal >= 0)
            return 0;

        /* Scale: normalized = raw * (STICK_NORMALIZED_MAX / abs(min_cal)) */
        int normalized = (raw_value * STICK_NORMALIZED_MAX) / (-min_cal);
        return clamp_i8(normalized, -STICK_NORMALIZED_MAX, 0);
    }
}

void stick_normalizer_init(void)
{
    /* Load calibration from save system */
    save_get_stick_calibration(&s_iMinX, &s_iMaxX, &s_iMinY, &s_iMaxY);

    /* Initialize normalized values to neutral */
    s_iNormalizedX = 0;
    s_iNormalizedY = 0;
}

void stick_normalizer_update(int8_t raw_x, int8_t raw_y)
{
    /* Normalize each axis independently */
    s_iNormalizedX = normalize_axis(raw_x, s_iMinX, s_iMaxX);
    s_iNormalizedY = normalize_axis(raw_y, s_iMinY, s_iMaxY);
}

int8_t stick_normalizer_get_x(void)
{
    return s_iNormalizedX;
}

int8_t stick_normalizer_get_y(void)
{
    return s_iNormalizedY;
}

void stick_normalizer_set_calibration(int8_t min_x, int8_t max_x, int8_t min_y, int8_t max_y)
{
    s_iMinX = min_x;
    s_iMaxX = max_x;
    s_iMinY = min_y;
    s_iMaxY = max_y;
}

void stick_normalizer_get_calibration(int8_t *min_x, int8_t *max_x, int8_t *min_y, int8_t *max_y)
{
    if (min_x)
        *min_x = s_iMinX;
    if (max_x)
        *max_x = s_iMaxX;
    if (min_y)
        *min_y = s_iMinY;
    if (max_y)
        *max_y = s_iMaxY;
}
