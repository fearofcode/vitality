#include "buffer/TextStorage.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace vitality {

struct TextStorage::Impl {
    std::vector<std::string> lines;
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

[[nodiscard]] std::vector<std::string> split_utf8_lines(const std::string_view utf8_text) {
    std::vector<std::string> lines;
    if (utf8_text.empty()) {
        return ensure_non_empty_lines(std::move(lines));
    }

    std::size_t line_start = 0;

    while (line_start < utf8_text.size()) {
        const std::size_t line_end = utf8_text.find('\n', line_start);
        const std::size_t raw_length = (line_end == std::string_view::npos)
            ? utf8_text.size() - line_start
            : line_end - line_start;
        const bool has_trailing_carriage_return =
            raw_length > 0 && utf8_text[line_start + raw_length - 1] == '\r';
        const std::size_t length = has_trailing_carriage_return ? raw_length - 1 : raw_length;

        lines.emplace_back(utf8_text.substr(line_start, length));

        if (line_end == std::string_view::npos) {
            break;
        }

        line_start = line_end + 1;
    }

    return ensure_non_empty_lines(std::move(lines));
}

}  // namespace

TextStorage TextStorage::make_empty() {
    auto impl = std::make_unique<Impl>();
    impl->lines.emplace_back();
    return TextStorage(std::move(impl));
}

TextStorage TextStorage::from_utf8(const std::string_view utf8_text) {
    auto impl = std::make_unique<Impl>();
    impl->lines = split_utf8_lines(utf8_text);
    return TextStorage(std::move(impl));
}

TextStorage TextStorage::load_from_stream(std::istream &input) {
    std::string utf8_text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return from_utf8(utf8_text);
}

TextStorage::TextStorage(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
}

TextStorage::TextStorage(TextStorage &&other) noexcept = default;

TextStorage &TextStorage::operator=(TextStorage &&other) noexcept = default;

TextStorage::~TextStorage() = default;

LineCount TextStorage::line_count() const {
    return LineCount{static_cast<int>(impl_->lines.size())};
}

LineTextView TextStorage::line_text(const LineIndex line) const {
    if (line.value < 0 || line.value >= static_cast<int>(impl_->lines.size())) {
        return LineTextView{.utf8_text = std::string_view()};
    }

    return LineTextView{.utf8_text = impl_->lines[line.value]};
}

ColumnIndex TextStorage::line_length(const LineIndex line) const {
    if (line.value < 0 || line.value >= static_cast<int>(impl_->lines.size())) {
        return ColumnIndex{};
    }

    return ColumnIndex{static_cast<int>(impl_->lines[line.value].size())};
}

CursorPos TextStorage::clamp_cursor(const CursorPos cursor) const {
    const int last_line_index = static_cast<int>(impl_->lines.size()) - 1;
    const int clamped_line = clamp_int(cursor.line.value, 0, last_line_index);
    const int max_column = static_cast<int>(impl_->lines[clamped_line].size());
    const int clamped_column = clamp_int(cursor.column.value, 0, max_column);

    return CursorPos{
        .line = LineIndex{clamped_line},
        .column = ColumnIndex{clamped_column},
    };
}

}  // namespace vitality
