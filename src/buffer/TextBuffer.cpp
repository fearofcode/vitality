#include "buffer/TextBuffer.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "buffer/BufferLoadResult.h"
#include "file/FilePath.h"

namespace vitality {

struct TextBuffer::Impl {
    std::vector<std::string> lines;
    std::optional<FilePath> file_path;
    std::string display_name;
};

namespace {

[[nodiscard]] int clamp_int(const int value, const int minimum, const int maximum) {
    return std::clamp(value, minimum, maximum);
}

[[nodiscard]] std::vector<std::string> ensure_non_empty_lines(std::vector<std::string> lines) {
    if (lines.empty()) {
        lines.emplace_back();
    }

    return lines;
}

[[nodiscard]] CursorPos clamp_cursor_to_lines(const std::vector<std::string> &lines, CursorPos cursor) {
    const int last_line_index = static_cast<int>(lines.size()) - 1;
    const int clamped_line = clamp_int(cursor.line.value, 0, last_line_index);
    const int max_column = static_cast<int>(lines[clamped_line].size());
    const int clamped_column = clamp_int(cursor.column.value, 0, max_column);

    return CursorPos{
        .line = LineIndex{clamped_line},
        .column = ColumnIndex{clamped_column},
    };
}

[[nodiscard]] int page_move_delta(const VisibleLineCount visible_lines) {
    return std::max(visible_lines.value - 1, 1);
}

}  // namespace

TextBuffer TextBuffer::make_empty() {
    auto impl = std::make_unique<Impl>();
    impl->lines.emplace_back();
    return TextBuffer(std::move(impl));
}

BufferLoadResult TextBuffer::load_from_path(const FilePath &path) {
    std::ifstream input(path.native_path(), std::ios::in);
    if (!input.is_open()) {
        return BufferLoadResult{
            .buffer = TextBuffer::make_empty(),
            .success = false,
        };
    }

    auto impl = std::make_unique<Impl>();
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        impl->lines.push_back(line);
    }

    impl->lines = ensure_non_empty_lines(std::move(impl->lines));
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
    return LineCount{static_cast<int>(impl_->lines.size())};
}

bool TextBuffer::has_file_path() const {
    return impl_->file_path.has_value();
}

std::string_view TextBuffer::display_name() const {
    return impl_->display_name;
}

LineTextView TextBuffer::line_text(const LineIndex line) const {
    if (line.value < 0 || line.value >= static_cast<int>(impl_->lines.size())) {
        return LineTextView{.utf8_text = std::string_view()};
    }

    return LineTextView{.utf8_text = impl_->lines[line.value]};
}

ColumnIndex TextBuffer::line_length(const LineIndex line) const {
    if (line.value < 0 || line.value >= static_cast<int>(impl_->lines.size())) {
        return ColumnIndex{0};
    }

    return ColumnIndex{static_cast<int>(impl_->lines[line.value].size())};
}

CursorPos TextBuffer::clamp_cursor(const CursorPos cursor) const {
    return clamp_cursor_to_lines(impl_->lines, cursor);
}

CursorPos TextBuffer::move_left(const CursorPos cursor) const {
    const CursorPos clamped = clamp_cursor(cursor);
    return CursorPos{
        .line = clamped.line,
        .column = ColumnIndex{std::max(clamped.column.value - 1, 0)},
    };
}

CursorPos TextBuffer::move_right(const CursorPos cursor) const {
    const CursorPos clamped = clamp_cursor(cursor);
    const int max_column = line_length(clamped.line).value;
    return CursorPos{
        .line = clamped.line,
        .column = ColumnIndex{std::min(clamped.column.value + 1, max_column)},
    };
}

CursorPos TextBuffer::move_up(const CursorPos cursor) const {
    const CursorPos clamped = clamp_cursor(cursor);
    const int target_line = std::max(clamped.line.value - 1, 0);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = clamped.column,
    });
}

CursorPos TextBuffer::move_down(const CursorPos cursor) const {
    const CursorPos clamped = clamp_cursor(cursor);
    const int last_line = line_count().value - 1;
    const int target_line = std::min(clamped.line.value + 1, last_line);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = clamped.column,
    });
}

CursorPos TextBuffer::move_page_up(const CursorPos cursor, const VisibleLineCount visible_lines) const {
    const CursorPos clamped = clamp_cursor(cursor);
    const int target_line = std::max(clamped.line.value - page_move_delta(visible_lines), 0);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = clamped.column,
    });
}

CursorPos TextBuffer::move_page_down(const CursorPos cursor, const VisibleLineCount visible_lines) const {
    const CursorPos clamped = clamp_cursor(cursor);
    const int last_line = line_count().value - 1;
    const int target_line = std::min(clamped.line.value + page_move_delta(visible_lines), last_line);
    return clamp_cursor(CursorPos{
        .line = LineIndex{target_line},
        .column = clamped.column,
    });
}

CursorPos TextBuffer::move_home(const CursorPos cursor) const {
    const CursorPos clamped = clamp_cursor(cursor);
    return CursorPos{
        .line = clamped.line,
        .column = ColumnIndex{0},
    };
}

CursorPos TextBuffer::move_end(const CursorPos cursor) const {
    const CursorPos clamped = clamp_cursor(cursor);
    return CursorPos{
        .line = clamped.line,
        .column = line_length(clamped.line),
    };
}

}  // namespace vitality
