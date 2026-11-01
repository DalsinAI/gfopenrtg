# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created GFOpenRTG, designs it and maintains it.

## The AmiCradle team

We are The AmiCradle Team, who build AmiCradle alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, who started the work on SacredTrees's x86 cores.
- **The threads**, each one taking a piece of the work from design to release.

## Copyright holder

Original GFOpenRTG code, GFOpenGPU, tools, measurements and documentation are
Copyright (c) 2026 AmiCradle Team, released under the MIT licence (`LICENSE`).

Everything in this repository is our own work.

## Work we learned from

These shaped our code without any of their code being copied in:

- **Picasso96**: GFOpenRTG's display database records, monitors and modes are laid out as Picasso96's are, measured on OS 3.2.3 (`library/displaydb.c`, `library/modes.c`).
- **CyberGraphX**: `compat/cybergraphics_lib.c` implements its published interface (its function table, attributes, tags and pixel formats) over `gfopenrtg.library`.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
