# KlangKunst

Part instrument, part sound toy, part architectural sandbox.

![](https://github.com/maetyu-d/klangkunst/blob/main/Screenshot%202026-03-04%20at%2021.50.30.png?raw=true)

## Build

```bash
cmake -S . -B build -DJUCE_DIR=/absolute/path/to/JUCE
cmake --build build -j
```

## Modes

- `Title`: neon-psychedelic animated vector title screen with keyboard menu.
- `Build`: isometric 16x16 world editor with two local players and immediate pitch audition.
- `Performance`: centered top-down snake performance view with persistent tile tools and transport-synced triggering.

## Title Controls

- `Up/Down`: menu select
- `Enter`: confirm

Menu:
- `Resume` (when a session exists)
- `Load Saved File`
- `Start Demo Song`
- `Start Blank World`

## Build Controls

- `P1`: `W A S D` move, `Q/E` layer down/up, `R` place stack, `F` remove down to marker layer
- `P2`: `Arrows` move, `[ ]` layer down/up, `/` place stack, `.` remove down to marker layer
- `Z/X`: rotate isometric view
- `Enter`: switch to Performance mode
- `- / =`: BPM down/up
- `K/L`: key root down/up
- `G`: cycle scale
- `T`: quantize to scale on/off
- `N`: cycle synth engine
- `M`: cycle melodic/chord/arpeggio logic
- `O`: save world JSON
- `U`: back to title

## Performance Controls

- `P1`: `W A S D` marker move
- `P2`: `Arrows` marker move
- `Tab`: cycle selected tool (`Redirect`, `Speed`, `Ratchet`, `Key`, `Scale`, `Section`)
- `R` or `/`: place/cycle selected tool at P1/P2 marker
- `F` or `.`: rotate tool at P1/P2 marker
- `M`: cycle play logic
- `N`: cycle synth engine
- `K/L`: key root down/up
- `G`: cycle scale
- `- / =`: BPM down/up
- `Esc`: back to Build mode
- `U`: back to title

## Save Format

`*.klangkunst.json` stores:
- block heights
- tool grid (`type`, `rotation`, `state`)
- bpm, key root, scale

Default save location:
- `/Users/<you>/Documents/KlangKunstWorld.klangkunst.json`
