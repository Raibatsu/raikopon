# Dekopon

<b>Dekopon</b> is an open-source 3DS emulator for the Nintendo Switch based on Azahar.

Many many thanks to the Azahar team for creating such an amazing project in the first place.

# Installation

Installation is as simple as downloading the release nro from the [releases](https://github.com/PalindromicBreadLoaf/dekopon/releases) page
and copying it to your SD card in your standard homebrew location (probably /switch).

Your legally acquired ROMs go in /switch/dekopon/roms/

# Build instructions
## Required packages
The current build requires DevkitPro. Please install from here [DevkitPro Install](https://devkitpro.org/wiki/Getting_Started)
### DevkitPro Packages
- switch-dev
- switch-mesa
- switch-bzip2
- switch-libpng
- switch-zlib
### System Packages
- cmake

From there, clone this repository, move into it, then run 
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
