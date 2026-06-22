#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32l4xx_hal.h"
void play_note(uint8_t num,uint16_t time);
extern const uint16_t note_freq[128];

#endif
