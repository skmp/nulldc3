# nullDC icon

The nullDC snail redrawn in the style of the Dreamcast DevTools icon: its shell is the DevTools icon's orange GD-ROM
(same gradient, hub, track ring and highlight), the body and antennae are the UI's blue, on the same tile.

- `nulldc.svg`: the icon, for 32 px and larger.
- `nulldc-small.svg`: a pixel-aligned simplification for 16 and 24 px (disc and hole only, thicker antennae).
- `png/`: pre-rendered with transparent corners: 16 and 24 px from the small SVG; 32 to 1024 px from the full one.
- `preview.png`: 16 to 256 px on a dark, a mid-grey and a light background, next to the DevTools icon.

## The files

| file | what it is |
|---|---|
| `nulldc.svg` | the icon as drawn, viewBox 256: a tile with rounded corners (`rect x=4 y=4 w=248 rx=56`), the snail's body and antennae, the GD-ROM shell |
| `nulldc-small.svg` | the same at viewBox 16, aligned to the pixel grid: the disc and its hole only, thicker antennae |
| `png/nulldc-16.png`, `png/nulldc-24.png` | `nulldc-small.svg` at 16 and 24 px |
| `png/nulldc-{32,48,64,128,180,192,256,512,1024}.png` | `nulldc.svg` at those sizes (180 and 192 are the web's sizes; no file of the emulator uses them) |
| `preview.png` | the sizes 16 to 256 on three backgrounds, next to the DevTools icon: for looking at, used by nothing |
| `make_icons.py` | makes the emulator's icon files from the PNGs (below) |

The PNGs are 8-bit RGBA, not interlaced, with only IHDR, IDAT and IEND chunks: no time stamp, text or colour
profile, so their bytes depend on their pixels alone and say nothing about the machine that made them. The tile fills
the canvas; its corners are transparent (at 16 px the antialiasing reaches the corner pixels: an alpha of 1 at the top
two, 12 at the bottom two).

## How the PNGs were made

They were rendered from the SVGs with Playwright's headless Chromium, as the Dreamcast DevTools UI renders its own icon
(its `scripts/build-icon.ts`): each SVG as an image of exactly n x n CSS pixels at a device scale factor of 1 on a
transparent page, captured with the background left out. This repository has no render script and needs no browser:
the PNGs are the approved art and are committed as such. A change of the art is rendered outside the tree in the same
way, committed here, and the files below are made again from it.

## What is made from them

`make_icons.py` reads the PNGs and writes three files, which are committed:

| file | what | used by |
|---|---|---|
| `nulldc3/windows/nulldc3.ico` | the Windows icon: seven entries, 16, 24, 32, 48, 64, 128 and 256 px, each the PNG verbatim (32 bits; `bWidth`/`bHeight` 0 for 256) | the resource script `nulldc3/windows/nulldc3.rc` |
| `nulldc3/apple/nulldc3.icns` | the macOS icon: `icp4` (16), `icp5` (32), `ic11` (16@2x), `ic12` (32@2x), `ic07` (128), `ic13` (128@2x), `ic08` (256), `ic14` (256@2x), `ic09` (512), `ic10` (512@2x), each the PNG verbatim; no `TOC ` | the macOS bundle |
| `libswirl/linux-dist/x11_icon.h` | the X11 window's `_NET_WM_ICON`: for 16, 24, 32, 48, 64 and 128 px the width, the height and the pixels row by row, each `A << 24 \| R << 16 \| G << 8 \| B` as the PNG stores it (not premultiplied), as `uint32_t`, 24,652 values, with the size table, a `static_assert` on the length and the 64-bit FNV-1a of the values | `libswirl/linux-dist/x11_icon.cpp`, whose `x11_icon_cardinals()` widens the values to the C `long`s Xlib takes; `x11.cpp` sets them and the class `nulldc3` (`WM_CLASS`, which `StartupWMClass` of `nulldc3/linux/nulldc3.desktop` names) on the window before it is mapped |

The script renders nothing and compresses nothing, so its outputs depend on the bytes of the PNGs alone: the `.ico` and
the `.icns` hold the PNG files as they are, and the header has a fixed layout (8 values a line, lower-case hex, LF)
and names only paths inside the tree. It refuses an input that is not what it expects (a file missing, no PNG
signature, a bad chunk CRC, a chunk other than IHDR, IDAT and IEND, a size that is not the one in the name, another bit
depth or colour type than 8-bit RGBA, interlacing, image data that does not inflate to exactly the rows of the image,
a row filter other than the five of PNG), and names the file. It reads and writes every file in binary mode, so a
system that ends text lines with CRLF changes nothing; `.gitattributes` (`* -text`) checks the header out with LF
whatever `core.autocrlf` says.

The 256 px icon is not in the X11 property: with it the property would no longer fit in one core X11 request.

## Running it

Python 3.9 or later with the standard library; it finds its files relative to itself, so it runs from any directory:

    python3 nulldc3/icon/make_icons.py            # writes the three files from the PNGs
    python3 nulldc3/icon/make_icons.py --check    # writes nothing: rebuilds the three in memory and compares them
                                                  # byte for byte with the committed ones, re-parses the committed
                                                  # .ico and .icns (count, sizes, offsets, lengths, a PNG at every
                                                  # offset); exits 1 naming each file that differs or is broken

The tests project runs `--check` as `icons.generated`, and checks what `x11_icon_cardinals()` gives the window as
`icons.x11` (`tests/README.md`). After a change of the PNGs, run the script and commit its three files with them.
Nothing else in the tree keeps a copy of the PNGs: what needs them when it is packaged (the AppImage's icons) copies
them from `png/`.
