/* Warp3D's shader keys for tests/pipe/test_ff.c: its own header, with the host build's switch. */
#ifndef W3D_HOST
#define W3D_HOST
#endif
#include "../../library/warp3d/w3d_vg.h"
#include "../../library/warp3d/w3d_vg_ffkey.h"

/* the reference generator (tests/pipe/ref/w3d_vg_shader.c): a text's length with the closing 0 */
ULONG w3dvg_vs_text(ULONG key, char *buf, ULONG cap);
ULONG w3dvg_fs_text(ULONG key, char *buf, ULONG cap);
