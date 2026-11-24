# M7 part 2: recording draws, and the bitmap lock

Design note, Team 2, 24 November 2026. Backlog GF-66, item 5 of the graphics list. Read from main at 55769a4
(`library/warp3d/w3d_batch.c`, `w3d_vg.c`, `w3d_ctx.c`).

**Decided by Dale, 24 November 2026: A1, B yes, C2.** The code landed with this note; "As built" at the end says
where it differs from the proposal.

## In one paragraph

Today Warp3D writes every draw down twice on the graphics card route: once as a recorded batch, then a second
pass reads the batch back and turns it into what the host draws. Part 2 removes the second pass: each Warp3D draw
call goes straight to the shared pipe. That is less work for the 68k on every frame. Two things leaned on the
recorded batch and need a decision: what happens when the host fails, and when the screen's bitmap is locked.

## 1. Recording draws

- **Now:** a draw call adds stream3d commands to the context's batch (`w3d_emit`, `send_render3d`, `send_texture`).
  At a flush the virgl route reads the batch back (`w3dvg_run`: `triangles`, `fill`, `clear3d`, about half of
  `w3d_vg.c`) and makes pipe calls from it.
- **Proposed:** the draw calls make the pipe calls themselves. On a graphics card context nothing is written as
  stream3d. On a 68k or GFOpenGPU context the pipe's stream3d builder (part 1) writes the same words as today,
  checked word for word by part 1's test. The batch reader is deleted; what stays of `w3d_vg.c` is "Warp3D state
  to pipe state".
- **A drawing area the host can't take** (a `W3D_Bitmap` in a format the host isn't given, an area that isn't
  direct): known at `W3D_SetDrawRegion`, not at the flush. Warp3D keeps one pipe context per kind it has needed
  (graphics card, stream3d) and draws through the one that fits the current area. The pipe's rule holds: a pipe
  context's kind never changes.

**Decision A, host failure.** Today, when the host fails a batch, the same batch is drawn again by the 68k, so no
frame is lost. With no recorded batch there is nothing to draw again.

- A1 (recommended): the draws since the last send are lost for that one frame, a trace line says so, and the
  context carries on with the 68k from the next draw. A host that fails a block has almost always gone away.
- A2: keep recording every draw as well, only for this case. It costs the 68k the time part 2 is meant to save.

## 2. The bitmap lock

- **Now:** `flush()` locks the drawing area's bitmap (`LockBitMapTags`), translates the whole batch, runs it, and
  unlocks. The lock is held for the translation too.
- **Problem:** with direct calls there is no one place where "the batch runs". The pipe sends when it is full,
  which can be inside any draw call.
- **Proposed:** the lock is taken only around a send that touches the bitmap, and never held between the
  program's calls. The pipe calls a hook just before such a send (Warp3D locks and gives the address) and just
  after it (Warp3D unlocks). On the graphics card route that is the present hook M6 already uses, so only a send
  that presents takes the lock; a send that only draws on the host takes none. On the stream3d routes every send
  draws into the bitmap, so every send takes it, as now. The lock code stays in `w3d_batch.c` until M8 moves it
  into OpenRTG.
- **When the lock is refused** (the bitmap isn't visible): the graphics card route keeps the frame on the host
  and presents at the next sync point (today that batch is dropped); the stream3d routes drop the batch and
  answer `W3D_NOTVISIBLE`, as today.
- If part 1's table has no hook for the stream3d route's bitmap address, one member is added at the end of
  `pipe.h`'s table and the version goes up by one. No member changes.

**Decision B:** yes or no to the lock rule above (shorter locks; one visible change, the kept frame).

## 3. One thing that doesn't fit the list as written

`ENV:Warp3D/PipeOld=1` (the route of before M6, kept one release) is itself a batch reader: it needs the recorded
batch. The list says part 2 deletes the batch reading, and the old routes come out only in the release after.

**Decision C.**
- C1 (recommended): part 2's code lands after the release that ships PipeOld, together with item 6. The design
  note and the tests come now.
- C2: land part 2 now and keep recording the batch only when PipeOld=1. Two ways of drawing in the front end for
  one release.

## Gate for the code

`tests/pipe/test_s3d_same` word for word on the 68k and GFOpenGPU routes; `tests/pipe_m0.sh` as captured; the 68k
builds; on a lab copy of Instance-32 with FRAMES 300: W3DCheck 183/0, W3DTest not under 349 fps, MGLTest not under
517. New unit test: the lock hook is called exactly once around each send that touches the bitmap, and never held
when a draw call returns.

## As built

- **The calls.** `w3d_vg.h` has the batch as calls: `w3dvg_begin`, `w3dvg_area`, `w3dvg_depth`, `w3dvg_clip`,
  `w3dvg_render3d`, `w3dvg_texenv`, `w3dvg_texture`, `w3dvg_triangles`, `w3dvg_fill`, `w3dvg_clear3d`, `w3dvg_end`.
  `w3d_batch.c` makes them from Warp3D's draw calls when the context draws on the graphics card into an area the
  host can take (`x->live`). Only the vertices are kept, in the batch's data area; each run of draws PUTs the part
  it reads when the run ends.
- **The batch reader** (`w3dvg_run`) is now a short loop over the same calls. It stays for `ENV:Warp3D/PipeOld=1`
  (C2: the batch is recorded only then) and for the PC test, and goes with the old routes (item 6). So Warp3D's
  last `stream3d` line stays on the ownership debt list until then.
- **One pipe context a kind** was not needed: the kind is decided when a batch starts (`live_now`), and an area the
  host can't take is drawn by the 68k from a recorded batch, as before.
- **A1.** `w3dvg_end` answers -1 when a block didn't run; Warp3D writes the trace line, closes the route and the
  68k draws from the next draw on.
- **B.** No member was added to `pipe.h`'s table. The pipe's present hook already runs just before and just after
  the block that presents, so the lock is taken in its first call and given back in its second. The one other send
  that touches the bitmap is the load of the frame's first pixels (a frame that doesn't start by filling the
  area): the lock is taken, the block with the load goes at once, and the lock is given back before the draw call
  returns. A refused lock at a load leaves the draws over the old frame out (the area isn't visible); at a present
  the frame stays on the host until the next sync, as proposed.
- **Tests.** `tests/pipe/test_w3d_direct.sh`: `tests/test_w3d_vg.c` run once through the reader and once through
  the calls (`W3D_DIRECT=1`), the pictures the same files; the lock counted (one for each load and present, each
  given back, never held when a call returns, never taken twice), and a frame drawn with the area refused, then
  shown by one present.
- **Found by that test.** The 15 fixed scenes are the same files both ways. Of 21 random scenes, three (seeds 5, 8
  and 80) are nearer the CPU's picture through the calls than read back, none further. Seed 80 shows why: a 16-bit
  area at an odd address with padded rows, random Z and stencil under it, one strip. Read back, the load of the
  area shares a block with the draws and 22 pixels the CPU draws are left as loaded; with the load sent in a block
  of its own, as the lock requires, the picture is the CPU's. The reader is left as it was (it is the old route);
  the test asks that no scene is further from the CPU with the calls, and lists the nearer ones.
