#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "buffer/TextStorage.h"

TEST_CASE("TextStorage make_empty creates one empty line") {
    const vitality::TextStorage storage = vitality::TextStorage::make_empty();

    CHECK(storage.line_count().value == 1);
    CHECK(storage.line_length(vitality::LineIndex{0}).value == 0);
}

TEST_CASE("TextStorage from_utf8 preserves lines and normalizes empty input") {
    const vitality::TextStorage populated = vitality::TextStorage::from_utf8("alpha\nbeta");
    CHECK(populated.line_count().value == 2);
    CHECK(populated.line_text(vitality::LineIndex{0}).utf8_text == std::string_view("alpha"));
    CHECK(populated.line_text(vitality::LineIndex{1}).utf8_text == std::string_view("beta"));

    const vitality::TextStorage empty = vitality::TextStorage::from_utf8("");
    CHECK(empty.line_count().value == 1);
    CHECK(empty.line_length(vitality::LineIndex{0}).value == 0);
}

TEST_CASE("TextStorage from_utf8 strips carriage returns before newlines") {
    const vitality::TextStorage storage = vitality::TextStorage::from_utf8("abcd\r\nxy\r\n");

    CHECK(storage.line_count().value == 2);
    CHECK(storage.line_text(vitality::LineIndex{0}).utf8_text == std::string_view("abcd"));
    CHECK(storage.line_text(vitality::LineIndex{1}).utf8_text == std::string_view("xy"));
}

TEST_CASE("TextStorage clamp_cursor clamps line and column to valid bounds") {
    const vitality::TextStorage storage = vitality::TextStorage::from_utf8("abcd\nxy");

    const vitality::CursorPos clamped = storage.clamp_cursor(vitality::CursorPos{
        .line = vitality::LineIndex{99},
        .column = vitality::ColumnIndex{99},
    });

    CHECK(clamped.line.value == 1);
    CHECK(clamped.column.value == 2);
}
