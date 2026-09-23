# Interstate '82 — 4K and Widescreen (3840x2160 / 1920x1080) and Windows 11 Fix

Small, source-available compatibility shim that makes the **GOG release of Interstate '82** run on Windows 10 and 11 — and adds the **4K and widescreen resolutions the game never shipped with**.

Interstate '82 was built for 4:3 and 5:4 monitors: it offers exactly four resolutions (640x480, 800x600, 1024x768, 1280x1024) and rejects everything else, so no config file or wrapper can give you 16:9. This shim widens that filter, and **1920x1080 and 3840x2160 appear in the game's own video options**. The engine handles the wider frame properly: the view extends sideways (Hor+) instead of stretching or cropping, the HUD moves to the screen edges — and it scales the HUD with the resolution, so 4K stays readable rather than shrinking into a corner.

4K measured around 100 fps on an RTX 4070 Ti where 1080p reached 240, so it costs roughly what the extra pixels are worth and nothing more.

It also fixes the crashes and input problems that stop the game on modern Windows, and removes the need for DxWnd's visible launcher. It does **not** contain any game files, nor a copy of DDrawCompat.

Tested on Windows 11 with an NVIDIA RTX 4070 Ti.

**Keywords:** Interstate 82 4K patch, Interstate '82 widescreen patch, Interstate 82 1080p, 2160p, 16:9 fix, Windows 11 crash fix, GOG, DDrawCompat, resolution patch, draw distance, view distance, fog.

## What it fixes

- DirectDraw/Direct3D 1-7 presentation through [DDrawCompat](https://github.com/narzoul/DDrawCompat), configured for borderless fullscreen.
- A mission/level-load crash in the tested GOG build by applying a narrowly targeted heap-compatibility workaround to `i82sim.dll`.
- Empty or localized DirectInput keyboard-object names that prevent normal in-game key binding. The proxy exposes canonical English DirectInput keyboard names only for the keyboard path used by I82.
- Modal developer diagnostics from the input system (`CInputBinding::bindControl` and its siblings). One appears between the menu and the load screen on every mission start and has to be dismissed by hand, although it reports a condition a player cannot act on. They are answered automatically and written to `dinput_msgbox.log` instead.
- The absence of any widescreen resolution. I82 accepts only four hardcoded modes (640x480, 800x600, 1024x768, 1280x1024), so no configuration file can produce a 16:9 frame. The shim widens that filter to offer **1920x1080** in place of 1280x1024, and **3840x2160** in place of 1024x768. The engine itself handles the wider frame correctly: the field of view extends horizontally (Hor+), the HUD moves to the screen edges, and it scales with the resolution.

  The 4K slot is only taken on a desktop that is at least 3840x2160; below that, 1024x768 is left alone rather than traded for a mode the monitor cannot show. 640x480 and 800x600 are never touched — the game's width dispatch tests ">800" first, so only the two upper slots can hold a widescreen mode.

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

   The profile's `SupportedResolutions` line is required for widescreen: the game offers 1920x1080 and 3840x2160 once the shim is in place, but DDrawCompat must accept the modes as well, and its default list holds 4:3 modes only. Select the resolution in the game under Options → Video.

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
- For widescreen it rewrites up to four immediate operands in the display-mode filter of `i82sim.dll` and `I82ShellDll.dll`: the accepted 1280x1024 pair becomes 1920x1080, and 1024x768 becomes 3840x2160 on a desktop that can show it. The site is located by matching the instruction shape, not a fixed address; the height compare of the second slot is reached by following the filter's own branch rather than by searching nearby, and nothing is written unless every part matches.
- Both game modules are packed, so their code is still encrypted for a short while after they appear in the loader's module list. The patch therefore never writes while a `LoadLibrary` call is in progress, and it keeps retrying until a scan actually matches instead of assuming the first attempt saw real code.
- It redirects three groups of imports: `HeapSize`, `HeapReAlloc` and `HeapFree` in `i82sim.dll`; the `LoadLibrary` family in `i82stubz.exe`, so the heap hooks are in place the moment the module is mapped rather than a poll interval later; and `MessageBoxA` in `i82sim.dll` and `I82ShellDll.dll`.
- The DirectInput hook affects only the system keyboard object.
- Only popups whose caption begins with `CInput` are suppressed. Everything else is passed through untouched.
- The only file written is `dinput_msgbox.log`, and only when such a popup is suppressed. There is no telemetry, no registry access and no background diagnostics.
- Remove `dinput.dll`, `dinput_orig.dll`, `ddraw.dll`, and `DDrawCompat-i82stubz.ini` to revert this method. Restore any files from your backup if they existed before installation.

## Optional: longer draw distance

`patch_viewdistance.py` is a separate, optional tool. It has nothing to do with the shim and is not needed to play — it edits the game's own level data to draw the world further out.

Interstate '82 stores a `World_Data` block per level as plain text inside `i82.zfs`, and `i82perf.ini`'s `Far_Clipping_Plane = 2` is already the highest setting the game offers. The real values are there:

```
Fog_Max:        200.00
Clipping_Plane: 200
```

The two are equal on purpose: the fog is what hides the edge where the world stops being drawn. Raising the clipping plane alone would only move the pop-in into a fog bank, so the tool scales both, with separate factors. Because `Fog_Alpha` is 128 the fog never becomes fully opaque, so geometry past `Fog_Max` still reads as a silhouette — pushing the clipping plane further than the fog adds depth instead of drawing something invisible. Hence the defaults: **clipping 3x, fog 2x**.

On the machine this was developed against it cost nothing measurable — 107 fps at 3840x2160, against 100 before. The engine is not limited by geometry here; the short view distance was a decision for 1999 hardware.

```sh
python patch_viewdistance.py                      # show what would change
python patch_viewdistance.py --apply              # clipping 3x, fog 2x
python patch_viewdistance.py --clip 4 --fog 2.5 --apply
python patch_viewdistance.py --restore            # undo
```

It edits your installed `i82.zfs`. Before the first change it keeps an untouched copy as `i82.zfs.original` beside it and always computes from that copy, so different factors never compound and `--restore` always works. Values live in fixed-width fields, so each is rewritten to the same byte length: every offset in the archive stays valid and nothing is repacked. Add `--game` if your installation is not in the default GOG location. Start a mission to see the difference — the menu shows nothing of it.

The tool ships no game data and redistributes nothing; it changes files you already own.

## Troubleshooting

**"Where is shell dll?" followed by "Shell Error!" on startup, and the game quits.**
An overlay is hooking the process as it launches. RivaTuner Statistics Server (RTSS, shipped with MSI Afterburner) does this reliably: with RTSS already running, the game dies before its menu module is ever loaded — first with an access violation in `RTSSHooks.dll` in the Windows application log, later with no crash entry at all, because the game bails out through its own error path first. It happens with any `dinput.dll`, including none, so it is easy to mistake for a problem with this shim.

Either start the game first and RTSS afterwards, which works because injecting into a running process does not disturb it, or give `i82stubz.exe` a profile in RTSS with *Application detection level* set to **None**. Other overlays that inject at process start are worth ruling out the same way. When a startup failure makes no sense, check the Windows application log for a foreign module before suspecting anything else.

**The video options only offer 4:3 modes.**
`SupportedResolutions` is missing from the DDrawCompat profile, or the profile is not named after the executable (`DDrawCompat-i82stubz.ini`).

## Credits

- [DDrawCompat](https://github.com/narzoul/DDrawCompat) by narzoul provides the DirectDraw/Direct3D 1-7 compatibility layer. Its DLL is not included here.
- Interstate '82 and its assets remain the property of their respective copyright holders. This repository contains no game assets or original game binaries.

## License

The shim source and configuration in this repository are available under the [MIT License](LICENSE).
