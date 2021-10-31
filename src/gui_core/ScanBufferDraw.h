#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma once

#ifndef ScanBufferDraw_h
#define ScanBufferDraw_h

#include <cstdint>

#ifndef BYTE
#define BYTE unsigned char
#endif

// Functions to use a scan buffer
// for rendering filled shapes 


// entry for each 'pixel' in a scan buffer
// 'drawing' involves writing a list of these, sorting by x-position, then filling the scanline
// Notes: 1080p resolution is 1920x1080 = 2'073'600 pixels. 2^22 is 4'194'304; 2^21 -1 = 2'097'151
// Using per-row buffers, we only need 2048, or about 11 bits
typedef struct SwitchPoint {
    uint32_t xPos:11;       // position of switch-point, as (y*width)+x; Limits us to 2048 width. (21 bits left)
    uint32_t id:16;         // the object ID (used for material lookup, 65k limit) (5 bits left)
    uint32_t state:1;       // 1 = 'on' point, 0 = 'off' point.
    uint32_t reserved:4;
} SwitchPoint;

typedef struct Material {
    uint32_t color;         // RGB (24 bit of color).
    int16_t  depth;         // z-position in final image
} Material;

typedef struct ScanLine {
	bool dirty;				// set to `true` when the scanline is updated
    int32_t count;          // number of items in the array (changes with draw commands)
    int32_t resetPoint;     // roll-back / undo marker for this line
    int32_t length;         // memory length of the array (fixed)

    SwitchPoint* points;    // When drawing to the buffer, we can just append. Before rendering, this must be sorted by x-pos
} ScanLine;

// buffer of switch points.
typedef struct ScanBuffer {
    int height;
    int width;

    ScanLine* scanLines;    // matrix of switch points. (height + SPARE_LINES is size)

    // TODO: move materials and the heaps into the scan-lines?
    uint16_t materialCount; // used to give each object a depth and color. TODO: add textures
    uint16_t materialReset; //  roll-back / undo marker for the materials list
    Material* materials;    // draw properties for each object (item count is the max used index, OBJECT_MAX is size)

    void *p_heap, *r_heap;  // internal heaps for depth sorting during rendering
} ScanBuffer;

ScanBuffer *InitScanBuffer(int width, int height);

void FreeScanBuffer(ScanBuffer *buf);

// Fill a triangle with a solid colour
void FillTriangle(ScanBuffer *buf,
                  int x0, int y0,
                  int x1, int y1,
                  int x2, int y2,
                  int z,
                  int r, int g, int b);

// Fill an axis aligned rectangle
void FillRect(ScanBuffer *buf,
    int left, int top, int right, int bottom,
    int z,
    int r, int g, int b);

void FillCircle(ScanBuffer *buf,
    int x, int y, int radius,
    int z,
    int r, int g, int b);

void FillEllipse(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int z,
    int r, int g, int b);

// Fill a quad given 3 points
void FillTriQuad(ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    int z,
    int r, int g, int b);

// draw a line with width
void DrawLine(ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int z, int w, // width
    int r, int g, int b);

// draw the border of an ellipse
void OutlineEllipse(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int z, int w, // outline width
    int r, int g, int b);

// Set a background plane
void SetBackground( ScanBuffer *buf,
    int z, // depth of the background. Anything behind this will be invisible
    int r, int g, int b);

// draw everywhere except in the ellipse
void EllipseHole(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int z,
    int r, int g, int b);

// Reset all drawing operations in the buffer, ready for next frame
// Do this *after* rendering to pixel buffer
void ClearScanBuffer(ScanBuffer *buf);

// blend two colors, by a proportion [0..255]
// 255 is 100% color1; 0 is 100% color2.
uint32_t Blend(uint32_t prop1, uint32_t color1, uint32_t color2);

// Render a scan buffer to a pixel framebuffer
// This can be done on a different processor core from other draw commands to spread the load
// Do not draw to a scan buffer while it is rendering (switch buffers if you need to)
void RenderScanBufferToFrameBuffer(
    ScanBuffer *buf, // source scan buffer
    BYTE* data       // target frame-buffer (must match ScanBuffer dimensions)
);

// Copy contents of src to dst, replacing dst.
// The two scan buffers should be the same size
void CopyScanBuffer(ScanBuffer *src, ScanBuffer *dst);

// Allow us to 'reset' to the drawing to its current state after future drawing commands and renders
void SetScanBufferResetPoint(ScanBuffer *buf);

// Remove any drawings after the last reset point was set. If none set, all drawings will be removed.
void ResetScanBuffer(ScanBuffer *buf);


// ** Lower-level bits for extending the render engine **

// Switch two horizontal lines
void SwapScanLines(ScanBuffer* buf, int a, int b);

// Clear a scanline (including background)
void ResetScanLine(ScanBuffer* buf, int line);

// Clear a scanline, and set a new background color and depth
void ResetScanLineToColor(ScanBuffer* buf, int line, int z, uint32_t color);

// Set a point with an exact position, clipped to bounds
void SetSP(ScanBuffer * buf, int x, int y, uint16_t objectId, uint8_t isOn);

// Set or update material values for an object
void SetMaterial(ScanBuffer* buf, uint16_t objectId, int depth, uint32_t color);

#endif

//#pragma clang diagnostic pop