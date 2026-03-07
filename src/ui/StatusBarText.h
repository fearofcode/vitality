#pragma once

#include <QString>

#include "buffer/TextBuffer.h"
#include "core/CoreTypes.h"

namespace vitality {

[[nodiscard]] QString make_status_bar_text(const TextBuffer &buffer, CursorPos cursor);

}  // namespace vitality
