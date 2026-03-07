#include "buffer/TextBuffer.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "buffer/BufferTypes.h"
#include "buffer/TextStorage.h"
#include "file/FilePath.h"

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

[[nodiscard]] int page_move_delta(const VisibleLineCount visible_lines) {
    return std::max(visible_lines.value - 1, 1);
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

    auto impl = std::make_unique<Impl>(TextStorage::load_from_stream(input));
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

LineTextView TextBuffer::line_text(const LineIndex line) const {
    return impl_->storage.line_text(line);
}

ColumnIndex TextBuffer::line_length(const LineIndex line) const {
    return impl_->storage.line_length(line);
}

CursorPos TextBuffer::clamp_cursor(const CursorPos cursor) const {
    return impl_->storage.clamp_cursor(cursor);
}

CursorPos TextBuffer::move_left(const CursorPos cursor) const {
    const auto [line, column] = clamp_cursor(cursor);
    return CursorPos{
        .line = line,
        .column = ColumnIndex{std::max(column.value - 1, 0)},
    };
}

CursorPos TextBuffer::move_right(const CursorPos cursor) const {
    const auto [line, column] = clamp_cursor(cursor);
    const int max_column = line_length(line).value;
    return CursorPos{
        .line = line,
        .column = ColumnIndex{std::min(column.value + 1, max_column)},
    };
}

CursorPos TextBuffer::move_up(const CursorPos cursor) const {
    const auto [line, column] = clamp_cursor(cursor);
    const int target_line = std::max(line.value - 1, 0);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = column,
    });
}

CursorPos TextBuffer::move_down(const CursorPos cursor) const {
    const auto [line, column] = clamp_cursor(cursor);
    const int last_line = line_count().value - 1;
    const int target_line = std::min(line.value + 1, last_line);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = column,
    });
}

CursorPos TextBuffer::move_page_up(const CursorPos cursor, const VisibleLineCount visible_lines) const {
    const auto [line, column] = clamp_cursor(cursor);
    const int target_line = std::max(line.value - page_move_delta(visible_lines), 0);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = column,
    });
}

CursorPos TextBuffer::move_page_down(const CursorPos cursor, const VisibleLineCount visible_lines) const {
    const auto [line, column] = clamp_cursor(cursor);
    const int last_line = line_count().value - 1;
    const int target_line = std::min(line.value + page_move_delta(visible_lines), last_line);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = column,
    });
}

CursorPos TextBuffer::move_home(const CursorPos cursor) const {
    const auto [line, column] = clamp_cursor(cursor);
    return CursorPos{
        .line = line,
        .column = ColumnIndex{},
    };
}

CursorPos TextBuffer::move_end(const CursorPos cursor) const {
    const auto [line, column] = clamp_cursor(cursor);
    return CursorPos{
        .line = line,
        .column = line_length(line),
    };
}

}  // namespace vitality
