#pragma once

#include <string>

#include "buffer/TextBuffer.h"

namespace vitality {

struct LineText {
    std::string utf8_text;
};

struct InsertTextResult {
    ByteRange inserted_range;
    bool success = false;
};

struct EraseTextResult {
    ByteRange erased_range;
    bool success = false;
};

struct BufferLoadResult {
    TextBuffer buffer;
    bool success = false;
};

}  // namespace vitality
