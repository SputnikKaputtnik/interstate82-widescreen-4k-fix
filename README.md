# Interstate '82 Windows 11 Compatibility Shim

Small, source-available compatibility shim for the **GOG release of Interstate '82** on current Windows systems. It was tested on Windows 11 with an NVIDIA RTX 4070 Ti.

It removes the need for DxWnd's visible launcher in this tested setup. It does **not** contain any game files, nor a copy of DDrawCompat.

## What it fixes

- DirectDraw/Direct3D 1-7 presentation through [DDrawCompat](https://github.com/narzoul/DDrawCompat), configured for borderless fullscreen.
- A mission/level-load crash in the tested GOG build by applying a narrowly targeted heap-compatibility workaround to `i82sim.dll`.
- Empty or localized DirectInput keyboard-object names that prevent normal in-game key binding. The proxy exposes canonical English DirectInput keyboard names only for the keyboard path used by I82.

The shim is version-specific: it contains an RVA for the tested GOG `i82sim.dll`. Do not use it with a different release, executable, mod, or language build without validating it first.

## Requirements

- A legally installed, unmodified GOG copy of Interstate '82.
- Windows 10 or 11, 64-bit.
- The current release of [DDrawCompat](https://github.com/narzoul/DDrawCompat/releases). Download it from its upstream project; do not redistribute a copy from this repository.
- A 32-bit copy of the system DirectInput DLL, normally `%WINDIR%\SysWOW64\dinput.dll`.

## Installation

1. Back up the game directory.
2. Download DDrawCompat upstream and copy its `ddraw.dll` next to `i82stubz.exe`.
3. Build `dinput.dll` from this repository, or use a release artifact when one is provided.
4. Copy the 32-bit system DLL `%WINDIR%\SysWOW64\dinput.dll` into the game directory as `dinput_orig.dll`.
5. Copy the built DLL and the included DDrawCompat profile as follows:

   - `dinput.dll` → game directory, next to `i82stubz.exe`
   - `dinput_orig.dll` → game directory, next to `i82stubz.exe`
   - `DDrawCompat-i82stubz.ini` → game directory, next to `i82stubz.exe`

   `dinput.def` is only needed to build the DLL; it does not need to be copied to the game directory.

6. Start `i82stubz.exe` normally. No DxWnd launcher is required.

The included DDrawCompat profile deliberately uses `FullscreenMode = borderless`, not exclusive fullscreen. It was the stable, confirmed mode for this setup.

## Build

Build with a 32-bit MinGW-w64 GCC toolchain. In an MSYS2 MinGW32 shell:

```sh
gcc -shared -s -Wl,--enable-stdcall-fixup -o dinput.dll dinput.c dinput.def
```

The resulting `dinput.dll` must stay beside `dinput_orig.dll`. The export definition forwards all non-intercepted DirectInput exports to `dinput_orig.dll`.

## Scope and safety

- The source is a targeted compatibility experiment, not a general DirectInput wrapper.
- It changes only I82's imported `HeapSize`, `HeapReAlloc`, and `HeapFree` call sites after `i82sim.dll` loads.
- The DirectInput hook affects only the system keyboard object.
- The release build contains no telemetry, logging, registry changes, popup hooks, or background diagnostics.
- Remove `dinput.dll`, `dinput_orig.dll`, `ddraw.dll`, and `DDrawCompat-i82stubz.ini` to revert this method. Restore any files from your backup if they existed before installation.

## Credits

- [DDrawCompat](https://github.com/narzoul/DDrawCompat) by narzoul provides the DirectDraw/Direct3D 1-7 compatibility layer. Its DLL is not included here.
- Interstate '82 and its assets remain the property of their respective copyright holders. This repository contains no game assets or original game binaries.

## License

The shim source and configuration in this repository are available under the [MIT License](LICENSE).
