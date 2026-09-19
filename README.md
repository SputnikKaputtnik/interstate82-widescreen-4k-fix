# Interstate '82 — Widescreen (1920x1080) and Windows 11 Fix

Small, source-available compatibility shim that makes the **GOG release of Interstate '82** run on Windows 10 and 11 — and adds the **widescreen resolution the game never shipped with**.

Interstate '82 was built for 4:3 and 5:4 monitors: it offers exactly four resolutions (640x480, 800x600, 1024x768, 1280x1024) and rejects everything else, so no config file or wrapper can give you 16:9. This shim widens that filter, and **1920x1080 appears in the game's own video options**. The engine handles the wider frame properly: the view extends sideways (Hor+) instead of stretching or cropping, and the HUD moves to the screen edges.

It also fixes the crashes and input problems that stop the game on modern Windows, and removes the need for DxWnd's visible launcher. It does **not** contain any game files, nor a copy of DDrawCompat.

Tested on Windows 11 with an NVIDIA RTX 4070 Ti.

**Keywords:** Interstate 82 widescreen patch, Interstate '82 1080p, 16:9 fix, Windows 11 crash fix, GOG, DDrawCompat, resolution patch.

## What it fixes

- DirectDraw/Direct3D 1-7 presentation through [DDrawCompat](https://github.com/narzoul/DDrawCompat), configured for borderless fullscreen.
- A mission/level-load crash in the tested GOG build by applying a narrowly targeted heap-compatibility workaround to `i82sim.dll`.
- Empty or localized DirectInput keyboard-object names that prevent normal in-game key binding. The proxy exposes canonical English DirectInput keyboard names only for the keyboard path used by I82.
- Modal developer diagnostics from the input system (`CInputBinding::bindControl` and its siblings). One appears between the menu and the load screen on every mission start and has to be dismissed by hand, although it reports a condition a player cannot act on. They are answered automatically and written to `dinput_msgbox.log` instead.
- The absence of any widescreen resolution. I82 accepts only four hardcoded modes (640x480, 800x600, 1024x768, 1280x1024), so no configuration file can produce a 16:9 frame. The shim widens that filter to offer **1920x1080** in place of 1280x1024. The engine itself handles the wider frame correctly: the field of view extends horizontally (Hor+) and the HUD moves to the screen edges.

The shim no longer hardcodes any address inside `i82sim.dll`, but it has only been developed and validated against the tested GOG release. Do not assume it is correct for a different release, executable, mod, or language build without checking it first.

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

   The profile's `SupportedResolutions` line is required for widescreen: the game offers 1920x1080 once the shim is in place, but DDrawCompat must accept the mode as well, and its default list holds 4:3 modes only. Select the resolution in the game under Options → Video.

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
- For widescreen it rewrites two immediate operands in the display-mode filter of `i82sim.dll` and `I82ShellDll.dll`, changing the accepted 1280x1024 pair to 1920x1080. The site is located by matching the instruction shape, not a fixed address, and nothing is written unless it matches.
- It redirects three groups of imports: `HeapSize`, `HeapReAlloc` and `HeapFree` in `i82sim.dll`; the `LoadLibrary` family in `i82stubz.exe`, so the heap hooks are in place the moment the module is mapped rather than a poll interval later; and `MessageBoxA` in `i82sim.dll` and `I82ShellDll.dll`.
- The DirectInput hook affects only the system keyboard object.
- Only popups whose caption begins with `CInput` are suppressed. Everything else is passed through untouched.
- The only file written is `dinput_msgbox.log`, and only when such a popup is suppressed. There is no telemetry, no registry access and no background diagnostics.
- Remove `dinput.dll`, `dinput_orig.dll`, `ddraw.dll`, and `DDrawCompat-i82stubz.ini` to revert this method. Restore any files from your backup if they existed before installation.

## Credits

- [DDrawCompat](https://github.com/narzoul/DDrawCompat) by narzoul provides the DirectDraw/Direct3D 1-7 compatibility layer. Its DLL is not included here.
- Interstate '82 and its assets remain the property of their respective copyright holders. This repository contains no game assets or original game binaries.

## License

The shim source and configuration in this repository are available under the [MIT License](LICENSE).
