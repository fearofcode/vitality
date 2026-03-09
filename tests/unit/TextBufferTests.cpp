#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "file/FilePath.h"

namespace {

class TempFile {
public:
    explicit TempFile(const std::string &contents)
        : path_(std::filesystem::temp_directory_path() / make_name()) {
        std::ofstream output(path_, std::ios::out | std::ios::trunc);
        output << contents;
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
        return "vitality-test-" + std::to_string(seed) + "-" + std::to_string(counter++) + ".txt";
    }

    std::filesystem::path path_;
};

[[nodiscard]] vitality::TextBuffer load_buffer_from_contents(const std::string &contents) {
    TempFile temp_file(contents);
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());
    auto load_result = vitality::TextBuffer::load_from_path(file_path);
    REQUIRE(load_result.success);
    return std::move(load_result.buffer);
}

}  // namespace

TEST_CASE("make_empty creates one empty line and no file path") {
    const vitality::TextBuffer buffer = vitality::TextBuffer::make_empty();

    CHECK(buffer.line_count().value == 1);
    CHECK(!buffer.has_file_path());
    CHECK(buffer.line_length(vitality::LineIndex{0}).value == 0);
}

TEST_CASE("load_from_path preserves line order and strips newlines") {
    TempFile temp_file("alpha\nbeta\n");
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());

    auto load_result = vitality::TextBuffer::load_from_path(file_path);

    REQUIRE(load_result.success);
    vitality::TextBuffer &buffer = load_result.buffer;
    CHECK(buffer.line_count().value == 2);
    CHECK(buffer.has_file_path());
    CHECK(buffer.display_name() == std::string_view(temp_file.path().filename().string()));
    CHECK(buffer.line_text(vitality::LineIndex{0}).utf8_text == "alpha");
    CHECK(buffer.line_text(vitality::LineIndex{1}).utf8_text == "beta");
}

TEST_CASE("loading an empty file creates one empty logical line") {
    TempFile temp_file("");
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());

    auto load_result = vitality::TextBuffer::load_from_path(file_path);

    REQUIRE(load_result.success);
    CHECK(load_result.buffer.line_count().value == 1);
    CHECK(load_result.buffer.line_length(vitality::LineIndex{0}).value == 0);
}

TEST_CASE("load_from_path normalizes CRLF to LF") {
    TempFile temp_file("alpha\r\nbeta\r\n");
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());

    auto load_result = vitality::TextBuffer::load_from_path(file_path);

    REQUIRE(load_result.success);
    CHECK(load_result.buffer.line_count().value == 2);
    CHECK(load_result.buffer.line_text(vitality::LineIndex{0}).utf8_text == "alpha");
    CHECK(load_result.buffer.line_text(vitality::LineIndex{1}).utf8_text == "beta");
}

TEST_CASE("clamp_cursor fixes out of range line and column values") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("abcd\nef\n");

    const vitality::ByteCursorPos clamped = buffer.clamp_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{99},
        .column = vitality::ByteColumn{99},
    });

    CHECK(clamped.line.value == 1);
    CHECK(clamped.column.value == 2);
}

TEST_CASE("logical cursor helpers make logical ownership explicit without changing behavior") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("e\u0301x\n");

    const auto logical_grapheme = buffer.logical_grapheme_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{2},
    });
    REQUIRE(logical_grapheme.success);
    CHECK(logical_grapheme.cursor.line.value == 0);
    CHECK(logical_grapheme.cursor.column.value == 0);

    const vitality::TextBuffer malformed = load_buffer_from_contents(std::string("\xC3(", 2) + "\n");
    const auto malformed_logical = malformed.logical_grapheme_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{1},
    });
    CHECK(!malformed_logical.success);
    CHECK(malformed_logical.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("TextBuffer exposes Unicode-aware boundary queries without changing movement behavior") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("e\u0301x\n");

    const auto aligned = buffer.align_line_byte_column_to_code_point_boundary(
        vitality::LineIndex{0},
        vitality::ByteColumn{1});
    REQUIRE(aligned.success);
    CHECK(aligned.aligned_column.value == 1);

    const auto misaligned = buffer.align_line_byte_column_to_code_point_boundary(
        vitality::LineIndex{0},
        vitality::ByteColumn{2});
    REQUIRE(misaligned.success);
    CHECK(misaligned.aligned_column.value == 1);

    const auto aligned_cursor = buffer.align_cursor_to_grapheme_boundary(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{2},
    });
    REQUIRE(aligned_cursor.success);
    CHECK(aligned_cursor.cursor.line.value == 0);
    CHECK(aligned_cursor.cursor.column.value == 0);

    const auto previous_cursor = buffer.previous_grapheme_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{4},
    });
    REQUIRE(previous_cursor.success);
    CHECK(previous_cursor.cursor.column.value == 3);

    const auto next_cursor = buffer.next_grapheme_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{2},
    });
    REQUIRE(next_cursor.success);
    CHECK(next_cursor.cursor.column.value == 3);

    const auto next = buffer.next_grapheme_boundary(
        vitality::LineIndex{0},
        vitality::ByteColumn{0});
    REQUIRE(next.success);
    CHECK(next.column.value == 3);

    const auto previous = buffer.previous_grapheme_boundary(
        vitality::LineIndex{0},
        vitality::ByteColumn{4});
    REQUIRE(previous.success);
    CHECK(previous.column.value == 3);

    const auto invalid = buffer.next_grapheme_boundary(
        vitality::LineIndex{99},
        vitality::ByteColumn{0});
    CHECK(!invalid.success);

    const auto invalid_cursor = buffer.align_cursor_to_grapheme_boundary(vitality::ByteCursorPos{
        .line = vitality::LineIndex{99},
        .column = vitality::ByteColumn{0},
    });
    CHECK(!invalid_cursor.success);

    const auto display_column = buffer.display_column(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{3},
    });
    REQUIRE(display_column.success);
    CHECK(display_column.column.value == 1);

    const auto cursor_for_display_column = buffer.cursor_for_display_column(
        vitality::LineIndex{0},
        vitality::GraphemeColumn{1});
    REQUIRE(cursor_for_display_column.success);
    CHECK(cursor_for_display_column.cursor.column.value == 3);

    const auto preferred_column = buffer.preferred_column(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{2},
    });
    CHECK(preferred_column.value == 0);
}

TEST_CASE("horizontal movement clamps at line edges") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("abc\n");
    const vitality::ByteCursorPos start{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{0},
    };

    CHECK(buffer.move_left(start).column.value == 0);
    CHECK(buffer.move_right(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{3},
          }).column.value == 3);
}

TEST_CASE("horizontal movement is grapheme-aware on valid UTF-8 lines") {
    const vitality::TextBuffer combining = load_buffer_from_contents("e\u0301x\n");
    CHECK(combining.move_right(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{0},
          }).column.value == 3);
    CHECK(combining.move_left(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{3},
          }).column.value == 0);
    CHECK(combining.move_right(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{2},
          }).column.value == 3);

    const vitality::TextBuffer emoji = load_buffer_from_contents("👍🏽a\n");
    CHECK(emoji.move_right(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{0},
          }).column.value == 8);
    CHECK(emoji.move_left(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{8},
          }).column.value == 0);

    const vitality::TextBuffer zwj = load_buffer_from_contents("👩‍💻x\n");
    CHECK(zwj.move_right(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{0},
          }).column.value == 11);
    CHECK(zwj.move_left(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{11},
          }).column.value == 0);

    const vitality::TextBuffer hangul = load_buffer_from_contents("하x\n");
    CHECK(hangul.move_right(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{0},
          }).column.value == 6);
    CHECK(hangul.move_left(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{6},
          }).column.value == 0);
}

TEST_CASE("TextBuffer maps display columns back to grapheme-aligned cursors") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("abcdef\nx\nこんにちは\n");

    const auto ascii_cursor = buffer.cursor_for_display_column(
        vitality::LineIndex{0},
        vitality::GraphemeColumn{4});
    REQUIRE(ascii_cursor.success);
    CHECK(ascii_cursor.cursor.line.value == 0);
    CHECK(ascii_cursor.cursor.column.value == 4);

    const auto clamped_short = buffer.cursor_for_display_column(
        vitality::LineIndex{1},
        vitality::GraphemeColumn{4});
    REQUIRE(clamped_short.success);
    CHECK(clamped_short.cursor.column.value == 1);

    const auto japanese = buffer.cursor_for_display_column(
        vitality::LineIndex{2},
        vitality::GraphemeColumn{2});
    REQUIRE(japanese.success);
    CHECK(japanese.cursor.column.value == 6);

    const auto invalid = buffer.cursor_for_display_column(
        vitality::LineIndex{99},
        vitality::GraphemeColumn{1});
    CHECK(!invalid.success);
}

TEST_CASE("preferred_column follows grapheme display semantics and falls back conservatively") {
    const vitality::TextBuffer valid = load_buffer_from_contents("e\u0301x\n");
    CHECK(valid.preferred_column(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{0},
          }).value == 0);
    CHECK(valid.preferred_column(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{2},
          }).value == 0);
    CHECK(valid.preferred_column(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{3},
          }).value == 1);

    const vitality::TextBuffer malformed = load_buffer_from_contents(std::string("\x80x\n", 3));
    CHECK(malformed.preferred_column(vitality::ByteCursorPos{
              .line = vitality::LineIndex{0},
              .column = vitality::ByteColumn{1},
          }).value == 1);
}

TEST_CASE("home and end respect line bounds") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("abc\n12345\nxy\n");
    const vitality::ByteCursorPos cursor{
        .line = vitality::LineIndex{1},
        .column = vitality::ByteColumn{2},
    };

    CHECK(buffer.move_home(cursor).column.value == 0);
    CHECK(buffer.move_end(cursor).column.value == 5);
}

TEST_CASE("malformed UTF-8 lines fall back to byte-based horizontal movement") {
    const vitality::TextBuffer buffer = load_buffer_from_contents(std::string("\x80x\n", 3));

    const vitality::ByteCursorPos moved_right = buffer.move_right(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{0},
    });
    CHECK(moved_right.column.value == 1);

    const vitality::ByteCursorPos moved_left = buffer.move_left(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{1},
    });
    CHECK(moved_left.column.value == 0);
}
