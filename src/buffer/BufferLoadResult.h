#pragma once

#include "buffer/TextBuffer.h"

namespace vitality {

struct BufferLoadResult {
    TextBuffer buffer;
    bool success = false;
};

}  // namespace vitality
