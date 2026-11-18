#pragma once

#include "tools/Tool.h"

#include <memory>
#include <vector>

namespace ie {

std::unique_ptr<Tool> makeMoveTool();
std::unique_ptr<Tool> makeSelectTool();
std::unique_ptr<Tool> makeLassoTool();
std::unique_ptr<Tool> makeWandTool();
std::unique_ptr<Tool> makeCropTool();
std::unique_ptr<Tool> makeBrushTool();
std::unique_ptr<Tool> makeBucketTool();
std::unique_ptr<Tool> makeTextTool();
std::unique_ptr<Tool> makeShapeTool();
std::unique_ptr<Tool> makeEraserTool();
std::unique_ptr<Tool> makeGradientTool();
std::unique_ptr<Tool> makeMaskTool();


std::vector<std::unique_ptr<Tool>> createTools();

}
