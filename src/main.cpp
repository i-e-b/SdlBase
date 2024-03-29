#include "src/gui_core/ScanBufferDraw.h"

#include <SDL.h>
#include <SDL_thread.h>

#include <iostream>
#include <app/app_start.h>

using namespace std;

// Two-thread rendering stuff:
SDL_Window* window; //The window we'll be rendering to
ScanBuffer *bufferA, *bufferB; // pair of scanline buffers. One is written while the other is read
TextureAtlas *textures; // one colour/texture map used across both buffers
volatile bool quit = false; // Quit flag
volatile bool drawDone = false; // Quit complete flag
volatile int writeBuffer = 0; // which buffer is being written (other will be read)
volatile int frameWait = 0; // frames waiting
volatile int renderThreadWaits = 0; // number of ms the render thread has spent paused
volatile int renderThreadSkips = 0; // number of frames the render skipped due to speed problems
volatile BYTE* base = nullptr; // graphics base
volatile int rowBytes = 0;

// User/Core shared data:
volatile ApplicationGlobalState gState = {};

// Scanline buffer to pixel buffer rendering on a separate thread
int RenderWorker(void*)
{
    int offset = 0;
    while (base == nullptr) {
        SDL_Delay(5);
    }
    SDL_Delay(150); // delay wake up
    while (!quit) {

        while (!quit && frameWait < 1) {
            SDL_Delay(1); // pause the thread until a new scan buffer is ready
            renderThreadWaits++;
        }
        auto fst = SDL_GetTicks();

        // Grab a buffer and release the lock
        auto scanBuf = (writeBuffer > 0) ? bufferA : bufferB; // must be opposite way to writing loop

        // Render odd then even scanlines, dropping the second set if rendering is getting late
        offset = 1-offset;
        BYTE* target = (BYTE*)base;
        RenderScanBufferToFrameBuffer(scanBuf, textures, target, offset, 1);

        // if no new frame is ready render the other lines of this one
        auto fTime = SDL_GetTicks() - fst;
        if (fTime < FRAME_TIME_TARGET) { // We have time after the frame
            offset = 1-offset;
            RenderScanBufferToFrameBuffer(scanBuf, textures, target, offset, 1);
        }
        else renderThreadSkips++;

        frameWait = 0;
    }
    drawDone = true;
    return 0;
}

void HandleEvents() {
    SDL_PumpEvents();
    SDL_Event next_event;
    while (SDL_PollEvent(&next_event)) {
        HandleEvent(&next_event, &gState);
        SDL_Delay(0);
    }
}

// We undefine the `main` macro in SDL_main.h, because it confuses the linker.
#undef main

int main()
{
    // The surface contained by the window
    SDL_Surface* screenSurface;

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        cout << "SDL initialization failed. SDL Error: " << SDL_GetError();
        return 1;
    } else {
        cout << "SDL initialization succeeded!\r\n";
    }

    // Create window
    window = SDL_CreateWindow("SDL project base", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        cout << "Window could not be created! SDL_Error: " << SDL_GetError();
        return 1;
    }

    // Let the app startup
    StartUp();

    screenSurface = SDL_GetWindowSurface(window); // Get window surface

    base = (BYTE*)screenSurface->pixels;
    int w = screenSurface->w;
    int h = screenSurface->h;
    rowBytes = screenSurface->pitch;
    int pixBytes = rowBytes / w;

    cout << "\r\nScreen format: " << SDL_GetPixelFormatName(screenSurface->format->format);
    cout << "\r\nBytesPerPixel: " << (pixBytes) << ", exact? " << (((screenSurface->pitch % pixBytes) == 0) ? "yes" : "no");

    bufferA = InitScanBuffer(w, h);
    bufferB = InitScanBuffer(w, h);
    textures = InitTextureAtlas(262144); // 512*512, not enough for "real" textures, but should work for testing
    // TODO: add auto-scaling to texture atlas so we don't need to guess!

    // run the rendering thread
#ifdef MULTI_THREAD
    SDL_Thread* threadA = SDL_CreateThread(RenderWorker, "RenderThread", nullptr);
#endif

    // Used to calculate the frames per second
    uint32_t startTicks = SDL_GetTicks();
    uint32_t idleTime = 0;
    uint32_t frame = 0;
    uint32_t fTime = FRAME_TIME_TARGET;
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // Draw loop                                                                                      //
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    auto writingScanBuf = bufferA;
    gState.running = true;
    while (gState.running) {
        uint32_t fst = SDL_GetTicks();
        // Wait for frame render to finish, then swap buffers and do next

        // Pick the write buffer and set switch points:
        auto draw = DrawTarget {textures, writingScanBuf};
        DrawToScanBuffer(&draw, frame++, fTime);

#ifdef MULTI_THREAD
        if (frameWait < 1) {
            SDL_UpdateWindowSurface(window);                        // update the surface -- need to do this every frame.

            // Swap buffers, we will render one to pixels while we're issuing draw commands to the other
            // If render can't keep up with frameWait, we skip this frame and draw to the same buffer.
            writeBuffer = 1 - writeBuffer;                          // switch buffer
            writingScanBuf = (writeBuffer > 0) ? bufferB : bufferA;        // MUST be opposite way to writing loop

            frameWait = 1;                                          // signal to the other thread that the buffer has changed
        }
#else
        // if not threaded, render immediately
        //RenderBuffer(writingScanBuf, base);
        RenderScanBufferToFrameBuffer(writingScanBuf, textures, base);
        SDL_UpdateWindowSurface(window);
#endif


        // Event loop and frame delay
#ifdef FRAME_LIMIT
        fTime = SDL_GetTicks() - fst;
        if (fTime < FRAME_TIME_TARGET) { // We have time after the frame
            HandleEvents(); // spend some budget on events
            auto left = SDL_GetTicks() - fst;
            if (left < FRAME_TIME_TARGET) SDL_Delay(FRAME_TIME_TARGET - left); // still got time? Then wait
            idleTime += FRAME_TIME_TARGET - fTime; // indication of how much slack we have
        }
        fTime = SDL_GetTicks() - fst;
#else
        fTime = SDL_GetTicks() - fst;
        HandleEvents();
#endif
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////

    quit = true;
    frameWait = 100;

    long endTicks = SDL_GetTicks();
    float avgFPS = static_cast<float>(frame) / (static_cast<float>(endTicks - startTicks) / 1000.f);
    float totalTime = FRAME_TIME_TARGET * frame;
    float idleFraction = static_cast<float>(idleTime) / totalTime;
    float rndrIdle = static_cast<float>(renderThreadWaits) / totalTime;
    float rndrSkip = static_cast<float>(renderThreadSkips) / static_cast<float>(frame);
    cout << "\r\nFPS ave = " << avgFPS << "\r\nLogic loop idle " << (100 * idleFraction) << "%\r\n"
        << "Render loop idle " << (100*rndrIdle) << "%\r\n"
        << "Render loop skipped " << (100*rndrSkip) << "%\r\n";

    // Let the app deallocate etc
    Shutdown();

#ifdef MULTI_THREAD
    while (!drawDone) { SDL_Delay(100); }// wait for the renderer to finish
#endif

#ifdef WAIT_AT_END
    // Wait for user to close the window
    SDL_Event close_event;
    while (SDL_WaitEvent(&close_event)) {
        if (close_event.type == SDL_QUIT) {
            break;
        }
    }
#endif

    // Close up shop
    FreeScanBuffer(bufferA);
    FreeScanBuffer(bufferB);
#ifdef MULTI_THREAD
    SDL_WaitThread(threadA, nullptr);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#pragma comment(linker, "/subsystem:Console")
