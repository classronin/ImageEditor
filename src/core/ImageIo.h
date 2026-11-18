#pragma once

#include "core/Image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ie {
namespace imageio {

struct SaveOptions {
    int quality = 92;
    int compression = 6;
    int effort = 4;
    bool lossless = false;
};

bool init();
std::string lastError();

bool load(const std::string& path, Image& out);
bool save(const std::string& path, const Image& img, const SaveOptions& opt);
inline bool save(const std::string& path, const Image& img, int quality = 92)
{
    SaveOptions o;
    o.quality = quality;
    return save(path, img, o);
}

bool readBytes(const std::string& path, std::vector<uint8_t>& bytes);
bool writeBytes(const std::string& path, const std::vector<uint8_t>& bytes);
bool decode(const std::vector<uint8_t>& bytes, Image& out);
bool encodePng(const Image& img, std::vector<uint8_t>& out, int compression = 6);

bool exists(const std::string& path);

}
}
