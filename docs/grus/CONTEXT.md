# Contexto del port

## Objetivo
Arrancar iOS 16 (kernel XNU compilado desde fuente pública + userland del iPhone X) en un
**Xiaomi Mi 9 SE "grus"** (Qualcomm SDM712), siguiendo el enfoque descrito en <https://hackiosx.dev>.
Es un proyecto de investigación, no un sistema de uso diario. No hay ningún port público de grus (2026-11-10).

## Linaje
```
dipa-studio/ios-sweet   Redmi Note 10 Pro (SM7150), base de casi todos los ports
  └─ cjxyz/ios-grus     este fork
```
Remotos git: `origin` = cjxyz/ios-grus, `upstream` = dipa-studio/ios-sweet.
Proyecto hermano: cjxyz/ios-vince (Redmi 5 Plus), en `../ios-vince`.

## Decisiones tomadas (2026-11-10)
| Decisión | Motivo |
|---|---|
| Fork directo de ios-sweet | El SDM712 comparte con el SM7150 GICv3, timer, UFS, SPMI, framebuffer de splash, USB, bus y relojes del touch, y el SMP 6+2. Ver [HARDWARE.md](HARDWARE.md). |
| Compilar XNU en GitHub Actions (macOS) | El build exige Xcode/iig; el equipo de trabajo es WSL/Linux. El repo es público, así que los runners son gratuitos. |
| Runner por defecto `macos-15-intel` | `macos-14` (el de upstream) está retirado y la cola arm64 de `macos-15` suele ir saturada; el Intel compila el kernel arm64 igual (comprobado en ios-vince). |
| Shim de diagnóstico propio (`grus/diag/`) antes de portar el shim de sweet | Validar en el teléfono panel, CPU, SMP, GIC y SPMI con imágenes pequeñas. |
| Arranque siempre con `fastboot boot`, nunca flasheando `boot` | Android sigue arrancable. |
| Kernel fuente stock en `../grus-stock` y volcados del teléfono en `../grus-dump` | Referencia de hardware; fuera de git. |

## Diferencias con sweet que importan
- **Panel EA8076 en command mode** (1080×2340). ABL deja el autorefresh activo: basta con escribir en el framebuffer, pero nunca hay que lanzar `CTL_START`.
- **Touch Goodix GT9886 (protocolo Normandy)**: mismo bus que sweet, pero otro mapa de registros.
- **RPMh**: el RSC está en `0x179e0000` (sweet: `0x18220000`). Los raíles del touch son `ldob8` (3,3 V) y `ldoa13` (1,8 V).
- **PARange**: 40 bits en los núcleos pequeños y 44 en los grandes. XNU debe usar 40.
- Ver el detalle en [HARDWARE.md](HARDWARE.md).

## Referencias
- hackiosx.dev: [proyectos](https://hackiosx.dev/projects.html), [compatibilidad](https://hackiosx.dev/compatibility.html)
- ios-sweet: <https://github.com/dipa-studio/ios-sweet> (README, NOTES.md)
- ios-topaz (Redmi Note 12, SD685): <https://github.com/MakrSas/ios-topaz>. GPU por Mesa, Wi‑Fi y sonido.
- ios-flame (Pixel 4): <https://github.com/NWylynko/ios-flame>. Metal sobre Adreno 640 con Mesa.
- r/hackios: <https://www.reddit.com/r/hackios/>
