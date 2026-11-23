# Proyecto 3 - Programa de arranque (UEFI)

Instalar dependencias (PowerShell):

```powershell
winget install MSYS2.MSYS2
winget install SoftwareFreedomConservancy.QEMU
```

En la terminal de MSYS2:

```bash
pacman -S mingw-w64-x86_64-gcc
```

Compilar:

```bash
x86_64-w64-mingw32-gcc -ffreestanding -nostdlib -Wl,--subsystem,10 -e efi_main -o esp/EFI/BOOT/BOOTX64.EFI boot.c
```

Ejecutar en QEMU (PowerShell):

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -machine q35 -drive if=pflash,format=raw,readonly=on,file="C:\Program Files\qemu\share\edk2-x86_64-code.fd" -drive file=fat:rw:esp,format=raw
```
