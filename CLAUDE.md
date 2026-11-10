# ios-grus (fork cjxyz)

Port de iOS 16 (XNU-8796 + userland del iPhone X) al Xiaomi Mi 9 SE "grus" (SDM712, 6× Kryo 360 Silver + 2× Gold).
Fork de dipa-studio/ios-sweet (Redmi Note 10 Pro, SM7150): el SoC es de la misma generación y casi todo el mapa de memoria coincide.

Lee primero:
- `docs/grus/CONTEXT.md`: objetivo, linaje, decisiones y referencias.
- `docs/grus/HARDWARE.md`: auditoría del hardware de grus frente a sweet (direcciones, IRQ, GPIO, PMIC, panel, touch).
- `docs/grus/BUILD.md`: cómo compilar (XNU en GitHub Actions; shim y boot.img en WSL) y arrancar.
- `docs/grus/PROGRESS.md`: estado, pendientes y bitácora.
- `NOTES.md` y `AGENTS.md` son de upstream (sweet): sirven de referencia, pero el estado de grus vive en `docs/grus/`.

Reglas:
- Cada cambio de parche o prueba en el teléfono termina con una entrada en `docs/grus/PROGRESS.md` y un commit.
- XNU solo se compila en CI (`.github/workflows/xnu.yml`). Los parches viven en `xnu/patches/NNNN-*.patch`, numerados y aplicados en orden.
- Arrancar siempre con `fastboot boot`, nunca flashear `boot`.
- **`userdata` es para la raíz iOS** (el usuario lo autorizó el 2026-11-10; solo tenía datos de prueba de Android). `--root` arranca con `ufsrw=1` y con el log persistente de sweet activo (últimos 4 MiB de `userdata`, detrás del volumen HFS+). El resto de particiones no se tocan nunca, y cada flasheo se confirma con el usuario antes de ejecutarlo.
- Nunca escribir `CTL_START` en el MDP: ABL deja el autorefresh activo y un kick cuelga el panel (ver PROGRESS).
- Rama de trabajo: `grus-port` (parte de `45263be`; los parches de XNU son los del último build verde de upstream, `fdad0de`).
  CI manual: `gh workflow run xnu.yml -R cjxyz/ios-grus --ref grus-port -f runner=macos-15-intel`.
- Remotos: `origin` = cjxyz/ios-grus, `upstream` = dipa-studio/ios-sweet.
- Fuera de git: volcados del teléfono (`../grus-dump`), números de serie, imágenes generadas y artefactos de Apple.

## Commits
Identidad: `cjxyz <cjxyz776@gmail.com>` (git config local del repo). Push con la cuenta gh `cjxyz`: el repo tiene un
credential helper local que siempre toma el token de `gh auth token --user cjxyz`, así que no depende de la cuenta activa.
Para `gh` usa `GH_TOKEN=$(gh auth token --user cjxyz)`.
Mensajes **en inglés**, con esta estructura:
```
type(scope): imperative summary

Body explaining what changes and why (the problem, how it was found,
what the fix does), wrapped at ~76 columns. Bullets are fine for
several independent points. Refer to other commits of a series by
phase, not by hash.

Co-Authored-By: ...
```
- `type`: feat, fix, perf, refactor, docs, ci, chore, test.
- `scope`: zona del repo: `grus`, `shim`, `tools`, `docs`, `ci`, o el parche XNU (`xnu/0090`).
- Verbo en imperativo: "add", "fix", "disable"; no "adds" ni "added".
- Por ahora no se usa indicador de serie `[series n/total]`.
- Solo los commits triviales pueden ir sin cuerpo.
