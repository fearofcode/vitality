#include <chrono>
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

[[nodiscard]] vitality::CursorPos make_cursor(const vitality::TextBuffer &buffer, const int line, const int column) {
    return buffer.clamp_cursor(vitality::CursorPos{
        .line = vitality::LineIndex{line},
        .column = vitality::ColumnIndex{column},
    });
}

[[nodiscard]] bool is_valid_cursor(const vitality::TextBuffer &buffer, const vitality::CursorPos cursor) {
    if (cursor.line.value < 0 || cursor.line.value >= buffer.line_count().value) {
        return false;
    }

    const int line_length = buffer.line_length(cursor.line).value;
    return cursor.column.value >= 0 && cursor.column.value <= line_length;
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
        const vitality::CursorPos cursor = make_cursor(buffer, raw_line, raw_column);

        RC_ASSERT(is_valid_cursor(buffer, buffer.move_left(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_right(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_up(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_down(cursor)));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_page_up(cursor, vitality::VisibleLineCount{5})));
        RC_ASSERT(is_valid_cursor(buffer, buffer.move_page_down(cursor, vitality::VisibleLineCount{5})));
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
        const vitality::CursorPos cursor = make_cursor(
            buffer,
            *rc::gen::arbitrary<int>(),
            *rc::gen::arbitrary<int>());

        vitality::CursorPos left = cursor;
        vitality::CursorPos up = cursor;
        vitality::CursorPos down = cursor;
        const int left_iterations = buffer.line_length(cursor.line).value + 1;
        const int vertical_iterations = buffer.line_count().value + 1;

        for (int i = 0; i < left_iterations; ++i) {
            left = buffer.move_left(left);
        }

        for (int i = 0; i < vertical_iterations; ++i) {
            up = buffer.move_up(up);
            down = buffer.move_down(down);
        }

        const vitality::CursorPos line_end = buffer.move_end(cursor);
        vitality::CursorPos right = line_end;
        for (int i = 0; i < 64; ++i) {
            right = buffer.move_right(right);
        }

        RC_ASSERT(left.column.value == 0);
        RC_ASSERT(buffer.move_left(left).column.value == 0);
        RC_ASSERT(up.line.value == 0);
        RC_ASSERT(buffer.move_up(up).line.value == 0);
        RC_ASSERT(down.line.value == buffer.line_count().value - 1);
        RC_ASSERT(buffer.move_down(down).line.value == down.line.value);
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
        const vitality::CursorPos first = buffer.clamp_cursor(vitality::CursorPos{
            .line = vitality::LineIndex{*rc::gen::arbitrary<int>()},
            .column = vitality::ColumnIndex{*rc::gen::arbitrary<int>()},
        });
        const vitality::CursorPos second = buffer.clamp_cursor(first);

        RC_ASSERT(first.line.value == second.line.value);
        RC_ASSERT(first.column.value == second.column.value);
    });
}
