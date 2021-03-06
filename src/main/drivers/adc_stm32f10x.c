/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#include "build_config.h"

#include "system.h"

#include "sensors/sensors.h" // FIXME dependency into the main code

#include "accgyro.h"

#include "adc.h"

// Driver for STM32F103CB onboard ADC
//
// Naze32
// Battery Voltage (VBAT) is connected to PA4 (ADC1_IN4) with 10k:1k divider
// RSSI ADC uses CH2 (PA1, ADC1_IN1)
// Current ADC uses CH8 (PB1, ADC1_IN9)
//
// NAZE rev.5 hardware has PA5 (ADC1_IN5) on breakout pad on bottom of board
//
// CC3D Only one ADC channel supported currently, for battery on S5_IN/PA0

extern adc_config_t adcConfig[ADC_CHANNEL_COUNT];
extern volatile uint16_t adcValues[ADC_CHANNEL_COUNT];

void adcInit(drv_adc_config_t *init)
{
#ifdef CC3D
    UNUSED(init);
#endif

    ADC_InitTypeDef adc;
    DMA_InitTypeDef dma;
    GPIO_InitTypeDef GPIO_InitStructure;

    uint8_t i;
    uint8_t configuredAdcChannels = 0;

    memset(&adcConfig, 0, sizeof(adcConfig));

    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AIN;

#ifdef CC3D
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    adcConfig[ADC_BATTERY].adcChannel = ADC_Channel_0;
    adcConfig[ADC_BATTERY].dmaIndex = configuredAdcChannels++;
    adcConfig[ADC_BATTERY].enabled = true;
    adcConfig[ADC_BATTERY].sampleTime = ADC_SampleTime_239Cycles5;

    if (init->enableCurrentMeter) {
        GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
        adcConfig[ADC_CURRENT].adcChannel = ADC_Channel_1;
        adcConfig[ADC_CURRENT].dmaIndex = configuredAdcChannels++;
        adcConfig[ADC_CURRENT].enabled = true;
        adcConfig[ADC_CURRENT].sampleTime = ADC_SampleTime_239Cycles5;
    }
    GPIO_Init(GPIOA, &GPIO_InitStructure);
#else
    // configure always-present battery index (ADC4)
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_4;
    adcConfig[ADC_BATTERY].adcChannel = ADC_Channel_4;
    adcConfig[ADC_BATTERY].dmaIndex = configuredAdcChannels++;
    adcConfig[ADC_BATTERY].enabled = true;
    adcConfig[ADC_BATTERY].sampleTime = ADC_SampleTime_239Cycles5;

    if (init->enableRSSI) {
        GPIO_InitStructure.GPIO_Pin   |= GPIO_Pin_1;

        adcConfig[ADC_RSSI].adcChannel = ADC_Channel_1;
        adcConfig[ADC_RSSI].dmaIndex = configuredAdcChannels++;
        adcConfig[ADC_RSSI].enabled = true;
        adcConfig[ADC_RSSI].sampleTime = ADC_SampleTime_239Cycles5;
    }

    if (init->enableExternal1) {
#ifdef OLIMEXINO
        GPIO_InitStructure.GPIO_Pin   |= GPIO_Pin_5;

        adcConfig[ADC_EXTERNAL1].adcChannel = ADC_Channel_5;
        adcConfig[ADC_EXTERNAL1].dmaIndex = configuredAdcChannels++;
        adcConfig[ADC_EXTERNAL1].enabled = true;
        adcConfig[ADC_EXTERNAL1].sampleTime = ADC_SampleTime_239Cycles5;
#endif

#ifdef NAZE
        GPIO_InitStructure.GPIO_Pin   |= GPIO_Pin_5;

        adcConfig[ADC_EXTERNAL1].adcChannel = ADC_Channel_5;
        adcConfig[ADC_EXTERNAL1].dmaIndex = configuredAdcChannels++;
        adcConfig[ADC_EXTERNAL1].enabled = true;
        adcConfig[ADC_EXTERNAL1].sampleTime = ADC_SampleTime_239Cycles5;
#endif
    }

    GPIO_Init(GPIOA, &GPIO_InitStructure);

    if (init->enableCurrentMeter) {
        GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
        GPIO_Init(GPIOB, &GPIO_InitStructure);

        adcConfig[ADC_CURRENT].adcChannel = ADC_Channel_9;
        adcConfig[ADC_CURRENT].dmaIndex = configuredAdcChannels++;
        adcConfig[ADC_CURRENT].enabled = true;
        adcConfig[ADC_CURRENT].sampleTime = ADC_SampleTime_239Cycles5;
    }
#endif // !CC3D

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    // FIXME ADC driver assumes all the GPIO was already placed in 'AIN' mode

    DMA_DeInit(DMA1_Channel1);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)adcValues;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = configuredAdcChannels;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = configuredAdcChannels > 1 ? DMA_MemoryInc_Enable : DMA_MemoryInc_Disable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = configuredAdcChannels > 1 ? ENABLE : DISABLE;
    adc.ADC_ContinuousConvMode = ENABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = configuredAdcChannels;
    ADC_Init(ADC1, &adc);

    uint8_t rank = 1;
    for (i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (!adcConfig[i].enabled) {
            continue;
        }
        ADC_RegularChannelConfig(ADC1, adcConfig[i].adcChannel, rank++, adcConfig[i].sampleTime);
    }

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}
