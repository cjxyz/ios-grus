/*
 * Minimal Qualcomm PMIC arbiter v5 access, from the grus stock
 * drivers/spmi/spmi-pmic-arb.c: reads go through the EE 0 observer
 * channel (as the ios-sweet keys driver does), writes through the
 * per-APID read/write channel, which only EE 0 (APPS) may use.
 */
#include "spmi.h"

#define SPMI_CORE	0x0c440000UL
#define SPMI_CHNLS	0x0c600000UL
#define SPMI_OBS	0x0e600000UL
#define SPMI_CNFG	0x0c40a000UL

#define ARB_CMD		0x00
#define ARB_STATUS	0x08
#define ARB_WDATA0	0x10
#define ARB_RDATA0	0x18
#define OP_EXT_WRITEL	0
#define OP_EXT_READL	1
#define ST_DONE		1u
#define ST_ERRORS	0x0eu	/* denied, failure, dropped */

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint32_t apid_count;

int spmi_init(struct spmi_info *si)
{
	si->version = rd32(SPMI_CORE);
	si->count = rd32(SPMI_CORE + 4) & 0x7ff;
	if (si->version < 0x50000000u || si->version >= 0x70000000u || si->count > 512)
		return -1;
	apid_count = si->count;
	return 0;
}

uint16_t spmi_find_apid(uint16_t ppid, uint32_t *owner)
{
	uint16_t found = SPMI_NO_APID;

	*owner = 0xff;
	for (uint32_t i = 0; i < apid_count; i++) {
		uint32_t map = rd32(SPMI_CORE + 0x900 + 4 * i);
		if (!map || ((map >> 8) & 0xfff) != ppid)
			continue;
		uint32_t own = rd32(SPMI_CNFG + 0x700 + 4 * i) & 7;
		if (found == SPMI_NO_APID || own == 0) {
			found = i;
			*owner = own;
		}
	}
	return found;
}

static uint32_t wait_done(uintptr_t ch)
{
	for (int i = 0; i < 100000; i++) {
		uint32_t st = rd32(ch + ARB_STATUS);
		if (st & ST_DONE)
			return st;
	}
	return 0;
}

uint32_t spmi_read(uint16_t apid, uint16_t addr, uint8_t *out)
{
	uintptr_t ch = SPMI_OBS + 0x80 * apid;	/* + 0x10000 * EE 0 */

	__asm__ volatile("dsb sy" ::: "memory");
	wr32(ch + ARB_CMD, OP_EXT_READL << 27 | (addr & 0xff) << 4);	/* bc 0 = 1 byte */
	uint32_t st = wait_done(ch);
	if ((st & ST_DONE) && !(st & ST_ERRORS))
		*out = rd32(ch + ARB_RDATA0);
	return st;
}

uint32_t spmi_write(uint16_t apid, uint16_t addr, uint8_t val)
{
	uintptr_t ch = SPMI_CHNLS + 0x10000 * (uintptr_t)apid;

	wr32(ch + ARB_WDATA0, val);
	__asm__ volatile("dsb sy" ::: "memory");
	wr32(ch + ARB_CMD, OP_EXT_WRITEL << 27 | (addr & 0xff) << 4);
	return wait_done(ch);
}
