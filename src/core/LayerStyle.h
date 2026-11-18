#pragma once

#include "core/Document.h"

namespace ie {

inline constexpr float kStyleStrokeWidthMax = 50.0f;
inline constexpr float kStyleShadowBlurMax = 50.0f;
inline constexpr float kStyleShadowDistMax = 100.0f;

LayerStyle clampStyleLimits(const LayerStyle& s);

bool styleActive(const LayerStyle& s);

int stylePad(const LayerStyle& s);

const Image& styledPixels(const Layer& l, int& ox, int& oy);

long textStyleHits();

}
