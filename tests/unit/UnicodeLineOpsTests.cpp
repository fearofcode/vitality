#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "unicode/UnicodeLineOps.h"

TEST_CASE("UnicodeLineOps aligns byte columns to code point boundaries") {
    const auto ascii = vitality::unicode::align_byte_column_to_code_point_boundary(
        "hello",
        vitality::ByteColumn{3});
    REQUIRE(ascii.success);
    CHECK(ascii.aligned_column.value == 3);

    const auto multibyte = vitality::unicode::align_byte_column_to_code_point_boundary(
        "éx",
        vitality::ByteColumn{1});
    REQUIRE(multibyte.success);
    CHECK(multibyte.aligned_column.value == 0);

    const auto out_of_range = vitality::unicode::align_byte_column_to_code_point_boundary(
        "éx",
        vitality::ByteColumn{99});
    REQUIRE(out_of_range.success);
    CHECK(out_of_range.aligned_column.value == 3);

    const std::string malformed("\x80x", 2);
    const auto invalid = vitality::unicode::align_byte_column_to_code_point_boundary(
        malformed,
        vitality::ByteColumn{1});
    CHECK(!invalid.success);
    CHECK(invalid.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("UnicodeLineOps aligns byte columns to grapheme boundaries") {
    const auto ascii = vitality::unicode::align_byte_column_to_grapheme_boundary(
        "hello",
        vitality::ByteColumn{3});
    REQUIRE(ascii.success);
    CHECK(ascii.column.value == 3);

    constexpr std::string_view combining = "e\u0301x";
    const auto combining_start = vitality::unicode::align_byte_column_to_grapheme_boundary(
        combining,
        vitality::ByteColumn{0});
    REQUIRE(combining_start.success);
    CHECK(combining_start.column.value == 0);

    const auto combining_base = vitality::unicode::align_byte_column_to_grapheme_boundary(
        combining,
        vitality::ByteColumn{1});
    REQUIRE(combining_base.success);
    CHECK(combining_base.column.value == 0);

    const auto combining_mark = vitality::unicode::align_byte_column_to_grapheme_boundary(
        combining,
        vitality::ByteColumn{2});
    REQUIRE(combining_mark.success);
    CHECK(combining_mark.column.value == 0);

    const auto combining_next = vitality::unicode::align_byte_column_to_grapheme_boundary(
        combining,
        vitality::ByteColumn{3});
    REQUIRE(combining_next.success);
    CHECK(combining_next.column.value == 3);

    constexpr std::string_view emoji_modifier = "👍🏽a";
    const auto emoji_inside = vitality::unicode::align_byte_column_to_grapheme_boundary(
        emoji_modifier,
        vitality::ByteColumn{5});
    REQUIRE(emoji_inside.success);
    CHECK(emoji_inside.column.value == 0);

    constexpr std::string_view zwj = "👩‍💻x";
    const auto zwj_inside = vitality::unicode::align_byte_column_to_grapheme_boundary(
        zwj,
        vitality::ByteColumn{7});
    REQUIRE(zwj_inside.success);
    CHECK(zwj_inside.column.value == 0);

    constexpr std::string_view hangul = "하x";
    const auto hangul_inside = vitality::unicode::align_byte_column_to_grapheme_boundary(
        hangul,
        vitality::ByteColumn{3});
    REQUIRE(hangul_inside.success);
    CHECK(hangul_inside.column.value == 0);

    const std::string malformed("\x80x", 2);
    const auto invalid = vitality::unicode::align_byte_column_to_grapheme_boundary(
        malformed,
        vitality::ByteColumn{1});
    CHECK(!invalid.success);
    CHECK(invalid.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("UnicodeLineOps finds grapheme boundaries for common line cases") {
    const auto ascii_next = vitality::unicode::next_grapheme_boundary(
        "abc",
        vitality::ByteColumn{0});
    REQUIRE(ascii_next.success);
    CHECK(ascii_next.column.value == 1);

    const auto ascii_prev = vitality::unicode::previous_grapheme_boundary(
        "abc",
        vitality::ByteColumn{2});
    REQUIRE(ascii_prev.success);
    CHECK(ascii_prev.column.value == 1);

    constexpr std::string_view combining = "e\u0301x";
    const auto combining_next = vitality::unicode::next_grapheme_boundary(
        combining,
        vitality::ByteColumn{0});
    REQUIRE(combining_next.success);
    CHECK(combining_next.column.value == 3);

    const auto combining_prev = vitality::unicode::previous_grapheme_boundary(
        combining,
        vitality::ByteColumn{4});
    REQUIRE(combining_prev.success);
    CHECK(combining_prev.column.value == 3);

    constexpr std::string_view emoji_modifier = "👍🏽a";
    const auto emoji_next = vitality::unicode::next_grapheme_boundary(
        emoji_modifier,
        vitality::ByteColumn{0});
    REQUIRE(emoji_next.success);
    CHECK(emoji_next.column.value == 8);

    constexpr std::string_view zwj = "👩‍💻x";
    const auto zwj_next = vitality::unicode::next_grapheme_boundary(
        zwj,
        vitality::ByteColumn{0});
    REQUIRE(zwj_next.success);
    CHECK(zwj_next.column.value == 11);

    constexpr std::string_view hangul = "하x";
    const auto hangul_next = vitality::unicode::next_grapheme_boundary(
        hangul,
        vitality::ByteColumn{0});
    REQUIRE(hangul_next.success);
    CHECK(hangul_next.column.value == 6);

    const auto at_start = vitality::unicode::previous_grapheme_boundary(
        "abc",
        vitality::ByteColumn{0});
    REQUIRE(at_start.success);
    CHECK(at_start.column.value == 0);

    const auto at_end = vitality::unicode::next_grapheme_boundary(
        "abc",
        vitality::ByteColumn{3});
    REQUIRE(at_end.success);
    CHECK(at_end.column.value == 3);

    const std::string malformed("\x80x", 2);
    const auto invalid = vitality::unicode::next_grapheme_boundary(
        malformed,
        vitality::ByteColumn{0});
    CHECK(!invalid.success);
    CHECK(invalid.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("UnicodeLineOps maps byte columns to Qt UTF-16 columns") {
    const auto ascii = vitality::unicode::map_byte_column_to_qt_utf16(
        "hello",
        vitality::ByteColumn{3});
    REQUIRE(ascii.success);
    CHECK(ascii.qt_column.value == 3);
    CHECK(ascii.aligned_byte_column.value == 3);

    const auto bmp = vitality::unicode::map_byte_column_to_qt_utf16(
        "éx",
        vitality::ByteColumn{2});
    REQUIRE(bmp.success);
    CHECK(bmp.qt_column.value == 1);
    CHECK(bmp.aligned_byte_column.value == 2);

    const auto supplementary = vitality::unicode::map_byte_column_to_qt_utf16(
        "😀a",
        vitality::ByteColumn{4});
    REQUIRE(supplementary.success);
    CHECK(supplementary.qt_column.value == 2);
    CHECK(supplementary.aligned_byte_column.value == 4);

    const std::string malformed("\x80x", 2);
    const auto invalid = vitality::unicode::map_byte_column_to_qt_utf16(
        malformed,
        vitality::ByteColumn{1});
    CHECK(!invalid.success);
    CHECK(invalid.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("UnicodeLineOps reports grapheme display columns at byte positions") {
    const auto ascii = vitality::unicode::grapheme_column_at_byte_column(
        "hello",
        vitality::ByteColumn{3});
    REQUIRE(ascii.success);
    CHECK(ascii.column.value == 3);

    const auto japanese = vitality::unicode::grapheme_column_at_byte_column(
        "こんにちは",
        vitality::ByteColumn{3});
    REQUIRE(japanese.success);
    CHECK(japanese.column.value == 1);

    const auto combining = vitality::unicode::grapheme_column_at_byte_column(
        "e\u0301x",
        vitality::ByteColumn{2});
    REQUIRE(combining.success);
    CHECK(combining.column.value == 0);

    const auto emoji = vitality::unicode::grapheme_column_at_byte_column(
        "👍🏽a",
        vitality::ByteColumn{8});
    REQUIRE(emoji.success);
    CHECK(emoji.column.value == 1);

    const std::string malformed("\x80x", 2);
    const auto invalid = vitality::unicode::grapheme_column_at_byte_column(
        malformed,
        vitality::ByteColumn{1});
    CHECK(!invalid.success);
    CHECK(invalid.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("UnicodeLineOps maps grapheme display columns back to byte boundaries") {
    const auto ascii_zero = vitality::unicode::grapheme_boundary_for_display_column(
        "abc",
        vitality::GraphemeColumn{0});
    REQUIRE(ascii_zero.success);
    CHECK(ascii_zero.column.value == 0);

    const auto ascii_one = vitality::unicode::grapheme_boundary_for_display_column(
        "abc",
        vitality::GraphemeColumn{1});
    REQUIRE(ascii_one.success);
    CHECK(ascii_one.column.value == 1);

    const auto ascii_clamped = vitality::unicode::grapheme_boundary_for_display_column(
        "abc",
        vitality::GraphemeColumn{99});
    REQUIRE(ascii_clamped.success);
    CHECK(ascii_clamped.column.value == 3);

    const auto japanese = vitality::unicode::grapheme_boundary_for_display_column(
        "こんにちは",
        vitality::GraphemeColumn{2});
    REQUIRE(japanese.success);
    CHECK(japanese.column.value == 6);

    const auto combining = vitality::unicode::grapheme_boundary_for_display_column(
        "e\u0301x",
        vitality::GraphemeColumn{1});
    REQUIRE(combining.success);
    CHECK(combining.column.value == 3);

    const auto emoji = vitality::unicode::grapheme_boundary_for_display_column(
        "👍🏽x",
        vitality::GraphemeColumn{1});
    REQUIRE(emoji.success);
    CHECK(emoji.column.value == 8);

    const auto zwj = vitality::unicode::grapheme_boundary_for_display_column(
        "👩‍💻x",
        vitality::GraphemeColumn{1});
    REQUIRE(zwj.success);
    CHECK(zwj.column.value == 11);

    const std::string malformed("\x80x", 2);
    const auto invalid = vitality::unicode::grapheme_boundary_for_display_column(
        malformed,
        vitality::GraphemeColumn{1});
    CHECK(!invalid.success);
    CHECK(invalid.error == vitality::UnicodeError::InvalidUtf8);
}
