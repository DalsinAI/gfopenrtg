/* SPDX-License-Identifier: MIT
 * Native GFOpenInput/SDL3 AC090 test, AmiCradle Team, 2026.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    SDL_Joystick *j=NULL;
    SDL_JoystickID id=0;
    Uint64 start,lastPrint;
    int adds=0,removes=0,gamepad=0,motion=0,buttons=0,trigger=0;
    Sint16 min=-0,max=0,prev=0;
    int requireMotion=(argc>1 && argv[1][0]=='M');
    puts("GFOS_SDL3_MOTION_BEGIN");fflush(stdout);
    if(!SDL_Init(SDL_INIT_JOYSTICK|SDL_INIT_GAMEPAD|SDL_INIT_EVENTS)){
        printf("GFOS_SDL3_MOTION init=FAIL error=%s\n",SDL_GetError());return 2;
    }
    start=lastPrint=SDL_GetTicks();
    while(SDL_GetTicks()-start<35000){
        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            if(ev.type==SDL_EVENT_JOYSTICK_ADDED){
                adds++;
                if(!j){j=SDL_OpenJoystick(ev.jdevice.which);if(j)id=ev.jdevice.which;}
            }else if(ev.type==SDL_EVENT_JOYSTICK_REMOVED){
                removes++;
                if(j && ev.jdevice.which==id){SDL_CloseJoystick(j);j=NULL;id=0;}
            }else if(ev.type==SDL_EVENT_GAMEPAD_ADDED) gamepad++;
        }
        if(j && SDL_JoystickConnected(j)){
            Sint16 value=SDL_GetJoystickAxis(j,0);
            Sint16 trig=SDL_GetJoystickAxis(j,4);
            if(value<min)min=value;
            if(value>max)max=value;
            if(value!=prev)motion++;
            if(SDL_GetJoystickButton(j,0))buttons++;
            if(trig>1000)trigger++;
            prev=value;
        }
        if(SDL_GetTicks()-lastPrint>=5000){
            printf("GFOS_SDL3_MOTION elapsed=%lu adds=%d removes=%d axisMin=%d axisMax=%d motion=%d buttons=%d triggers=%d\n",
               (unsigned long)(SDL_GetTicks()-start),adds,removes,(int)min,(int)max,motion,buttons,trigger);
            fflush(stdout);lastPrint=SDL_GetTicks();
        }
        SDL_Delay(40);
    }
    if(j)SDL_CloseJoystick(j);
    SDL_Quit();
    {
        int ok=adds>0 && gamepad>0 && (!requireMotion || (motion>10 && min< -5000 && max>5000 && trigger>10 && buttons>0 && removes>0));
        printf("GFOS_SDL3_MOTION adds=%d removes=%d gamepad=%d min=%d max=%d motion=%d buttons=%d trigger=%d status=%s\n",adds,removes,gamepad,(int)min,(int)max,motion,buttons,trigger,ok?"PASS":"FAIL");
        puts("GFOS_SDL3_MOTION_END");fflush(stdout);
        return ok?0:2;
    }
}
