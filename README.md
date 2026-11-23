# StrikeTrilogy-X360

*English | [Português](LEIAME.md)*

Desert Strike: Return to the Gulf (1992), Jungle Strike (1993) and Urban Strike (1994), the Sega Genesis versions, running **natively** on a modded Xbox 360: one `.xex` per game, 60 fps, 1280x720 with a wider 16:9 view, a live minimap and save states.

It isn't an emulator running the game one instruction at a time. It starts from [StrikeHDCollection](https://github.com/Icedomega13/StrikeHDCollection) by Icedomega13, which translates each game's own 68000 code to C (checked instruction for instruction against the Musashi 68000 core), and I ported it to the Xbox 360: video on the console's GPU (D3D9), XInput, XAudio2, its files, threads for picture and sound. The translation is made on your PC from **your** ROMs and compiled into the `.xex`. The sound chips (Z80, YM2612, PSG) and the video chip are still emulated around it.

> **There is no game in here.** No ROM, no translated code, no prebuilt `.xex`, and no Microsoft SDK. You need your own ROMs and the tools below to build it. Please don't ask me for ROMs.

---

## What works

- The three complete games, each started directly by its own `.xex`
- 60 fps, one game frame per TV refresh (tested on the console)
- 1280x720, 16:9 with the wider battlefield (menus, maps and cutscenes stay 4:3), or the original 4:3
- Live minimap of the pause map with the objectives; LB / RB step through them
- Save states (10 slots), game speed Smooth or Original (the console's slowdown), sharp or smooth picture, settings saved per game
- Picture drawn on 4 hardware threads, FM sound on its own core

On PC the port's code gives the same game state as the Musashi reference frame by frame, and the same sound sample by sample.

## What you need

### On the console
- A modded Xbox 360 (RGH/JTAG) with Aurora, FreeStyle Dash or XeXMenu

### On the PC (Windows 10/11)
1. **WSL** with Ubuntu. The Xbox build runs there.
2. **[OXDK](https://github.com/MrMilenko/OXDK)** in WSL (`~/OXDK`), with its LLVM (`~/oxdk-llvm`) built with the Xenon patches from `OXDK/xbox360/llvm`, plus `Tools/patches/llvm-lld-thunk-r0.patch` (a long-branch thunk for lld that keeps r12 free).
3. **Xbox 360 SDK (XDK)** files where OXDK looks for them (`~/xdk360/XDK`).
4. **Python 3** with `capstone` and `pillow` (`pip install capstone pillow`), for the base's translator.
5. **Git**.
6. Optional: **Visual Studio 2022 Build Tools** with C++ and its clang, only for the PC checks.

### The ROMs
The Genesis USA/Europe dumps, checked by SHA-1:

| Game | File name | SHA-1 |
|---|---|---|
| Desert Strike: Return to the Gulf | `desert_strike.bin` | `d7e7d8c358eb845b84fb08f904cc0b95d0a4053d` |
| Jungle Strike | `jungle_strike.bin` | `7a7568e39341b1bb218280ee05c2b37c273317b5` |
| Urban Strike | `urban_strike.bin` | `897a8f6a08d6d7d6e5316f8047532b3e4603e705` |

The translated code works by address, so other versions (or the SNES games) won't do. The `.xex` checks the SHA-1 and tells you if it's wrong.

## Building

**1. Download this project and the base:**

```bash
git clone https://github.com/EduDicaseGameplay/StrikeTrilogy-X360.git
cd StrikeTrilogy-X360
git clone https://github.com/Icedomega13/StrikeHDCollection.git Reference/StrikeHDCollection
git -C Reference/StrikeHDCollection checkout 9c0be81
```

The port was made on that commit of the base (v1.0.0).

**2. Translate the games.** Put your ROMs in `Reference\StrikeHDCollection\rom\` with the file names above, then:

```
Tools\gen_all.bat
```

**3. Build the three `.xex`** in WSL:

```bash
bash Xbox360/Build/build.sh
```

`make` doesn't take spaces in paths, so the script reaches the project through a link, `~/strike-trilogy`. You can also build one game: `bash Xbox360/Build/build.sh jungle`.

The results are `Builds/DesertStrike/DesertStrike.xex`, `Builds/JungleStrike/JungleStrike.xex` and `Builds/UrbanStrike/UrbanStrike.xex`, each also copied as `Builds/<Game>/Deploy/<Game>/default.xex`.

**Optional PC checks** (Visual Studio 2022 Build Tools): `Tools\pcbuild.bat` builds the base's lockstep test programs, and `Tools\pc_check\build_pc_check.bat` builds a tool that runs a game with this port's sound and picture code and writes the state log, the audio and screenshots to compare with the base.

## On the console

Make one folder per game, for example:

```
Hdd1:\Games\DesertStrike\
    default.xex
    rom\desert_strike.bin
```

and the same for `JungleStrike` (`rom\jungle_strike.bin`) and `UrbanStrike` (`rom\urban_strike.bin`). The ROM can also sit next to `default.xex` or in `roms\`. Add the folder to Aurora's scan paths, or open `default.xex` from XeXMenu.

### Controls

| Xbox 360 | Genesis |
|---|---|
| D-pad / left stick | D-pad |
| X or LT | A (Hellfire; held: strafe) |
| A | B (Hydras) |
| B or RT | C (chain gun) |
| Start | Start (pause / map) |
| Y (hold) or right stick left/right | strafe without firing; X while strafing fires a Hellfire |
| LB / RB | previous / next objective on the minimap |

With **Back** held: LB save state, RB load state, D-pad left/right state slot, Y game speed, X sharp/smooth picture, A aspect ratio, B sprite limit (console flicker on/off), D-pad up widescreen, D-pad down minimap, LT + RT for 2 seconds reset. **Back + Start for 3 seconds** quits.

Next to the `.xex` the game writes `<game>.cfg` (settings), `states\`, `minimap\` (each level's map, captured once) and a log, `desertstrike_log.txt` (and so on), that says where it stopped if something goes wrong.

## Credits

- [StrikeHDCollection](https://github.com/Icedomega13/StrikeHDCollection) by Icedomega13 and contributors (MIT): the translator, the hardware model and the game modules this port is built on
- [OXDK](https://github.com/MrMilenko/OXDK) by MrMilenko (public domain): the toolchain
- Musashi, z80 by Nicolas Allemand (superzazu) and ymfm by Aaron Giles, through the base
- The Xbox 360 platform layer comes from my earlier Mega Drive ports (SoR1/SoR2/SoR3-X360)

See `THIRD_PARTY.txt` and `LICENSE`. Desert Strike, Jungle Strike and Urban Strike are trademarks of Electronic Arts. This is an unofficial fan project, not affiliated with EA or Sega.
