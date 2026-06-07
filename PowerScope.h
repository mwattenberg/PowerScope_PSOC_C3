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

/* Select DMA vs polling transfer. */
#define USE_DMA

/* DMA channel assignments — override before including this header if needed. */
#ifndef PS_DMA_HW
#define PS_DMA_HW       DW0
#endif
#ifndef PS_DMA_CHANNEL
#define PS_DMA_CHANNEL  0U
#endif
#ifndef PS_DMA_IRQ
#define PS_DMA_IRQ      cpuss_interrupts_dw0_0_IRQn
#endif

typedef struct {
    channel_data_t data[PowerScope_NUMBER_OF_FRAMES * (PowerScope_NUMBER_OF_CHANNELS + 1)];
    uint16_t current_frame;
    bool data_is_currently_being_written;
} PowerScope_buffer_t;

/* Initialize PowerScope.
 *
 * hw: pointer to the SCB peripheral to use for transmission (e.g. UART_HW,
 *     SPI_HW). The caller must have already called Cy_SCB_UART_Init /
 *     Cy_SCB_UART_Enable  OR  Cy_SCB_SPI_Init / Cy_SCB_SPI_Enable on this
 *     peripheral before calling PowerScope_init. PowerScope reads the SCB
 *     MODE field at runtime to determine which protocol is active — no
 *     compile-time define is required. */
void PowerScope_init(CySCB_Type *hw);
void PowerScope_update(channel_data_t *channel_data);

#endif /* POWERSCOPE_H_ */
