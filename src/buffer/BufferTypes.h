#pragma once

#include <string_view>

#include "buffer/TextBuffer.h"

namespace vitality {

struct LineTextView {
    std::string_view utf8_text;
};

struct BufferLoadResult {
    TextBuffer buffer;
    bool success = false;
};

}  // namespace vitality
