# uaegfx backend: qualification (11 November 2026)

gfopenrtg.library 0.26 and DEVS:Monitors/GFOpenRTG 0.9 on GFOS (the
PiMiga's GFOpenKick ROM pair and GFOpenUp DH0/DH1, copied), with
Startup-Sequence and screenmode.prefs as in docs/design/UAEGFX-20261111.md
section 9.4.

- `daletop-amiberry85-x86-*`: Amiberry 8.5.0 x86_64 AppImage, GFOS.uae
  with the PiMiga's paths, uaegfx Z3 16 MiB, RTG hardware sprite on, JIT
  off (Amiberry's x86 JIT stops this ROM before DOS, with or without this
  change), under a private Xvfb at 1920 x 1080. The screenshot is the
  host window: OpenBench on the RTG board at 1920 x 1080 32-bit, scaled to
  half. The pointer is the host's cursor (the RTG hardware sprite), which
  the X screenshot does not capture.
