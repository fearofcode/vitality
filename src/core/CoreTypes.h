#pragma once

#include <cstdint>

namespace vitality {

struct LineIndex {
    std::int64_t value = 0;
};

struct ByteOffset {
    std::int64_t value = 0;
};

struct ByteCount {
    std::int64_t value = 0;
};

struct ByteColumn {
    std::int64_t value = 0;
};

struct ByteRange {
    ByteOffset start;
    ByteCount length;
};

struct LineCount {
    std::int64_t value = 0;
};

struct VisibleLineCount {
    std::int64_t value = 0;
};

struct ByteCursorPos {
    LineIndex line;
    ByteColumn column;
};

}  // namespace vitality
