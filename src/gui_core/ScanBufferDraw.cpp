#include "ScanBufferDraw.h"

#include "Sort.h"
#include "BinHeap.h"

#include <cstdlib>
#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
using namespace std;

#define ON 0x01u
#define OFF 0x00u

#define OBJECT_MAX 65535
#define SPARE_LINES 2

// NOTES:

// Backgrounds: To set a general background color, the first position (possibly at pos= -1) should be an 'ON' at the furthest depth per scanline.
//              There should be no matching 'OFF'.
//              In areas where there is no fill present, no change to the existing image is made.

// Holes: A CCW winding polygon will have OFFs before ONs, being inside-out. If a single 'ON' is set before this shape
//        (Same as a background) then we will fill only where the polygon is *not* present -- this makes vignette effects simple

// TODO: Rather than having colors, we do one giant texture atlas. Spans have a start index, and a length
//       (length is 2^n, use a mask?), and an increment. Do `next = (curr + incr) & mask`
//       For flat colors, index is the color, increment and length are zero.

ScanBuffer * InitScanBuffer(int width, int height)
{
    auto buf = (ScanBuffer*)calloc(1, sizeof(ScanBuffer));
    if (buf == nullptr) return nullptr;

    auto sizeEstimate = width * 2;

    // we use a spare pairs of lines as sorting temp memory
    buf->scanLines = (ScanLine*)calloc(height+SPARE_LINES, sizeof(ScanLine));
    if (buf->scanLines == nullptr) { FreeScanBuffer(buf); return nullptr; }

    // set-up all the scanlines
    for (int i = 0; i < height + SPARE_LINES; i++) {
        auto scanBuf = (SwitchPoint*)calloc(sizeEstimate + 1, sizeof(SwitchPoint));
        if (scanBuf == nullptr) { FreeScanBuffer(buf); return nullptr; }
        buf->scanLines[i].points = scanBuf;
        buf->scanLines[i].length = sizeEstimate;

        buf->scanLines[i].count = 0;
        buf->scanLines[i].resetPoint = 0;
        buf->scanLines[i].dirty = true;
    }

    // set up the layer heaps
    buf->p_heap = HeapInit(OBJECT_MAX);
    if (buf->p_heap == nullptr) {
        FreeScanBuffer(buf);
        return nullptr;
    }

    buf->r_heap = HeapInit(OBJECT_MAX);
    if (buf->r_heap == nullptr) {
        FreeScanBuffer(buf);
        return nullptr;
    }

    // set initial sizes and counts

    buf->height = height;
    buf->width = width;

    return buf;
}

void FreeScanBuffer(ScanBuffer * buf)
{
    if (buf == nullptr) return;
    if (buf->scanLines != nullptr) {
        for (int i = 0; i < buf->height + SPARE_LINES; i++) {
            if (buf->scanLines[i].points != nullptr) free(buf->scanLines[i].points);
        }
        free(buf->scanLines);
    }
    if (buf->p_heap != nullptr) HeapDestroy((PriorityQueue)buf->p_heap);
    if (buf->r_heap != nullptr) HeapDestroy((PriorityQueue)buf->r_heap);
    free(buf);
}

// Set a point with an exact position, clipped to bounds
// gradient is 0..15; 15 = vertical; 0 = near horizontal.
void SetSP(ScanBuffer * buf, int x, int y, uint16_t objectId, uint8_t isOn) {
    if (y < 0 || y > buf->height) return;
    
   // SwitchPoint sp;
    ScanLine* line = &(buf->scanLines[y]);

	if (line->count >= line->length) return; // buffer full. TODO: grow?

    auto idx = line->count;
    auto points = line->points;

    points[idx].xPos = (x < 0) ? 0 : x;
    points[idx].id = objectId;
    points[idx].state = isOn;

	line->dirty = true; // ensure it's marked dirty
	line->count++; // increment pointer
}



// INTERNAL: Write scan switch points into buffer for a single line.
//           Used to draw any other polygons
void SetLine(
    ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    uint32_t objectId)
{
    if (y0 == y1) {
        return; // ignore: no scanlines would be affected
    }

    int h = buf->height;
    uint8_t isOn;
    
    if (y0 < y1) { // going down
        isOn = OFF;
    } else { // going up
        isOn = ON;
        // swap coords so we can always calculate down (always 1 entry per y coord)
        int tmp;
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
    }

    int top = (y0 < 0) ? 0 : y0;
    int bottom = (y1 > h) ? h : y1;
    float grad = (float)(x0 - x1) / (float)(y0 - y1);

    for (int y = top; y < bottom; y++) // skip the last pixel to stop double-counting
    {
        // add a point.
        int x = (int)(grad * static_cast<float>(y-y0) + static_cast<float>(x0));
        SetSP(buf, x, y, objectId, isOn);
    }

}

// Internal: Fill an axis aligned rectangle
void GeneralRect(ScanBuffer *buf,
    int left, int top, int right, int bottom,
    int objectId)
{
    if (left >= right || top >= bottom) return; //empty
    SetLine(buf,
        left, bottom,
        left, top,
        objectId);
    SetLine(buf,
        right, top,
        right, bottom,
        objectId);
}

// Fill an axis aligned rectangle
void FillRect(ScanBuffer *buf,
    int left, int top, int right, int bottom,
    int objectId)
{
    GeneralRect(buf, left, top, right, bottom, objectId);
}

void FillCircle(ScanBuffer *buf,
    int x, int y, int radius,
    int objectId) {
    FillEllipse(buf,
        x, y, radius * 2, radius * 2,
        objectId);
}


void GeneralEllipse(ScanBuffer *buf,
                    int xc, int yc, int width, int height,
                    bool positive,
                    int objectId)
{
    uint8_t left = (positive) ? (ON) : (OFF);
    uint8_t right = (positive) ? (OFF) : (ON);

    int a2 = width * width;
    int b2 = height * height;
    int fa2 = 4 * a2, fb2 = 4 * b2;
    int x, y, ty, sigma;

    // Top and bottom (need to ensure we don't double the scanlines)
    for (x = 0, y = height, sigma = 2 * b2 + a2 * (1 - 2 * height); b2*x <= a2 * y; x++) {
        if (sigma >= 0) {
            sigma += fa2 * (1 - y);
            // only draw scan points when we change y
            SetSP(buf, xc - x, yc + y, objectId, left);
            SetSP(buf, xc + x, yc + y, objectId, right);

            SetSP(buf, xc - x, yc - y, objectId, left);
            SetSP(buf, xc + x, yc - y, objectId, right);
            y--;
        }
        sigma += b2 * ((4 * x) + 6);
    }
    ty = y; // prevent overwrite

    // Left and right
    SetSP(buf, xc - width, yc, objectId, left);
    SetSP(buf, xc + width, yc, objectId, right);
    for (x = width, y = 1, sigma = 2 * a2 + b2 * (1 - 2 * width); a2*y < b2 * x; y++) {
        if (y > ty) break; // started to overlap 'top-and-bottom'

        SetSP(buf, xc - x, yc + y, objectId, left);
        SetSP(buf, xc + x, yc + y, objectId, right);

        SetSP(buf, xc - x, yc - y, objectId, left);
        SetSP(buf, xc + x, yc - y, objectId, right);

        if (sigma >= 0) {
            sigma += fb2 * (1 - x);
            x--;
        }
        sigma += a2 * ((4 * y) + 6);
    }
}


void FillEllipse(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int objectId)
{
    GeneralEllipse(buf,
        xc, yc, width, height,
        true, objectId);
}

void EllipseHole(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int objectId) {
    // set background
    GeneralRect(buf, 0, 0, buf->width, buf->height, objectId);

    // Same as ellipse, but with on and off flipped to make hole
    GeneralEllipse(buf,
        xc, yc, width, height,
        false, objectId);
}

// Fill a quad given 3 points
void FillTriQuad(ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    int objectId) {
    // Basically the same as triangle, but we also draw a mirror image across the xy1/xy2 plane
    if (buf == nullptr) return;

    if (x2 == x1 && x0 == x1 && y0 == y1 && y1 == y2) return; // empty

    // Cross product (finding only z)
    // this tells us if we are clockwise or ccw.
    int dx1 = x1 - x0; int dx2 = x2 - x0;
    int dy1 = y1 - y0; int dy2 = y2 - y0;
    int dz = dx1 * dy2 - dy1 * dx2;

    if (dz <= 0) { // ccw
        auto tmp = x1; x1 = x2; x2 = tmp;
        tmp = y1; y1 = y2; y2 = tmp;
        dx1 = dx2; dy1 = dy2;
    }
    SetLine(buf, x0, y0, x1, y1, objectId);
    SetLine(buf, x1, y1, x2 + dx1, y2 + dy1, objectId);
    SetLine(buf, x2 + dx1, y2 + dy1, x2, y2,objectId);
    SetLine(buf, x2, y2, x0, y0, objectId);
}

float isqrt(float number) {
	unsigned long i;
	float x2, y;
	int j;
	const float threeHalfs = 1.5F;

	x2 = number * 0.5F;
	y = number;
	i = *(long*)&y;
	i = 0x5f3759df - (i >> 1u);
	y = *(float*)&i;
	j = 3;
	while (j--) {	y = y * (threeHalfs - (x2 * y * y)); }

	return y;
}

void DrawLine(ScanBuffer * buf, int x0, int y0, int x1, int y1, int w, int objectId)
{
    if (w < 1) return; // empty

    // TODO: special case for w < 2

    // Use tri-quad and the gradient's normal to draw
    auto ndy = (float)(   x1 - x0  );
    auto ndx = (float)( -(y1 - y0) );

    // normalise
    float mag_w = static_cast<float>(w) * isqrt((ndy*ndy) + (ndx*ndx));
    ndx *= mag_w;
    ndy *= mag_w;

    int hdx = (int)(ndx / 2);
    int hdy = (int)(ndy / 2);

    // Centre points on line width 
    x0 -= hdx;
    y0 -= hdy;
    x1 -= (int)(ndx - static_cast<float>(hdx));
    y1 -= (int)(ndy - static_cast<float>(hdy));

    FillTriQuad(buf, x0, y0, x1, y1,
        x0 + (int)(ndx), y0 + (int)(ndy),
        objectId);
}

void OutlineEllipse(ScanBuffer * buf, int xc, int yc, int width, int height, int w, int objectId)
{
    int w1 = w / 2;
    int w2 = w - w1;

    GeneralEllipse(buf,
        xc, yc, width + w2, height + w2,
        true, objectId);

    GeneralEllipse(buf,
        xc, yc, width - w1, height - w1,
        false, objectId);
}

// Fill a triangle with a solid colour
// Triangle must be clockwise winding (if dy is -ve, line is 'on', otherwise line is 'off')
// counter-clockwise contours are detected and rearranged
void FillTriangle(
    ScanBuffer *buf, 
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    int objectId)
{
    if (buf == nullptr) return;

    if (x0 == x1 && x1 == x2) return; // empty
    if (y0 == y1 && y1 == y2) return; // empty

    // Cross product (finding only z)
    // this tells us if we are clockwise or ccw.
    int dx1 = x1 - x0; int dx2 = x2 - x0;
    int dy1 = y1 - y0; int dy2 = y2 - y0;
    int dz = dx1 * dy2 - dy1 * dx2;

    if (dz > 0) { // cw
        SetLine(buf, x0, y0, x1, y1, objectId);
        SetLine(buf, x1, y1, x2, y2, objectId);
        SetLine(buf, x2, y2, x0, y0, objectId);
    } else { // ccw - switch vertex 1 and 2 to make it clockwise.
        SetLine(buf, x0, y0, x2, y2, objectId);
        SetLine(buf, x2, y2, x1, y1, objectId);
        SetLine(buf, x1, y1, x0, y0, objectId);
    }
}

// Set a single 'on' point at the given level on each scan line
void SetBackground(
    ScanBuffer *buf,
    int objectId)
{
    SetLine(buf,
        0, buf->height,
        0, 0,
        objectId);
}

// Reset all drawing operations in the buffer, ready for next frame
// Do this *after* rendering to pixel buffer
void ClearScanBuffer(ScanBuffer * buf)
{
    if (buf == nullptr) return;

    for (int i = 0; i < buf->height; i++)
    {
        buf->scanLines[i].count = 0;
        buf->scanLines[i].resetPoint = 0;
        buf->scanLines[i].dirty = true;
    }
}

// Clear a scanline (including background)
void ResetScanLine(ScanBuffer* buf, int line)
{
    if (buf == nullptr) return;
    if (line < 0 || line >= buf->height) return;

    buf->scanLines[line].count = 0;
    buf->scanLines[line].resetPoint = 0;
    buf->scanLines[line].dirty = true;
}

// Clear a scanline, and set a new background color and depth
void ResetScanLineToColor(ScanBuffer* buf, int line, int objectId)
{
    if (buf == nullptr) return;
    if (line < 0 || line >= buf->height) return;

    buf->scanLines[line].count = 0;
    buf->scanLines[line].resetPoint = 0;
    buf->scanLines[line].dirty = true;

    SetSP(buf, 0, line, objectId, true);
}

// Switch two horizontal lines
void SwapScanLines(ScanBuffer* buf, int a, int b) {
    if (buf == nullptr) return;
    auto limit = buf->height - 1;
    if (a < 0 || b < 0 || a > limit || b > limit) return; // invalid range. Note: we keep a spare 'offscreen' buffer line

    ScanLine tmp;
    tmp.points     = buf->scanLines[a].points;
    tmp.count      = buf->scanLines[a].count;
    tmp.resetPoint = buf->scanLines[a].resetPoint;
    tmp.length     = buf->scanLines[a].length;

    buf->scanLines[a].points     = buf->scanLines[b].points;
    buf->scanLines[a].count      = buf->scanLines[b].count;
    buf->scanLines[a].resetPoint = buf->scanLines[b].resetPoint;
    buf->scanLines[a].length     = buf->scanLines[b].length;
    buf->scanLines[a].dirty      = true;

    buf->scanLines[b].points     = tmp.points;
    buf->scanLines[b].count      = tmp.count;
    buf->scanLines[b].resetPoint = tmp.resetPoint;
    buf->scanLines[b].length     = tmp.length;
    buf->scanLines[b].dirty      = true;
}

void CopyScanBuffer(ScanBuffer *src, ScanBuffer *dst)
{
    if (src == nullptr || dst == nullptr) return;

    // scanline switch points
    auto max = src->height;
    if (dst->height < max) max = dst->height;
    for (int i = 0; i < max; ++i) {
        auto c = src->scanLines[i].count;
        for (int j = 0; j < c; ++j) {
            dst->scanLines[i].points[j] = src->scanLines[i].points[j];
        }
        dst->scanLines[i].count      = src->scanLines[i].count;
        dst->scanLines[i].resetPoint = src->scanLines[i].resetPoint;
        dst->scanLines[i].length     = src->scanLines[i].length;
        dst->scanLines[i].dirty      = src->scanLines[i].dirty;
    }
}

// blend two colors, by a proportion [0..255]
// 255 is 100% color1; 0 is 100% color2.
uint32_t Blend(uint32_t prop1, uint32_t color1, uint32_t color2) {
    if (prop1 >= 255) return color1;
    if (prop1 <= 0) return color2;

    uint32_t prop2 = 255u - prop1;
    uint32_t r = prop1 * ((color1 & 0x00FF0000u) >> 16u);
    uint32_t g = prop1 * ((color1 & 0x0000FF00u) >> 8u);
    uint32_t b = prop1 * (color1 & 0x000000FFu);

    r += prop2 * ((color2 & 0x00FF0000u) >> 16u);
    g += prop2 * ((color2 & 0x0000FF00u) >> 8u);
    b += prop2 * (color2 & 0x000000FFu);

    // everything needs shifting 8 bits, we've integrated it into the color merge
    return ((r & 0xff00u) << 8u) + ((g & 0xff00u)) + ((b >> 8u) & 0xffu);
}

// reduce display heap to the minimum by merging with remove heap
inline void CleanUpHeaps(PriorityQueue p_heap, PriorityQueue r_heap) {
    // clear first rank (ended objects that are on top)
    // while top of p_heap and r_heap match, remove both.
    auto nextRemove = ElementType{ 0,-1,0 };
    auto top = ElementType{ 0,-1,0 };
    while (HeapTryFindMin(p_heap, &top) && HeapTryFindMin(r_heap, &nextRemove)
        && top.identifier == nextRemove.identifier) {
        HeapDeleteMin(r_heap);
        HeapDeleteMin(p_heap);
    }

    // clear up second rank (ended objects that are behind the top)
    auto nextObj = ElementType{ 0,-1,0 };

    // clean up the heaps more
    if (HeapTryFindNext(p_heap, &nextObj)) {
        if (HeapPeekMin(r_heap).identifier == nextObj.identifier) {
            auto current = HeapDeleteMin(p_heap); // remove the current top (we'll put it back after)
            while (HeapTryFindMin(p_heap, &top) && HeapTryFindMin(r_heap, &nextRemove)
                && top.identifier == nextRemove.identifier) {
                HeapDeleteMin(r_heap);
                HeapDeleteMin(p_heap);
            }
            HeapInsert(current, p_heap);
        }
    }
}

// The core rendering algorithm. This is done for each scanline.
void RenderScanLine(
    ScanBuffer *buf,             // source scan buffer
    TextureAtlas *map,           // color/texture map to use
    int lineIndex,               // index of the line we're drawing
    BYTE* data                   // target frame-buffer
) {
	auto scanLine = &(buf->scanLines[lineIndex]);
	if (!scanLine->dirty) return;
	scanLine->dirty = false;

    auto tmpLine1 = &(buf->scanLines[buf->height]);
    auto tmpLine2 = &(buf->scanLines[buf->height+1]);

    int yOff = buf->width * lineIndex;
    auto materials = map->materials;
    auto count = scanLine->count;

    // Copy switch points to the scratch space. This allows for our push/pop graphics storage.
    for (int i = 0; i < count; ++i) {
        tmpLine1->points[i] = scanLine->points[i];
    }

    // Note: sorting takes a lot of the time up. Anything we can do to improve it will help frame rates
    // Note: we use a single pair of 'spare' lines for sorting. If this rendering gets multi-threaded,
    //       you'll need to add scratch space per thread.
    auto list = IterativeMergeSort(tmpLine1->points, tmpLine2->points, count);

    auto p_heap = (PriorityQueue)buf->p_heap;   // presentation heap
    auto r_heap = (PriorityQueue)buf->r_heap;   // removal heap
    
    HeapMakeEmpty(p_heap);
    HeapMakeEmpty(r_heap);

    uint32_t end = buf->width; // end of data in 32bit words

    bool on = false;
    uint32_t p = 0; // current pixel

    // texture mapping
    uint32_t mapBase = 0; // index into texture atlas of current object
    uint32_t mapOffset = 0; // offset from mapBase for this pixel
    uint32_t mapIncrement = 0; // size of step through texture atlas for each pixel
    uint32_t mapMask = 0; // mask for map offset (constrains length)

    auto texture = map->textureAtlas;

    SwitchPoint current; // top-most object's most recent "on" switch
    for (int i = 0; i < count; i++)
    {
        SwitchPoint sw = list[i];
        if (sw.xPos > end) break; // ran off the end

        Material m = materials[sw.id];

        if (sw.xPos > p) { // render up to this switch point
            if (on) {
                auto max = (sw.xPos > end) ? end : sw.xPos; // clip right edge
                auto d = (uint32_t*)(data + ((p + yOff) * sizeof(uint32_t))); // get display pointer

                for (; p < max; p++) {
                    // copy textel to output
                    *(d++) = texture[mapBase+mapOffset];

                    // advance to next textel
                    mapOffset = (mapOffset + mapIncrement) & mapMask;
                } // draw pixels up to the point
            } else p = sw.xPos; // skip direct to the point
        }

        auto heapElem = ElementType{ /*depth:*/ m.depth, /*unique id:*/(int)sw.id, /*lookup index:*/ i };
        if (sw.state == ON) { // 'on' point, add to presentation heap
            HeapInsert(heapElem, p_heap);
        } else { // 'off' point, add to removal heap
            HeapInsert(heapElem, r_heap);
        }

        CleanUpHeaps(p_heap, r_heap);
        ElementType top = { 0,0,0 };
        on = HeapTryFindMin(p_heap, &top);

        if (on) {
            // set mapIndex for next run based on top of p_heap
            auto next = list[top.lookup];
            if (current.id != next.id) { // switching material
                current = list[top.lookup];
                auto paint = materials[current.id];
                mapBase = paint.startIndex;
                mapIncrement = paint.increment;
                mapMask = paint.length - 1; // MUST be a power-of-two or things will go weird

                // update initial offset if it was hidden
                //       we need to calculate how far through the texture we are;
                //       e.g. if we become uncovered half way along the span, we should
                //       start the texture 50% across.
                if (paint.screenSpace) {
                    mapOffset = ((p + paint.startOffset) * mapIncrement) & mapMask;
                } else {
                    mapOffset = paint.startOffset;
                    mapOffset = (mapOffset + (p - next.xPos) * mapIncrement) & mapMask;
                }
            }
        } else {
            mapBase = 0;
        }
    } // out of switch points

    
    if (on) { // fill to end of data
        for (; p < end; p++) {
            // copy textel to output
            ((uint32_t*)data)[p + yOff] = texture[mapBase+mapOffset];

            // advance to next textel
            mapOffset = (mapOffset + mapIncrement) & mapMask;
        }
    }
    
}

// Render a scan buffer to a pixel framebuffer
// This can be done on a different processor core from other draw commands to spread the load
// Do not draw to a scan buffer while it is rendering (switch buffers if you need to)
void RenderScanBufferToFrameBuffer(
    ScanBuffer *buf,   // source scan buffer
    TextureAtlas *map, // color/texture map to use
    BYTE* data,        // target frame-buffer (must match scanbuffer dimensions)
    int start,         // which line to start at? For full frame render, use 0
    int skip           // how many lines to skip? For full frame render, use 0
) {
    if (buf == nullptr || data == nullptr) return;

    int incr = skip+1;
    for (int i = start; i < buf->height; i+=incr) {
        RenderScanLine(buf, map, i, data);
    }
}

TextureAtlas *InitTextureAtlas(int textureSpace) {
    auto map = (TextureAtlas*)calloc(1, sizeof(TextureAtlas));
    if (map == nullptr) return nullptr;

    // the texture atlas' textels
    map->textelCount = textureSpace;
    map->textureAtlas = (uint32_t*)calloc(textureSpace + 1, sizeof(uint32_t));
    if (map->textureAtlas == nullptr) { FreeTextureAtlas(map); return nullptr; }
    map->textelCount = 0;

    // Lookups into the texture atlas
    map->materialSize = OBJECT_MAX;
    map->materials = (Material*)calloc(OBJECT_MAX + 1, sizeof(Material));
    if (map->materials == nullptr) { FreeTextureAtlas(map); return nullptr; }
    map->materialCount = 0;

    return map;
}

void FreeTextureAtlas(TextureAtlas *map) {
    if (map == nullptr) return;

    if (map->textureAtlas != nullptr) free(map->textureAtlas);
    if (map->materials != nullptr) free(map->textureAtlas);
}

void ResetTextureAtlas(TextureAtlas *map) {
    if (map == nullptr) return;
    map->textelCount = 0;
    map->materialCount = 0;
}
uint16_t AddSingleColorMaterialRgb(TextureAtlas* map, int depth, uint8_t r, uint8_t g, uint8_t b){
    uint32_t color = ((r & 0xffu) << 16u) + ((g & 0xffu) << 8u) + (b & 0xffu);
    return AddSingleColorMaterial(map, depth, color);
}

uint16_t AddSingleColorMaterial(TextureAtlas* map, int depth, uint32_t color) {
    if (map->materialCount+1 >= OBJECT_MAX) return 0;

    uint16_t objectId = ++(map->materialCount);
    uint32_t newIndex = map->textelCount++;
    map->textureAtlas[newIndex] = color;

    map->materials[objectId].startIndex = newIndex;
    map->materials[objectId].increment = 0;
    map->materials[objectId].length = 1;
    map->materials[objectId].depth = (int16_t)depth;

    return objectId;
}

uint32_t AddTextureRgb(TextureAtlas *map, uint8_t *bytes, int pixelCount) {
    if (map == nullptr) return 0;
    if (pixelCount > (map->atlasSize - map->textelCount)) return 0; // no free space. TODO: grow.

    uint32_t base = map->textelCount;
    for (int i = 0; i < pixelCount; ++i) {
        uint8_t r = *(bytes++);
        uint8_t g = *(bytes++);
        uint8_t b = *(bytes++);
        uint32_t color = ((r & 0xffu) << 16u) + ((g & 0xffu) << 8u) + (b & 0xffu);
        map->textureAtlas[map->textelCount] = color;
        map->textelCount++;
    }
    return base;
}

uint16_t AddTextureMaterial(TextureAtlas *map, int16_t depth, uint32_t base, uint16_t increment, uint16_t length) {
    if (map == nullptr) return 0;
    if ((map->materialSize - map->materialCount) < 1) return 0; // no free space

    uint16_t objectId = ++(map->materialCount);

    map->materials[objectId].startIndex = base;
    map->materials[objectId].startOffset = 0;
    map->materials[objectId].increment = increment;
    map->materials[objectId].length = length;
    map->materials[objectId].depth = depth;
    map->materials[objectId].screenSpace = false;

    return objectId;
}

uint16_t
AddTextureMaterialScreenSpace(TextureAtlas *map, int16_t depth, uint32_t base, uint16_t increment, uint16_t length) {
    if (map == nullptr) return 0;
    if ((map->materialSize - map->materialCount) < 1) return 0; // no free space

    uint16_t objectId = ++(map->materialCount);

    map->materials[objectId].startIndex = base;
    map->materials[objectId].startOffset = 0;
    map->materials[objectId].increment = increment;
    map->materials[objectId].length = length;
    map->materials[objectId].depth = depth;
    map->materials[objectId].screenSpace = true;

    return objectId;
}

void SetMaterialOffset(TextureAtlas *map, uint16_t objectId, uint16_t newOffset) {
    if (map == nullptr) return;
    if (objectId > map->materialCount) return;

    map->materials[objectId].startOffset = newOffset;
}

void SetMaterialDepth(TextureAtlas *map, uint16_t objectId, int16_t newDepth) {
    if (map == nullptr) return;
    if (objectId > map->materialCount) return;

    map->materials[objectId].depth = newDepth;
}

#pragma clang diagnostic pop