#pragma once

#include <string>

#include "buffer/TextBuffer.h"

namespace vitality {

// LineText is the owned line-text result that crosses the public buffer
// boundary. It carries UTF-8 bytes for one logical line without promising
// anything about the internal storage representation used to produce them.
struct LineText {
    std::string utf8_text;
};

// InsertTextResult reports the absolute byte range that was inserted into the
// document when a storage insert succeeds. The range is expressed in document
// byte coordinates after the insert request has been accepted.
struct InsertTextResult {
    ByteRange inserted_range;
    bool success = false;
};

// EraseTextResult reports the absolute byte range that was removed from the
// document when a storage erase succeeds. The range describes what the caller
// asked to erase, not a remaining position after the mutation.
struct EraseTextResult {
    ByteRange erased_range;
    bool success = false;
};

// BufferLoadResult is the file-loading boundary result for TextBuffer. It
// returns a usable buffer even on failure so the application can fall back to
// an empty editor surface without crashing.
struct BufferLoadResult {
    TextBuffer buffer;
    bool success = false;
};

}  // namespace vitality
