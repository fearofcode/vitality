#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck/catch.h>
#include <rapidcheck/gen/Container.h>
#include <rapidcheck/gen/Text.h>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "file/FilePath.h"
#include "unicode/UnicodeLineOps.h"

namespace {

class TempFile {
public:
    explicit TempFile(const std::vector<std::string> &lines)
        : path_(std::filesystem::temp_directory_path() / make_name()) {
        std::ofstream output(path_, std::ios::out | std::ios::trunc);
        for (std::size_t index = 0; index < lines.size(); ++index) {
            output << lines[index];
            if (index + 1 < lines.size()) {
                output << '\n';
            }
        }
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path &path() const {
        return path_;
    }

private:
    [[nodiscard]] static std::string make_name() {
        static int counter = 0;
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        return "vitality-property-" + std::to_string(seed) + "-" + std::to_string(counter++) + ".txt";
    }

    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::string> sanitize_lines(std::vector<std::string> lines) {
    for (std::string &line : lines) {
        for (char &ch : line) {
            if (ch == '\n' || ch == '\r') {
                ch = ' ';
            }
        }
    }

    if (lines.empty()) {
        lines.emplace_back();
    }

    return lines;
}

[[nodiscard]] vitality::TextBuffer load_buffer_from_lines(const std::vector<std::string> &lines) {
    TempFile temp_file(lines);
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());
    auto load_result = vitality::TextBuffer::load_from_path(file_path);
    RC_ASSERT(load_result.success);
    return std::move(load_result.buffer);
}

[[nodiscard]] vitality::ByteCursorPos make_cursor(const vitality::TextBuffer &buffer, const int line, const int column) {
    return buffer.clamp_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{line},
        .column = vitality::ByteColumn{column},
    });
}

[[nodiscard]] bool is_valid_cursor(const vitality::TextBuffer &buffer, const vitality::ByteCursorPos cursor) {
    if (cursor.line.value < 0 || cursor.line.value >= buffer.line_count().value) {
        return false;
    }

    const std::int64_t line_length = buffer.line_length(cursor.line).value;
    return cursor.column.value >= 0 && cursor.column.value <= line_length;
}

[[nodiscard]] vitality::ByteCursorPos move_left_reference_by_byte(
    const vitality::TextBuffer &buffer,
    const vitality::ByteCursorPos cursor) {
    const vitality::ByteCursorPos clamped = buffer.clamp_cursor(cursor);
    return vitality::ByteCursorPos{
        .line = clamped.line,
        .column = vitality::ByteColumn{std::max<std::int64_t>(clamped.column.value - 1, 0)},
    };
}

[[nodiscard]] vitality::ByteCursorPos move_right_reference_by_byte(
    const vitality::TextBuffer &buffer,
    const vitality::ByteCursorPos cursor) {
    const vitality::ByteCursorPos clamped = buffer.clamp_cursor(cursor);
    const std::int64_t max_column = buffer.line_length(clamped.line).value;
    return vitality::ByteCursorPos{
        .line = clamped.line,
        .column = vitality::ByteColumn{std::min<std::int64_t>(clamped.column.value + 1, max_column)},
    };
}

}  // namespace

TEST_CASE("navigation methods always return valid cursors") {
    rc::prop("movement stays inside the buffer", [] {
        auto raw_lines = *rc::gen::container<std::vector<std::string>>(
            *rc::gen::inRange<std::size_t>(1, static_cast<std::size_t>(8)),
            rc::gen::string<std::string>());
        raw_lines = sanitize_lines(std::move(raw_lines));

        const vitality::TextBuffer buffer = load_buffer_from_lines(raw_lines);
        const int raw_line = *rc::gen::arbitrary<int>();
        const int raw_column = *rc::gen::arbitrary<int>();
        const vitality::ByteCursorPos cursor = make_cursor(buffer, raw_line, raw_column);

        RC_ASSERT(is_valid_cursor(buffer, buffer.move_left(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_right(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_home(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_end(cursor)));
    });
}

TEST_CASE("boundary movement becomes idempotent at the edges") {
    rc::prop("repeated movement settles at boundaries", [] {
        auto raw_lines = *rc::gen::container<std::vector<std::string>>(
            *rc::gen::inRange<std::size_t>(1, static_cast<std::size_t>(8)),
            rc::gen::string<std::string>());
        raw_lines = sanitize_lines(std::move(raw_lines));

        const vitality::TextBuffer buffer = load_buffer_from_lines(raw_lines);
        const vitality::ByteCursorPos cursor = make_cursor(
            buffer,
            *rc::gen::arbitrary<int>(),
            *rc::gen::arbitrary<int>());

        vitality::ByteCursorPos left = cursor;
        const std::int64_t left_iterations = buffer.line_length(cursor.line).value + 1;

        for (std::int64_t i = 0; i < left_iterations; ++i) {
            left = buffer.move_left(left);
        }

        const vitality::ByteCursorPos line_end = buffer.move_end(cursor);
        vitality::ByteCursorPos right = line_end;
        for (int i = 0; i < 64; ++i) {
            right = buffer.move_right(right);
        }

        RC_ASSERT(left.column.value == 0);
        RC_ASSERT(buffer.move_left(left).column.value == 0);
        RC_ASSERT(right.column.value == buffer.line_length(right.line).value);
        RC_ASSERT(buffer.move_right(right).column.value == right.column.value);
    });
}

TEST_CASE("clamp_cursor is idempotent") {
    rc::prop("clamping twice is the same as clamping once", [] {
        auto raw_lines = *rc::gen::container<std::vector<std::string>>(
            *rc::gen::inRange<std::size_t>(1, static_cast<std::size_t>(8)),
            rc::gen::string<std::string>());
        raw_lines = sanitize_lines(std::move(raw_lines));

        const vitality::TextBuffer buffer = load_buffer_from_lines(raw_lines);
        const vitality::ByteCursorPos first = buffer.clamp_cursor(vitality::ByteCursorPos{
            .line = vitality::LineIndex{*rc::gen::arbitrary<int>()},
            .column = vitality::ByteColumn{*rc::gen::arbitrary<int>()},
        });
        const vitality::ByteCursorPos second = buffer.clamp_cursor(first);

        RC_ASSERT(first.line.value == second.line.value);
        RC_ASSERT(first.column.value == second.column.value);
    });
}

TEST_CASE("successful horizontal movement on valid UTF-8 lines lands on grapheme boundaries") {
    rc::prop("left and right return grapheme boundaries for representative valid lines", [] {
        const std::vector<std::string> corpus{
            "plain ascii",
            "e\u0301x",
            "👍🏽a",
            "👩‍💻x",
            "하x",
        };

        const std::string line = *rc::gen::elementOf(corpus);
        const vitality::TextBuffer buffer = load_buffer_from_lines(std::vector<std::string>{line});
        const auto raw_column = *rc::gen::inRange<std::int64_t>(0, buffer.line_length(vitality::LineIndex{0}).value + 1);
        const vitality::ByteCursorPos cursor = make_cursor(buffer, 0, static_cast<int>(raw_column));

        const vitality::ByteCursorPos moved_left = buffer.move_left(cursor);
        const vitality::ByteCursorPos moved_right = buffer.move_right(cursor);
        const std::string_view utf8_line = buffer.line_text(vitality::LineIndex{0}).utf8_text;

        const auto left_alignment = vitality::unicode::align_byte_column_to_grapheme_boundary(
            utf8_line,
            moved_left.column);
        const auto right_alignment = vitality::unicode::align_byte_column_to_grapheme_boundary(
            utf8_line,
            moved_right.column);

        RC_ASSERT(left_alignment.success);
        RC_ASSERT(right_alignment.success);
        RC_ASSERT(left_alignment.column.value == moved_left.column.value);
        RC_ASSERT(right_alignment.column.value == moved_right.column.value);
    });
}

TEST_CASE("malformed UTF-8 horizontal movement matches the byte-step fallback model") {
    rc::prop("left and right match byte movement on malformed lines", [] {
        std::string suffix = *rc::gen::string<std::string>();
        for (char &ch : suffix) {
            if (ch == '\n' || ch == '\r') {
                ch = ' ';
            }
        }

        const std::string malformed_line = std::string("\x80", 1) + suffix;
        const vitality::TextBuffer buffer = load_buffer_from_lines(std::vector<std::string>{malformed_line});
        const auto raw_column = *rc::gen::inRange<std::int64_t>(0, buffer.line_length(vitality::LineIndex{0}).value + 1);
        const vitality::ByteCursorPos cursor = make_cursor(buffer, 0, static_cast<int>(raw_column));

        const vitality::ByteCursorPos moved_left = buffer.move_left(cursor);
        const vitality::ByteCursorPos moved_right = buffer.move_right(cursor);
        const vitality::ByteCursorPos expected_left = move_left_reference_by_byte(buffer, cursor);
        const vitality::ByteCursorPos expected_right = move_right_reference_by_byte(buffer, cursor);

        RC_ASSERT(moved_left.line.value == expected_left.line.value);
        RC_ASSERT(moved_left.column.value == expected_left.column.value);
        RC_ASSERT(moved_right.line.value == expected_right.line.value);
        RC_ASSERT(moved_right.column.value == expected_right.column.value);
    });
}

TEST_CASE("display column and cursor-for-display-column round trip on representative valid UTF-8 lines") {
    rc::prop("round-trips up to line-end clamping", [] {
        const std::vector<std::string> corpus{
            "plain ascii",
            "e\u0301x",
            "👍🏽a",
            "👩‍💻x",
            "こんにちは",
        };

        const std::string line = *rc::gen::elementOf(corpus);
        const vitality::TextBuffer buffer = load_buffer_from_lines(std::vector<std::string>{line});
        const auto raw_column = *rc::gen::inRange<std::int64_t>(0, 8);

        const auto cursor = buffer.cursor_for_display_column(
            vitality::LineIndex{0},
            vitality::GraphemeColumn{raw_column});
        RC_ASSERT(cursor.success);

        const auto display_column = buffer.display_column(vitality::ByteCursorPos{
            .line = cursor.cursor.line,
            .column = vitality::ByteColumn{cursor.cursor.column.value},
        });
        RC_ASSERT(display_column.success);
        RC_ASSERT(display_column.column.value <= raw_column);

        const auto line_end_column = buffer.display_column(vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = buffer.line_length(vitality::LineIndex{0}),
        });
        RC_ASSERT(line_end_column.success);
        RC_ASSERT(display_column.column.value == std::min<std::int64_t>(raw_column, line_end_column.column.value));
    });
}
