<h1 align="center">Raika Azahar</h1>

<p align="center">
  <img src="docs/raikopon_icon.jpg" alt="Raika Azahar logo" width="220">
</p>

<p align="center"><sub>Logo by Noihs</sub></p>

<b>Raika Azahar</b> is an open-source 3DS emulator for the Nintendo Switch, built on top of Azahar.

**We are moving away from the Dekopon name.** This project started as a fork of Dekopon (itself
a Switch port of Azahar), but has since diverged enough with its own features, defaults, and
direction that it's continuing on as its own thing under the Raika Azahar name. None of this would exist
without the work that came before it, so many thanks to the Azahar team. Special thanks to both PalindromicBreadLoaf &
dantiicu for porting it to Switch in the first place.

# Project status

Currently, the project boots and runs games at mostly full speed (see the compatibility list for details: https://cryptpad.fr/sheet/#/2/sheet/view/PJKtoq0haezswSwH8qgEJkp7NWO57qNNL7cq04JsJAM/)\
\
Other features include:
- Full gyro support
- CIA installation support
- Switch software (and hardware) keyboard support
- Full async shader compilation.
- Most games running at full speed on stock speeds.
- Multiple screen layouts via R3 (Press the right stick)
- Virtual touch input via L3
- Full button remapping support
- In-game menu accessible via '+' and '-'
- Cheat, mod (LayeredFS), and texture-pack support
- System language/region toggle
- And other things I'm probably forgetting.

## What's new

- **The '+' exit prompt now actually exits.** Confirming "Exit Emulator" from the in-game menu
  used to just stop the game and drop you back at the ROM library. It now closes the app
  entirely, back to the Switch home menu/hbmenu.
- **New default folders.** The app's folder moved to `/switch/azahar/`, and the default ROMs
  folder is now `/switch/azahar/roms/`. If you're updating from an older build, you'll need to
  move your saves/config/ROMs over manually, or just re-point the folders in Settings > Paths.
- **More reliable ROM/CIA scanning.** Hidden dotfiles and dot-folders (the `.DS_Store`,
  `._Game.3ds`-style sidecar files macOS leaves behind when copying to a FAT32/exFAT SD card)
  could previously cause the ROM library, CIA install list, or folder browser to fail to read a
  directory. These are now skipped outright before anything tries to parse them.
- **Fewer confusing/half-finished debug toggles in Settings.** A couple of options that don't do
  much or aren't ready for general use yet ("Disable Pipeline Fast Path", "GPU Frame Log") are
  hidden from the Settings UI for now rather than left in place to confuse people.
- **Better out-of-the-box renderer defaults**, tuned from testing: Ubershaders off, Async Shader
  Compilation on, Disable Right Eye Render on, Compile Boost off by default. All of these remain
  fully adjustable in Settings.

Features currently in the pipeline are:
- Remove all Dekopon codebase and rewrite from scratch (most of it is done, much is still remaining)

Feel free to request more features, but do note they may or may not be implemented due to either feasibility or time.

# Installation

Installation is as simple as downloading the release nro from the [releases](https://github.com/Raibatsu/raikopon/releases) page
and copying it to your SD card in your standard homebrew location (probably /switch).

Your legally acquired ROMs go in `/switch/azahar/roms/`
(This can be changed in settings)

> **Updating from Dekopon or an older Raika Azahar build?** The default folder moved from
> `/switch/dekopon/` to `/switch/azahar/`. Move your existing folder over (or point Settings >
> Paths at your old one) so you don't lose your saves, cheats, and config.

# Cheats, mods and texture packs

All three use the same folder layout as desktop Azahar/Citra, rooted at the Raika Azahar directory on
your SD card (`/switch/azahar/` by default). In every path below, `<TITLE_ID>` is the game's Title ID
in uppercase (e.g. `00040000000EC800`). If you have changed the default path, please reflect that in any of the directories below.

## Cheats

- Put a cheat file at `/switch/azahar/cheats/<TITLE_ID>.txt`. The format is the standard Gateway / Action Replay format.
- You can also use the in-game menu to write/modify cheats manually.
- Open the in-game menu (`+` and `-`) to switch individual cheats on and off while the game runs.
  Your choices are written back to the cheat file, so they persist across launches.

## Mods

Place mod files under `/switch/azahar/load/mods/<TITLE_ID>/`:

Mods are applied when the game boots.

Currently, selecting a mod from a list is not supported. Be sure that the folders under titleID are
`romfs` `exefs` and/or `exheader.bin`

## Texture packs

- Loading: drop a pack in `/switch/azahar/load/textures/<TITLE_ID>/`, then enable
  **Custom Textures** in the in-game menu (`+` and `-`). This toggle applies immediately and is
  remembered. For large packs, `preload_textures = true` loads the whole pack at boot to avoid 
  in-game hitching, at the cost of more memory. This may run you out of RAM depending on the texture pack size.
  It's best to use no more than 1080p textures since you'll run out of RAM fast using 4K textures for basically zero visual gain.
  Also, note that you may run into a crash trying to use too many custom textures and higher resolutions than 1x. There are
  some safeguards in place to prevent crashes, but it may still happen.
- Dumping): set `dump_textures = true` in the `[Utility]` config section. Textures
  the game uses are written to `/switch/azahar/dump/textures/<TITLE_ID>/`. This setting takes
  effect on the next launch. (You should also really just do this on PC. Performance will be degraded
  using this option.)

# Build instructions
## Required packages
The current build requires DevkitPro. Please install from here [DevkitPro Install](https://devkitpro.org/wiki/Getting_Started)
### DevkitPro Packages
- switch-dev
- switch-freetype
- switch-bzip2
- switch-libpng
- switch-zlib
- switch-mesa *(only for the legacy GLES backend\*)*
### System Packages
- cmake
- git

\*The default GPU backend is Vulkan via [NXVK](https://github.com/PalindromicBreadLoaf/nxvk)
and switch-mesa cannot be included simultaneously, so renderer must be chosen at build time.
Vulkan is highly recommended and I have yet to encounter an issues regarding it.
Hopefully in the future NXVK will also include OpenGL drivers of some sort to resolve this issue.

## 1. Clone the repository
```shell
git clone --recursive https://github.com/Raibatsu/raikopon.git
cd raikopon
```

## 2. Build NXVK
NXVK has it's own build documentation that lives in the [NXVK repository](https://github.com/raibatsu/nxvk-port).
Please follow that and create a libnvk.a file. Once you've done that you can return here.

Configuring looks for `switch/build/cross/src/nouveau/vulkan/libnvk.a` under `externals/nxvk`.
If you built nxvk in-place, it should be found automatically.

## 3. Configure
```shell
cmake -S . -B build/switch \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

## 4. Build
```shell
cmake --build build/switch --target citra_switch_nro -j$(nproc)
```

The output nro should be located in build/switch/src/citra_switch/raikopon.nro

# How can I contribute?

### Pull requests

If you want to implement a change and have the technical capability to do so, we would be happy to accept your contributions.

If you are contributing a new feature, it is highly suggested that you first make a Feature Request issue to discuss the addition before writing any code. This is to ensure that your time isn't wasted working on a feature which isn't deemed appropriate for the project.

After creating a pull request, please don't repeatedly merge `master` into your branch. A maintainer will update the branch for you if/ when it is appropriate to do so.

### Compatibility reports

Do not ever contact the upstream Azahar project about any issues regarding this port. 
They have no relation to this project whatsoever and do not wish to deal with random issues regarding it.
Please only create issues in this repository regarding bugs found here unless they are directly applicable upstream and also happen 
the exact same as here.

To do so, simply read https://github.com/azahar-emu/compatibility-list/blob/master/CONTRIBUTING.md and follow the instructions.

Contributing compatibility data helps more accurately reflect the current capabilities of the emulator, so it would be highly appreciated if you could go through the reporting process after completing/playing a game.
