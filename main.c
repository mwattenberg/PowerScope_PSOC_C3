/*******************************************************************************
* File Name:   main.c
*
* Description: ADC triggered by TIMER_ADC, results transmitted via UART/DMA
*              using the PowerScope framing protocol.
*
*******************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "cy_scb_spi.h"
#include "mtb_hal.h"
#include "cybsp.h"
#include "cy_hppass.h"
#include "cy_tcpwm_counter.h"
#include "cy_sysint.h"
#include "cy_sysclk.h"
#include "cy_scb_uart.h"
#include "cycfg_peripherals.h"
#include "cycfg_clocks.h"
#include "PowerScope.h"

/* Desired ADC sample rate in Hz. Adjust this value to change the sample rate.
 * The timer period is computed at runtime from the actual CLK_HF3 frequency
 * and the peripheral clock divider so the result is accurate across clock
 * tolerances (CLK_HF3 nominal 240 MHz ±1%). */
#define SAMPLE_RATE_HZ  5000UL

static void TIMER_ADC_isr(void)
{
    /* Read the 8 ADC results from the previous conversion */
    uint16_t samples[PowerScope_NUMBER_OF_CHANNELS];
    for (uint8_t i = 0; i < PowerScope_NUMBER_OF_CHANNELS; i++)
        samples[i] = Cy_HPPASS_SAR_Result_ChannelRead(i);

    /* Push samples into buffer and trigger DMA flush to UART */
    PowerScope_update(samples);

    /* Fire a new ADC conversion via firmware trigger 0 */
    Cy_HPPASS_SetFwTrigger(CY_HPPASS_TRIG_0_MSK);

    /* Clear timer interrupt */
    Cy_TCPWM_ClearInterrupt(TIMER_ADC_HW, TIMER_ADC_NUM, CY_TCPWM_INT_ON_TC);
}

static void init(void)
{
    /* Init ADC */
    Cy_HPPASS_Init(&pass_0_config);
    Cy_HPPASS_SAR_Init(&pass_0_sar_0_config);
    Cy_HPPASS_AC_Start(0, 500);
    Cy_HPPASS_SAR_SetTempSensorCurrent(false);

    /* Init SCB for UART, then hand the peripheral to PowerScope */
    Cy_SCB_UART_Init(UART_HW, &UART_config, NULL);
    Cy_SCB_UART_Enable(UART_HW);
	
	Cy_SCB_SPI_Init(SPI_HW, &SPI_config, NULL);
	Cy_SCB_SPI_Enable(SPI_HW);
	
	//Use SPI for PowerScope
    PowerScope_init(SPI_HW);

	//Use UART for PowerScope
	//PowerScope_init(UART_HW);

    /* Init and register timer ISR */
    cy_stc_sysint_t timerIrqCfg = { .intrSrc = TIMER_ADC_IRQ, .intrPriority = 3 };
    Cy_SysInt_Init(&timerIrqCfg, TIMER_ADC_isr);
    NVIC_ClearPendingIRQ(TIMER_ADC_IRQ);
    NVIC_EnableIRQ(TIMER_ADC_IRQ);

    /* Compute timer period from actual CLK_HF3 frequency.
     * Clock chain: CLK_HF3 -> 8-bit peri divider (div_num 0, group 5) -> TCPWM (prescaler DIVBY_1)
     * Divider register value is (N-1), so actual division = divider_val + 1. */
    uint32_t clkHf3Hz    = Cy_SysClk_ClkHfGetFrequency(3U);
    uint32_t periDiv     = Cy_SysClk_PeriPclkGetDivider(PCLK_TCPWM0_CLOCK_COUNTER_EN256,
                                                         CY_SYSCLK_DIV_8_BIT, 0U);
    uint32_t timerClkHz  = clkHf3Hz / (periDiv + 1U);
    uint32_t timerPeriod = (timerClkHz / SAMPLE_RATE_HZ) - 1U;

    /* Init and start TIMER_ADC with the computed period */
    cy_stc_tcpwm_counter_config_t timerCfg = TIMER_ADC_config;
    timerCfg.period = timerPeriod;
    Cy_TCPWM_Counter_Init(TIMER_ADC_HW, TIMER_ADC_NUM, &timerCfg);
    Cy_TCPWM_Counter_Enable(TIMER_ADC_HW, TIMER_ADC_NUM);
    Cy_TCPWM_TriggerStart_Single(TIMER_ADC_HW, TIMER_ADC_NUM);

    /* Fire the first ADC conversion to prime the pipeline */
    Cy_HPPASS_SetFwTrigger(CY_HPPASS_TRIG_0_MSK);
}

int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    init();

    /* Enable global interrupts */
    __enable_irq();

    for (;;)
    {
    }
}

/* [] END OF FILE */
