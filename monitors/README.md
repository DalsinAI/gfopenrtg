# The chipset's monitors: AGA and ECS

The chipset is treated as a graphics card, so it has monitors like one.
`DEVS:Monitors/AGA` and `DEVS:Monitors/ECS` hold the modes those chipsets offer.
LoadMonDrvs runs them with the others.

**ModeIDs don't change.** Programs hard-code them, and their top half names the
timing. Each native mode's name says its chipset: `AGA PAL:High Res`.

| | What the monitor does |
|---|---|
| AGA, on an AGA machine | Adds the 31 kHz timings, names every native mode |
| ECS, on an ECS machine | Adds Multiscan, names every native mode |
| ECS, on an AGA machine | A filtered view: leaves DblNTSC and DblPAL out, and each mode's largest depth is ECS's |
| ECS, with AGA running | Nothing |
| On a chipset that can't show them | Nothing |

**How the timings come in** depends on the `graphics.library`:

- **GFOpenGfx (47.100 and up):** it holds Multiscan, DblNTSC and DblPAL built in,
  and they appear while a MonitorSpec of the right name is on the monitor list. The
  monitor adds that, with the values OS 3.2.3's drivers have. No driver file is
  needed. This matters, because OS 3.2's own drivers hang on GFOpenGfx: its private
  `AddDisplayInfo` calls are stubs. Don't put them in `DEVS:Monitors` on that ROM.
- **OS 3.2's own (47.10):** the machine's drivers in `SYS:Storage/Monitors` are
  run, so Euro36, Euro72 and Super72 come too. GFOpenGfx has no records for those.

| Lab, 8 November 2026 | Result |
|---|---|
| GFOpenGfx 47.102, AA+, AGA monitor | Boots. 138 modes become 194. Screens opened in DblPAL, Multiscan and PAL |
| OS 3.2.3 47.10, A1200 AGA, AGA monitor | Boots. 431 modes. A DblPAL screen opened |
| GFOpenGfx, ECS monitor | 162 modes, DblNTSC and DblPAL gone, depths 6, 4 and 2 |
| GFOpenGfx, ECS with AGA running | Does nothing |

**Known:**

- On OS 3.2's own `graphics.library`, once all the drivers have run, the default
  monitor's IDs (`$00008000` and so on) list as DblPAL's. Those are the drivers' own
  doing, but it's a change from a machine with only PAL.
- The 31 kHz beam registers are written as a driver writes them, but AC090's
  display doesn't model them, so the picture's timing is unproven on real hardware.

`ListModes` and `ModeScreen ID [SECONDS] [DEPTH]` are the tools used to check them.
