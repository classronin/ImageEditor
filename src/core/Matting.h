#pragma once

#include "core/Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ie {

std::vector<uint8_t> maskToTrimap(const std::vector<uint8_t>& mask, int w, int h, int band);

bool closedFormAlpha(const Image& img, const uint8_t* trimap, std::vector<float>& out, std::string* err);

void decontaminateForeground(Image& img, const std::vector<float>& alpha, const uint8_t* trimap);

}
