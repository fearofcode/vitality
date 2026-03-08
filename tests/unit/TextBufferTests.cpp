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

TEST_CASE("vertical movement clamps to buffer bounds and line length") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("abcdef\nxy\n");

    const vitality::ByteCursorPos moved_up = buffer.move_up(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{3},
    });
    CHECK(moved_up.line.value == 0);
    CHECK(moved_up.column.value == 3);

    const vitality::ByteCursorPos moved_down = buffer.move_down(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{5},
    });
    CHECK(moved_down.line.value == 1);
    CHECK(moved_down.column.value == 2);

    const vitality::ByteCursorPos at_bottom = buffer.move_down(vitality::ByteCursorPos{
        .line = vitality::LineIndex{1},
        .column = vitality::ByteColumn{1},
    });
    CHECK(at_bottom.line.value == 1);
}

TEST_CASE("home end and paging respect line bounds") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("abc\n12345\nxy\n");
    const vitality::ByteCursorPos cursor{
        .line = vitality::LineIndex{1},
        .column = vitality::ByteColumn{2},
    };

    CHECK(buffer.move_home(cursor).column.value == 0);
    CHECK(buffer.move_end(cursor).column.value == 5);
    CHECK(buffer.move_page_up(cursor, vitality::VisibleLineCount{3}).line.value == 0);
    CHECK(buffer.move_page_down(cursor, vitality::VisibleLineCount{3}).line.value == 2);
}
