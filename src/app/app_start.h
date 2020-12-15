#ifndef SDLBASE_APP_START_H
#define SDLBASE_APP_START_H

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

void DrawToScanBuffer(ScanBuffer *scanBuf, int frame, uint32_t frameTime);
void HandleEvent(SDL_Event *event, volatile ApplicationGlobalState *state);


#endif //SDLBASE_APP_START_H
