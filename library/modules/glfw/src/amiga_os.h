//========================================================================
// GLFW 3.4 AmigaOS 3 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2026 AmiCradle Team
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
//
// The Amiga system headers, for the amiga_*.c files only (after
// internal.h). GLFW's calls go through its own library bases in _glfw, so
// a program may have its own IntuitionBase and the rest, or none.

#ifndef _glfw3_amiga_os_h_
#define _glfw3_amiga_os_h_

#define __NOLIBBASE__
#define INTUITION_BASE_NAME _glfw.os3.IntuitionBase
#define GRAPHICS_BASE_NAME  _glfw.os3.GfxBase
#define KEYMAP_BASE_NAME    _glfw.os3.KeymapBase
#define LOWLEVEL_BASE_NAME  _glfw.os3.LowLevelBase
#define TIMER_BASE_NAME     _glfw.timer.os3.base

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/lowlevel.h>

// libnix's startup gives these two
extern struct ExecBase* SysBase;
extern struct DosLibrary* DOSBase;

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/lowlevel.h>
#include <proto/timer.h>

#endif // _glfw3_amiga_os_h_

