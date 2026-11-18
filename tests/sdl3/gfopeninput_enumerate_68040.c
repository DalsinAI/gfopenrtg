/* SPDX-License-Identifier: MIT
 * Native GFOpenInput/SDL3 AC090 test, AmiCradle Team, 2026.
 */
/* Native AmigaOS SDL3/GFOpenInput probe; test executable, not a game. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    SDL_JoystickID *ids;
    int n = 0, i, errors = 0, samples = 0;
    Uint64 start;
    (void)argc; (void)argv;
    puts("GFOS_SDL3_PAD_BEGIN"); fflush(stdout);
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        printf("GFOS_SDL3_PAD init=FAIL error=%s\n", SDL_GetError());
        return 2;
    }
    ids=SDL_GetJoysticks(&n);
    printf("GFOS_SDL3_PAD count=%d status=%s\n",n,n>=0?"PASS":"FAIL");
    for (i=0;i<n;i++) {
        SDL_Joystick *j=SDL_OpenJoystick(ids[i]);
        if (!j) {
            printf("GFOS_SDL3_PAD open=%d status=FAIL error=%s\n",i,SDL_GetError());
            errors++;
            continue;
        }
        printf("GFOS_SDL3_PAD id=%ld name=%s axes=%d buttons=%d hats=%d isGamepad=%d status=PASS\n",
            (long)ids[i],SDL_GetJoystickName(j),SDL_GetNumJoystickAxes(j),
            SDL_GetNumJoystickButtons(j),SDL_GetNumJoystickHats(j),SDL_IsGamepad(ids[i]));
        SDL_CloseJoystick(j);
    }
    SDL_free(ids);
    start=SDL_GetTicks();
    while (SDL_GetTicks()-start<1200) {
        SDL_Event ev;
        while(SDL_PollEvent(&ev)) {
            if(ev.type==SDL_EVENT_JOYSTICK_ADDED || ev.type==SDL_EVENT_JOYSTICK_REMOVED ||
               ev.type==SDL_EVENT_GAMEPAD_ADDED || ev.type==SDL_EVENT_GAMEPAD_REMOVED) {
                printf("GFOS_SDL3_PAD hotplug=%lu\n",(unsigned long)ev.type);
                samples++;
            }
        }
        SDL_Delay(10);
    }
    SDL_Quit();
    printf("GFOS_SDL3_PAD completed=1 events=%d errors=%d status=%s\n",samples,errors,errors?"FAIL":"PASS");
    puts("GFOS_SDL3_PAD_END");
    fflush(stdout);
    return errors?2:0;
}
