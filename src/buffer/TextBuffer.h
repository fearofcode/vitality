#pragma once

#include <memory>
#include <string_view>

#include "buffer/LineTextView.h"
#include "core/CoreTypes.h"

namespace vitality {

struct BufferLoadResult;
class FilePath;

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
    [[nodiscard]] LineTextView line_text(LineIndex line) const;
    [[nodiscard]] ColumnIndex line_length(LineIndex line) const;

    [[nodiscard]] CursorPos clamp_cursor(CursorPos cursor) const;
    [[nodiscard]] CursorPos move_left(CursorPos cursor) const;
    [[nodiscard]] CursorPos move_right(CursorPos cursor) const;
    [[nodiscard]] CursorPos move_up(CursorPos cursor) const;
    [[nodiscard]] CursorPos move_down(CursorPos cursor) const;
    [[nodiscard]] CursorPos move_page_up(CursorPos cursor, VisibleLineCount visible_lines) const;
    [[nodiscard]] CursorPos move_page_down(CursorPos cursor, VisibleLineCount visible_lines) const;
    [[nodiscard]] CursorPos move_home(CursorPos cursor) const;
    [[nodiscard]] CursorPos move_end(CursorPos cursor) const;

private:
    struct Impl;

    explicit TextBuffer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace vitality
