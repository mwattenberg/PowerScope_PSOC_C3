/*
 * PowerScope.c
 *
 * Self-contained data buffering and UART/DMA transmission module.
 * Adapted from 12kW_Primary_PLECS/PowerScope.c
 */

#include "PowerScope.h"
#include <stdbool.h>
#include "cy_sysint.h"
#include "cy_scb_uart.h"
#include "cy_scb_spi.h"
#ifdef USE_DMA
#include "cy_dma.h"
#endif

#define PS_SCB_MODE_SPI   (1U)
#define PS_SCB_MODE_UART  (2U)

static CySCB_Type *PS_scbHw;
static uint32_t    PS_scbMode;
#ifdef USE_DMA

static cy_stc_dma_descriptor_t PS_dmaDescriptor;

static cy_stc_dma_descriptor_config_t PS_dmaDescriptorConfig =
{
    .retrigger       = CY_DMA_RETRIG_IM,
    .interruptType   = CY_DMA_DESCR,
    .triggerOutType  = CY_DMA_X_LOOP,
    .channelState    = CY_DMA_CHANNEL_DISABLED,
    .triggerInType   = CY_DMA_X_LOOP,
    .dataSize        = CY_DMA_BYTE,
    .srcTransferSize = CY_DMA_TRANSFER_SIZE_DATA,
    .dstTransferSize = CY_DMA_TRANSFER_SIZE_WORD,
    .descriptorType  = CY_DMA_1D_TRANSFER,
    .srcAddress      = NULL,
    .dstAddress      = NULL,
    .srcXincrement   = 1,
    .dstXincrement   = 0,
    .xCount          = 8,
    .srcYincrement   = 1,
    .dstYincrement   = 1,
    .yCount          = 1,
    .nextDescriptor  = &PS_dmaDescriptor,
};

static cy_stc_dma_channel_config_t PS_dmaChannelConfig =
{
    .descriptor  = &PS_dmaDescriptor,
    .preemptable = false,
    .priority    = 3,
    .enable      = false,
    .bufferable  = false,
};
#endif /* USE_DMA */

static PowerScope_buffer_t buffer;
static PowerScope_buffer_t *myBuffer;

static void PowerScope_dmaCallback(void);
static void PowerScope_initDMA(PowerScope_buffer_t *buffer);
static void PowerScope_initBuffer(PowerScope_buffer_t *buffer);
static void PowerScope_addFrame(PowerScope_buffer_t *buffer, channel_data_t *channel_data);
static void PowerScope_writeBufferToOutput(PowerScope_buffer_t *buffer);

void PowerScope_init(CySCB_Type *hw)
{
    PS_scbHw   = hw;
    PS_scbMode = _FLD2VAL(SCB_CTRL_MODE, hw->CTRL);

    PowerScope_initBuffer(&buffer);
}

void PowerScope_update(channel_data_t *channel_data)
{
    /* Reset frame counter so addFrame always writes into slot 0.
     * With NUMBER_OF_FRAMES=1 the counter would otherwise sit at 1 after the
     * first call and block all subsequent writes. The flag is left for the DMA
     * callback to clear — if it is still set here the previous DMA transfer
     * has not finished yet and addFrame will correctly skip the write. */
    buffer.current_frame = 0;
    PowerScope_addFrame(&buffer, channel_data);
    PowerScope_writeBufferToOutput(&buffer);
}

#ifdef USE_DMA
static void PowerScope_dmaCallback(void)
{
    myBuffer->data_is_currently_being_written = false;
    myBuffer->current_frame = 0;

    Cy_DMA_Channel_ClearInterrupt(PS_DMA_HW, PS_DMA_CHANNEL);
}

static void PowerScope_initDMA(PowerScope_buffer_t *buf)
{
    uint32_t framesPerXTransfer = 1;
    uint32_t bytePerFrame = PowerScope_CHANNEL_DATA_SIZE * (PowerScope_NUMBER_OF_CHANNELS + 1);

    while (((framesPerXTransfer + 1) * bytePerFrame) < 128 && framesPerXTransfer < PowerScope_NUMBER_OF_FRAMES)
        framesPerXTransfer++;

    PS_dmaDescriptorConfig.xCount       = framesPerXTransfer * bytePerFrame;
    PS_dmaDescriptorConfig.yCount       = PowerScope_NUMBER_OF_FRAMES / framesPerXTransfer;
    if (PS_dmaDescriptorConfig.yCount == 0)
        PS_dmaDescriptorConfig.yCount   = 1;
    PS_dmaDescriptorConfig.srcYincrement = (int32_t)PS_dmaDescriptorConfig.xCount;

    Cy_DMA_Descriptor_Init(&PS_dmaDescriptor, &PS_dmaDescriptorConfig);
    Cy_DMA_Descriptor_SetSrcAddress(&PS_dmaDescriptor, buf->data);
    Cy_DMA_Descriptor_SetDstAddress(&PS_dmaDescriptor, (void *)(&SCB_TX_FIFO_WR(PS_scbHw)));

    Cy_DMA_Channel_Init(PS_DMA_HW, PS_DMA_CHANNEL, &PS_dmaChannelConfig);
    Cy_DMA_Channel_SetDescriptor(PS_DMA_HW, PS_DMA_CHANNEL, &PS_dmaDescriptor);
    Cy_DMA_Channel_SetInterruptMask(PS_DMA_HW, PS_DMA_CHANNEL, CY_DMA_INTR_MASK);

    cy_stc_sysint_t irqCfg = { .intrSrc = PS_DMA_IRQ, .intrPriority = 3 };
    Cy_SysInt_Init(&irqCfg, PowerScope_dmaCallback);
    NVIC_ClearPendingIRQ(PS_DMA_IRQ);
    NVIC_EnableIRQ(PS_DMA_IRQ);

    Cy_DMA_Enable(PS_DMA_HW);
}
#endif /* USE_DMA */

static void PowerScope_initBuffer(PowerScope_buffer_t *buf)
{
    myBuffer = buf;
    buf->current_frame = 0;
    buf->data_is_currently_being_written = false;

    for (int i = 0; i < (int)PowerScope_NUMBER_OF_FRAMES; i++)
    {
        buf->data[i * (PowerScope_NUMBER_OF_CHANNELS + 1)] = PowerScope_START_SEQUENCE;
        for (int j = 1; j < PowerScope_NUMBER_OF_CHANNELS + 1; j++)
            buf->data[i * (PowerScope_NUMBER_OF_CHANNELS + 1) + j] = 0;
    }

#ifdef USE_DMA
    PowerScope_initDMA(buf);
#endif
}

static void PowerScope_addFrame(PowerScope_buffer_t *buf, channel_data_t *channel_data)
{
    if (buf->data_is_currently_being_written)
        return;

    if (buf->current_frame < PowerScope_NUMBER_OF_FRAMES)
    {
        uint32_t offset = buf->current_frame * (PowerScope_NUMBER_OF_CHANNELS + 1);
        for (int i = 1; i < PowerScope_NUMBER_OF_CHANNELS + 1; i++)
            buf->data[offset + i] = channel_data[i - 1];
        buf->current_frame++;
    }
}

static void PowerScope_writeBufferToOutput(PowerScope_buffer_t *buf)
{
    buf->data_is_currently_being_written = true;

#ifdef USE_DMA
    Cy_DMA_Channel_Enable(PS_DMA_HW, PS_DMA_CHANNEL);
#else
    uint32_t buffer_length = sizeof(buf->data);
    uint32_t dataSend = 0;

    while (buffer_length - dataSend > 0)
    {
        if (PS_scbMode == PS_SCB_MODE_SPI)
            dataSend += Cy_SCB_SPI_WriteArray(PS_scbHw, (void *)(buf->data + dataSend), buffer_length - dataSend);
        else
            dataSend += Cy_SCB_UART_PutArray(PS_scbHw, (void *)(buf->data + dataSend), buffer_length - dataSend);
    }

    buf->data_is_currently_being_written = false;
    buf->current_frame = 0;
#endif
}
