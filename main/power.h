#pragma once

// Dynamic CPU frequency (80-160 MHz) always; automatic light sleep between
// frames when enabled (the power_save setting).
#include <stdbool.h>

void power_apply(bool light_sleep);
