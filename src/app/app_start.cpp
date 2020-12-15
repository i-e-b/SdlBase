#include "app_start.h"

#include "src/gui_core/ScanBufferFont.h"


void HandleEvent(SDL_Event *event, volatile ApplicationGlobalState *state) {
    // see lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/include/SDL_events.h
    if (event->type == SDL_KEYDOWN || event->type == SDL_QUIT) {
        state->running = false;
    }
}

void DrawToScanBuffer(ScanBuffer *scanBuf, int frame, uint32_t frameTime) {
    ClearScanBuffer(scanBuf); // wipe out switch-point buffer
    SetBackground(scanBuf, 10000, 50, 80, 70);

    // Basic message while other stuff is prepared
    auto demo1 = "Welcome to the sdl program base!                     ";
    auto demo2 = "This system is not ready - it doesn't do anything yet";
    auto demo3 = "...have a look in the code-base...                   ";
    for (int i = 0; i < 53; i++) {
        AddGlyph(scanBuf, demo1[i], (2 + i) * 8, 20, 1, 0xffffff);
        AddGlyph(scanBuf, demo2[i], (2 + i) * 8, 30, 1, 0x77ffff);
        AddGlyph(scanBuf, demo3[i], (2 + i) * 8, 40, 1, 0x77ffff);
    }
    AddGlyph(scanBuf, '0'+(frameTime%10), 16, 50, 1, 0xff00ff);
    AddGlyph(scanBuf, '0'+(frame%10), 32, 50, 1, 0xff00ff);

}

