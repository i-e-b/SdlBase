#include <types/MathBits.h>
#include <types/String.h>
#include <gui_core/ScanBufferFont.h>
#include "demo.h"

void log(DrawTarget *draw, String *line, int x, int y, int z, uint32_t color) {
    auto objectId = SetSingleColorMaterial(draw->textures, z, color);
    while (auto c = StringDequeue(line)) {
        AddGlyph(draw->scanBuffer, c, x, y, objectId);
        x += 8;
    }
}

bool RandomNumberTest(DrawTarget *draw) {
    log (draw, nullptr, 0,0,0,0);
    random_at_most(255);
    return false;
}

bool RunTest(DrawTarget *draw, int index){
    switch (index) {
        case 0: return RandomNumberTest(draw);

        default: return false;
    }
}