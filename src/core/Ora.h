#pragma once

#include "core/Document.h"

#include <string>

namespace ie {
namespace ora {

bool save(const std::string& path, Document& doc);
bool load(const std::string& path, Document& doc);
std::string lastError();

}
}
