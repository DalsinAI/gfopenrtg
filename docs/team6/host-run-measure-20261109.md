# Host-run measure, 9 Nov 2026

Team 6. Task 1 of the host-run libraries list. Not a lab result.

This machine cannot reach daletop, so AC_PROFILE was not run. There are no timings here, and none were invented. Task 7 (SDL2.module blits as hostlib functions) stays blocked on this.

What the lab run is, when a copy is free:

```
AC_PROFILE=1 AC_PROFILE_BYTES=1
```

Four workloads, the costliest calls first in the report that replaces this file:

1. A zip unpack in GFOpenCompress.
2. A large PNG through datatypes.
3. A game-sized data load.
4. SDL2 blits in a demo.

The report names the guest block, its share of the emulation thread, and whether it is glue, a copy, or pixels. Only calls that show up there get a hostlib id.
