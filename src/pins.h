/* pins.h — BCM GPIO map for the karaoke box.
 * Encoders: KY-040 (CLK/DT/SW, pull-ups on, active-low button).
 * Display: HD44780 1602 in 4-bit mode.
 */
#ifndef MICFX_PINS_H
#define MICFX_PINS_H

/* LCD */
#define PIN_LCD_RS 25
#define PIN_LCD_EN 12
#define PIN_LCD_D4 16
#define PIN_LCD_D5 21
#define PIN_LCD_D6 9
#define PIN_LCD_D7 10

/* Buttons (until the encoder knobs arrive): NEXT selects param,
 * UP/DOWN adjust. NOTE: these reuse the future music-encoder pins;
 * remap when the knobs land. */
#define PIN_BTN_NEXT 17
#define PIN_BTN_UP 18
#define PIN_BTN_DOWN 27

/* Encoders: music, mic1, mic2, reverb, master (not yet wired) */
#define PIN_ENC0_CLK 17
#define PIN_ENC0_DT  18
#define PIN_ENC0_SW  27
#define PIN_ENC1_CLK 22
#define PIN_ENC1_DT  23
#define PIN_ENC1_SW  24
#define PIN_ENC2_CLK 5
#define PIN_ENC2_DT  6
#define PIN_ENC2_SW  13
#define PIN_ENC3_CLK 19
#define PIN_ENC3_DT  20
#define PIN_ENC3_SW  26
#define PIN_ENC4_CLK 4
#define PIN_ENC4_DT  7
#define PIN_ENC4_SW  8

#endif
