/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's ABI against the clean-room spec: reads the table in
 * docs/design/MiniGL-Compatible-Spec.md section 8 itself (not through
 * gen_api.py) and checks that <libraries/minigl_dispatch.h>, as the compiler
 * lays it out (32-bit), has every entry at the spec's offset, in the spec's
 * order, and the spec's size; and the header fields and type sizes of
 * sections 4.1 and 6.
 *   test_minigl_abi Spec.md */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <mgl/gl.h>
#include <libraries/minigl_dispatch.h>
#include "mgl_spec_expect.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    FILE *f;
    char line[512];
    int in8 = 0, rows = 0, i;
    if (argc < 2 || !(f = fopen(argv[1], "r"))) { printf("test_minigl_abi: no spec\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        int n, off;
        char name[64];
        if (!strncmp(line, "## 8.", 5)) { in8 = 1; continue; }
        if (in8 && (!strncmp(line, "## ", 3) || !strncmp(line, "### ", 4))) break;
        if (!in8 || sscanf(line, "| %d | %d | `%63[A-Za-z0-9_]` |", &n, &off, name) != 3) continue;
        rows++;
        CHECK(n == rows, "row %d is entry %d", rows, n);
        CHECK(off == 20 + 4 * (n - 1), "entry %d: the spec's offset %d", n, off);
        CHECK(n <= TABLE_FIELDS && !strcmp(table_fields[n - 1].name, name), "entry %d is %s in the header (%s)", n, name,
              n <= TABLE_FIELDS ? table_fields[n - 1].name : "-");
        for (i = 0; i < TABLE_FIELDS; i++)
            if (!strcmp(table_fields[i].name, name)) {
                CHECK(table_fields[i].off == (unsigned long)off, "%s at %lu, the spec says %d", name, table_fields[i].off, off);
                break;
            }
        CHECK(i < TABLE_FIELDS, "%s is not in the table", name);
    }
    fclose(f);
    CHECK(rows == 205, "the spec lists %d entries", rows);
    CHECK(TABLE_FIELDS == rows, "the header has %d entries", TABLE_FIELDS);
    /* 4.1: the header and the size */
    CHECK(sizeof(MGLDispatchTable) == 840 && MGL_DISPATCH_SIZE == 840, "the table is %lu bytes", (unsigned long)sizeof(MGLDispatchTable));
    CHECK(MGL_DISPATCH_ABI == 5, "ABI 5");
    CHECK(offsetof(MGLDispatchTable, abi_version) == 0 && offsetof(MGLDispatchTable, table_size) == 4
          && offsetof(MGLDispatchTable, backend_flags) == 8 && offsetof(MGLDispatchTable, reserved) == 12
          && offsetof(MGLDispatchTable, current_context) == 16, "the five header longwords");
    CHECK(MGL_DISPATCH_SIZE_290 == 836, "29.0's size");
    CHECK((MGL_BACKEND_AMICRADLE & 7) == 0, "no published back-end bit");
    /* 6: types */
    CHECK(sizeof(GLboolean) == 4 && sizeof(GLenum) == 4 && sizeof(GLsizei) == 4 && sizeof(GLcontext) == 4, "4-byte types");
    CHECK(sizeof(MGLLockInfo) == 24 && offsetof(MGLLockInfo, address) == 16 && offsetof(MGLLockInfo, bytes_per_row) == 20,
          "MGLLockInfo");
    CHECK(sizeof(MGLScreenMode) == 96 && offsetof(MGLScreenMode, name) == 16, "MGLScreenMode");
    CHECK(MGL_KEY_F1 == 0 && MGL_KEY_F10 == 9 && MGL_KEY_UP == 10 && MGL_KEY_DOWN == 11 && MGL_KEY_LEFT == 12 && MGL_KEY_RIGHT == 13,
          "MGLspecial");
    /* 7: a few values */
    CHECK(GL_POLYGON_OFFSET == 0x7001 && MGL_LOCK_MANUAL == 0x7011 && MGL_ARRAY_TRANSFORMATIONS == 0x701D
          && MGL_UBYTE_BGRA == 0x7018 && MGL_UBYTE_ARGB == 0x7019, "MiniGL's tokens");
    CHECK(GL_TEXTURE_BINDING_2D == 0x8069 && GL_TEXTURE_COORD_ARRAY_POINTER == 0x8092 && GL_POLYGON_OFFSET_FILL == 0x8037,
          "Khronos values");
    CHECK(GLU_SMOOTH == 100000 && GLU_INVALID_ENUM == 100900 && GLU_ERROR == 100103 && GLUT_DEPTH == 0x10
          && GLUT_ELAPSED_TIME == 700, "GLU and GLUT values");
    printf("minigl ABI: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
