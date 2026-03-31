# FlipPar

FlipPar is a Flipper Zero external app for tracking golf or disc golf rounds on-device. It lets you set the number of holes and players, rename players, record par and score values hole-by-hole, and export a plain-text score sheet to the SD card.

## Features

- Supports 1 to 27 holes
- Supports 1 to 4 players
- Editable player names
- Per-hole par tracking
- Per-player hole scores
- Running total view with leader summary relative to par
- Plain-text score-sheet export to the SD card

## Controls

### Setup screen

- `Up` / `Down`: move between setup fields
- `Left` / `Right`: change holes, players, or selected player name slot
- `OK` on `Name`: open the text editor for that player
- `OK` on `Start Round`: begin score entry
- `OK` on `Save Score Sheet`: export the current round

### Score grid

- `Left` / `Right`: move between holes
- `Up` / `Down`: move between par row and player rows
- Short `OK`: increase the selected par or score value
- Long `OK`: decrease the selected par or score value
- `Back`: return to setup

## Save Location

Exported score sheets are written to:

`/ext/apps_data/flippar`

Files are created with a date-based name such as:

`FlipPar_2026-3-31.txt`

If a file for that date already exists, FlipPar appends a numeric suffix.

## Project Layout

- `flippar.c` - main application source
- `application.fam` - Flipper Zero app manifest
- `icon.png` - app icon
- `assets/` - bundled image assets, including the splash screen

## Building

This repository contains a standard Flipper Zero external app layout:

- `application.fam` defines the app metadata and entry point
- `flippar.c` contains the app implementation

To build it, place this project in your Flipper Zero firmware external-apps workflow and compile it with your preferred Flipper build toolchain. If you already build external `.fap` apps, this project is ready to drop into that process as-is.

## Installing

After building:

1. Copy the generated `.fap` to your Flipper Zero SD card.
2. Launch `FlipPar` from the Apps menu.

## Notes

- Default round setup is 18 holes and 2 players.
- Default player names are `P1` through `P4`.
- Par and score values are clamped between `0` and `15`.

## Author

Created by ConsultingJoe.
