# Progreso (grus)

Hardware y comparación con sweet: [HARDWARE.md](HARDWARE.md).

## Estado
| Etapa | Estado | Notas |
|---|---|---|
| Auditoría de hardware | ✅ | 2026-11-10 |
| Volcado del teléfono (`../grus-dump`) | ✅ | boot.img v2 y DTB vivo |
| Shim de diagnóstico m1 (refresco del panel) | ✅ | ABL deja el autorefresh activo: basta con escribir en el FB |
| Shim m2: CPU, PSCI, SMP, GIC y reinicio a fastboot | ✅ | 8 núcleos, LSE/RCPC/16K por hardware, IRQ del timer y SGI, vuelta a fastboot |
| ADT + payload XNU para grus | ⏳ | |
| XNU | ⏳ | |

## Build
```
python3 grus/diag/build.py            # -> artifacts/grus-diag.img
python3 grus/tools/bootimg.py selftest ../grus-dump/boot.img
```
Toolchain: clang/ld.lld/llvm-objcopy 21 (los mismos que en ios-vince). La cabecera v2 y la sección DTB se copian del `boot.img` del teléfono.

## Bitácora
### 2026-11-10
- `grus/tools/bootimg.py`: pack/unpack de v2. El selftest reconstruye `grus-dump/boot.img` byte a byte. La sección DTB lleva 2 DTB base (SDM670 `0x150` y SDM710 `0x168`/`0x189`); el overlay de grus (`board-id 0x22`) lo aplica ABL desde `dtbo`.
- Shim m1 (`grus/diag/diag.c`), sin MMU: lee el formato del framebuffer de la tubería SSPP que apunta a `0x9c000000` (fallback: 1080×2340, 24 bpp) y pasa por tres fases:
  1. azul, sin kick (10 s);
  2. verde, `CTL_START` por cada actualización (10 s);
  3. morado, `PP_AUTOREFRESH_CONFIG` = `0x80000001` + `CTL_START` (30 s).

  Luego reinicia a fastboot (restart reason `0x77665500` en IMEM `0x146bf65c`). La fila TIME cuenta segundos. Atiende el watchdog si ABL lo deja activo. La pantalla de panic también lanza un frame.
- Comprobado en el PC: la consola en 24 bpp se renderiza bien. No hay relocaciones absolutas (el `switch` de cadenas del panic generaba una tabla ABS64; quitado).
- Para que `fastboot` vea el teléfono en Windows hizo falta la clave `usbflags\18D1D00D0100` (`osvc`, `SkipContainerIdQuery`, `SkipBOSDescriptorQuery`). Sin ella aparece "press any key to shutdown" al conectar el cable.
- **Primera prueba en el teléfono** (`grus-diag.img` `8d9a43a7…`), con fotos:
  - Fase 1 (azul, sin kick): **se ve y TIME avanza**. ABL deja el autorefresh activo en PP0 (`ar=80000001`, `te=1`).
  - FB: pipe VIG0 (P0) `a=9C000000`, `s=09240438` (1080×2340), `f=000237FF` (32 bpp), `u=03020001`, stride `0x10E0`. Los colores salen bien.
  - CTL0 `top=00020020` (command mode), `l0=01000005`; CTL1–4 `top=00001F00`; PP1–4 a cero.
  - EL1, MPIDR `81000000`, MIDR `517F803C` (Kryo 360 Silver), CNTFRQ 19,2 MHz. ABL carga la imagen en **`0x80080000`** y el DTB en `0x853FD000`. Watchdog apagado (`en=0`).
  - Fase 2 (verde, `CTL_START` cada segundo): el primer kick con el autorefresh activo cuelga la transferencia. La pantalla se queda a medio pintar (TIME en 0) y la parte de abajo muestra rayas beige que se van alargando. Después el teléfono se reinicia.
  - El reinicio llega a los ~40 s del verde (10 s de fase 2 + 30 s de fase 3, lo programado): la CPU siguió viva con el panel colgado. Pero **arranca Android, no fastboot**: en este ABL no basta con el `0x77665500` en IMEM. Android además escribe el motivo en el PMIC: PM660 PON (SID 0, `0x800`) `SOFT_RB_SPARE` `+0x8F`, bits [7:1] = `PON_RESTART_REASON_BOOTLOADER` (2) << 1 (`qpnp-power-on.c`, PON gen2, `qcom,store-hard-reset-reason`). Hace falta una escritura SPMI; queda pendiente.
  - Conclusión: no lanzar frames a mano. Los kicks se quitan del shim; queda solo la fase sin kick como base para m2.
- **Shim m2** (`grus-diag.img` `81bd5aa8…`), basado en el m2 de sweet:
  - Registros ID (ISAR0/1, MMFR0/1, PFR0) decodificados en la fila `HAS` (16K, LSE, RCPC, PAC, PAN, GIC por sysregs).
  - PSCI: versión, SMCCC y `PSCI_FEATURES` de CPU_ON/OFF/SUSPEND, SYSTEM_SUSPEND/RESET/RESET2.
  - SMP: `CPU_ON` a MPIDR `i << 8` (i = 1…7). Cada núcleo anota MPIDR, MIDR, ISAR0/1 y MMFR0 y se apaga. Se comprueba si los grandes (A75) y los pequeños (A55) tienen las mismas capacidades.
  - GICv3: distribuidor y redistribuidor en Grupo 1 NS, ventana de 300 ms con el timer virtual (INTID 19) y otra con SGI 1 hacia sí mismo (se esperan ~30 y 10 IRQ).
  - SPMI (`grus/diag/spmi.c`, arbitro v5): APID del PON de PM660 (PPID `0x008`), propietario y lectura de `SOFT_RB_SPARE`. Al reiniciar escribe `(v & 1) | 2 << 1`, como `qpnp_pon_set_restart_reason`, solo si el canal es de EE 0. No toca la configuración de PS_HOLD: con `qcom,store-hard-reset-reason`, Android también reinicia en hard reset y el motivo se conserva.
  - Mantiene la pantalla 60 s (cuenta atrás en REBOOT) y reinicia. El panic hace lo mismo con 30 s.
- **Prueba de m2 en el teléfono** (`81bd5aa8…`), con foto. Sin panic:
  - ISAR0 `10211120` (Atomic 2 = LSE), ISAR1 `00100001` (LRCPC 1, sin PAC), MMFR0 `00101122` (TGran16 = 1, PARange 40 bits), MMFR1 `10212122` (PAN 2, VH 1), PFR0 `11112222` (GIC sysregs 1). `HAS 16K=y LSE=y RCPC=y PAC=n PAN=y`.
  - PSCI `00010001` (v1.1). SMCCC_VERSION no disponible (SMCCC 1.0). FEATURES: CPU_ON 0, CPU_OFF 0, CPU_SUSPEND 3, SYSTEM_SUSPEND −1, SYSTEM_RESET 0, SYSTEM_RESET2 −1.
  - SMP: los 8 núcleos arrancan. CPU0–5 MIDR `517F803C` (Kryo 360 Silver), CPU6–7 `516F802D` (Kryo 360 Gold). MPIDR `0x81000000 | i << 8`. ISAR0/1 iguales en todos; **MMFR0 de los grandes = `00101124` (PARange 44 bits)**. XNU debe configurar 40 bits (el mínimo común).
  - GICD `0x10` → `0x12`. GICR de CPU0 en `0x17A60000`, waker 0. VTIMER: 30 IRQ en 300 ms (`0x1E`, como se esperaba). SGI1: 10 de 10.
  - SPMI ver `50000000`, 512 APID. PON en APID 7, owner 0. `SOFT_RB_SPARE` se lee bien (st 1, val 0).
  - **Reinicia a fastboot**: con `SOFT_RB_SPARE` = `2 << 1` por SPMI más PSCI SYSTEM_RESET, ABL entra en el bootloader (con solo IMEM arrancaba Android).
  - Conclusión: la CPU y el SMP de grus son compatibles con el kernel sin PAC de 16 K de sweet sin emulación. Siguiente hito: ADT y payload de XNU para grus.
