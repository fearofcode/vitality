#pragma once

#include <istream>
#include <memory>
#include <string_view>

#include "buffer/BufferTypes.h"
#include "core/CoreTypes.h"

namespace vitality {

class TextStorage {
public:
    [[nodiscard]] static TextStorage make_empty();
    [[nodiscard]] static TextStorage from_utf8(std::string_view utf8_text);
    [[nodiscard]] static TextStorage load_from_stream(std::istream &input);

    TextStorage(TextStorage &&other) noexcept;
    TextStorage &operator=(TextStorage &&other) noexcept;
    TextStorage(const TextStorage &) = delete;
    TextStorage &operator=(const TextStorage &) = delete;
    ~TextStorage();

    [[nodiscard]] LineCount line_count() const;
    [[nodiscard]] LineTextView line_text(LineIndex line) const;
    [[nodiscard]] ColumnIndex line_length(LineIndex line) const;
    [[nodiscard]] CursorPos clamp_cursor(CursorPos cursor) const;

private:
    struct Impl;

    explicit TextStorage(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace vitality
