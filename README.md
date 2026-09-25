# Interstate '82 — 4K and Widescreen (3840x2160 / 1920x1080) and Windows 11 Fix

Small, source-available compatibility shim that makes the **GOG release of Interstate '82** run on Windows 10 and 11 — and adds the **4K and widescreen resolutions the game never shipped with**.

Interstate '82 was built for 4:3 and 5:4 monitors: it offers exactly four resolutions (640x480, 800x600, 1024x768, 1280x1024) and rejects everything else, so no config file or wrapper can give you 16:9. This shim widens that filter, and **1920x1080 and 3840x2160 appear in the game's own video options**. The engine handles the wider frame properly: the view extends sideways (Hor+) instead of stretching or cropping, the HUD moves to the screen edges — and it scales the HUD with the resolution, so 4K stays readable rather than shrinking into a corner.

4K measured around 100 fps on an RTX 4070 Ti where 1080p reached 240, so it costs roughly what the extra pixels are worth and nothing more.

It also fixes the crashes and input problems that stop the game on modern Windows, and removes the need for DxWnd's visible launcher. It does **not** contain any game files, nor a copy of DDrawCompat.

Tested on Windows 11 with an NVIDIA RTX 4070 Ti.

Project page: https://sputnikkaputtnik.github.io/interstate82-widescreen-4k-fix/

**Keywords:** Interstate 82 4K patch, Interstate '82 widescreen patch, Interstate 82 1080p, 2160p, 16:9 fix, Windows 11 crash fix, GOG, DDrawCompat, resolution patch, draw distance, view distance, fog.

## What it fixes

- DirectDraw/Direct3D 1-7 presentation through [DDrawCompat](https://github.com/narzoul/DDrawCompat), configured for borderless fullscreen.
- A mission/level-load crash in the tested GOG build by applying a narrowly targeted heap-compatibility workaround to `i82sim.dll`.
- Keyboard controls that are dead in missions. DirectInput takes the names of the keyboard, the mouse and every key from Windows' language files, so on a German system it reports "Tastatur", "Maus" and "Leertaste". I82 cannot register its controls with localized names. The menus still work, but every in-mission action "does not exist". The proxy gives I82 empty names for the system keyboard and mouse, and the canonical English DirectInput name for every key, whatever the Windows language.
- Modal developer diagnostics from the input system (`CInputBinding::bindControl` and its siblings). One appears between the menu and the load screen on every mission start and has to be dismissed by hand, although it reports a condition a player cannot act on. They are answered automatically and written to `dinput_msgbox.log` instead.
- The absence of any widescreen resolution. I82 accepts only four hardcoded modes (640x480, 800x600, 1024x768, 1280x1024), so no configuration file can produce a 16:9 frame. The shim widens that filter to offer **1920x1080** in place of 1280x1024, and **3840x2160** in place of 1024x768. The engine itself handles the wider frame correctly: the field of view extends horizontally (Hor+), the HUD moves to the screen edges, and it scales with the resolution.

  The 4K slot is only taken on a desktop that is at least 3840x2160; below that, 1024x768 is left alone rather than traded for a mode the monitor cannot show. 640x480 and 800x600 are never touched — the game's width dispatch tests ">800" first, so only the two upper slots can hold a widescreen mode.

The shim no longer hardcodes any address inside `i82sim.dll`, but it has only been developed and validated against the tested GOG release. Do not assume it is correct for a different release, executable, mod, or language build without checking it first.

## Requirements

- A legally installed, unmodified GOG copy of Interstate '82.
- Windows 10 or 11, 64-bit.
- The current release of [DDrawCompat](https://github.com/narzoul/DDrawCompat/releases). Download it from its upstream project; do not redistribute a copy from this repository.

## Installation

1. Back up the game directory.
2. Download DDrawCompat upstream and copy its `ddraw.dll` next to `i82stubz.exe`.
3. Download `dinput.dll` and `DDrawCompat-i82stubz.ini` from the [latest release](https://github.com/SputnikKaputtnik/interstate82-widescreen-4k-fix/releases/latest). `SHA256SUMS.txt` there lets you verify both. If you would rather not run a prebuilt binary, build `dinput.dll` yourself (see [Build](#build)); the profile is also in this repository.
4. Place the files as follows:

   - `dinput.dll` → game directory, next to `i82stubz.exe`
   - `DDrawCompat-i82stubz.ini` → game directory, next to `i82stubz.exe`

   The profile's `SupportedResolutions` line is required for widescreen: the game offers 1920x1080 and 3840x2160 once the shim is in place, but DDrawCompat must accept the modes as well, and its default list holds 4:3 modes only. Select the resolution in the game under Options → Video.

   `dinput.def` is only needed to build the DLL; it does not need to be copied to the game directory.

5. Start `i82stubz.exe` normally. No DxWnd launcher is required.

Upgrading from v1.2 or earlier: replace `dinput.dll`. The `dinput_orig.dll` that older versions needed can stay or be deleted. If it is there, the shim still uses it.

The included DDrawCompat profile deliberately uses `FullscreenMode = borderless`, not exclusive fullscreen. It was the stable, confirmed mode for this setup.

## Build

Build with a 32-bit MinGW-w64 GCC toolchain. In an MSYS2 MinGW32 shell:

```sh
gcc -shared -s -Wl,--enable-stdcall-fixup -o dinput.dll dinput.c dinput.def
```

The shim loads the real DirectInput itself: a `dinput_orig.dll` next to the game if one is there, otherwise the system's `dinput.dll`. The game is a 32-bit process, so Windows serves the 32-bit DLL from SysWOW64. All DirectInput exports the shim does not change are passed straight through to it.

The optional draw-distance tool builds the same way, as a plain console program:

```sh
gcc -O2 -s -o patch_viewdistance.exe patch_viewdistance.c
```

## Scope and safety

- The source is a targeted compatibility experiment, not a general DirectInput wrapper.
- For widescreen it rewrites up to four immediate operands in the display-mode filter of `i82sim.dll` and `I82ShellDll.dll`: the accepted 1280x1024 pair becomes 1920x1080, and 1024x768 becomes 3840x2160 on a desktop that can show it. The site is located by matching the instruction shape, not a fixed address; the height compare of the second slot is reached by following the filter's own branch rather than by searching nearby, and nothing is written unless every part matches.
- Both game modules are packed, so their code is still encrypted for a short while after they appear in the loader's module list. The patch therefore never writes while a `LoadLibrary` call is in progress, and it keeps retrying until a scan actually matches instead of assuming the first attempt saw real code. A module counts as patched only while the patched bytes are still there, so a module the game unloads and loads again, as it does on "Restart Mission", is patched again.
- It redirects three groups of imports: `HeapSize`, `HeapReAlloc` and `HeapFree` in `i82sim.dll`; the `LoadLibrary` family in `i82stubz.exe`, so the heap hooks are in place the moment the module is mapped rather than a poll interval later; and `MessageBoxA` in `i82sim.dll` and `I82ShellDll.dll`.
- The DirectInput hooks change only names: the device names of the system keyboard and mouse, and the names of their keys and buttons. Input itself is untouched.
- Only popups whose caption begins with `CInput` are suppressed. Everything else is passed through untouched.
- The only file written is `dinput_msgbox.log`, and only when such a popup is suppressed. There is no telemetry, no registry access and no background diagnostics.
- Remove `dinput.dll`, `ddraw.dll`, `DDrawCompat-i82stubz.ini` and, if an older version left one, `dinput_orig.dll` to revert this method. Restore any files from your backup if they existed before installation.

## Optional: longer draw distance

`patch_viewdistance.exe` is a separate, optional tool. It has nothing to do with the shim and is not needed to play — it edits the game's own level data to draw the world further out. The same tool is also available as a Python script, `patch_viewdistance.py`; both produce byte-identical results.

Interstate '82 stores a `World_Data` block per level as plain text inside `i82.zfs`, and `i82perf.ini`'s `Far_Clipping_Plane = 2` is already the highest setting the game offers. The real values are there:

```
Fog_Max:        200.00
Clipping_Plane: 200
```

The two are equal on purpose: the fog is what hides the edge where the world stops being drawn. Raising the clipping plane alone would only move the pop-in into a fog bank, so the tool scales both, with separate factors. Because `Fog_Alpha` is 128 the fog never becomes fully opaque, so geometry past `Fog_Max` still reads as a silhouette — pushing the clipping plane further than the fog adds depth instead of drawing something invisible. Hence the defaults: **clipping 3x, fog 2x**.

On the machine this was developed against it cost nothing measurable — 107 fps at 3840x2160, against 100 before. The engine is not limited by geometry here; the short view distance was a decision for 1999 hardware.

**The simple way:** download `patch_viewdistance.exe` from the [latest release](https://github.com/SputnikKaputtnik/interstate82-widescreen-4k-fix/releases/latest), put it in the game folder next to `i82.zfs`, and double-click it. It applies clipping 3x and fog 2x and shows what it changed. Double-clicking it again offers to put the original draw distance back. You can also drop `i82.zfs` onto it wherever the program is. Windows may warn about an unrecognized program the first time, because the exe is not code-signed; the source is `patch_viewdistance.c` in this repository.

From a command prompt it takes options:

```sh
patch_viewdistance.exe --dry-run                   # show what would change
patch_viewdistance.exe --clip 4 --fog 2.5          # other factors
patch_viewdistance.exe --restore                   # undo
patch_viewdistance.exe "D:\Games\Interstate 82"    # another game folder
```

With Python installed, the script does the same. Note that it only shows what would change unless you pass `--apply`:

```sh
python patch_viewdistance.py                      # show what would change
python patch_viewdistance.py --apply              # clipping 3x, fog 2x
python patch_viewdistance.py --clip 4 --fog 2.5 --apply
python patch_viewdistance.py --restore            # undo
```

Either way, it edits your installed `i82.zfs`. Before the first change it keeps an untouched copy as `i82.zfs.original` beside it and always computes from that copy, so different factors never compound and `--restore` always works. Values live in fixed-width fields, so each is rewritten to the same byte length: every offset in the archive stays valid and nothing is repacked. The exe finds the archive next to itself, in the current folder or in the default GOG location; the script looks in the default GOG location. Pass `--game` with your installation folder to either of them otherwise. Close the game before running it. Start a mission to see the difference — the menu shows nothing of it.

The tool ships no game data and redistributes nothing; it changes files you already own.

## Troubleshooting

**"Where is shell dll?" followed by "Shell Error!" on startup, and the game quits.**
An overlay is hooking the process as it launches. RivaTuner Statistics Server (RTSS, shipped with MSI Afterburner) does this reliably: with RTSS already running, the game dies before its menu module is ever loaded — first with an access violation in `RTSSHooks.dll` in the Windows application log, later with no crash entry at all, because the game bails out through its own error path first. It happens with any `dinput.dll`, including none, so it is easy to mistake for a problem with this shim.

Either start the game first and RTSS afterwards, which works because injecting into a running process does not disturb it, or give `i82stubz.exe` a profile in RTSS with *Application detection level* set to **None**. Other overlays that inject at process start are worth ruling out the same way. When a startup failure makes no sense, check the Windows application log for a foreign module before suspecting anything else.

**The video options only offer 4:3 modes.**
`SupportedResolutions` is missing from the DDrawCompat profile, or the profile is not named after the executable (`DDrawCompat-i82stubz.ini`).

**Crash to the desktop when leaving or restarting a mission.**
Fixed in v1.3.2: the included `DDrawCompat-i82stubz.ini` now sets `GdiInterops = none`, so update that file. While the game rebuilds its display surfaces, which happens on mission exit and restart, DDrawCompat presents from a copy of the whole virtual desktop. On a large or multi-monitor desktop that copy is big enough that allocating it can fail inside the 32-bit game, and the game then crashes in the graphics driver. The game does not appear to need GDI interop, so it can do without that copy. DDrawCompat's author tracked this down in [narzoul/DDrawCompat#625](https://github.com/narzoul/DDrawCompat/issues/625). In testing on a two-display setup there were no crashes in 36 mission starts and restarts with the setting. Without it, about one in five crashed. On a single display the copy is smaller and the crash is probably rarer, and the setting should not hurt there. Alt+Tab goes through the same path, but was not tested separately with this setting. If Alt+Tab still crashes, see the next entry.

**Crash on Alt+Tab, or cars briefly vanish after you Alt+Tab back into the game.**
Add this line to `DDrawCompat-i82stubz.ini`:

```ini
AltTabFix = keepvidmem(1)
```

It stops the game from losing its video memory when it goes to the background, so it has nothing to rebuild when it returns. Without `GdiInterops = none`, it removed the Alt+Tab crash in testing, which otherwise came within three to five Alt+Tabs in a mission. It is not in the included profile because it has only been tested on one machine.

**"Display Error: Interstate 82 is unable to set your chosen screen resolution" after "Restart Mission", followed by "World Init failed" and a loop of error messages.**
Fixed in v1.3.1; update `dinput.dll`. "Restart Mission" unloads the game's mission module and loads it again, usually at the same address, and earlier versions sometimes missed that and left the fresh copy at the game's original four resolutions. If you get stuck in the loop on an older version, the dialogs sit behind the fullscreen window: press Ctrl+Alt+Del and end `i82stubz.exe` in Task Manager, or sign out if Task Manager opens behind the game as well.

## Credits

- [DDrawCompat](https://github.com/narzoul/DDrawCompat) by narzoul provides the DirectDraw/Direct3D 1-7 compatibility layer. Its DLL is not included here.
- Interstate '82 and its assets remain the property of their respective copyright holders. This repository contains no game assets or original game binaries.

## License

The shim source and configuration in this repository are available under the [MIT License](LICENSE).
