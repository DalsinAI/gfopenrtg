#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_AMIGAOS3

#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/lowlevel.h>
#include <libraries/lowlevel.h>
#include <libraries/gfopeninput.h>
#define OPENINPUT_BASE_NAME SDL_OS3_OpenInputBase
#define NO_INLINE_STDARG
#include <inline/gfopeninput.h>


struct Library *LowLevelBase = NULL;
struct Library *SDL_OS3_OpenInputBase = NULL;
static bool using_oi = false;


typedef struct joystick_hwdata {
    int port;
    ULONG type;
    ULONG last_state;
    APTR input_handle;
    ULONG flags;
    bool raw_mode;
} joystick_hwdata;

typedef struct {
    int port;
    ULONG type;
    SDL_JoystickID instance_id;
    ULONG oi_id;
    struct OIControllerInfo oi_info;
    char oi_path[40];
} OS3_JoyDevice;

static OS3_JoyDevice devices[64];
static int num_devices;

/* GFOpenInput reports stable controller IDs. Reconcile on every Detect so
 * hotplug works even without a notification port. Never reuse SDL instance
 * IDs for a removed device; clients must see detach/attach. */
static void OS3_ScanOpenInput(void)
{
    static struct OIControllerInfo list[64];
    static OS3_JoyDevice next[64];
    static OS3_JoyDevice old[64];
    int old_count;
    ULONG n, i;
    int j, count = 0;
    old_count=num_devices;
    SDL_memcpy(old,devices,sizeof(old));
    n = OIN_ListControllers(NULL, 0, sizeof(list[0]));
    if (n > 64) n = 64;
    SDL_zeroa(list);
    SDL_zeroa(next);
    if (n) n = OIN_ListControllers(list, n, sizeof(list[0]));
    if (n > 64) n = 64;
    for (i = 0; i < n; ++i) {
        if (!list[i].oci_ID) continue;
        next[count].oi_id = list[i].oci_ID;
        next[count].oi_info = list[i];
        next[count].oi_info.oci_Name[63] = 0;
        SDL_snprintf(next[count].oi_path,sizeof(next[count].oi_path),"gfopeninput:%lu",(unsigned long)list[i].oci_ID);
        for (j = 0; j < num_devices; ++j) {
            if (devices[j].oi_id == next[count].oi_id) {
                next[count].instance_id = devices[j].instance_id;
                break;
            }
        }
        if (!next[count].instance_id)
            next[count].instance_id = SDL_GetNextObjectID();
        ++count;
    }
    /* Publish the new table before emitting SDL hotplug events; SDL may
     * resolve instance IDs synchronously inside the event dispatcher. */
    SDL_memcpy(devices, next, sizeof(next));
    num_devices = count;
    for (j = 0; j < old_count; ++j) {
        bool found = false;
        for (i = 0; i < (ULONG)count; ++i) {
            if (old[j].oi_id == devices[i].oi_id) { found = true; break; }
        }
        if (!found) SDL_PrivateJoystickRemoved(old[j].instance_id);
    }
    for (i = 0; i < (ULONG)count; ++i) {
        bool found = false;
        for (j = 0; j < old_count; ++j) {
            if (old[j].oi_id == devices[i].oi_id) { found = true; break; }
        }
        if (!found) SDL_PrivateJoystickAdded(devices[i].instance_id);
    }
}

static void OS3_ScanDevices(void)
{
    OS3_JoyDevice old[4], next[4];
    int old_count=num_devices, count=0, port, i, j;
    if (using_oi) { OS3_ScanOpenInput(); return; }
    SDL_zeroa(old);
    SDL_zeroa(next);
    for (i=0;i<old_count && i<4;i++) old[i]=devices[i];
    if (LowLevelBase) {
        for (port=0;port<4;port++) {
            ULONG state=ReadJoyPort((ULONG)port);
            ULONG type=state&JP_TYPE_MASK;
            /* JP_TYPE_UNKNOWN alone is *not* a connected controller.
             * The AC090 returns UNKNOWN while ports settle, even with no
             * device. Recognised joysticks and CD32 pads are unambiguous. */
            if (type!=JP_TYPE_JOYSTK && type!=JP_TYPE_GAMECTLR) {
                /* Some adapters report UNKNOWN but still supply live
                 * direction/button bits. Accept those, not an idle UNKNOWN. */
                if (type!=JP_TYPE_UNKNOWN || !(state&0x0fffffffUL)) continue;
                type=JP_TYPE_JOYSTK;
            }
            next[count].port=port;
            next[count].type=type;
            for (j=0;j<old_count && j<4;j++) {
                if (old[j].port==port) {
                    next[count].instance_id=old[j].instance_id;
                    break;
                }
            }
            if (!next[count].instance_id)
                next[count].instance_id=SDL_GetNextObjectID();
            count++;
        }
    }
    SDL_memcpy(devices,next,sizeof(next));
    num_devices=count;
    for (j=0;j<old_count && j<4;j++) {
        bool found=false;
        for (i=0;i<count;i++) if (old[j].port==devices[i].port) found=true;
        if (!found) SDL_PrivateJoystickRemoved(old[j].instance_id);
    }
    for (i=0;i<count;i++) {
        bool found=false;
        for (j=0;j<old_count && j<4;j++) if (old[j].port==devices[i].port) found=true;
        if (!found) SDL_PrivateJoystickAdded(devices[i].instance_id);
    }
}

static bool OS3_JoystickInit(void)
{
    int port;

    SDL_OS3_OpenInputBase=OpenLibrary((CONST_STRPTR)OPENINPUT_NAME, OPENINPUT_VERSION);
    if (SDL_OS3_OpenInputBase &&
        (SDL_OS3_OpenInputBase->lib_Version > OPENINPUT_VERSION ||
        (SDL_OS3_OpenInputBase->lib_Version == OPENINPUT_VERSION &&
         SDL_OS3_OpenInputBase->lib_Revision >= OPENINPUT_REVISION_CORE))) {
        using_oi=true;
        num_devices=0;
        OS3_ScanOpenInput();
        return true;
    }
    if (SDL_OS3_OpenInputBase) {
        CloseLibrary(SDL_OS3_OpenInputBase);
        SDL_OS3_OpenInputBase=NULL;
    }
    LowLevelBase=OpenLibrary("lowlevel.library",40);
    if (!LowLevelBase) {
        return SDL_SetError("AmigaOS3: cannot open lowlevel.library V40+");
    }

    /*
     * Put all four lowlevel ports into autosense mode. Ports 2/3 can be
     * provided by four-player/splitter adapters, so do not assume only the
     * two motherboard control ports exist.
     */
    for (port=0;port<4;port++) {
        SetJoyPortAttrs((ULONG)port,
                        SJA_Type,SJA_TYPE_AUTOSENSE,
                        TAG_DONE);
    }

    OS3_ScanDevices();
    return true;
}

static int OS3_JoystickGetCount(void) { return num_devices; }
static void OS3_JoystickDetect(void)
{
    /* Ports are fixed, but emulators/USB stacks can change what lowlevel
       reports at runtime. Keep the discovery snapshot fresh. */
    OS3_ScanDevices();
}

static bool OS3_JoystickIsDevicePresent(Uint16 vendor_id,Uint16 product_id,Uint16 version,const char *name)
{
    (void)vendor_id;(void)product_id;(void)version;(void)name;
    return false;
}

static const char *OS3_JoystickGetDeviceName(int idx)
{
    if (idx<0 || idx>=num_devices) return NULL;
    if (using_oi) return devices[idx].oi_info.oci_Name;
    return devices[idx].type==JP_TYPE_GAMECTLR ? "Amiga lowlevel game controller" : "Amiga lowlevel joystick";
}
static const char *OS3_JoystickGetDevicePath(int idx)
{
    static const char *paths[4]={
        "lowlevel:port0",
        "lowlevel:port1",
        "lowlevel:port2",
        "lowlevel:port3"
    };
    int port;

    if (idx<0 || idx>=num_devices) {
        return NULL;
    }
    if (using_oi) return devices[idx].oi_path;
    port=devices[idx].port;
    return (port>=0 && port<4) ? paths[port] : NULL;
}
static int OS3_JoystickGetSteamSlot(int idx){(void)idx;return -1;}
static int OS3_JoystickGetPlayerIndex(int idx){return (idx>=0&&idx<num_devices)?(using_oi ? devices[idx].oi_info.oci_Player : devices[idx].port):-1;}
static void OS3_JoystickSetPlayerIndex(int idx,int player){
    if (using_oi && idx>=0 && idx<num_devices) devices[idx].oi_info.oci_Player=(WORD)player;
}

static SDL_GUID OS3_JoystickGetDeviceGUID(int idx)
{
    SDL_GUID guid;
    SDL_zero(guid);
    if (using_oi && idx>=0 && idx<num_devices) {
        SDL_memcpy(guid.data,devices[idx].oi_info.oci_GUID,16);
        return guid;
    }
    if (idx>=0 && idx<num_devices) {
        const char *tag=devices[idx].type==JP_TYPE_GAMECTLR ? "AMIGA-LOWLEVEL-CD32" : "AMIGA-LOWLEVEL-JOY";
        SDL_memcpy(guid.data,tag,SDL_min((size_t)16,SDL_strlen(tag)));
        guid.data[15]=(Uint8)devices[idx].port;
    }
    return guid;
}
static SDL_JoystickID OS3_JoystickGetDeviceInstanceID(int idx)
{
    return (idx>=0&&idx<num_devices)?devices[idx].instance_id:0;
}

static bool OS3_JoystickOpen(SDL_Joystick *joy,int idx)
{
    joystick_hwdata *hw;
    if (idx<0 || idx>=num_devices) return SDL_SetError("Invalid Amiga joystick index");
    hw=(joystick_hwdata *)SDL_calloc(1,sizeof(*hw));
    if (!hw) return false;
    if (using_oi) {
        struct TagItem tags[3];
        int tagcount=1;
        tags[0].ti_Tag=OIT_Exclusive;
        tags[0].ti_Data=TRUE;
        if (devices[idx].oi_info.oci_Player>=0) {
            tags[tagcount].ti_Tag=OIT_Player;
            tags[tagcount++].ti_Data=(ULONG)devices[idx].oi_info.oci_Player;
        }
        tags[tagcount].ti_Tag=TAG_DONE;
        tags[tagcount].ti_Data=0;
        hw->input_handle=OIN_OpenControllerA(devices[idx].oi_id,tags);
        if (!hw->input_handle) {
            tags[0].ti_Data=FALSE;
            hw->input_handle=OIN_OpenControllerA(devices[idx].oi_id,tags);
        }
        if (!hw->input_handle) {
            SDL_free(hw);
            return SDL_SetError("GFOpenInput controller unavailable");
        }
        hw->flags=devices[idx].oi_info.oci_Flags;
        hw->raw_mode=devices[idx].oi_info.oci_Type==OITYPE_WHEEL ||
                     devices[idx].oi_info.oci_Type==OITYPE_FLIGHTSTICK;
        joy->hwdata=hw;
        if (hw->raw_mode) {
            joy->naxes=SDL_min((int)devices[idx].oi_info.oci_NumAxes,OIRAW_AXES);
            joy->nhats=SDL_min((int)devices[idx].oi_info.oci_NumHats,OIRAW_HATS);
            joy->nbuttons=SDL_min((int)devices[idx].oi_info.oci_NumButtons,OIRAW_BUTTONS);
        } else {
            joy->naxes=OIAXIS_COUNT;
            joy->nhats=1;
            joy->nbuttons=OIB_COUNT;
        }
        return true;
    }
    hw->port=devices[idx].port;
    hw->type=devices[idx].type;
    hw->last_state=ReadJoyPort(hw->port);
    joy->hwdata=hw;
    joy->naxes=2;
    joy->nhats=0;
    joy->nbuttons=(hw->type==JP_TYPE_GAMECTLR)?7:2;
    return true;
}

static bool OS3_UnsupportedRumble(SDL_Joystick *j,Uint16 a,Uint16 b){
    joystick_hwdata *hw=j->hwdata;
    if (using_oi && hw && hw->input_handle && (hw->flags & OICF_RUMBLE))
        return OIN_Rumble(hw->input_handle,a,b,(a||b)?200:0)==OIERR_OK;
    return SDL_Unsupported();
}
static bool OS3_UnsupportedLED(SDL_Joystick *j,Uint8 r,Uint8 g,Uint8 b){(void)j;(void)r;(void)g;(void)b;return SDL_Unsupported();}
static bool OS3_UnsupportedEffect(SDL_Joystick *j,const void *p,int n){(void)j;(void)p;(void)n;return SDL_Unsupported();}
static bool OS3_UnsupportedSensors(SDL_Joystick *j,bool e){(void)j;(void)e;return SDL_Unsupported();}

static void OS3_JoystickUpdate(SDL_Joystick *joy)
{
    joystick_hwdata *hw=joy->hwdata;
    ULONG s;
    Sint16 x=0,y=0;
    static const ULONG buttons[7]={
        JPF_BUTTON_RED,JPF_BUTTON_BLUE,JPF_BUTTON_YELLOW,JPF_BUTTON_GREEN,
        JPF_BUTTON_FORWARD,JPF_BUTTON_REVERSE,JPF_BUTTON_PLAY
    };
    int i;
    Uint64 now=SDL_GetTicksNS();
    if (!hw) return;
    if (using_oi) {
        struct OIState state;
        int k;
        Uint8 hat=SDL_HAT_CENTERED;
        if (!hw->input_handle) return;
        if (hw->raw_mode) {
            struct OIRawState raw;
            SDL_zero(raw);
            if (OIN_ReadRaw(hw->input_handle,&raw,sizeof(raw))!=OIERR_OK) return;
            for (k=0;k<joy->nbuttons;k++)
                SDL_SendJoystickButton(now,joy,(Uint8)k,
                    (raw.oir_Buttons[k/32]&(1UL<<(k%32)))!=0);
            for (k=0;k<joy->naxes;k++)
                SDL_SendJoystickAxis(now,joy,(Uint8)k,raw.oir_Axes[k]);
            for (k=0;k<joy->nhats;k++)
                SDL_SendJoystickHat(now,joy,(Uint8)k,raw.oir_Hats[k]&0x0f);
            return;
        }
        SDL_zero(state);
        if (OIN_ReadState(hw->input_handle,&state,sizeof(state))!=OIERR_OK) return;
        for (k=0;k<OIB_COUNT;k++)
            SDL_SendJoystickButton(now,joy,(Uint8)k,(state.ois_Buttons&OIBF(k))!=0);
        for (k=0;k<OIAXIS_COUNT;k++) {
            Sint16 value=state.ois_Axes[k];
            if (k>=OIAXIS_TRIGGERLEFT) value=(Sint16)(2*(LONG)(value>0?value:0)-32767);
            SDL_SendJoystickAxis(now,joy,(Uint8)k,value);
        }
        if (state.ois_Buttons&OIBF(OIB_DPAD_UP)) hat|=SDL_HAT_UP;
        if (state.ois_Buttons&OIBF(OIB_DPAD_DOWN)) hat|=SDL_HAT_DOWN;
        if (state.ois_Buttons&OIBF(OIB_DPAD_LEFT)) hat|=SDL_HAT_LEFT;
        if (state.ois_Buttons&OIBF(OIB_DPAD_RIGHT)) hat|=SDL_HAT_RIGHT;
        SDL_SendJoystickHat(now,joy,0,hat);
        return;
    }
    s=ReadJoyPort(hw->port);

    if (s&JPF_JOY_LEFT) x=SDL_MIN_SINT16;
    else if (s&JPF_JOY_RIGHT) x=SDL_MAX_SINT16;
    if (s&JPF_JOY_UP) y=SDL_MIN_SINT16;
    else if (s&JPF_JOY_DOWN) y=SDL_MAX_SINT16;

    SDL_SendJoystickAxis(now,joy,0,x);
    SDL_SendJoystickAxis(now,joy,1,y);

    for (i=0;i<joy->nbuttons;i++) {
        SDL_SendJoystickButton(now,joy,(Uint8)i,(s&buttons[i])?true:false);
    }
    hw->last_state=s;
}

static void OS3_JoystickClose(SDL_Joystick *joy)
{
    if (using_oi && joy->hwdata && joy->hwdata->input_handle) {
        if (joy->hwdata->flags&OICF_RUMBLE)
            OIN_Rumble(joy->hwdata->input_handle,0,0,0);
        OIN_CloseController(joy->hwdata->input_handle);
    }
    SDL_free(joy->hwdata);
    joy->hwdata=NULL;
}
static void OS3_JoystickQuit(void)
{
    int port;

    num_devices=0;
    if (SDL_OS3_OpenInputBase) {
        CloseLibrary(SDL_OS3_OpenInputBase);
        SDL_OS3_OpenInputBase=NULL;
        using_oi=false;
    }
    if (LowLevelBase) {
        for (port=0;port<4;port++) {
            SetJoyPortAttrs((ULONG)port,
                            SJA_Type,SJA_TYPE_AUTOSENSE,
                            TAG_DONE);
        }
        CloseLibrary(LowLevelBase);
        LowLevelBase=NULL;
    }
}
static bool OS3_JoystickGetGamepadMapping(int idx,SDL_GamepadMapping *out)
{
    if (!using_oi || idx<0 || idx>=num_devices ||
        !(devices[idx].oi_info.oci_Flags&OICF_MAPPED) ||
        devices[idx].oi_info.oci_Type==OITYPE_WHEEL ||
        devices[idx].oi_info.oci_Type==OITYPE_FLIGHTSTICK) return false;
#define OI_MAP_BUTTON(field,n) do {out->field.kind=EMappingKind_Button;out->field.target=(Uint8)(n);}while(0)
#define OI_MAP_AXIS(field,n) do {out->field.kind=EMappingKind_Axis;out->field.target=(Uint8)(n);}while(0)
    OI_MAP_BUTTON(a,OIB_A); OI_MAP_BUTTON(b,OIB_B);
    OI_MAP_BUTTON(x,OIB_X); OI_MAP_BUTTON(y,OIB_Y);
    OI_MAP_BUTTON(back,OIB_BACK); OI_MAP_BUTTON(guide,OIB_GUIDE);
    OI_MAP_BUTTON(start,OIB_START);
    OI_MAP_BUTTON(leftstick,OIB_LEFTSTICK); OI_MAP_BUTTON(rightstick,OIB_RIGHTSTICK);
    OI_MAP_BUTTON(leftshoulder,OIB_LEFTSHOULDER);
    OI_MAP_BUTTON(rightshoulder,OIB_RIGHTSHOULDER);
    OI_MAP_BUTTON(dpup,OIB_DPAD_UP); OI_MAP_BUTTON(dpdown,OIB_DPAD_DOWN);
    OI_MAP_BUTTON(dpleft,OIB_DPAD_LEFT); OI_MAP_BUTTON(dpright,OIB_DPAD_RIGHT);
    OI_MAP_BUTTON(misc1,OIB_MISC1);
    OI_MAP_BUTTON(right_paddle1,OIB_PADDLE1);
    OI_MAP_BUTTON(left_paddle1,OIB_PADDLE2);
    OI_MAP_BUTTON(right_paddle2,OIB_PADDLE3);
    OI_MAP_BUTTON(left_paddle2,OIB_PADDLE4);
    OI_MAP_BUTTON(touchpad,OIB_TOUCHPAD);
    OI_MAP_AXIS(leftx,OIAXIS_LEFTX); OI_MAP_AXIS(lefty,OIAXIS_LEFTY);
    OI_MAP_AXIS(rightx,OIAXIS_RIGHTX); OI_MAP_AXIS(righty,OIAXIS_RIGHTY);
    OI_MAP_AXIS(lefttrigger,OIAXIS_TRIGGERLEFT);
    OI_MAP_AXIS(righttrigger,OIAXIS_TRIGGERRIGHT);
#undef OI_MAP_BUTTON
#undef OI_MAP_AXIS
    return true;
}

SDL_JoystickDriver SDL_AMIGAOS3_JoystickDriver={
    OS3_JoystickInit,
    OS3_JoystickGetCount,
    OS3_JoystickDetect,
    OS3_JoystickIsDevicePresent,
    OS3_JoystickGetDeviceName,
    OS3_JoystickGetDevicePath,
    OS3_JoystickGetSteamSlot,
    OS3_JoystickGetPlayerIndex,
    OS3_JoystickSetPlayerIndex,
    OS3_JoystickGetDeviceGUID,
    OS3_JoystickGetDeviceInstanceID,
    OS3_JoystickOpen,
    OS3_UnsupportedRumble,
    OS3_UnsupportedRumble,
    OS3_UnsupportedLED,
    OS3_UnsupportedEffect,
    OS3_UnsupportedSensors,
    OS3_JoystickUpdate,
    OS3_JoystickClose,
    OS3_JoystickQuit,
    OS3_JoystickGetGamepadMapping
};

#endif
