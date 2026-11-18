#pragma once

#include "core/Image.h"

#include <string>

namespace ie {
namespace clipboard {

void setImage(const Image& img);
bool getImage(Image& out);
bool hasImage();

void setText(const std::string& utf8);
bool getText(std::string& out);
bool hasText();

}
}
