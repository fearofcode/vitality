#pragma once

namespace vitality {

struct LineIndex {
    int value = 0;
};

struct ColumnIndex {
    int value = 0;
};

struct LineCount {
    int value = 0;
};

struct VisibleLineCount {
    int value = 0;
};

struct CursorPos {
    LineIndex line;
    ColumnIndex column;
};

}  // namespace vitality
