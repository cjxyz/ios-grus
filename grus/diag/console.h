#pragma once
#include <stdint.h>

/*
 * Framebuffer described by the MDP pipe that scans it out. comp[i] is the
 * channel stored in byte i of a pixel, in SDE unpack-pattern terms:
 * 0 = G, 1 = B, 2 = R, 3 = A.
 */
struct fb {
	uintptr_t base;
	uint32_t w, h, stride, bpp;
	uint8_t comp[4];
};

#define RED	0xff0000U
#define BLUE	0x0000a0U
#define GREEN	0x006000U
#define PURPLE	0x600080U
#define WHITE	0xffffffU
#define YELLOW	0xffff00U
#define BLACK	0x000000U

void con_init(const struct fb *fb);
void con_clear(uint32_t bg);
void con_skip(void);
int con_row(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
/* Redraw one row in place (for counters). */
void con_str_at(int row, const char *label, const char *s);
void fmt_dec(char *buf, uint32_t v);
