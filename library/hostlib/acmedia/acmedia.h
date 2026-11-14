/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_ACMEDIA_H
#define HOSTLIB_ACMEDIA_H
/* ACMedia v1's calls (designs/ACMEDIA-DESIGN-20261110.md 2.2; HOSTLIB-ABI.md
 * 8.7): twelve host services, $00012000-$0001200B, owner ACMedia in
 * ranges.lock. Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Each is a service (hostlib.toml: service = true): its host side is the
 * runtime's own (the broker), run once, never compared, never declining.
 * The 68k body, acmedia.c, writes nothing and returns ACM_E_NOTAVAIL; that
 * is the answer when the runtime has no broker, or under AC_HOSTLIB=0,
 * AC_HOSTLIB_OFF, or an invalid span. akikoplus.device then takes its next
 * rung (7.5). The body is real in a thin shell's build too (HL_THIN), as
 * the probe's is, so it uses ac_magic.h's tag directly.
 *
 * Every call takes one block of big-endian longs (ACM_<CALL>_* are their
 * numbers) and returns a signed LONG: >= 0 success, < 0 an ACM_E_* code.
 * Nothing waits on a decoder: slow work returns ACM_PENDING and its outcome
 * comes as an event (2.5). A session handle is a slot in bits 0-7 and a
 * generation in bits 8-30 (23 bits). Bit 31 stays clear, so a successful
 * ACM_OPEN stays nonnegative; a stale handle is ACM_E_NOSUCH.
 *
 * Ranges: read and write ranges are each at most ACM_RANGE_MAX (16 MB);
 * blocks, caps, info, results, events and fault lines at most ACM_SMALL_MAX
 * (4 KB); a source descriptor at most ACM_SRC_MAX (1 KB). No state ranges.
 *
 *   ACM_CAPS    $00012000  0 version wanted  1 out  2 len
 *               the caps table into out; returns entries written
 *   ACM_PROBE   $00012001  0 source  1 source len  2 out  3 len
 *               returns a ticket (ACM_RESULT reads the info), or an error
 *   ACM_OPEN    $00012002  0 source  1 source len  2 streams (ACM_ST_*)
 *               3 video target  4 audio target (ACM_TARGET_BYTES each, or 0
 *               for NONE)  5 event port id  6 flags (ACM_OF_*)
 *               returns a session; ACM_EV_OPENED or ACM_EV_ERROR follows
 *   ACM_INFO    $00012003  0 session  1 out  2 len; returns bytes written
 *   ACM_CONTROL $00012004  0 session  1 command (ACM_CTL_*)  2-5 its args
 *               returns 0 or an error
 *   ACM_FRAME   $00012005  0 session  1 out  2 len  3 pitch  4 format
 *               (ACM_PIX_*)  5 width  6 height; returns the pts in ms, or
 *               ACM_E_AGAIN when no frame is due
 *   ACM_AUDIO   $00012006  0 session  1 out  2 len  3 format (ACM_PCM_*)
 *               4 rate; returns bytes written, or ACM_E_AGAIN
 *   ACM_FEED    $00012007  0 session  1 in  2 len  3 flags (ACM_FF_EOF)
 *               returns bytes taken (fewer when the queue is full)
 *   ACM_EVENTS  $00012008  0 session, or 0 for all of this caller's
 *               1 out  2 len; ACM_EVENT_BYTES a record; returns records
 *   ACM_CLOSE   $00012009  0 session; returns 0
 *   ACM_RESULT  $0001200A  0 ticket  1 out  2 len; returns bytes, or
 *               ACM_PENDING
 *   ACM_FAULT   $0001200B  0 code  1 out  2 len; an English line (and a
 *               deny's reason); returns bytes written
 */
#include "../hl.h"

#define ACM_ABI_VERSION 1u
#define ACM_PENDING 1
#define ACM_RANGE_MAX (16u << 20)
#define ACM_SMALL_MAX 4096u
#define ACM_SRC_MAX 1024u

/* error codes (2.2): never a hardware failure, which falls back on the host (3.3) */
#define ACM_E_NOTAVAIL  (-1)   /* no host body: the 68k answer; the next rung */
#define ACM_E_BADARG    (-2)
#define ACM_E_NOSUCH    (-3)
#define ACM_E_FORMAT    (-4)
#define ACM_E_DENIED    (-5)   /* policy: never falls to another rung */
#define ACM_E_NOCODEC   (-6)
#define ACM_E_TOOBIG    (-7)
#define ACM_E_DECODER   (-8)   /* failed, crashed or stopped: never falls to another rung */
#define ACM_E_IO        (-9)
#define ACM_E_LIMIT     (-10)
#define ACM_E_EOF       (-11)
#define ACM_E_AGAIN     (-12)
#define ACM_E_VERSION   (-13)

/* blocks */
enum { ACM_CAPS_VERSION, ACM_CAPS_OUT, ACM_CAPS_LEN, ACM_CAPS_LONGS };
enum { ACM_PROBE_SRC, ACM_PROBE_SRCLEN, ACM_PROBE_OUT, ACM_PROBE_LEN, ACM_PROBE_LONGS };
enum { ACM_OPEN_SRC, ACM_OPEN_SRCLEN, ACM_OPEN_STREAMS, ACM_OPEN_VIDEO, ACM_OPEN_AUDIO, ACM_OPEN_PORT, ACM_OPEN_FLAGS, ACM_OPEN_LONGS };
enum { ACM_INFO_SESSION, ACM_INFO_OUT, ACM_INFO_LEN, ACM_INFO_LONGS };
enum { ACM_CONTROL_SESSION, ACM_CONTROL_CMD, ACM_CONTROL_A, ACM_CONTROL_B, ACM_CONTROL_C, ACM_CONTROL_D, ACM_CONTROL_LONGS };
enum { ACM_FRAME_SESSION, ACM_FRAME_OUT, ACM_FRAME_LEN, ACM_FRAME_PITCH, ACM_FRAME_FORMAT, ACM_FRAME_WIDTH, ACM_FRAME_HEIGHT, ACM_FRAME_LONGS };
enum { ACM_AUDIO_SESSION, ACM_AUDIO_OUT, ACM_AUDIO_LEN, ACM_AUDIO_FORMAT, ACM_AUDIO_RATE, ACM_AUDIO_LONGS };
enum { ACM_FEED_SESSION, ACM_FEED_IN, ACM_FEED_LEN, ACM_FEED_FLAGS, ACM_FEED_LONGS };
enum { ACM_EVENTS_SESSION, ACM_EVENTS_OUT, ACM_EVENTS_LEN, ACM_EVENTS_LONGS };
enum { ACM_CLOSE_SESSION, ACM_CLOSE_LONGS };
enum { ACM_RESULT_TICKET, ACM_RESULT_OUT, ACM_RESULT_LEN, ACM_RESULT_LONGS };
enum { ACM_FAULT_CODE, ACM_FAULT_OUT, ACM_FAULT_LEN, ACM_FAULT_LONGS };

/* the source descriptor: long 0 is the kind, big-endian, then the kind's own.
 * ACM_SRC_ACFS is not a unit and not a DH number. After the kind long come
 * three NUL-ended names: the drive folder (boot, nvme, ide, usb1), the
 * volume folder (the name the Amiga presents), and the path from that
 * volume's root. The host keys by drive/volume. The unit number is not in
 * this descriptor. It lives only in that drive's drive.json. */
enum { ACM_SRC_ACFS = 1,   /* drive, volume, path. No unit. */
       ACM_SRC_FEED = 2,   /* 1 size hint (0 unknown), 2 flags: ACM_SF_SEEKABLE */
       ACM_SRC_CD = 3 };   /* 1 track, 2 first LBA, 3 last LBA */
#define ACM_SF_SEEKABLE 1u

/* streams wanted (ACM_OPEN 2) */
#define ACM_ST_VIDEO 1u
#define ACM_ST_AUDIO 2u

/* an output target, ACM_TARGET_BYTES: big-endian longs, 0 its kind, then the kind's own */
#define ACM_TARGET_BYTES 32u
enum { ACM_OUT_NONE, ACM_OUT_PLANE,   /* 1-4 the box (x, y, w, h) on the shown screen; falls to RTG_SURFACE on its own */
       ACM_OUT_RTG_SURFACE,           /* 1 the bitmap's address, 2 pitch, 3 format (ACM_PIX_*), 4-7 the box */
       ACM_OUT_CHIPSET,               /* 1 planes' first address, 2 depth, 3 bytes a row, 4 rows */
       ACM_OUT_GUEST,                 /* pulled with ACM_FRAME / ACM_AUDIO */
       ACM_OUT_ACAHI };               /* audio: a host voice of ACAudioMixer */

/* ACM_OPEN flags */
#define ACM_OF_EVENTS_FRAME 1u        /* send ACM_EV_FRAME */

/* ACM_CONTROL commands and their args */
enum { ACM_CTL_PLAY = 1, ACM_CTL_PAUSE, ACM_CTL_STOP,
       ACM_CTL_SEEK,                  /* a ms, b ACM_SEEK_* */
       ACM_CTL_RATE,                  /* a: 1/65536 */
       ACM_CTL_VOLUME,                /* a: 0-65536 */
       ACM_CTL_SELECT,                /* a: kind (ACM_ST_*), b: stream index */
       ACM_CTL_SETRECT,               /* a: x << 16 | y, b: w << 16 | h, c: visible, d: clip id (0 none) */
       ACM_CTL_EVENTS,                /* a: the ACM_EVM_* mask */
       ACM_CTL_TARGET };              /* a: the stream kind, b: a target's address (ACM_TARGET_BYTES) */
#define ACM_SEEK_KEYFRAME 0u
#define ACM_SEEK_EXACT 1u

/* pixel formats (ACM_FRAME, RTG_SURFACE) */
enum { ACM_PIX_CHUNKY8 = 1, ACM_PIX_RGB16, ACM_PIX_ARGB32, ACM_PIX_HAM8, ACM_PIX_PLANAR, ACM_PIX_YUV420 };
/* PCM formats (ACM_AUDIO) */
enum { ACM_PCM_S16BE_STEREO = 1, ACM_PCM_S16BE_MONO, ACM_PCM_S8_MONO };
#define ACM_FF_EOF 1u

/* events, ACM_EVENT_BYTES: big-endian longs 0 serial, 1 type << 16 | b,
 * 2 session, 3 a */
#define ACM_EVENT_BYTES 16u
enum { ACM_EV_OPENED = 1, ACM_EV_PROBED, ACM_EV_FRAME, ACM_EV_NEED, ACM_EV_END, ACM_EV_ERROR, ACM_EV_PATH, ACM_EV_RECT };
#define ACM_EVM(ev) (1u << (ev))

/* paths and routes (ACM_INFO, ACM_EV_PATH) */
enum { ACM_PATH_SW = 0, ACM_PATH_HW_V4L2_REQUEST, ACM_PATH_HW_V4L2_M2M, ACM_PATH_HW_VAAPI };
enum { ACM_ROUTE_NONE = 0, ACM_ROUTE_PLANE, ACM_ROUTE_RTG_SURFACE, ACM_ROUTE_CHIPSET, ACM_ROUTE_GUEST, ACM_ROUTE_ACAHI };

/* codec tags: four letters, media.decode/1's, and the Amiga's */
#define ACM_TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

#define ACM_SESSION_SLOT(h) ((h) & 0xffu)
#define ACM_SESSION_GEN(h) (((h) >> 8) & 0x7fffffu)

#ifndef HL_HOST
int32_t hostlib_acm_caps(hl_addr block);
int32_t hostlib_acm_probe(hl_addr block);
int32_t hostlib_acm_open(hl_addr block);
int32_t hostlib_acm_info(hl_addr block);
int32_t hostlib_acm_control(hl_addr block);
int32_t hostlib_acm_frame(hl_addr block);
int32_t hostlib_acm_audio(hl_addr block);
int32_t hostlib_acm_feed(hl_addr block);
int32_t hostlib_acm_events(hl_addr block);
int32_t hostlib_acm_close(hl_addr block);
int32_t hostlib_acm_result(hl_addr block);
int32_t hostlib_acm_fault(hl_addr block);
#endif

#endif
