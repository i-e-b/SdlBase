#include <types/MathBits.h>
#include <types/String.h>
#include <gui_core/ScanBufferFont.h>
#include "demo.h"

void log(ScanBuffer *scanBuf, String *line, int x, int y, int z, uint32_t color) {
    while (auto c = StringDequeue(line)) {
        AddGlyph(scanBuf, c, x, y, z, color);
        x += 8;
    }
}

bool RandomNumberTest(ScanBuffer *scanBuf) {
    log (scanBuf, nullptr, 0,0,0,0);
    random_at_most(255);
    return false;
}

bool RunTest(ScanBuffer *scanBuf, int index){
    switch (index) {
        case 0: return RandomNumberTest(scanBuf);

        default: return false;
    }
}