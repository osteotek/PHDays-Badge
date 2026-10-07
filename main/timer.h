#pragma once

// Pomodoro timer: focus, then break, then idle. Each phase end plays the
// alert melody from settings.
#include "screens.h"

void timer_init(void);
void timer_start(void);  // (re)starts a focus phase
void timer_stop(void);
void timer_skip(void);   // ends the current phase early
void timer_toggle(void); // button: start when idle, otherwise stop
void timer_get(screen_timer_t *out);
const char *timer_phase_name(timer_phase_t phase);
