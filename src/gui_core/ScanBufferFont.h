#pragma once

#ifndef ScanBufferFont_h
#define ScanBufferFont_h

#include "ScanBufferDraw.h"

// This provides a *VERY* minimal glyph-drawing function, with a single fixed 8x8 font
// You would probably use this for consoles or diagnostics

// Write a glyph at the given position (y is baseline)
void AddGlyph(ScanBuffer *buf, char c, int x, int y, int objectId);

#endif
