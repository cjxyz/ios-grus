# Auditoría de hardware: grus (Mi 9 SE) frente a ios-sweet

Fecha: 2026-11-10.
Fuentes:
- Kernel stock de grus en `../grus-stock` (fuera del repo): `arch/arm64/boot/dts/qcom/{sdm670,sdm710,grus-sdm710,xiaomi-sdm710-common}.dtsi`, `pinctrl-sdm670.c`, `gcc-sdm845.c` y el driver `tp_goodix_driver`.
- ios-sweet en `45263be` (2026-10-23): `tools/mkadt.py`, `shim/` y el README.
- Parches sweet 0001–0081 de `xnu/patches` de este repo.

El SDM710/712 (grus) se describe en el kernel como `sdm670.dtsi` y algunos overrides. El SM7150 (sweet) es de la misma generación (familia SDM845), así que casi todo el mapa de memoria coincide.

Leyenda: ✅ igual que sweet · ⚠️ cambia un valor · ❌ hay que escribir código nuevo · ❓ hay que comprobarlo en el teléfono.

## CPU y núcleo del sistema
| Bloque | grus | sweet | |
|---|---|---|---|
| CPU | 6× A55 + 2× A75 (Kryo 360), ARMv8.2 | 6× A55 + 2× A76 (Kryo 470) | ✅ LSE, LDAPR y granulado de 16 K por hardware, sin emulación (lo contrario que vince) |
| MPIDR | `0x000`…`0x700` (Aff1 = núcleo) | `core << 8` | ✅ mismo esquema 6+2, sirven 0065/0078 (SMP PSCI) |
| `compatible` de las CPU | Kryo 360 | `qcom,kryo470` en mkadt | ⚠️ cosmético |
| GICv3 GICD / GICR | `0x17a00000`/0x10000, `0x17a60000`/0x100000, stride 0x20000 | igual | ✅ |
| Timer | 19,2 MHz, timer virtual PPI 3 → INTID 19 | `TIMEBASE 19200000`, `VTIMER_INTID 19` | ✅ |
| PS_HOLD (reinicio) | `0x0c264000` | `PSHOLD 0x0c264000` | ✅ |
| Watchdog APSS | `0x17980000` | (no lo toca) | ❓ ver si ABL lo deja activo |
| cmd-db (RPMh) | `0x0c3f000c` | `RH_DICT_PA 0x0c3f000c` | ✅ |
| **RSC de RPMh (apps_rsc)** | **`0x179e0000`, TCS `0x179e0d00`** | `RH_RSC_PA 0x18220000`, `RH_TCS_PA 0x18220d00` | ⚠️ **cambia**: hay que parametrizar 0062. El tamaño del TCS (0x3000) sí coincide; el stride y el número de TCS hay que comprobarlos en `drivers/mailbox/qcom-rpmh-mailbox.c` |
| GCC | `0x00100000` | `GCC_PA 0x00100000` | ✅ |

## Memoria
| | grus | sweet |
|---|---|---|
| Carve-outs fijos | 0x85700000 (hyp) … 0x97617000 (pil_gpu); `removed_region` 0x85fc0000+0x4b40000 | todos por debajo de `PHYS_BASE` |
| cont_splash | `0x9c000000` + 0x2300000; dfps `0x9e300000` | `FB 0x9c000000` + 0x1700000 |
| RAM | 6 GB, probablemente `<0x80000000 2G>` y `<0x1_00000000 ...>` | `PHYS_BASE 0xb0000000` + `0x4d000000`, highmem `0x1_00000000` |

✅ La ventana de sweet (`0xb0000000`…`0xfd000000`) queda por encima de todos los carve-outs de grus. ✅ `/memory` del DTB vivo: `<0x80000000 0x80000000>, <0x1_00000000 0xfd6c0000>` (≈5,96 GB). Para `--highmem`: `HIGH_MEM_SIZE = 0xfd6c0000` (sweet: `0xfcc00000`).

## Almacenamiento
| | grus | sweet | |
|---|---|---|---|
| UFS host | `ufshc@1d84000` (0x3000), PHY `0x1d87000` | `UFS 0x01d84000`, 0x3000 | ✅ sirven el shim `ufs.c` y 0028/0030/0034/0069 tal cual. ❓ el número de LUN y de partición de `userdata` |

## Pantalla (riesgo principal)
| | grus | sweet | |
|---|---|---|---|
| Panel | Samsung EA8076 AMOLED, **1080×2340**, `dsi_cmd_mode`, TE por GPIO 10, reset GPIO 75, alimentación GPIO 76 | 1080×2400, video mode con DSC, "no refresh kick needed" | ❌ |
| Brillo | DCS (`bl_ctrl_dcs`, 2…2047) | backlight | ⚠️ |

**Resuelto en el teléfono (2026-11-10, shim m1):** ABL deja el autorefresh del PP0 activo (`PP_AUTOREFRESH_CONFIG = 0x80000001`, `PP_TEAR_CHECK_EN = 1`, CTL0 `top = 0x00020020`, command mode). Escribir en el framebuffer basta, igual que en sweet. Framebuffer: SSPP VIG0 en `0x9c000000`, 1080×2340, 32 bpp, stride 4320, unpack `0x03020001` (B, G, R, A en memoria = a8r8g8b8 en u32, igual que sweet). **No hay que escribir `CTL_START` con el autorefresh activo:** la transferencia se cuelga a media pantalla y el panel deja de refrescarse.

Análisis previo: en video mode el MDP lee el framebuffer de forma continua, y por eso sweet solo tiene que escribir en `0x9c000000`. **En command mode el panel tiene su propia RAM y solo se actualiza cuando el MDP lanza una transferencia.** Si ABL no deja activado el *autorefresh* del ping-pong, la pantalla se queda con el splash y nada de lo que escriba el shim se verá.
- Primera prueba (barata): arrancar el shim de diagnóstico y ver si aparece el texto.
- Si no aparece: activar `PP_AUTOREFRESH_CONFIG` en el ping-pong que usa ABL, o lanzar a mano un `CTL_START` tras cada copia.
- Direcciones (del DTB vivo y de `sde_hw_*.c`): MDP `0xae00000`; CTL 0–4 en `+0x2000` paso 0x200 (`CTL_FLUSH +0x18`, `CTL_START +0x1c`); PINGPONG 0–4 en `+0x71000` paso 0x800 (`PP_TEAR_CHECK_EN +0x0`, `PP_AUTOREFRESH_CONFIG +0x30`); INTF 0–3 en `+0x6b000` paso 0x800; DSC en `+0x81000`/`+0x81400`. ❓ No se sabe qué CTL/PP usa ABL (lo normal es CTL_0 + PP_0 + INTF_1 = DSI0). El shim puede leer esos registros y mostrarlos.
- ✅ ABL elige `msm_drm.dsi_display0=dsi_ss_ea8076_fhd_cmd_display:config2`: el command mode queda confirmado en esta unidad.
- Hay que cambiar `FB_H` a 2340 y el escalado a la presentación 1125×2436 (parche 0061 y el ADT).

## Touch
| | grus | sweet | |
|---|---|---|---|
| Controlador | Goodix GT9886/GT9889, protocolo **Normandy (GTx8)** | Goodix GT9896 | ❌ hay que reescribir la capa de protocolo |
| Bus | QUP wrapper1 SE1 `0xa84000`, I²C `0x5d` | `TOUCH_SE_PA 0x00a84000`, `0x5d` | ✅ sirve el transporte GENI (0060/0063) |
| Reloj | RCG `0x18148`, voto `0x5200c` | `TOUCH_RCG 0x18148`, `TOUCH_GCC_VOTE 0x5200c` | ✅ |
| GPIO | reset 5, IRQ 125, ambos en la tile NORTH (`0x3400000` + `0x500000`) | `TOUCH_NORTH_PA 0x03900000` | ⚠️ solo cambian los números de pin |
| Alimentación | `ldob8` (PM660L L8) 3,3 V + `ldoa13` (PM660 L13) 1,8 V | `ldoc11` 3,2 V | ⚠️ cambian los nombres en 0062, y hay dos raíles en vez de uno |
| Resolución | 1079×2339, `x2x`/`y2y` | 1080×2400 | ⚠️ 0071 (bordes) |

Registros Normandy (de `goodix_ts_i2c.c`): coordenadas `0x824E` (8 bytes por punto), comando `0x8040`, request `0x8044`, versión `0x8240`, config `0x8050`. Reset: GPIO a 0, 2 ms, a 1, 100 ms. Los de sweet (`0x4180` evento, `0x4022` PID) son de otra familia. ❓ Ver si el firmware que carga Android sobrevive a `fastboot boot` o si el chip necesita que se le mande la config (`goodix_gt9886_cfg_f2.bin`).

## Botones
| | grus | sweet | |
|---|---|---|---|
| SPMI | core `0xc440000`, obsrvr `0xe600000`, cnfg `0xc40a000` | `KEYS_CORE_PA`, `KEYS_OBS_PA`, `KEYS_CNFG_PA`, mismos valores | ✅ |
| Power / vol− | PON de PM660 (SID 0, `0x800`) | PON de PM6150 (`KEYS_PON_RT 0x0810`) | ✅ probablemente igual (mismo bloque PON gen2) |
| Vol+ | **PM660L GPIO 7** (SID 2, `0xc600`), activo en bajo | PM6150L GPIO 2 (SID 4, `KEYS_GPIO_RT 0xc110`) | ⚠️ cambian el SID y el offset |
| Hall | TLMM GPIO 124 | — | opcional |

## USB, Wi‑Fi y GPU
| | grus | sweet | |
|---|---|---|---|
| DWC3 | `0x0a600000` | `DWC3_PA 0x0a600000` | ✅ |
| QUSB2 PHY | `0x088e2000` | `QUSB2_PA 0x088e2000` | ✅ (USB ECM y log por red 0072–0081, en principio sin cambios). ❓ QSCRATCH `0x0a6f8800` |
| Wi‑Fi | WCN3990 por ICNSS `0x18800000` | ver HackiOS/topaz | ❓ |
| GPU | Adreno 616, chipid `0x06010600`, kgsl `0x5000000` | Adreno 618 | ⚠️ Mesa freedreno soporta toda la familia a6xx gen1 (615/616/618); camino parecido a ios-flame/topaz |

## Arranque
- ABL de Qualcomm igual que en sweet, con `fastboot boot`. El defconfig genera `Image.gz-dtb` (`CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE=y`) más DTBO.
- ✅ El boot.img de la unidad (`xnuios/grus-dump/boot.img`, ROM custom Android 13 userdebug con Magisk, `os_version 0x1a000178` = A13, parche 2023-08) es de **cabecera v2**, igual que sweet: page 0x1000, kernel_addr 0x8000, ramdisk_addr 0x1000000, tags 0x100, sección DTB de 0xbc6cc B en dtb_addr 0x1f00000. Sirven `unpack_bootv2.py` y el empaquetado de sweet.
- ✅ Identidad del DTB vivo: `qcom,msm-id = <0x189 0x0>` (SDM712), `qcom,board-id = <0x22 0x0>`, model "SDM710 PM660 + PM660L MTP, Grus". El DTB del shim tiene que llevar estos dos valores.
- ABL carga el ramdisk en `0x854b9000` (de `linux,initrd-start` en `/chosen`), dentro del banco bajo y por debajo de `PHYS_BASE`. `kaslr-seed` = 0.
- UART de depuración: `earlycon=msm_geni_serial,0xA90000` (QUP wrapper1). Los pads no son accesibles sin abrir el teléfono; como en sweet, el log irá por el framebuffer y por la partición.

## Resumen
Lo que sirve tal cual de sweet: GICv3, timer, SMP 6+2, UFS, SPMI, framebuffer en `0x9c000000`, USB, transporte GENI y relojes del touch, y la ventana de RAM.

Cambios pequeños (constantes):
1. RSC de RPMh: `0x179e0000`/`0x179e0d00`.
2. Raíles del touch: `ldob8` 3,3 V + `ldoa13` 1,8 V.
3. GPIO del touch: 5 y 125.
4. Vol+: PM660L GPIO 7.
5. Alto del panel: 2340.

Código nuevo:
1. **Panel en command mode**: autorefresh o lanzar cada frame a mano. Es lo primero que hay que validar.
2. **Protocolo Goodix Normandy** para el touch.

Datos del teléfono (`xnuios/grus-dump/`: `boot.img` y `live.dtb`), ya incorporados arriba. Siguiente paso: adaptar el shim de sweet con estos valores (board-id, alto 2340, sin UFS ni XNU todavía) y arrancar la imagen de diagnóstico para ver si el texto aparece en el panel en command mode. Si no aparece, la siguiente imagen prueba `CTL_START` y autorefresh sobre CTL_0/PP_0.
