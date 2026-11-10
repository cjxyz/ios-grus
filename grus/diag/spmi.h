#pragma once
#include <stdint.h>

#define SPMI_NO_APID	0xffffu

struct spmi_info {
	uint32_t version, count;
};

/* 0 on success, else the arbiter version is not v5/v6. */
int spmi_init(struct spmi_info *si);
/* APID of ppid = (sid << 8) | periph, preferring one owned by EE 0; *owner gets its owner. */
uint16_t spmi_find_apid(uint16_t ppid, uint32_t *owner);
/* One-byte transfers; return the arbiter status word (bit 0 done, bits 1-3 error), or 0 on timeout. */
uint32_t spmi_read(uint16_t apid, uint16_t addr, uint8_t *out);
uint32_t spmi_write(uint16_t apid, uint16_t addr, uint8_t val);
