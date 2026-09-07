/* lcd.h — HD44780 1602 in 4-bit mode over a libgpiod line request.
 * Blocking micro-delays inside: call only from a background thread,
 * never from the audio loop.
 */
#ifndef MICFX_LCD_H
#define MICFX_LCD_H

#include <gpiod.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    struct gpiod_line_request *req;
    unsigned rs, en, d4, d5, d6, d7;
} lcd_t;

static inline void lcd_nib(lcd_t *l, unsigned v) {
    gpiod_line_request_set_value(l->req, l->d4, (v >> 0) & 1);
    gpiod_line_request_set_value(l->req, l->d5, (v >> 1) & 1);
    gpiod_line_request_set_value(l->req, l->d6, (v >> 2) & 1);
    gpiod_line_request_set_value(l->req, l->d7, (v >> 3) & 1);
    gpiod_line_request_set_value(l->req, l->en, 1);
    gpiod_line_request_set_value(l->req, l->en, 0);
    usleep(60);
}

static inline void lcd_cmd(lcd_t *l, unsigned c) {
    gpiod_line_request_set_value(l->req, l->rs, 0);
    lcd_nib(l, c >> 4);
    lcd_nib(l, c);
}

static inline void lcd_init(lcd_t *l, struct gpiod_line_request *req,
                            unsigned rs, unsigned en,
                            unsigned d4, unsigned d5, unsigned d6, unsigned d7) {
    l->req = req;
    l->rs = rs; l->en = en;
    l->d4 = d4; l->d5 = d5; l->d6 = d6; l->d7 = d7;
    usleep(50000);
    gpiod_line_request_set_value(req, rs, 0);
    lcd_nib(l, 0x3);
    usleep(5000);
    lcd_nib(l, 0x3);
    usleep(200);
    lcd_nib(l, 0x3);
    usleep(200);
    lcd_nib(l, 0x2); /* 4-bit mode */
    lcd_cmd(l, 0x28); /* 2 lines, 5x8 */
    lcd_cmd(l, 0x0C); /* display on, no cursor */
    lcd_cmd(l, 0x06); /* entry: inc, no shift */
    lcd_cmd(l, 0x01); /* clear */
    usleep(3000);
}

/* Define custom chars 0..3 as 1..4 px-wide left bars (CGRAM). */
static inline void lcd_defbar(lcd_t *l) {
    for (unsigned k = 1; k <= 4; k++) {
        lcd_cmd(l, 0x40 + 8 * (k - 1));
        gpiod_line_request_set_value(l->req, l->rs, 1);
        unsigned pat = (0x1Fu << (5 - k)) & 0x1F;
        for (int r = 0; r < 8; r++) {
            lcd_nib(l, pat >> 4);
            lcd_nib(l, pat);
        }
        gpiod_line_request_set_value(l->req, l->rs, 0);
    }
}

/* Write 16 raw bytes (custom chars 0..3 survive: no NUL scan). */
static inline void lcd_raw(lcd_t *l, int row, const char b[16]) {
    lcd_cmd(l, row ? 0xC0 : 0x80);
    gpiod_line_request_set_value(l->req, l->rs, 1);
    for (int i = 0; i < 16; i++) {
        lcd_nib(l, ((unsigned char)b[i]) >> 4);
        lcd_nib(l, (unsigned char)b[i]);
    }
    gpiod_line_request_set_value(l->req, l->rs, 0);
}

/* Print exactly 16 chars (pads/truncates) on row 0 or 1. */
static inline void lcd_text(lcd_t *l, int row, const char *s) {
    char b[17];
    size_t n = strlen(s);
    if (n > 16) n = 16;
    memcpy(b, s, n);
    memset(b + n, ' ', 16 - n);
    b[16] = '\0';
    lcd_cmd(l, row ? 0xC0 : 0x80);
    gpiod_line_request_set_value(l->req, l->rs, 1);
    for (int i = 0; i < 16; i++) {
        lcd_nib(l, (unsigned)b[i] >> 4);
        lcd_nib(l, (unsigned)b[i]);
    }
    gpiod_line_request_set_value(l->req, l->rs, 0);
}

#endif
