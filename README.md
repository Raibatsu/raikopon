<h1 align="center">Dekopon</h1>

<p align="center">
  <img src="docs/dekopon_square.webp" alt="Dekopon logo" width="220">
</p>

<p align="center"><sub>Logo by <a href="https://stickymangorice.com/">StickyMangoRice</a></sub></p>

<b>Dekopon</b> is an open-source 3DS emulator for the Nintendo Switch based on Azahar.

Many many thanks to the Azahar team for creating such an amazing project in the first place.

# Project status

Currently, the project boots and runs games at mostly full speed (see the compatibility list for details: https://cryptpad.fr/sheet/#/2/sheet/view/PJKtoq0haezswSwH8qgEJkp7NWO57qNNL7cq04JsJAM/)\
\
Other features include:
- Full gyro support
- CIA installation support
- Switch software (and hardware) keyboard support
- Multiple screen layouts via R3 (Press the right stick)
- Virtual touch input via L3
- Full button remapping support
- In-game menu accessible via '+' and '-'
- System language/region toggle
- And other things I'm probably forgetting.

Features currently in the pipeline are:
- Mod support
- Arctic Base support
- Cheats support

Feel free to request more features, but do note they may or may not be implemented due to either feasibility or time.

# Installation

Installation is as simple as downloading the release nro from the [releases](https://github.com/PalindromicBreadLoaf/dekopon/releases) page
and copying it to your SD card in your standard homebrew location (probably /switch).

Your legally acquired ROMs go in `/switch/dekopon/roms/`
(This can be changed in settings)

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
NXVK and switch-mesa cannot be included simultaneously, so renderer must be chosen at build time.
Vulkan is highly recommended and I have yet to encounter an issues regarding it.
Hopefully in the future NXVK will also include OpenGL drivers of some sort to resolve this issue.

## 1. Clone the repository
```shell
git clone --recursive https://github.com/PalindromicBreadLoaf/dekopon.git
cd dekopon
```

## 2. Build NXVK
NXVK has it's own build documentation that lives in the [NXVK repository](https://github.com/PalindromicBreadLoaf/nxvk).
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

The output nro should be located in build/switch/src/citra_switch/dekopon.nro

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
