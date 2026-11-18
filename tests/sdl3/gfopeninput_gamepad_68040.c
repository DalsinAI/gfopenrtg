/* SPDX-License-Identifier: MIT
 * Native GFOpenInput/SDL3 AC090 test, AmiCradle Team, 2026.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
int main(void){
 SDL_Gamepad *pad=NULL; SDL_JoystickID *ids=NULL;
 Uint64 start; int n=0,i,axisMove=0,rumble=-1; Sint16 min=32767,max=-32768;
 puts("GFOS_SDL3_GAMEPAD_BEGIN");fflush(stdout);
 if(!SDL_Init(SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK|SDL_INIT_EVENTS)){
    printf("GFOS_SDL3_GAMEPAD init=FAIL %s\n",SDL_GetError());return 2;
 }
 start=SDL_GetTicks();
 while(SDL_GetTicks()-start<8000 && !pad){
    SDL_Event e;while(SDL_PollEvent(&e)){}
    ids=SDL_GetGamepads(&n);
    for(i=0;i<n && !pad;i++)pad=SDL_OpenGamepad(ids[i]);
    SDL_free(ids);ids=NULL;
    if(!pad)SDL_Delay(80);
 }
 if(!pad){puts("GFOS_SDL3_GAMEPAD open=FAIL");SDL_Quit();return 3;}
 {
    char *mapping=SDL_GetGamepadMapping(pad);
    printf("GFOS_SDL3_GAMEPAD open=PASS name=%s mapping=%s\n",SDL_GetGamepadName(pad),mapping?mapping:"(none)");
    SDL_free(mapping);
 }
 rumble=SDL_RumbleGamepad(pad,0x3000,0x3000,200)?1:0;
 start=SDL_GetTicks();
 while(SDL_GetTicks()-start<4000){
    SDL_Event ev;while(SDL_PollEvent(&ev)){}
    if(!SDL_GamepadConnected(pad))break;
    {
       Sint16 x=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX);
       if(x<min)min=x;
       if(x>max)max=x;
       if(x!=0)axisMove++;
    }
    SDL_Delay(40);
 }
 printf("GFOS_SDL3_GAMEPAD rumble=%d xMin=%d xMax=%d motionSamples=%d status=%s\n",
      rumble,(int)min,(int)max,axisMove,(rumble==1 && axisMove>10 && min< -5000 && max>5000)?"PASS":"FAIL");
 i=(rumble==1 && axisMove>10 && min< -5000 && max>5000)?0:2;
 SDL_CloseGamepad(pad);SDL_Quit();
 puts("GFOS_SDL3_GAMEPAD_END");fflush(stdout);
 return i;
}
