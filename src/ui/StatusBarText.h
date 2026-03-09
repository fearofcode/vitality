#pragma once

#include <QString>

#include "buffer/TextBuffer.h"
#include "core/CoreTypes.h"

namespace vitality {

// The status bar prefers a logical grapheme-based display column when Unicode
// queries succeed. Stage 7 keeps this logical column model even though
// Left/Right/Home/End become visual on bidi lines.
[[nodiscard]] QString make_status_bar_text(const TextBuffer &buffer, ByteCursorPos cursor);

}  // namespace vitality
