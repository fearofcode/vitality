#include <string>
#include <string_view>
#include <vector>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck/catch.h>
#include <rapidcheck/gen/Container.h>

#include "buffer/TextStorage.h"

namespace {

[[nodiscard]] std::vector<std::string> split_reference_lines(const std::string_view text) {
    std::vector<std::string> lines;
    if (text.empty()) {
        lines.emplace_back();
        return lines;
    }

    std::size_t line_start = 0;
    while (line_start < text.size()) {
        const std::size_t line_end = text.find('\n', line_start);
        const std::size_t end = (line_end == std::string_view::npos) ? text.size() : line_end;
        lines.emplace_back(text.substr(line_start, end - line_start));
        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (lines.empty()) {
        lines.emplace_back();
    }

    return lines;
}

[[nodiscard]] vitality::ByteCursorPos reference_clamp_cursor(
    const std::vector<std::string> &lines,
    const vitality::ByteCursorPos cursor) {
    const std::int64_t last_line = static_cast<std::int64_t>(lines.size()) - 1;
    const std::int64_t clamped_line = std::clamp<std::int64_t>(cursor.line.value, 0, last_line);
    const std::int64_t max_column = static_cast<std::int64_t>(lines[static_cast<std::size_t>(clamped_line)].size());
    const std::int64_t clamped_column = std::clamp<std::int64_t>(cursor.column.value, 0, max_column);
    return vitality::ByteCursorPos{
        .line = vitality::LineIndex{clamped_line},
        .column = vitality::ByteColumn{clamped_column},
    };
}

}  // namespace

TEST_CASE("TextStorage preserves exact bytes and matches a reference line model") {
    rc::prop("storage facts match the reference model", [] {
        const auto text = *rc::gen::container<std::string>(
            *rc::gen::inRange<std::size_t>(0, static_cast<std::size_t>(128)),
            rc::gen::arbitrary<char>());
        const auto reference_lines = split_reference_lines(text);
        const vitality::TextStorage storage = vitality::TextStorage::from_utf8(text);

        RC_ASSERT(storage.text() == text);
        RC_ASSERT(storage.line_count().value == static_cast<std::int64_t>(reference_lines.size()));
        RC_ASSERT(storage.check_invariants());

        for (std::int64_t index = 0; index < storage.line_count().value; ++index) {
            const auto line_text = storage.line_text(vitality::LineIndex{index});
            RC_ASSERT(line_text.utf8_text == reference_lines[static_cast<std::size_t>(index)]);
            RC_ASSERT(storage.line_length(vitality::LineIndex{index}).value ==
                      static_cast<std::int64_t>(reference_lines[static_cast<std::size_t>(index)].size()));
        }
    });
}

TEST_CASE("TextStorage clamp_cursor matches the reference model and is idempotent") {
    rc::prop("cursor clamping is valid and stable", [] {
        const auto text = *rc::gen::container<std::string>(
            *rc::gen::inRange<std::size_t>(0, static_cast<std::size_t>(128)),
            rc::gen::arbitrary<char>());
        const auto reference_lines = split_reference_lines(text);
        const vitality::TextStorage storage = vitality::TextStorage::from_utf8(text);
        const vitality::ByteCursorPos cursor{
            .line = vitality::LineIndex{static_cast<std::int64_t>(*rc::gen::arbitrary<int>())},
            .column = vitality::ByteColumn{static_cast<std::int64_t>(*rc::gen::arbitrary<int>())},
        };

        const vitality::ByteCursorPos first = storage.clamp_cursor(cursor);
        const vitality::ByteCursorPos second = storage.clamp_cursor(first);
        const vitality::ByteCursorPos expected = reference_clamp_cursor(reference_lines, cursor);

        RC_ASSERT(first.line.value == expected.line.value);
        RC_ASSERT(first.column.value == expected.column.value);
        RC_ASSERT(second.line.value == first.line.value);
        RC_ASSERT(second.column.value == first.column.value);
    });
}
