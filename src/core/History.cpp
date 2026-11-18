#include "core/History.h"

namespace ie {

History::State History::capture(const Document& doc, const Selection& sel)
{
    State s;
    s.w = doc.width;
    s.h = doc.height;
    s.layers = doc.layers;
    s.active = doc.active;
    s.sel = sel;
    return s;
}

void History::restore(Document& doc, Selection& sel, const State& s)
{
    doc.width = s.w;
    doc.height = s.h;
    doc.layers = s.layers;
    doc.active = s.active;
    doc.selIds.clear();
    sel = s.sel;
    doc.clampLayersState();
    doc.touchStructure();
    doc.markAllDirty();
}

void History::reset(const Document& doc)
{
    undo_.clear();
    redo_.clear();
    undoLabels_.clear();
    redoLabels_.clear();
    mergeDepth_ = 0;
    mergedAny_ = false;
    (void)doc;
}

void History::beginMerge()
{
    if (mergeDepth_++ == 0) mergedAny_ = false;
}

void History::endMerge()
{
    if (mergeDepth_ > 0 && --mergeDepth_ == 0) mergedAny_ = false;
}

void History::record(const Document& doc, const Selection& sel, std::string label)
{
    if (!doc.valid()) return;
    if (mergeDepth_ > 0) {
        if (mergedAny_) return;
        mergedAny_ = true;
    }
    undo_.push_back(capture(doc, sel));
    undoLabels_.push_back(std::move(label));
    redo_.clear();
    redoLabels_.clear();
    while (undo_.size() > limit_) {
        undo_.erase(undo_.begin());
        undoLabels_.erase(undoLabels_.begin());
    }
}

bool History::undo(Document& doc, Selection& sel)
{
    mergeDepth_ = 0;
    mergedAny_ = false;
    if (undo_.empty()) return false;
    redo_.push_back(capture(doc, sel));
    redoLabels_.push_back(undoLabels_.back());

    State s = std::move(undo_.back());
    undo_.pop_back();
    undoLabels_.pop_back();
    restore(doc, sel, s);
    return true;
}

bool History::redo(Document& doc, Selection& sel)
{
    mergeDepth_ = 0;
    mergedAny_ = false;
    if (redo_.empty()) return false;
    undo_.push_back(capture(doc, sel));
    undoLabels_.push_back(redoLabels_.back());

    State s = std::move(redo_.back());
    redo_.pop_back();
    redoLabels_.pop_back();
    restore(doc, sel, s);
    return true;
}

bool History::dropLast(Document& doc, Selection& sel)
{
    mergeDepth_ = 0;
    mergedAny_ = false;
    if (undo_.empty()) return false;
    State s = std::move(undo_.back());
    undo_.pop_back();
    undoLabels_.pop_back();
    restore(doc, sel, s);
    return true;
}

const std::string& History::undoLabel() const
{
    static const std::string kEmpty;
    return undo_.empty() ? kEmpty : undoLabels_.back();
}

const std::string& History::redoLabel() const
{
    static const std::string kEmpty;
    return redo_.empty() ? kEmpty : redoLabels_.back();
}

}
