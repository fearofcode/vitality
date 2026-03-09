#include <string>

#include <catch2/catch_test_macros.hpp>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "TestHelpers.h"
#include "ui/StatusBarText.h"

TEST_CASE("status bar reports grapheme-based columns for valid UTF-8 text") {
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require("こんにちは\n");

    const QString status = vitality::make_status_bar_text(
        buffer,
        vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{3},
        });

    CHECK(status.contains(QStringLiteral("Ln 1")));
    CHECK(status.contains(QStringLiteral("Col 2")));
}

TEST_CASE("status bar falls back to byte columns for malformed UTF-8 text") {
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string("\x80x\n", 3));

    const QString status = vitality::make_status_bar_text(
        buffer,
        vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{1},
        });

    CHECK(status.contains(QStringLiteral("Col 2")));
}

TEST_CASE("status bar keeps combining sequences to a single displayed column") {
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require("e\u0301x\n");

    const QString status = vitality::make_status_bar_text(
        buffer,
        vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{2},
        });

    CHECK(status.contains(QStringLiteral("Col 1")));
}
