#pragma once

#include <memory>
#include <string_view>

#include "core/CoreTypes.h"

namespace vitality {

class FilePath;
struct LineText;
struct BufferLoadResult;
class TextStorage;

class TextBuffer {
public:
    [[nodiscard]] static TextBuffer make_empty();
    [[nodiscard]] static BufferLoadResult load_from_path(const FilePath &path);

    TextBuffer(TextBuffer &&other) noexcept;
    TextBuffer &operator=(TextBuffer &&other) noexcept;
    TextBuffer(const TextBuffer &) = delete;
    TextBuffer &operator=(const TextBuffer &) = delete;
    ~TextBuffer();

    [[nodiscard]] LineCount line_count() const;
    [[nodiscard]] bool has_file_path() const;
    [[nodiscard]] std::string_view display_name() const;
    [[nodiscard]] LineText line_text(LineIndex line) const;
    [[nodiscard]] ByteColumn line_length(LineIndex line) const;

    [[nodiscard]] ByteCursorPos clamp_cursor(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_left(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_right(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_up(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_down(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_page_up(ByteCursorPos cursor, VisibleLineCount visible_lines) const;
    [[nodiscard]] ByteCursorPos move_page_down(ByteCursorPos cursor, VisibleLineCount visible_lines) const;
    [[nodiscard]] ByteCursorPos move_home(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_end(ByteCursorPos cursor) const;

private:
    struct Impl;

    explicit TextBuffer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace vitality
