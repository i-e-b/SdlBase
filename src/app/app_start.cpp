#include "app_start.h"

#include "src/gui_core/ScanBufferFont.h"
#include "src/types/MemoryManager.h"
#include "src/types/String.h"

// Handy SDL docs: https://wiki.libsdl.org/


void HandleEvent(SDL_Event *event, volatile ApplicationGlobalState *state) {
    // see lib/SDL2-devel-2.0.9-VC/SDL2-2.0.9/include/SDL_events.h
    //     https://wiki.libsdl.org/SDL_EventType

    if (event->type == SDL_KEYDOWN || event->type == SDL_QUIT) {
        state->running = false;
        return;
    }
}

void writeString(ScanBuffer *scanBuf, String *line, int x, int y, int z, uint32_t color) {
    while (auto c = StringDequeue(line)) {
        AddGlyph(scanBuf, c, x, y, z, color);
        x += 8;
    }
}

void drawInfoMessage(ScanBuffer *scanBuf, int frame, uint32_t frameTime) {
    if (frameTime < 1) frameTime = 1;
    auto line = StringNewFormat("Frame rate:  \x02; Frame count: \x02.", 1000 / frameTime, frame);
    writeString(scanBuf, line, 16, 40, 10, 0x7755ff);

    size_t allocBytes, freeBytes, largestBlock;
    int allocZones, freeZones, refCount;
    ArenaGetState(MMCurrent(), &allocBytes, &freeBytes, &allocZones, &freeZones, &refCount, &largestBlock);

    StringAppendFormat(line, "Area use: alloc \x02 bytes; free \x02 bytes; largest free block \x02 bytes.",
                       allocBytes, freeBytes, largestBlock);
    writeString(scanBuf, line, 16, 100, 10, 0x77ffaa);

    StringAppendFormat(line, "alloc \x02 zones; free \x02 zones; total \x02 objects referenced.",
                       allocZones, freeZones, refCount);
    writeString(scanBuf, line, 16, 120, 10, 0x77ffaa);
}

void drawMouseHalo(ScanBuffer *scanBuf){
    int x,y;
    int sz = 20;
    int r=0xaa,g=0x77,b= 0x77;
    if (SDL_GetMouseState(&x, &y) & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        g = b = 0x00;
        sz = 15;
    }
    OutlineEllipse(scanBuf, x, y, sz, sz, 5, 5, r, g, b);
}

void DrawToScanBuffer(ScanBuffer *scanBuf, int frame, uint32_t frameTime) {
    MMPush(1 MEGABYTE); // prepare a per-frame bump allocator

    if (frame < 1) {
        ClearScanBuffer(scanBuf); // wipe out switch-point buffer
        SetBackground(scanBuf, 10000, 50, 50, 70);

        auto line = StringNew("Welcome to the sdl program base! Press any key to stop. Close window to exit");
        writeString(scanBuf, line, 16, 30, 1, 0xffffff);

        SetScanBufferResetPoint(scanBuf); // Allow us to 'reset' to the drawing to here
                                          // TODO: later, allow push/pop?
    }
    else {
        ResetScanBuffer(scanBuf); // 'undo' any changes after the last reset point

        drawInfoMessage(scanBuf, frame, frameTime);
        drawMouseHalo(scanBuf);
    }

    MMPop(); // wipe out anything we allocated in this frame.
}

void StartUp() {
    StartManagedMemory(); // use the semi-auto memory helper
}

void Shutdown() {
    ShutdownManagedMemory(); // deallocate everything
}

