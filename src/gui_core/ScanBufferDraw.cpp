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

ScanBuffer * InitScanBuffer(int width, int height)
{
    auto buf = (ScanBuffer*)calloc(1, sizeof(ScanBuffer));
    if (buf == nullptr) return nullptr;

    auto sizeEstimate = width * 2;

    // Idea: Have a single list and sort by overall position rather than x (would need a background reset at each scan start?)
    //       Could also do a 'region' like difference-from-last-scanline?

    buf->materials = (Material*)calloc(OBJECT_MAX + 1, sizeof(Material));
    if (buf->materials == nullptr) { FreeScanBuffer(buf); return nullptr; }

    buf->scanLines = (ScanLine*)calloc(height+SPARE_LINES, sizeof(ScanLine)); // we use a spare pairs of lines as sorting temp memory
    if (buf->scanLines == nullptr) { FreeScanBuffer(buf); return nullptr; }

    for (int i = 0; i < height + SPARE_LINES; i++) {
        auto scanBuf = (SwitchPoint*)calloc(sizeEstimate + 1, sizeof(SwitchPoint));
        if (scanBuf == nullptr) { FreeScanBuffer(buf); return nullptr; }
        buf->scanLines[i].points = scanBuf;
        buf->scanLines[i].length = sizeEstimate;

        buf->scanLines[i].count = 0;
        buf->scanLines[i].resetPoint = 0;
        buf->scanLines[i].dirty = true;
    }

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

    buf->materialCount = 0;
    buf->materialReset = 0;

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
    if (buf->materials != nullptr) free(buf->materials);
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

void SetMaterial(ScanBuffer* buf, uint16_t objectId, int depth, uint32_t color) {
    if (objectId > OBJECT_MAX) return;
    buf->materials[objectId].color = color;
    buf->materials[objectId].depth = (int16_t)depth;
}

// INTERNAL: Write scan switch points into buffer for a single line.
//           Used to draw any other polygons
void SetLine(
    ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int z,
    uint32_t r, uint32_t g, uint32_t b)
{
    if (y0 == y1) {
        return; // ignore: no scanlines would be affected
    }

    uint32_t color = ((r & 0xffu) << 16u) + ((g & 0xffu) << 8u) + (b & 0xffu);
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

    auto objectId = buf->materialCount;
    SetMaterial(buf, objectId, z, color);

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
    int z,
    int r, int g, int b)
{
    if (left >= right || top >= bottom) return; //empty
    SetLine(buf,
        left, bottom,
        left, top,
        z, r, g, b);
    SetLine(buf,
        right, top,
        right, bottom,
        z, r, g, b);
}

// Fill an axis aligned rectangle
void FillRect(ScanBuffer *buf,
    int left, int top, int right, int bottom,
    int z,
    int r, int g, int b)
{
    if (z < 0) return; // behind camera
    GeneralRect(buf, left, top, right, bottom, z, r, g, b);

    buf->materialCount++;
}

void FillCircle(ScanBuffer *buf,
    int x, int y, int radius,
    int z,
    int r, int g, int b) {
    FillEllipse(buf,
        x, y, radius * 2, radius * 2,
        z,
        r, g, b);
}


void GeneralEllipse(ScanBuffer *buf,
                    int xc, int yc, int width, int height,
                    int z, bool positive,
                    uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t color = ((r & 0xffu) << 16u) + ((g & 0xffu) << 8u) + (b & 0xffu);

    uint8_t left = (positive) ? (ON) : (OFF);
    uint8_t right = (positive) ? (OFF) : (ON);

    int a2 = width * width;
    int b2 = height * height;
    int fa2 = 4 * a2, fb2 = 4 * b2;
    int x, y, ty, sigma;
    
    auto objectId = buf->materialCount;
    SetMaterial(buf, objectId, z, color);

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
    int z,
    int r, int g, int b)
{
    if (z < 0) return; // behind camera

    GeneralEllipse(buf,
        xc, yc, width, height,
        z, true,
        r, g, b);

    buf->materialCount++;
}

void EllipseHole(ScanBuffer *buf,
    int xc, int yc, int width, int height,
    int z,
    int r, int g, int b) {

    if (z < 0) return; // behind camera

    // set background
    GeneralRect(buf, 0, 0, buf->width, buf->height, z, r, g, b);

    // Same as ellipse, but with on and off flipped to make hole
    GeneralEllipse(buf,
        xc, yc, width, height,
        z, false,
        r, g, b);

    buf->materialCount++;
}

// Fill a quad given 3 points
void FillTriQuad(ScanBuffer *buf,
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    int z,
    int r, int g, int b) {
    // Basically the same as triangle, but we also draw a mirror image across the xy1/xy2 plane
    if (buf == nullptr) return;
    if (z < 0) return; // behind camera

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
    SetLine(buf, x0, y0, x1, y1, z, r, g, b);
    SetLine(buf, x1, y1, x2 + dx1, y2 + dy1, z, r, g, b);
    SetLine(buf, x2 + dx1, y2 + dy1, x2, y2, z, r, g, b);
    SetLine(buf, x2, y2, x0, y0, z, r, g, b);

    buf->materialCount++;
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

void DrawLine(ScanBuffer * buf, int x0, int y0, int x1, int y1, int z, int w, int r, int g, int b)
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
        z, r, g, b);
}

void OutlineEllipse(ScanBuffer * buf, int xc, int yc, int width, int height, int z, int w, int r, int g, int b)
{
    if (z < 0) return; // behind camera

    int w1 = w / 2;
    int w2 = w - w1;

    GeneralEllipse(buf,
        xc, yc, width + w2, height + w2,
        z, true, r, g, b);

    GeneralEllipse(buf,
        xc, yc, width - w1, height - w1,
        z, false, r, g, b);

    buf->materialCount++;
}

// Fill a triangle with a solid colour
// Triangle must be clockwise winding (if dy is -ve, line is 'on', otherwise line is 'off')
// counter-clockwise contours are detected and rearranged
void FillTriangle(
    ScanBuffer *buf, 
    int x0, int y0,
    int x1, int y1,
    int x2, int y2,
    int z,
    int r, int g, int b)
{
    if (buf == nullptr) return;
    if (z < 0) return; // behind camera

    if (x0 == x1 && x1 == x2) return; // empty
    if (y0 == y1 && y1 == y2) return; // empty

    // Cross product (finding only z)
    // this tells us if we are clockwise or ccw.
    int dx1 = x1 - x0; int dx2 = x2 - x0;
    int dy1 = y1 - y0; int dy2 = y2 - y0;
    int dz = dx1 * dy2 - dy1 * dx2;

    if (dz > 0) { // cw
        SetLine(buf, x0, y0, x1, y1, z, r, g, b);
        SetLine(buf, x1, y1, x2, y2, z, r, g, b);
        SetLine(buf, x2, y2, x0, y0, z, r, g, b);
    } else { // ccw - switch vertex 1 and 2 to make it clockwise.
        SetLine(buf, x0, y0, x2, y2, z, r, g, b);
        SetLine(buf, x2, y2, x1, y1, z, r, g, b);
        SetLine(buf, x1, y1, x0, y0, z, r, g, b);
    }

    buf->materialCount++;
}

// Set a single 'on' point at the given level on each scan line
void SetBackground(
    ScanBuffer *buf,
    int z, // depth of the background. Anything behind this will be invisible
    int r, int g, int b) {
    if (z < 0) return; // behind camera

    SetLine(buf,
        0, buf->height,
        0, 0,
        z, r, g, b);

    buf->materialCount++;
}

// Reset all drawing operations in the buffer, ready for next frame
// Do this *after* rendering to pixel buffer
void ClearScanBuffer(ScanBuffer * buf)
{
    if (buf == nullptr) return;
    buf->materialCount = 0; // reset object ids
    buf->materialReset = 0; // reset object ids
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
void ResetScanLineToColor(ScanBuffer* buf, int line, int z, uint32_t color)
{
    if (buf == nullptr) return;
    if (line < 0 || line >= buf->height) return;

    buf->scanLines[line].count = 0;
    buf->scanLines[line].resetPoint = 0;
    buf->scanLines[line].dirty = true;

    auto objectId = buf->materialCount;
    SetMaterial(buf, objectId, z, color);
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

    // object materials
    auto mc = src->materialCount;
    for (int i = 0; i < mc; ++i) {
        dst->materials[i].color = src->materials[i].color;
        dst->materials[i].depth = src->materials[i].depth;
    }
    dst->materialCount = src->materialCount;
    dst->materialReset = src->materialReset;

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


// Allow us to 'reset' to the drawing to its current state after future drawing commands and renders
void SetScanBufferResetPoint(ScanBuffer *buf) {
    if (buf == nullptr) return;
    buf->materialReset = buf->materialCount;
    for (int i = 0; i < buf->height; i++)
    {
        buf->scanLines[i].resetPoint = buf->scanLines[i].count;
    }
}

// Remove any drawings after the last reset point was set. If none set, all drawings will be removed.
void ResetScanBuffer(ScanBuffer *buf){
    if (buf == nullptr) return;
    buf->materialCount = buf->materialReset;
    for (int i = 0; i < buf->height; i++)
    {
        buf->scanLines[i].count = buf->scanLines[i].resetPoint;
        buf->scanLines[i].dirty = true;
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
    int lineIndex,               // index of the line we're drawing
    BYTE* data                   // target frame-buffer
) {
	auto scanLine = &(buf->scanLines[lineIndex]);
	if (!scanLine->dirty) return;
	scanLine->dirty = false;

    auto tmpLine1 = &(buf->scanLines[buf->height]);
    auto tmpLine2 = &(buf->scanLines[buf->height+1]);

    int yOff = buf->width * lineIndex;
    auto materials = buf->materials;
    auto count = scanLine->count;

    // Copy switch points to the scratch space. This allows for our push/pop graphics storage.
    /*auto dst = tmpLine1->points;
    auto src = scanLine->points;
    for (int i = 0; i < count; ++i) { *(dst++) = *(src++); }*/
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
    uint32_t color = 0; // color of current object
    SwitchPoint current; // top-most object's most recent "on" switch
    for (int i = 0; i < count; i++)
    {
        SwitchPoint sw = list[i];
        if (sw.xPos > end) break; // ran off the end

        Material m = materials[sw.id];

        if (sw.xPos > p) { // render up to this switch point
            if (on) {
                auto max = (sw.xPos > end) ? end : sw.xPos;
                auto d = (uint32_t*)(data + ((p + yOff) * sizeof(uint32_t)));
                for (; p < max; p++) {
                    // TODO: more materials here (textures at least)
                    // -- 'fade rate'
                    //if (current.fade < 15) current.fade++;

                    // -- smear / blur test
                    //c = Blend(128, color, c); // smearing blur. `prop1` Should lower for flatter angles
                    //*(d++) = c;

                    // -- plain:
                    *(d++) = color;

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
            // set color for next run based on top of p_heap
            //color = materials[top.identifier].color;
            current = list[top.lookup];
            color = materials[current.id].color;

            // If there is another object underneath, we store the color for antialiasing.
            /*ElementType nextObj = { 0,0,0 };
            if (HeapTryFindNext(p_heap, &nextObj)) {
                color_under = materials[nextObj.identifier].color;
            } else {
                color_under = 0;
            }*/
        } else {
            color = 0;
        }


#if 0
        // DEBUG: show switch point in black
        int pixoff = ((yOff + sw.xPos - 1) * 4);
        if (pixoff > 0) { data[pixoff + 0] = data[pixoff + 1] = data[pixoff + 2] = 0; }
        // END
#endif
    } // out of switch points

    
    if (on) { // fill to end of data
        for (; p < end; p++) {
            ((uint32_t*)data)[p + yOff] = color;
        }
    }
    
}

// Render a scan buffer to a pixel framebuffer
// This can be done on a different processor core from other draw commands to spread the load
// Do not draw to a scan buffer while it is rendering (switch buffers if you need to)
void RenderScanBufferToFrameBuffer(
    ScanBuffer *buf, // source scan buffer
    BYTE* data       // target frame-buffer (must match scanbuffer dimensions)
) {
    if (buf == nullptr || data == nullptr) return;

    for (int i = 0; i < buf->height; i++) {
        RenderScanLine(buf, i, data);
    }
}


#pragma clang diagnostic pop