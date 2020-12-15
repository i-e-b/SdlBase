#include "app_start.h"

#include "src/gui_core/ScanBufferFont.h"
#include "src/types/MemoryManager.h"
#include "src/types/String.h"


void HandleEvent(SDL_Event *event, volatile ApplicationGlobalState *state) {
    // see lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/include/SDL_events.h
    if (event->type == SDL_KEYDOWN || event->type == SDL_QUIT) {
        state->running = false;
    }
}

// TODO: change frameTime from call duration to elapsed time since last frame
void DrawToScanBuffer(ScanBuffer *scanBuf, int frame, uint32_t frameTime) {
    ClearScanBuffer(scanBuf); // wipe out switch-point buffer
    SetBackground(scanBuf, 10000, 50, 80, 70);

    MMPush(1 MEGABYTE);

    // TODO: bring the console stuff into the project from MECS
    auto line = StringNew("Welcome to the sdl program base! Press any key to stop. Close window to exit");
    int x = 16; int y = 30;
    while (auto c = StringDequeue(line)) {
        AddGlyph(scanBuf, c, x, y, 1, 0xffffff);
        x += 8;
    }

    StringAppend(line, "Frame rate: ");
    StringAppend(line, StringFromInt32(1000 / (frameTime+1)));
    StringAppend(line, "; Frame count: ");
    StringAppend(line, StringFromInt32(frame));
    x = 16; y = 40;
    while (auto c = StringDequeue(line)) {
        AddGlyph(scanBuf, c, x, y, 1, 0x7755ff);
        x += 8;
    }
    MMPop();
}

void StartUp() {
    StartManagedMemory(); // use the semi-auto memory helper
}

void Shutdown() {
    ShutdownManagedMemory(); // deallocate everything
}

