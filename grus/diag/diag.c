/*
 * grus diag shim, milestone 2: one boot answers the questions that decide
 * the XNU port (after ios-sweet's milestone 2).
 *   - CPU feature registers on every core (16K granule, LSE, LDAPR, PAC)
 *   - PSCI/SMCCC version and which PSCI calls the Qualcomm TZ offers
 *   - SMP: PSCI CPU_ON for the 7 secondaries, each reports its IDs
 *   - GICv3 Group 1 IRQs: virtual timer (INTID 19) and a self SGI
 * Then it holds the screen for 60 s and reboots into fastboot by writing
 * the bootloader reason to the PM660 PON spare register, as Android does.
 *
 * Display (milestone 1): ABL leaves PINGPONG autorefresh on, so writing
 * the splash buffer is enough. Never write CTL_START on top of it: the
 * transfer hangs half way and the panel stops refreshing.
 */
#include <stdint.h>
#include "console.h"
#include "spmi.h"

#define MDP			0x0ae00000UL
#define NR_SSPP			5
#define SSPP_SRC_SIZE		0x00
#define SSPP_SRC0_ADDR		0x14
#define SSPP_SRC_YSTRIDE0	0x24
#define SSPP_SRC_FORMAT		0x30
#define SSPP_SRC_UNPACK		0x34
#define SPLASH_BASE		0x9c000000UL	/* cont_splash_region */
#define SPLASH_END		0x9e300000UL

#define GICD_BASE		0x17a00000UL
#define GICR_BASE		0x17a60000UL
#define GICR_SIZE		0x100000UL
#define GICR_PE_SIZE		0x20000UL
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14
#define GICR_SGI		0x10000
#define GICR_IGROUPR0		(GICR_SGI + 0x080)
#define GICR_ISENABLER0		(GICR_SGI + 0x100)
#define GICR_ICENABLER0		(GICR_SGI + 0x180)
#define GICR_IPRIORITYR		(GICR_SGI + 0x400)
#define GICR_ISPENDR0		(GICR_SGI + 0x200)
#define VTIMER_INTID		19	/* DT: <GIC_PPI 3 ...> */
#define SGI_INTID		1

#define PSCI_VERSION		0x84000000UL
#define PSCI_CPU_SUSPEND	0xc4000001UL
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define PSCI_FEATURES		0x8400000aUL
#define PSCI_SYSTEM_SUSPEND	0xc400000eUL
#define PSCI_SYSTEM_RESET2	0xc4000012UL
#define SMCCC_VERSION		0x80000000UL

#define PON_PPID		0x008	/* PM660 SID 0, PON at 0x800 */
#define PON_SOFT_RB_SPARE	0x88f
#define PON_REASON_BOOTLOADER	2	/* bits [7:1] on PON gen2 */
#define IMEM_RESTART_REASON	0x146bf65cUL
#define RESTART_BOOTLOADER	0x77665500U
#define PSHOLD			0x0c264000UL

#define NR_CPUS			8
#define HOLD_SECONDS		60

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")
#define dsb() __asm__ volatile("dsb sy" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

extern char secondary_entry[];

struct cpu_ids {
	uint64_t mpidr, midr, isar0, isar1, mmfr0;
};

static const uint32_t sspp_off[NR_SSPP] = { 0x5000, 0x7000, 0x25000, 0x27000, 0x29000 };

static const char *stage;	/* set at runtime: no absolute pointers in data */
static uint64_t image_base;
static volatile struct cpu_ids sec[NR_CPUS];
static volatile uint32_t irq_count, irq_other, irq_last;
static volatile int irq_src;

static uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

static void delay_ms(unsigned ms)
{
	uint64_t f = rsr("cntfrq_el0");
	if (!f)
		f = 19200000;
	uint64_t end = now() + f / 1000 * ms;
	while (now() < end)
		;
}

/* ---- framebuffer (milestone 1) ---- */

static void find_fb(struct fb *fb)
{
	fb->base = SPLASH_BASE;	/* measured on the phone, used if the pipe looks odd */
	fb->w = 1080;
	fb->h = 2340;
	fb->bpp = 4;
	fb->stride = 1080 * 4;
	fb->comp[0] = 1;
	fb->comp[1] = 0;
	fb->comp[2] = 2;
	fb->comp[3] = 3;
	con_init(fb);	/* so a fault on the reads below still has a console */

	for (int i = 0; i < NR_SSPP; i++) {
		uintptr_t p = MDP + sspp_off[i];
		uint32_t addr = rd32(p + SSPP_SRC0_ADDR);
		if (addr < SPLASH_BASE || addr >= SPLASH_END)
			continue;
		uint32_t size = rd32(p + SSPP_SRC_SIZE);
		uint32_t w = size & 0xffff, h = size >> 16;
		uint32_t bpp = ((rd32(p + SSPP_SRC_FORMAT) >> 9) & 3) + 1;
		uint32_t stride = rd32(p + SSPP_SRC_YSTRIDE0) & 0xffff;
		uint32_t unpack = rd32(p + SSPP_SRC_UNPACK);
		if (w < 64 || w > 4096 || h < 64 || h > 4096 || bpp < 3 || stride < w * bpp)
			return;
		fb->base = addr;
		fb->w = w;
		fb->h = h;
		fb->bpp = bpp;
		fb->stride = stride;
		for (int c = 0; c < 4; c++)
			fb->comp[c] = (unpack >> (8 * c)) & 3;
		con_init(fb);
		return;
	}
}

/* ---- reboot ---- */

static uint16_t pon_apid = SPMI_NO_APID;

static void __attribute__((noreturn)) reboot_fastboot(void)
{
	uint8_t v = 0;

	if (pon_apid != SPMI_NO_APID &&
	    (spmi_read(pon_apid, PON_SOFT_RB_SPARE, &v) & 0xf) == 1)
		spmi_write(pon_apid, PON_SOFT_RB_SPARE, (v & 1) | PON_REASON_BOOTLOADER << 1);
	wr32(IMEM_RESTART_REASON, RESTART_BOOTLOADER);
	dsb();
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
	wr32(PSHOLD, 0);
	for (;;)
		__asm__ volatile("wfe");
}

static void hold(unsigned seconds)
{
	char buf[12];

	con_skip();
	int r = con_row();
	for (unsigned t = seconds; t; t--) {
		fmt_dec(buf, t);
		con_str_at(r, "REBOOT", buf);
		delay_ms(1000);
	}
	con_str_at(r, "REBOOT", "now (fastboot)");
}

/* ---- exceptions ---- */

static void irq_handler(void)
{
	uint64_t iar = rsr("icc_iar1_el1");
	uint32_t id = iar & 0xffffff;

	if (id >= 1020 && id <= 1023)
		return;	/* spurious */
	irq_last = id;
	if (id == (uint32_t)irq_src)
		irq_count++;
	else
		irq_other++;
	if (id == VTIMER_INTID)
		wsr("cntv_tval_el0", rsr("cntfrq_el0") / 100);	/* 10 ms */
	wsr("icc_eoir1_el1", iar);
	isb();
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	if (slot == 5) {	/* IRQ from current EL, SPx */
		irq_handler();
		return;
	}
	con_clear(RED);
	/* no switch over strings: clang turns it into a pointer table (ABS64) */
	con_str("PANIC", "kind 0=SYNC 1=IRQ 2=FIQ 3=SERROR");
	con_hex2("SLOT", "n=", slot, "kind=", slot & 3);
	con_hex2("", "cpu=", rsr("mpidr_el1"), "el=", rsr("CurrentEL") >> 2);
	con_str("STAGE", stage);
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("X0", regs[0]);
	con_hex("X30", regs[29]);	/* stashed lr is just above the frame */
	con_hex("IMAGE", image_base);
	hold(30);
	reboot_fastboot();
}

/* ---- CPU features ---- */

static void read_ids(volatile struct cpu_ids *c)
{
	c->midr = rsr("midr_el1");
	c->isar0 = rsr("id_aa64isar0_el1");
	c->isar1 = rsr("id_aa64isar1_el1");
	c->mmfr0 = rsr("id_aa64mmfr0_el1");
	dsb();
	c->mpidr = rsr("mpidr_el1") | 1ULL << 63;	/* written last: "alive" */
}

static char yn(uint64_t reg, int shift, unsigned min)
{
	return ((reg >> shift) & 0xf) >= min ? 'y' : 'n';
}

static void test_features(void)
{
	char buf[40];
	uint64_t isar0 = rsr("id_aa64isar0_el1"), isar1 = rsr("id_aa64isar1_el1");
	uint64_t mmfr0 = rsr("id_aa64mmfr0_el1"), mmfr1 = rsr("id_aa64mmfr1_el1");
	uint64_t pfr0 = rsr("id_aa64pfr0_el1");

	stage = "features";
	con_hex2("ISAR0", "", isar0 >> 32, "", isar0);
	con_hex2("ISAR1", "", isar1 >> 32, "", isar1);
	con_hex2("MMFR0", "", mmfr0 >> 32, "", mmfr0);
	con_hex2("MMFR1", "", mmfr1 >> 32, "", mmfr1);
	con_hex2("PFR0", "", pfr0 >> 32, "", pfr0);
	/* TGran16 [23:20] = 1, Atomic [23:20] >= 2, LRCPC [23:20] >= 1,
	 * APA [7:4] / API [11:8], PAN [23:20], GIC sysregs PFR0 [27:24] */
	const char *t = "16K=? LSE=? RCPC=? PAC=? PAN=? GICSR=?";
	for (int i = 0; (buf[i] = t[i]); i++)
		;
	buf[4] = ((mmfr0 >> 20) & 0xf) == 1 ? 'y' : 'n';
	buf[10] = yn(isar0, 20, 2);
	buf[17] = yn(isar1, 20, 1);
	buf[23] = (yn(isar1, 4, 1) == 'y' || yn(isar1, 8, 1) == 'y') ? 'y' : 'n';
	buf[29] = yn(mmfr1, 20, 1);
	buf[37] = yn(pfr0, 24, 1);
	con_str("HAS", buf);
}

/* ---- PSCI + SMP ---- */

void secondary_main(uint64_t idx)
{
	if (idx < NR_CPUS)
		read_ids(&sec[idx]);
	dsb();
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

static void test_psci(void)
{
	stage = "psci";
	con_hex2("PSCI", "ver=", smc(PSCI_VERSION, 0, 0, 0), "smccc=",
		 smc(PSCI_FEATURES, SMCCC_VERSION, 0, 0) == 0 ? smc(SMCCC_VERSION, 0, 0, 0) : 0xffffffff);
	con_hex2(" feat", "on=", smc(PSCI_FEATURES, PSCI_CPU_ON, 0, 0),
		 "off=", smc(PSCI_FEATURES, PSCI_CPU_OFF, 0, 0));
	con_hex2(" feat", "susp=", smc(PSCI_FEATURES, PSCI_CPU_SUSPEND, 0, 0),
		 "ssusp=", smc(PSCI_FEATURES, PSCI_SYSTEM_SUSPEND, 0, 0));
	con_hex2(" feat", "rst=", smc(PSCI_FEATURES, PSCI_SYSTEM_RESET, 0, 0),
		 "rst2=", smc(PSCI_FEATURES, PSCI_SYSTEM_RESET2, 0, 0));
}

static void test_smp(void)
{
	int64_t ret[NR_CPUS];
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	struct cpu_ids boot;

	stage = "smp";
	read_ids(&boot);
	for (int i = 1; i < NR_CPUS; i++)
		ret[i] = ((uint64_t)i << 8) == self ? 0 :
			 smc(PSCI_CPU_ON, (uint64_t)i << 8, (uintptr_t)secondary_entry, i);
	delay_ms(500);

	int alive = 1, same_isa = 1, same_mm = 1;
	for (int i = 1; i < NR_CPUS; i++) {
		char label[] = "CPU0";
		label[3] = '0' + i;
		if (!sec[i].mpidr) {
			con_hex2(label, "ret=", (uint32_t)ret[i], "dead", 0);
			continue;
		}
		alive++;
		same_isa &= sec[i].isar0 == boot.isar0 && sec[i].isar1 == boot.isar1;
		same_mm &= sec[i].mmfr0 == boot.mmfr0;
		con_hex2(label, "mpidr=", (uint32_t)sec[i].mpidr, "midr=", (uint32_t)sec[i].midr);
	}
	con_hex2("SMP", "alive=", alive, "isa_eq=", same_isa << 4 | same_mm);
	/* show the first core whose features differ, if any */
	for (int i = 1; i < NR_CPUS; i++)
		if (sec[i].mpidr && (sec[i].isar0 != boot.isar0 || sec[i].mmfr0 != boot.mmfr0)) {
			char label[] = "DIFF0";
			label[4] = '0' + i;
			con_hex2(label, "isar0=", (uint32_t)sec[i].isar0, "mmfr0=", (uint32_t)sec[i].mmfr0);
			break;
		}
}

/* ---- GIC + timer ---- */

static uintptr_t find_gicr(void)
{
	uint64_t aff = rsr("mpidr_el1");
	uint32_t want = (aff & 0xffffff) | ((aff >> 8) & 0xff000000);

	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == want)
			return GICR_BASE + off;
		if (typer & (1 << 4))	/* Last */
			break;
	}
	return 0;
}

static void gicd_wait_rwp(void)
{
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE) & (1U << 31)); i++)
		;
}

static uint64_t sgi_self(unsigned id)
{
	uint64_t m = rsr("mpidr_el1");
	return ((m >> 16) & 0xff) << 32 |	/* Aff2 */
	       ((m >> 8) & 0xff) << 16 |	/* Aff1 */
	       (uint64_t)id << 24 |
	       (1U << (m & 0xf));		/* target list: Aff0 */
}

/* Run one interrupt source for ~300 ms and report what the GIC saw. */
static void irq_window(const char *label, uintptr_t r, int src)
{
	uint64_t f = rsr("cntfrq_el0");

	irq_count = irq_other = 0;
	irq_last = 0x3ff;
	irq_src = src;
	if (src == VTIMER_INTID) {
		wsr("cntv_tval_el0", f / 100);
		wsr("cntv_ctl_el0", 1);
	}
	isb();
	uint64_t t = now() + f / 50;
	while (now() < t)
		;
	if (src == SGI_INTID)
		wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
	isb();
	uint32_t pend = rd32(r + GICR_ISPENDR0);
	uint32_t hppi = rsr("icc_hppir1_el1");

	__asm__ volatile("msr daifclr, #2" ::: "memory");
	t = now() + f * 3 / 10;
	/* SGI: one already sent above, send 9 more 10 ms apart -> expect 10 */
	uint64_t next = now() + f / 100;
	for (int sent = 1; now() < t;)
		if (src == SGI_INTID && sent < 10 && now() >= next) {
			wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
			isb();
			sent++;
			next += f / 100;
		}
	__asm__ volatile("msr daifset, #2" ::: "memory");
	wsr("cntv_ctl_el0", 0);
	isb();

	con_hex2(label, "irqs=", irq_count, "other=", irq_other);
	con_hex2(" pre", "pend=", pend, "hppi=", hppi);
}

static void test_gic(void)
{
	stage = "gic sre";
	uint64_t sre = rsr("icc_sre_el1");
	if (!(sre & 1)) {
		wsr("icc_sre_el1", sre | 1);
		isb();
	}
	if (!(rsr("icc_sre_el1") & 1)) {
		con_str("GIC", "no sysreg iface, skip");
		return;
	}

	stage = "gic dist";
	uint32_t dctlr = rd32(GICD_BASE);
	/* Non-secure view (DS=0): bit0 EnableGrp1, bit1 EnableGrp1A, bit4 ARE_NS; Linux writes 0x13 */
	wr32(GICD_BASE, dctlr | 0x13);
	gicd_wait_rwp();
	con_hex2("GICD", "was=", dctlr, "now=", rd32(GICD_BASE));

	stage = "gic redist";
	uintptr_t r = find_gicr();
	if (!r) {
		con_str("GICR", "not found");
		return;
	}
	uint32_t waker = rd32(r + GICR_WAKER);
	if (waker & 2) {
		wr32(r + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 100000 && (rd32(r + GICR_WAKER) & 4); i++)
			;
	}
	con_hex2("GICR", "base=", r, "waker=", rd32(r + GICR_WAKER));

	stage = "gic config";
	wr32(r + GICR_IGROUPR0, 0xffffffff);	/* NS view: secure bits are WI */
	for (int id = 0; id < 32; id++)
		wr8(r + GICR_IPRIORITYR + id, 0xa0);
	wr32(r + GICR_ISENABLER0, (1U << VTIMER_INTID) | (1U << SGI_INTID));
	uint64_t ctlr = rsr("icc_ctlr_el1");
	wsr("icc_ctlr_el1", ctlr & ~2ULL);	/* EOImode 0: EOIR also deactivates */
	wsr("icc_pmr_el1", 0xf0);
	wsr("icc_bpr1_el1", 0);
	wsr("icc_igrpen1_el1", 1);
	isb();
	con_hex2("ICC", "ctlr=", ctlr, "grpen1=", rsr("icc_igrpen1_el1"));

	stage = "gic vtimer";
	irq_window("VTIMER", r, VTIMER_INTID);
	stage = "gic sgi";
	irq_window("SGI1", r, SGI_INTID);
	wr32(r + GICR_ICENABLER0, 0xffffffff);
	isb();
}

/* ---- SPMI ---- */

static void test_spmi(void)
{
	struct spmi_info si;
	uint32_t owner;
	uint8_t v = 0;

	stage = "spmi";
	if (spmi_init(&si)) {
		con_hex2("SPMI", "bad ver=", si.version, "n=", si.count);
		return;
	}
	pon_apid = spmi_find_apid(PON_PPID, &owner);
	con_hex2("SPMI", "ver=", si.version, "apids=", si.count);
	if (pon_apid == SPMI_NO_APID) {
		con_str("PON", "no apid");
		return;
	}
	uint32_t st = spmi_read(pon_apid, PON_SOFT_RB_SPARE, &v);
	con_hex2("PON", "apid=", pon_apid, "owner=", owner);
	con_hex2(" spare", "st=", st, "val=", v);
	if (owner)
		pon_apid = SPMI_NO_APID;	/* not ours to write: plain reset at the end */
}

/* ---- main ---- */

void shim_main(uint64_t dtb, uint64_t head)
{
	struct fb fb;

	image_base = head;
	stage = "fb";
	find_fb(&fb);
	con_clear(BLUE);
	con_str("GRUS", "diag shim m2: cpu smp gic spmi");
	con_hex2("EL", "el=", rsr("CurrentEL") >> 2, "mpidr=", rsr("mpidr_el1"));
	con_hex2("MIDR", "", rsr("midr_el1"), "dtb=", dtb);
	con_skip();

	test_features();
	con_skip();
	test_psci();
	con_skip();
	test_smp();
	con_skip();
	test_gic();
	con_skip();
	test_spmi();

	stage = "hold";
	hold(HOLD_SECONDS);
	reboot_fastboot();
}
