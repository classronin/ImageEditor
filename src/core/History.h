#pragma once

#include "core/Document.h"
#include "core/Selection.h"

#include <string>
#include <vector>

namespace ie {

class History {
public:
    struct State {
        int w = 0, h = 0;
        std::vector<Layer> layers;
        int active = -1;
        Selection sel;
    };

    void reset(const Document& doc);
    void record(const Document& doc, const Selection& sel, std::string label);
    void beginMerge();
    void endMerge();
    bool merging() const { return mergeDepth_ > 0; }
    bool undo(Document& doc, Selection& sel);
    bool redo(Document& doc, Selection& sel);
    bool dropLast(Document& doc, Selection& sel);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    const std::string& undoLabel() const;
    const std::string& redoLabel() const;

    void setLimit(size_t n) { limit_ = n; }

private:
    std::vector<State> undo_, redo_;
    std::vector<std::string> undoLabels_, redoLabels_;
    size_t limit_ = 48;
    int mergeDepth_ = 0;
    bool mergedAny_ = false;

    static State capture(const Document& doc, const Selection& sel);
    static void restore(Document& doc, Selection& sel, const State& s);
};

}
