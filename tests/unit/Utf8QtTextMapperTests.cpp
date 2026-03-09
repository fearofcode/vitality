#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ui/Utf8QtTextMapper.h"

TEST_CASE("Utf8QtTextMapper maps ASCII byte columns directly to Qt UTF-16 columns") {
    const vitality::QtTextCursorMapping mapping =
        vitality::map_utf8_byte_column_to_qt_cursor("hello", vitality::ByteColumn{3});

    REQUIRE(mapping.success);
    CHECK(mapping.qt_column.value == 3);
    CHECK(mapping.aligned_byte_column.value == 3);
}

TEST_CASE("Utf8QtTextMapper maps multibyte UTF-8 text to Qt UTF-16 columns") {
    constexpr std::string_view text = "😀a";

    const vitality::QtTextCursorMapping mapping =
        vitality::map_utf8_byte_column_to_qt_cursor(text, vitality::ByteColumn{4});

    REQUIRE(mapping.success);
    CHECK(mapping.qt_column.value == 2);
    CHECK(mapping.aligned_byte_column.value == 4);
}

TEST_CASE("Utf8QtTextMapper aligns mid-code-point byte columns back to a valid UTF-8 boundary") {
    constexpr std::string_view text = "éx";

    const vitality::QtTextCursorMapping mapping =
        vitality::map_utf8_byte_column_to_qt_cursor(text, vitality::ByteColumn{1});

    REQUIRE(mapping.success);
    CHECK(mapping.qt_column.value == 0);
    CHECK(mapping.aligned_byte_column.value == 0);
}

TEST_CASE("Utf8QtTextMapper reports failure for malformed UTF-8") {
    const std::string malformed("\x80x", 2);

    const vitality::QtTextCursorMapping mapping =
        vitality::map_utf8_byte_column_to_qt_cursor(malformed, vitality::ByteColumn{1});

    CHECK(!mapping.success);
    CHECK(mapping.error == vitality::UnicodeError::InvalidUtf8);
}
