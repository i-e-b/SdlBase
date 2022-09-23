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

// describes a scan line 'object', which is a slice of the
// texture atlas and a Z depth. TODO: rename all the 'objectId' stuff to 'materialId'
typedef struct Material {
    uint32_t startIndex;    // First textel index
    uint16_t increment;     // per pixel offset through texture atlas
    uint16_t length;        // count of textels before looping (must be power of two, can be zero)
    int16_t  depth;         // z-position in final image
} Material;

typedef struct TextureAtlas {
    uint32_t* textureAtlas; // all the texture maps squished together.
    uint32_t textureEnd;    // offset of next free texture index.

    Material* materials;    // draw properties for each object (item count is the max used index, OBJECT_MAX is size)
    uint16_t materialCount; // offset of the next free object
} TextureAtlas;

// Init a next texture atlas with space for the given number of textels
TextureAtlas *InitTextureAtlas(int textureSpace);

// Deallocate a texture atlas
void FreeTextureAtlas(TextureAtlas* map);

// Reset material and texture indexes, so new objects will overwrite old ones
void ResetTextureAtlas(TextureAtlas* map);

// create a new single-color material at the given depth. Returns new material ID
uint16_t SetSingleColorMaterial(TextureAtlas* map, int depth, uint32_t color);

// create a new single-color material at the given depth. Returns new material ID
uint16_t SetSingleColorMaterialRgb(TextureAtlas* map, int depth, uint8_t r, uint8_t g, uint8_t b);

//---------------------------- SCANLINES ----------------------------------------//


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

    void *p_heap, *r_heap;  // internal heaps for depth sorting during rendering
} ScanBuffer;

// Allocate and configure a new scan buffer, attaching a default texture map
ScanBuffer *InitScanBuffer(int width, int height);

// Deallocate a scan buffer. Does not affect any attached default texture map.
void FreeScanBuffer(ScanBuffer *buf);


// Fill a triangle with a solid colour
void FillTriangle(ScanBuffer *buf,
                  int x0, int y0,
                  int x1, int y1,
                  int x2, int y2,
                  int objectId);

// Fill an axis aligned rectangle
void FillRect(ScanBuffer *buf,
    int left, int top, int right, int bottom,
    int objectId);

void FillCircle(ScanBuffer *buf,
    int x, int y, int radius,
    int objectId);

void FillEllipse(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int objectId);

// Fill a quad given 3 points
void FillTriQuad(ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    int objectId);

// draw a line with width
void DrawLine(ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int w, // pen width
    int objectId);

// draw the border of an ellipse
void OutlineEllipse(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int w, // outline width
    int objectId);

// Set a full-screen plane. Usually with a high Z value object id
void SetBackground( ScanBuffer *buf,
    int objectId);

// draw everywhere except in the ellipse
void EllipseHole(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int objectId);

// Reset all drawing operations in the buffer, ready for next frame
// Do this *after* rendering to pixel buffer
void ClearScanBuffer(ScanBuffer *buf);

// blend two colors, by a proportion [0..255]
// 255 is 100% color1; 0 is 100% color2.
uint32_t Blend(uint32_t prop1, uint32_t color1, uint32_t color2);

// Render a scan buffer to a pixel framebuffer
// This can be done on a different processor core from other draw commands to spread the load
// Do not draw to a scan buffer while it is rendering (switch buffers if you need to)
// The texture/color map can be swapped out for old-school palette effects or texture animation.
void RenderScanBufferToFrameBuffer(
    ScanBuffer *buf,   // source scan buffer
    TextureAtlas *map, // color/texture map to use
    BYTE* data         // target frame-buffer (must match ScanBuffer dimensions)
);

// Copy contents of src to dst, replacing dst.
// The two scan buffers should be the same size
void CopyScanBuffer(ScanBuffer *src, ScanBuffer *dst);

// ** Lower-level bits for extending the render engine **

// Switch two horizontal lines
void SwapScanLines(ScanBuffer* buf, int a, int b);

// Clear a scanline (including background)
void ResetScanLine(ScanBuffer* buf, int line);

// Clear a scanline, and set a new background 'object' ID
void ResetScanLineToColor(ScanBuffer* buf, int line, int objectId);

// Set a point with an exact position, clipped to bounds
void SetSP(ScanBuffer * buf, int x, int y, uint16_t objectId, uint8_t isOn);



typedef struct DrawTarget{
    TextureAtlas *textures;
    ScanBuffer *scanBuffer;
} DrawTarget;

#endif

//#pragma clang diagnostic pop