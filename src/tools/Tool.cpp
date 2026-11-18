#include "tools/Tools.h"

namespace ie {

std::vector<std::unique_ptr<Tool>> createTools()
{
    std::vector<std::unique_ptr<Tool>> v;
    v.push_back(makeMoveTool());
    v.push_back(makeSelectTool());
    v.push_back(makeLassoTool());
    v.push_back(makeWandTool());
    v.push_back(makeCropTool());
    v.push_back(makeBrushTool());
    v.push_back(makeEraserTool());
    v.push_back(makeMaskTool());
    v.push_back(makeBucketTool());
    v.push_back(makeGradientTool());
    v.push_back(makeTextTool());
    v.push_back(makeShapeTool());
    return v;
}

}
