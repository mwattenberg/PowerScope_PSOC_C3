/*
 * PowerScope.c
 *
 * Self-contained data buffering and UART/DMA transmission module.
 * Adapted from 12kW_Primary_PLECS/PowerScope.c
 */

#include "PowerScope.h"
#include "cy_scb_uart.h"
#include "cy_sysint.h"
#include "cycfg_peripherals.h"
#ifdef USE_DMA
#include "cy_dma.h"
#include "cycfg_dmas.h"
#endif

#include <stdbool.h>

static PowerScope_buffer_t buffer;
static PowerScope_buffer_t *myBuffer;

static void PowerScope_dmaCallback(void);
static void PowerScope_initDMA(PowerScope_buffer_t *buffer);
static void PowerScope_initBuffer(PowerScope_buffer_t *buffer);
static void PowerScope_addFrame(PowerScope_buffer_t *buffer, channel_data_t *channel_data);
static void PowerScope_writeBufferToOutput(PowerScope_buffer_t *buffer);

void PowerScope_init(void)
{
    Cy_SCB_UART_Init(UART_HW, &UART_config, NULL);
    Cy_SCB_UART_Enable(UART_HW);

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

    Cy_DMA_Channel_ClearInterrupt(DMA_PowerScope_HW, DMA_PowerScope_CHANNEL);
}

static void PowerScope_initDMA(PowerScope_buffer_t *buf)
{
    uint32_t framesPerXTransfer = 1;
    uint32_t bytePerFrame = PowerScope_CHANNEL_DATA_SIZE * (PowerScope_NUMBER_OF_CHANNELS + 1);

    while (((framesPerXTransfer + 1) * bytePerFrame) < 128 && framesPerXTransfer < PowerScope_NUMBER_OF_FRAMES)
        framesPerXTransfer++;

    cy_stc_dma_descriptor_config_t descCfg = DMA_PowerScope_Descriptor_0_config;
    descCfg.xCount          = framesPerXTransfer * bytePerFrame;
    descCfg.dataSize        = CY_DMA_BYTE;
    descCfg.srcTransferSize = CY_DMA_TRANSFER_SIZE_DATA;
    descCfg.dstTransferSize = CY_DMA_TRANSFER_SIZE_WORD;
    descCfg.yCount          = PowerScope_NUMBER_OF_FRAMES / framesPerXTransfer;
    if (descCfg.yCount == 0)
        descCfg.yCount = 1;
    descCfg.srcYincrement   = (int32_t)descCfg.xCount;

    Cy_DMA_Descriptor_Init(&DMA_PowerScope_Descriptor_0, &descCfg);
    Cy_DMA_Descriptor_SetSrcAddress(&DMA_PowerScope_Descriptor_0, buf->data);
    Cy_DMA_Descriptor_SetDstAddress(&DMA_PowerScope_Descriptor_0, (void *)(&SCB_TX_FIFO_WR(UART_HW)));

    Cy_DMA_Channel_Init(DMA_PowerScope_HW, DMA_PowerScope_CHANNEL, &DMA_PowerScope_channelConfig);
    Cy_DMA_Channel_SetDescriptor(DMA_PowerScope_HW, DMA_PowerScope_CHANNEL, &DMA_PowerScope_Descriptor_0);
    Cy_DMA_Channel_SetInterruptMask(DMA_PowerScope_HW, DMA_PowerScope_CHANNEL, CY_DMA_INTR_MASK);

    cy_stc_sysint_t irqCfg = { .intrSrc = DMA_PowerScope_IRQ, .intrPriority = 3 };
    Cy_SysInt_Init(&irqCfg, PowerScope_dmaCallback);
    NVIC_ClearPendingIRQ(DMA_PowerScope_IRQ);
    NVIC_EnableIRQ(DMA_PowerScope_IRQ);

    Cy_DMA_Enable(DMA_PowerScope_HW);
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
    Cy_DMA_Channel_Enable(DMA_PowerScope_HW, DMA_PowerScope_CHANNEL);
#else
    uint32_t buffer_length = sizeof(buf->data);
    uint32_t dataSend = 0;

    while (buffer_length - dataSend > 0)
        dataSend += Cy_SCB_UART_PutArray(UART_HW, (void *)(buf->data + dataSend), buffer_length - dataSend);

    buf->data_is_currently_being_written = false;
    buf->current_frame = 0;
#endif
}
