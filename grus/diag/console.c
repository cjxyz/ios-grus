/*
 * Text console on the ABL splash framebuffer, for 24 or 32 bpp in any
 * component order. With the MMU off every access is Device memory, so
 * stay aligned (-mstrict-align) and keep full-line fills on 32-bit stores.
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

static struct fb fb;
static uint8_t bg_px[4];
static int row;

void con_init(const struct fb *f)
{
	fb = *f;
}

static void encode(uint32_t rgb, uint8_t *px)
{
	for (uint32_t i = 0; i < fb.bpp; i++) {
		switch (fb.comp[i]) {
		case 0: px[i] = rgb >> 8; break;	/* G */
		case 1: px[i] = rgb; break;		/* B */
		case 2: px[i] = rgb >> 16; break;	/* R */
		default: px[i] = 0xff; break;		/* A */
		}
	}
}

static inline volatile uint8_t *pix(uint32_t x, uint32_t y)
{
	return (volatile uint8_t *)(fb.base + (uintptr_t)y * fb.stride + (uintptr_t)x * fb.bpp);
}

/* Fill n pixels from (x, y) with the encoded pixel px. */
static void span(uint32_t x, uint32_t y, uint32_t n, const uint8_t *px)
{
	volatile uint8_t *p = pix(x, y);

	if (!((uintptr_t)p & 3) && fb.bpp == 4) {
		uint32_t w = px[0] | px[1] << 8 | px[2] << 16 | (uint32_t)px[3] << 24;
		volatile uint32_t *q = (volatile uint32_t *)p;
		for (uint32_t i = 0; i < n; i++)
			q[i] = w;
		return;
	}
	if (!((uintptr_t)p & 3) && fb.bpp == 3 && !(n & 3)) {
		/* 4 pixels = 12 bytes = 3 words */
		uint8_t b[12];
		for (int i = 0; i < 12; i++)
			b[i] = px[i % 3];
		uint32_t w0 = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
		uint32_t w1 = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
		uint32_t w2 = b[8] | b[9] << 8 | b[10] << 16 | (uint32_t)b[11] << 24;
		volatile uint32_t *q = (volatile uint32_t *)p;
		for (uint32_t i = 0; i < n / 4; i++) {
			q[3 * i] = w0;
			q[3 * i + 1] = w1;
			q[3 * i + 2] = w2;
		}
		return;
	}
	for (uint32_t i = 0; i < n; i++)
		for (uint32_t c = 0; c < fb.bpp; c++)
			p[i * fb.bpp + c] = px[c];
}

void con_clear(uint32_t bg)
{
	encode(bg, bg_px);
	for (uint32_t y = 0; y < fb.h; y++)
		span(0, y, fb.w, bg_px);
	row = 0;
}

static void put_char(int r, int col, char ch, uint32_t fg)
{
	uint8_t fg_px[4];

	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	encode(fg, fg_px);
	const unsigned char *g = font8x16[ch - 0x20];
	uint32_t x0 = MARGIN + col * CH_W, y0 = MARGIN + r * CH_H;
	if (x0 + CH_W > fb.w || y0 + CH_H > fb.h)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			const uint8_t *px = (g[gy] & (0x80 >> gx)) ? fg_px : bg_px;
			for (int sy = 0; sy < SCALE; sy++)
				span(x0 + gx * SCALE, y0 + gy * SCALE + sy, SCALE, px);
		}
}

static int put_str(int r, int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(r, col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void fmt_dec(char *buf, uint32_t v)
{
	char t[11];
	int n = 0;
	do {
		t[n++] = '0' + v % 10;
		v /= 10;
	} while (v);
	for (int i = 0; i < n; i++)
		buf[i] = t[n - 1 - i];
	buf[n] = 0;
}

void con_skip(void)
{
	row++;
}

int con_row(void)
{
	return row;
}

void con_str_at(int r, const char *label, const char *s)
{
	int col = put_str(r, 0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(r, col, s, WHITE);
	put_str(r, col, "    ", WHITE);	/* wipe a longer previous value */
}

void con_str(const char *label, const char *s)
{
	con_str_at(row++, label, s);
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(row, 0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(row, col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(row, col, buf, WHITE);
	if (b) {
		col = put_str(row, col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(row, col, buf, WHITE);
	}
	row++;
}
