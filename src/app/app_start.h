#ifndef SdlBase_App_Start_h
#define SdlBase_App_Start_h

#include <SDL.h>
#include "src/gui_core/ScanBufferDraw.h"

/******************************************
 * Application settings                   *
 ******************************************/

// Screen dimension constants
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;

// Ideal frame duration for frame limit, in milliseconds
#define FRAME_TIME_TARGET 15
// If defined, data will be copied between the write and render buffers. If you always redraw on every frame, you can undefine this.
#define COPY_SCAN_BUFFERS 1
// If set, renderer will try to hit the ideal frame time (by delaying frames, or postponing input events as required)
// Otherwise, drawing will be as fast as possible, and events are handled every frame
#define FRAME_LIMIT 1
// If defined, renderer will run in a parallel thread. Otherwise, draw and render will run in sequence
#define MULTI_THREAD 1
// If defined, the output screen will remain visible after the test run is complete
#define WAIT_AT_END 1

// This is the global state shared between the core and your app.
// The running flag is required, you can add extra fields as you need.
typedef struct ApplicationGlobalState {
    bool running;
} ApplicationGlobalState;


/******************************************
 * Main application implementation points *
 ******************************************/

// Called once at app start
void StartUp();
// Called for every frame. The scan buffer is not cleared before calling
void DrawToScanBuffer(ScanBuffer *scanBuf, int frame, uint32_t frameTime);
// Called when an SDL event is consumed
void HandleEvent(SDL_Event *event, volatile ApplicationGlobalState *state);

// Called once at app stop
void Shutdown();

#endif //SdlBase_App_Start_h
