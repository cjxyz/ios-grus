# Build y arranque

## 1. Kernel XNU (GitHub Actions)
Workflow: `.github/workflows/xnu.yml` (heredado de sweet; runner elegible, por defecto `macos-15-intel`).
```
export GH_TOKEN=$(gh auth token --user cjxyz)
gh workflow run xnu.yml -R cjxyz/ios-grus --ref grus-port                       # macos-15-intel
gh workflow run xnu.yml -R cjxyz/ios-grus --ref grus-port -f runner=macos-15    # cola arm64
gh run list -R cjxyz/ios-grus -w xnu.yml
gh run download <run-id> -R cjxyz/ios-grus -n xnu-kernel -D out/kernel
```
- Entradas: `macos_version=13.5` (xnu-8796), `kernel_config=DEVELOPMENT`, `machine_config=SWEET` (todavía no hay config GRUS), `keep_going`, `runner`.
- Clona blacktop/darwin-xnu-build en el SHA fijado y le copia `xnu/patches/*`.
- Usa el Xcode más antiguo instalado en el runner. `iig` tiene que generar código compatible con la ABI IORPC de xnu-8796; en ios-vince funcionó Xcode 16.0.
- Artefactos: `xnu-kernel` (`kernel.development.sweet` + SHA256SUMS) y `xnu-buildtree`.

## 2. Shim de diagnóstico y boot.img (local, WSL)
Herramientas: `python3` y LLVM (`clang`, `ld.lld`, `llvm-objcopy`, `llvm-nm`, `llvm-readelf`).
```
python3 grus/diag/build.py                                   # -> artifacts/grus-diag.img
python3 grus/tools/bootimg.py selftest ../grus-dump/boot.img # el empaquetador reproduce el boot.img stock
```
- La cabecera v2 (direcciones, page size, os_version) y la sección DTB se copian del `boot.img` del teléfono (`../grus-dump/boot.img`). ABL aplica el overlay de grus desde `dtbo`, igual que con Android.
- El build falla si los objetos C tienen relocaciones `R_AARCH64_ABS64`: ABL ejecuta la imagen donde la carga (`0x80080000`), sin reubicarla.

## 3. Imagen de arranque de XNU (local, WSL)
```
gh run download <run-id> -R cjxyz/ios-grus -n xnu-kernel -D out/kernel   # kernel del CI
python3 grus/tools/mkxnuboot.py                                          # -> artifacts/grus-xnu.img
python3 grus/tools/mkadt_grus.py build/grus.adt --dump                   # inspeccionar el ADT
```
- Necesita `research/ipsw-x/d22ap.adt` (DeviceTree del iPhone X, artefacto de Apple, fuera de git) para el nodo `/product`. Sin él hay un aviso y no se genera ese nodo.
- `--cmdline` cambia los boot-args; `--ramdisk` añade una imagen raíz para `rd=md0`.

## 4. Volcados del teléfono (Windows, PowerShell, Magisk)
Desde `C:\platform-tools`:
```powershell
.\adb.exe shell "su -c 'dd if=/dev/block/by-name/boot of=/sdcard/boot.img'"
.\adb.exe shell "su -c 'cat /sys/firmware/fdt > /sdcard/live.dtb'"
.\adb.exe pull /sdcard/boot.img
.\adb.exe pull /sdcard/live.dtb
```
Se guardan en `xnuios/grus-dump/` (fuera de git).

## 5. Arranque
```powershell
.\adb.exe reboot bootloader
$img = "\\wsl.localhost\Ubuntu\home\lau\proyectos\personal\xnuios\ios-grus\artifacts\grus-diag.img"
.\fastboot.exe boot $img
```
Para XNU, la misma orden con `artifacts\grus-xnu.img`.
- Si al conectar el cable en fastboot aparece "press any key to shutdown", Windows no está enumerando el dispositivo. Arreglo (PowerShell como administrador):
  ```powershell
  $k = "HKLM\SYSTEM\CurrentControlSet\Control\usbflags\18D1D00D0100"
  reg add $k /v osvc /t REG_BINARY /d 0000 /f
  reg add $k /v SkipContainerIdQuery /t REG_BINARY /d 01000000 /f
  reg add $k /v SkipBOSDescriptorQuery /t REG_BINARY /d 01000000 /f
  ```
- El shim m2 vuelve solo a fastboot al terminar (motivo de reinicio en `SOFT_RB_SPARE` del PM660). Para volver a Android: `.\fastboot.exe reboot`.
