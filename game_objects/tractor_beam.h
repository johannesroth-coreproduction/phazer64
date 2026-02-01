#pragma once

#include <stdbool.h>

/* Initialize tractor beam resources */
void tractor_beam_init(void);

/* Free tractor beam resources */
void tractor_beam_free(void);

/* Update tractor beam logic */
void tractor_beam_update(bool _bBeamPressed, bool _bTurnCW, bool _bTurnCCW, bool _bRotateCW, bool _bRotateCCW, bool _bExtend, bool _bRetract);

/* Render tractor beam */
void tractor_beam_render(void);

/* Render tractor beam UI */
void tractor_beam_render_ui(void);

/* Check if active */
bool tractor_beam_is_active(void);

/* Disengage tractor beam (stop sound and release target) */
void tractor_beam_disengage(void);