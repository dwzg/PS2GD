# Third-party notices

Pulse Dash's own code and content (game code, levels, music and sound
effects, graphics, font, tools) are original work under the MIT license in
[LICENSE](LICENSE). The builds also contain or use the following software,
under its own license.

## In the PlayStation 2 build (`pulsedash.elf`)

| Component | Used for | License |
|-----------|----------|---------|
| [ps2sdk](https://github.com/ps2dev/ps2sdk) | EE kernel and SIF libraries, libpad, libmc, the audsrv client, libpatches, and the IOP modules `libsd`, `audsrv`, `sio2man`, `padman`, `mcman`, `mcserv` (compiled into the ELF) | Academic Free License 2.0 |
| [gsKit and dmaKit](https://github.com/ps2dev/gsKit) | Graphics Synthesizer and DMA access | Academic Free License 2.0 |
| newlib (part of the [ps2dev toolchain](https://github.com/ps2dev/ps2toolchain)) | C library | BSD-style licenses (see the newlib sources) |

The game uses no GPL code. The controller and memory card modules are
loaded from the console's BIOS when available.

## In the PC build

| Component | Used for | License |
|-----------|----------|---------|
| [SDL2](https://www.libsdl.org/) (dynamically linked) | window, input, audio | zlib license |

## Development tools only (not part of the game)

| Component | Used for | License |
|-----------|----------|---------|
| [Play!](https://github.com/jpd002/Play-) | emulator core in `tools/play-harness` (fetched at build time; the harness itself is adapted from Play!'s AutoTest, see [its LICENSE](tools/play-harness/LICENSE)) | BSD 2-Clause |

## Trademarks

Pulse Dash is an independent, unofficial fan project. It is not affiliated
with, endorsed or sponsored by RobTop Games or Sony Interactive
Entertainment. "Geometry Dash" is a trademark of RobTop Games;
"PlayStation" and "PS2" are trademarks of Sony Interactive Entertainment.
These names are used only to describe what the game is inspired by and
what it runs on. The MIT license covers this project's own work and grants
no rights to anyone's trademarks.
