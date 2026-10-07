/**
 * @file stm32f4ve_peripherals.h
 * @brief Definições de pinos para botões, LEDs, TFT LCD e nRF24L01 (STM32F4VE V2.0)
 */

#ifndef STM32F4VE_PERIPHERALS_H
#define STM32F4VE_PERIPHERALS_H

/* ================================================================= *
 * 1. BOTÕES (Inputs)                                                *
 * ================================================================= */
#define BTN_RESET_PIN GPIO_PIN_RESET
#define BTN_WKUP PA0
#define BTN_K0 PE4
#define BTN_K1 PE3

/* ================================================================= *
 * 2. LEDs (Outputs)                                                 *
 * ================================================================= */
#define LED_D2 PA6
#define LED_D3 PA7

/* ================================================================= *
 * 3. TFT LCD HEADER (J1)                                            *
 * ================================================================= */
#define TFT_LCD_RST GPIO_PIN_RESET /* Conectado ao botão de Reset */
#define TFT_LCD_BACKLIGHT PB1

/* Barramento de Dados */
#define TFT_LCD_FSMC_D0 PD14
#define TFT_LCD_FSMC_D1 PD15
#define TFT_LCD_FSMC_D2 PD0
#define TFT_LCD_FSMC_D3 PD1
#define TFT_LCD_FSMC_D4 PE7
#define TFT_LCD_FSMC_D5 PE8
#define TFT_LCD_FSMC_D6 PE9
#define TFT_LCD_FSMC_D7 PE10
#define TFT_LCD_FSMC_D8 PE11
#define TFT_LCD_FSMC_D9 PE12
#define TFT_LCD_FSMC_D10 PE13
#define TFT_LCD_FSMC_D11 PE14
#define TFT_LCD_FSMC_D12 PE15
#define TFT_LCD_FSMC_D13 PD8
#define TFT_LCD_FSMC_D14 PD9
#define TFT_LCD_FSMC_D15 PD10

/* Sinais de Controle FSMC */
#define TFT_LCD_FSMC_NOE PD4
#define TFT_LCD_FSMC_NWE PD5
#define TFT_LCD_FSMC_A18 PD13
#define TFT_LCD_FSMC_NE1 PD7

/* Sinais de Touch */
#define TFT_LCD_TOUCH_CLK PB13
#define TFT_LCD_TOUCH_CS PB12
#define TFT_LCD_TOUCH_MOSI PB15
#define TFT_LCD_TOUCH_MISO PB14
#define TFT_LCD_TOUCH_PEN PC5

/* ================================================================= *
 * 4. USB HEADER (J4)                                                *
 * ================================================================= */
#define USB_DNEG PA11
#define USB_DPOS PA12

/* ================================================================= *
 * 5. JTAG HEADER (P1)                                               *
 * ================================================================= */
#define JTAG_TRST PB4
#define JTAG_TDI PA15
#define JTAG_TMS PA13
#define JTAG_SWDIO PA13
#define JTAG_TCK PA14
#define JTAG_SWCLK PA14
#define JTAG_TDO PB3
#define JTAG_SWO PB3

/* ================================================================= *
 * 6. SD-Card HEADER (U5)                                            *
 * ================================================================= */
#define SD_CARD_DAT0 PC8
#define SD_CARD_DAT1 PC9
#define SD_CARD_DAT2 PC10
#define SD_CARD_DAT3 PC11
#define SD_CARD_CD PC11
#define SD_CARD_CMD PD2
#define SD_CARD_CLK PC12

/* ================================================================= *
 * 7. MÓDULO nRF24L01 HEADER (JP2)                                   *
 * ================================================================= */
#define NRF24_CE PB6
#define NRF24_CSN PB7
#define NRF24_SCK PB3
#define NRF24_MOSI PB5
#define NRF24_MISO PB4
#define NRF24_IRQ PB8

/* ================================================================= *
 * 8. MÓDULO W25Q16 (U3)                                             *
 * ================================================================= */
#define W25Q16_CS PB0
#define W25Q16_DO PB4
#define W25Q16_DI PB5
#define W25Q16_CLK PB3

#endif /* STM32F4VE_PERIPHERALS_H */