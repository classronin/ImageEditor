#pragma once

#include "core/Image.h"
#include "core/Types.h"

namespace ie {
namespace shadow {

struct Params {
    float strength = 0.0f;
    float radius = 0.0f;
    float sensitivity = 0.5f;
    float softness = 0.0f;
    float maxGain = 3.0f;
};

void removeShadow(Image& img, Rect roi, const Params& p);

}
}
