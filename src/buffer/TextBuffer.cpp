#include "buffer/TextBuffer.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "buffer/BufferTypes.h"
#include "buffer/TextStorage.h"
#include "file/FilePath.h"
#include "unicode/UnicodeLineOps.h"

namespace vitality {

struct TextBuffer::Impl {
    TextStorage storage;
    std::optional<FilePath> file_path;
    std::string display_name;

    explicit Impl(TextStorage storage_to_use)
        : storage(std::move(storage_to_use)) {
    }
};

namespace {

[[nodiscard]] std::int64_t page_move_delta(const VisibleLineCount visible_lines) {
    return std::max<std::int64_t>(visible_lines.value - 1, 1);
}

[[nodiscard]] ByteCursorPos move_left_by_byte(const TextBuffer &buffer, const ByteCursorPos cursor) {
    const auto [line, column] = buffer.clamp_cursor(cursor);
    return ByteCursorPos{
        .line = line,
        .column = ByteColumn{std::max<std::int64_t>(column.value - 1, 0)},
    };
}

[[nodiscard]] ByteCursorPos move_right_by_byte(const TextBuffer &buffer, const ByteCursorPos cursor) {
    const auto [line, column] = buffer.clamp_cursor(cursor);
    const std::int64_t max_column = buffer.line_length(line).value;
    return ByteCursorPos{
        .line = line,
        .column = ByteColumn{std::min<std::int64_t>(column.value + 1, max_column)},
    };
}

[[nodiscard]] std::string read_stream_bytes(std::istream &input) {
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string normalize_file_newlines(const std::string_view bytes) {
    std::string normalized;
    normalized.reserve(bytes.size());

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const char ch = bytes[index];
        if (ch == '\r' && index + 1 < bytes.size() && bytes[index + 1] == '\n') {
            continue;
        }
        normalized.push_back(ch);
    }

    return normalized;
}

}  // namespace

TextBuffer TextBuffer::make_empty() {
    auto impl = std::make_unique<Impl>(TextStorage::make_empty());
    return TextBuffer(std::move(impl));
}

BufferLoadResult TextBuffer::load_from_path(const FilePath &path) {
    std::ifstream input(path.native_path(), std::ios::in);
    if (!input.is_open()) {
        return BufferLoadResult{
            .buffer = make_empty(),
            .success = false,
        };
    }

    const std::string file_bytes = read_stream_bytes(input);
    auto impl = std::make_unique<Impl>(TextStorage::from_utf8(normalize_file_newlines(file_bytes)));
    impl->file_path = path;
    impl->display_name = path.display_name();

    return BufferLoadResult{
        .buffer = TextBuffer(std::move(impl)),
        .success = true,
    };
}

TextBuffer::TextBuffer(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
}

TextBuffer::TextBuffer(TextBuffer &&other) noexcept = default;

TextBuffer &TextBuffer::operator=(TextBuffer &&other) noexcept = default;

TextBuffer::~TextBuffer() = default;

LineCount TextBuffer::line_count() const {
    return impl_->storage.line_count();
}

bool TextBuffer::has_file_path() const {
    return impl_->file_path.has_value();
}

std::string_view TextBuffer::display_name() const {
    return impl_->display_name;
}

LineText TextBuffer::line_text(const LineIndex line) const {
    return impl_->storage.line_text(line);
}

ByteColumn TextBuffer::line_length(const LineIndex line) const {
    return impl_->storage.line_length(line);
}

LogicalGraphemeCursorResult TextBuffer::logical_grapheme_cursor(const ByteCursorPos cursor) const {
    const auto aligned = align_cursor_to_grapheme_boundary(cursor);
    if (!aligned.success) {
        return LogicalGraphemeCursorResult{
            .success = false,
            .error = aligned.error,
        };
    }

    return LogicalGraphemeCursorResult{
        .cursor = LogicalGraphemeCursorPos{
            .line = aligned.cursor.line,
            .column = aligned.cursor.column,
        },
        .success = true,
        .error = UnicodeError::None,
    };
}

ByteColumnAlignmentResult TextBuffer::align_line_byte_column_to_code_point_boundary(
    const LineIndex line,
    const ByteColumn column) const {
    if (line.value < 0 || line.value >= line_count().value) {
        return ByteColumnAlignmentResult{
            .aligned_column = ByteColumn{},
            .success = false,
            .error = UnicodeError::None,
        };
    }

    return unicode::align_byte_column_to_code_point_boundary(line_text(line).utf8_text, column);
}

GraphemeBoundaryCursorResult TextBuffer::align_cursor_to_grapheme_boundary(const ByteCursorPos cursor) const {
    if (cursor.line.value < 0 || cursor.line.value >= line_count().value) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    const ByteCursorPos clamped = clamp_cursor(cursor);
    const LineIndex line = clamped.line;
    const auto aligned = unicode::align_byte_column_to_grapheme_boundary(
        line_text(line).utf8_text,
        clamped.column);
    if (!aligned.success) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = aligned.error,
        };
    }

    return GraphemeBoundaryCursorResult{
        .cursor = GraphemeBoundaryCursorPos{
            .line = line,
            .column = aligned.column,
        },
        .success = true,
        .error = UnicodeError::None,
    };
}

GraphemeBoundaryCursorResult TextBuffer::previous_grapheme_cursor(const ByteCursorPos cursor) const {
    if (cursor.line.value < 0 || cursor.line.value >= line_count().value) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    const ByteCursorPos clamped = clamp_cursor(cursor);
    const LineIndex line = clamped.line;
    const auto previous = unicode::previous_grapheme_boundary(
        line_text(line).utf8_text,
        clamped.column);
    if (!previous.success) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = previous.error,
        };
    }

    return GraphemeBoundaryCursorResult{
        .cursor = GraphemeBoundaryCursorPos{
            .line = line,
            .column = previous.column,
        },
        .success = true,
        .error = UnicodeError::None,
    };
}

GraphemeBoundaryCursorResult TextBuffer::next_grapheme_cursor(const ByteCursorPos cursor) const {
    if (cursor.line.value < 0 || cursor.line.value >= line_count().value) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    const ByteCursorPos clamped = clamp_cursor(cursor);
    const LineIndex line = clamped.line;
    const auto next = unicode::next_grapheme_boundary(
        line_text(line).utf8_text,
        clamped.column);
    if (!next.success) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = next.error,
        };
    }

    return GraphemeBoundaryCursorResult{
        .cursor = GraphemeBoundaryCursorPos{
            .line = line,
            .column = next.column,
        },
        .success = true,
        .error = UnicodeError::None,
    };
}

GraphemeColumnResult TextBuffer::display_column(const ByteCursorPos cursor) const {
    if (cursor.line.value < 0 || cursor.line.value >= line_count().value) {
        return GraphemeColumnResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    const ByteCursorPos clamped = clamp_cursor(cursor);
    return unicode::grapheme_column_at_byte_column(
        line_text(clamped.line).utf8_text,
        clamped.column);
}

GraphemeBoundaryCursorResult TextBuffer::cursor_for_display_column(
    const LineIndex line,
    const GraphemeColumn column) const {
    if (line.value < 0 || line.value >= line_count().value) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    const auto boundary = unicode::grapheme_boundary_for_display_column(
        line_text(line).utf8_text,
        column);
    if (!boundary.success) {
        return GraphemeBoundaryCursorResult{
            .success = false,
            .error = boundary.error,
        };
    }

    return GraphemeBoundaryCursorResult{
        .cursor = GraphemeBoundaryCursorPos{
            .line = line,
            .column = boundary.column,
        },
        .success = true,
        .error = UnicodeError::None,
    };
}

PreferredVisualColumn TextBuffer::preferred_column(const ByteCursorPos cursor) const {
    const auto column = display_column(cursor);
    if (column.success) {
        return PreferredVisualColumn{column.column.value};
    }

    return PreferredVisualColumn{clamp_cursor(cursor).column.value};
}

GraphemeBoundarySearchResult TextBuffer::previous_grapheme_boundary(
    const LineIndex line,
    const ByteColumn column) const {
    if (line.value < 0 || line.value >= line_count().value) {
        return GraphemeBoundarySearchResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    return unicode::previous_grapheme_boundary(line_text(line).utf8_text, column);
}

GraphemeBoundarySearchResult TextBuffer::next_grapheme_boundary(
    const LineIndex line,
    const ByteColumn column) const {
    if (line.value < 0 || line.value >= line_count().value) {
        return GraphemeBoundarySearchResult{
            .success = false,
            .error = UnicodeError::None,
        };
    }

    return unicode::next_grapheme_boundary(line_text(line).utf8_text, column);
}

ByteCursorPos TextBuffer::clamp_cursor(const ByteCursorPos cursor) const {
    return impl_->storage.clamp_cursor(cursor);
}

ByteCursorPos TextBuffer::move_left(const ByteCursorPos cursor) const {
    const auto previous = previous_grapheme_cursor(cursor);
    if (!previous.success) {
        return move_left_by_byte(*this, cursor);
    }

    return ByteCursorPos{
        .line = previous.cursor.line,
        .column = ByteColumn{previous.cursor.column.value},
    };
}

ByteCursorPos TextBuffer::move_right(const ByteCursorPos cursor) const {
    const auto next = next_grapheme_cursor(cursor);
    if (!next.success) {
        return move_right_by_byte(*this, cursor);
    }

    return ByteCursorPos{
        .line = next.cursor.line,
        .column = ByteColumn{next.cursor.column.value},
    };
}

ByteCursorPos TextBuffer::move_home(const ByteCursorPos cursor) const {
    const LineIndex line = clamp_cursor(cursor).line;
    return ByteCursorPos{
        .line = line,
        .column = ByteColumn{},
    };
}

ByteCursorPos TextBuffer::move_end(const ByteCursorPos cursor) const {
    const LineIndex line = clamp_cursor(cursor).line;
    return ByteCursorPos{
        .line = line,
        .column = line_length(line),
    };
}

}  // namespace vitality
