/*
 * PowerScope.h
 *
 * Self-contained data buffering and UART/DMA transmission module.
 * Adapted from 12kW_Primary_PLECS/PowerScope.h
 */

#ifndef POWERSCOPE_H_
#define POWERSCOPE_H_

#include "cybsp.h"
#include <stdint.h>
#include <stdbool.h>

typedef int16_t channel_data_t;

#define PowerScope_NUMBER_OF_FRAMES     1UL
#define PowerScope_NUMBER_OF_CHANNELS   8
#define PowerScope_START_SEQUENCE       ((channel_data_t)0xAAAA)
#define PowerScope_CHANNEL_DATA_SIZE    sizeof(channel_data_t)

#define USE_DMA

typedef struct {
    channel_data_t data[PowerScope_NUMBER_OF_FRAMES * (PowerScope_NUMBER_OF_CHANNELS + 1)];
    uint16_t current_frame;
    bool data_is_currently_being_written;
} PowerScope_buffer_t;

void PowerScope_init(void);
void PowerScope_update(channel_data_t *channel_data);

#endif /* POWERSCOPE_H_ */
