#pragma once

namespace vitality {

struct LineIndex {
    int value = 0;
};

struct ByteOffset {
    int value = 0;
};

struct ByteCount {
    int value = 0;
};

struct ByteColumn {
    int value = 0;
};

struct ByteRange {
    ByteOffset start;
    ByteCount length;
};

struct LineCount {
    int value = 0;
};

struct VisibleLineCount {
    int value = 0;
};

struct ByteCursorPos {
    LineIndex line;
    ByteColumn column;
};

}  // namespace vitality
