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

    [[nodiscard]] ByteCount byte_count() const;
    [[nodiscard]] LineCount line_count() const;
    [[nodiscard]] LineText line_text(LineIndex line) const;
    [[nodiscard]] ByteColumn line_length(LineIndex line) const;
    // Cursor construction remains byte-based in Stage 2. These helpers keep
    // that fact explicit until stronger cursor types start being produced.
    [[nodiscard]] ByteColumn clamp_line_byte_column(LineIndex line, ByteColumn column) const;
    [[nodiscard]] ByteCursorPos clamp_cursor(ByteCursorPos cursor) const;
    [[nodiscard]] std::string text() const;
    [[nodiscard]] bool check_invariants() const;
    [[nodiscard]] InsertTextResult insert(ByteOffset offset, std::string_view utf8_text);
    [[nodiscard]] EraseTextResult erase(ByteRange range);

private:
    struct Impl;

    explicit TextStorage(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace vitality
