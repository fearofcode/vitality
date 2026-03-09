#pragma once

#include <memory>
#include <string_view>

#include "core/CoreTypes.h"
#include "core/PositionConversions.h"

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

    [[nodiscard]] LogicalGraphemeCursorResult logical_grapheme_cursor(ByteCursorPos cursor) const;
    [[nodiscard]] ByteColumnAlignmentResult align_line_byte_column_to_code_point_boundary(
        LineIndex line,
        ByteColumn column) const;
    [[nodiscard]] GraphemeBoundaryCursorResult align_cursor_to_grapheme_boundary(
        ByteCursorPos cursor) const;
    [[nodiscard]] GraphemeBoundaryCursorResult previous_grapheme_cursor(
        ByteCursorPos cursor) const;
    [[nodiscard]] GraphemeBoundaryCursorResult next_grapheme_cursor(
        ByteCursorPos cursor) const;
    [[nodiscard]] GraphemeColumnResult display_column(ByteCursorPos cursor) const;
    [[nodiscard]] GraphemeBoundaryCursorResult cursor_for_display_column(
        LineIndex line,
        GraphemeColumn column) const;
    [[nodiscard]] PreferredVisualColumn preferred_column(ByteCursorPos cursor) const;
    [[nodiscard]] GraphemeBoundarySearchResult previous_grapheme_boundary(
        LineIndex line,
        ByteColumn column) const;
    [[nodiscard]] GraphemeBoundarySearchResult next_grapheme_boundary(
        LineIndex line,
        ByteColumn column) const;
    [[nodiscard]] ByteCursorPos clamp_cursor(ByteCursorPos cursor) const;
    // These movement helpers remain logical/document-order helpers. Stage 7
    // bidi-aware visual keyboard behavior is derived above the buffer in the
    // layout service plus editor key-handling path.
    [[nodiscard]] ByteCursorPos move_left(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_right(ByteCursorPos cursor) const;
    // Home and End remain logical compatibility helpers. The editor's actual
    // keyboard path may use richer layout-aware behavior above the buffer, but
    // these still provide the logical line-start and line-end fallbacks.
    [[nodiscard]] ByteCursorPos move_home(ByteCursorPos cursor) const;
    [[nodiscard]] ByteCursorPos move_end(ByteCursorPos cursor) const;

private:
    struct Impl;

    explicit TextBuffer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace vitality
