#include <cstdint>
#include <sstream>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "buffer/TextStorage.h"

namespace {

enum class TypingOpKind : std::uint8_t {
    InsertByte,
    Backspace,
};

struct TypingOp {
    TypingOpKind kind = TypingOpKind::InsertByte;
    char byte = '\0';
};

[[nodiscard]] std::vector<TypingOp> shortened_typing_script(
    const std::string_view fragment,
    const int repetitions,
    const std::string_view replacements,
    const int correction_interval) {
    std::vector<TypingOp> ops;
    int inserted_since_correction = 0;

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        for (const char byte : fragment) {
            ops.push_back(TypingOp{
                .kind = TypingOpKind::InsertByte,
                .byte = byte,
            });
            ++inserted_since_correction;

            if (inserted_since_correction < correction_interval) {
                continue;
            }

            inserted_since_correction = 0;
            for (int index = 0; index < static_cast<int>(replacements.size()); ++index) {
                ops.push_back(TypingOp{
                    .kind = TypingOpKind::Backspace,
                });
            }
            for (const char replacement : replacements) {
                ops.push_back(TypingOp{
                    .kind = TypingOpKind::InsertByte,
                    .byte = replacement,
                });
            }
        }
    }

    return ops;
}

}  // namespace

TEST_CASE("TextStorage make_empty creates one empty line") {
    const vitality::TextStorage storage = vitality::TextStorage::make_empty();

    CHECK(storage.byte_count().value == 0);
    CHECK(storage.line_count().value == 1);
    CHECK(storage.line_length(vitality::LineIndex{0}).value == 0);
    CHECK(storage.text().empty());
    CHECK(storage.check_invariants());
}

TEST_CASE("TextStorage from_utf8 preserves exact bytes and line splitting") {
    const vitality::TextStorage populated = vitality::TextStorage::from_utf8("alpha\nbeta");
    CHECK(populated.byte_count().value == 10);
    CHECK(populated.line_count().value == 2);
    CHECK(populated.line_text(vitality::LineIndex{0}).utf8_text == "alpha");
    CHECK(populated.line_text(vitality::LineIndex{1}).utf8_text == "beta");
    CHECK(populated.text() == std::string_view("alpha\nbeta"));
    CHECK(populated.check_invariants());

    const vitality::TextStorage empty = vitality::TextStorage::from_utf8("");
    CHECK(empty.line_count().value == 1);
    CHECK(empty.line_length(vitality::LineIndex{0}).value == 0);
}

TEST_CASE("TextStorage from_utf8 preserves carriage returns") {
    const vitality::TextStorage storage = vitality::TextStorage::from_utf8("abcd\r\nxy\r\n");

    CHECK(storage.line_count().value == 2);
    CHECK(storage.line_text(vitality::LineIndex{0}).utf8_text == "abcd\r");
    CHECK(storage.line_text(vitality::LineIndex{1}).utf8_text == "xy\r");
    CHECK(storage.text() == std::string_view("abcd\r\nxy\r\n"));
}

TEST_CASE("TextStorage clamp_cursor clamps line and column to valid bounds") {
    const vitality::TextStorage storage = vitality::TextStorage::from_utf8("abcd\nxy");

    const vitality::ByteCursorPos clamped = storage.clamp_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{99},
        .column = vitality::ByteColumn{99},
    });

    CHECK(clamped.line.value == 1);
    CHECK(clamped.column.value == 2);
}

TEST_CASE("TextStorage load_from_stream preserves bytes exactly") {
    std::istringstream input("abc\r\nxyz");
    const vitality::TextStorage storage = vitality::TextStorage::load_from_stream(input);

    CHECK(storage.text() == std::string_view("abc\r\nxyz"));
    CHECK(storage.line_count().value == 2);
    CHECK(storage.line_text(vitality::LineIndex{0}).utf8_text == "abc\r");
    CHECK(storage.line_text(vitality::LineIndex{1}).utf8_text == "xyz");
}

TEST_CASE("TextStorage insert and erase report ranges and preserve bytes") {
    vitality::TextStorage storage = vitality::TextStorage::from_utf8("ab\ncd");

    const auto inserted = storage.insert(vitality::ByteOffset{2}, "XYZ");
    REQUIRE(inserted.success);
    CHECK(inserted.inserted_range.start.value == 2);
    CHECK(inserted.inserted_range.length.value == 3);
    CHECK(storage.text() == std::string_view("abXYZ\ncd"));
    CHECK(storage.line_text(vitality::LineIndex{0}).utf8_text == "abXYZ");
    CHECK(storage.check_invariants());

    const auto erased = storage.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{2},
        .length = vitality::ByteCount{3},
    });
    REQUIRE(erased.success);
    CHECK(erased.erased_range.start.value == 2);
    CHECK(erased.erased_range.length.value == 3);
    CHECK(storage.text() == std::string_view("ab\ncd"));
    CHECK(storage.check_invariants());
}

TEST_CASE("TextStorage invalid edits leave storage unchanged") {
    vitality::TextStorage storage = vitality::TextStorage::from_utf8("abcd");
    const std::string original_text = storage.text();

    const auto bad_insert = storage.insert(vitality::ByteOffset{-1}, "x");
    CHECK(!bad_insert.success);
    CHECK(storage.text() == original_text);

    const auto empty_insert = storage.insert(vitality::ByteOffset{2}, "");
    CHECK(empty_insert.success);
    CHECK(empty_insert.inserted_range.start.value == 2);
    CHECK(empty_insert.inserted_range.length.value == 0);
    CHECK(storage.text() == original_text);

    const auto bad_erase = storage.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{3},
        .length = vitality::ByteCount{5},
    });
    CHECK(!bad_erase.success);
    CHECK(storage.text() == original_text);

    const auto empty_erase = storage.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{4},
        .length = vitality::ByteCount{0},
    });
    CHECK(empty_erase.success);
    CHECK(empty_erase.erased_range.start.value == 4);
    CHECK(empty_erase.erased_range.length.value == 0);
    CHECK(storage.text() == original_text);
}

TEST_CASE("TextStorage insert erase hot path returns to the original text") {
    std::string input;
    for (int index = 0; index < 256; ++index) {
        input += "alpha beta gamma delta\n";
    }

    vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);

    for (int index = 0; index < 64; ++index) {
        const auto inserted = storage.insert(vitality::ByteOffset{storage.byte_count().value / 2}, "z");
        REQUIRE(inserted.success);
        REQUIRE(storage.check_invariants());

        const auto erased = storage.erase(inserted.inserted_range);
        REQUIRE(erased.success);
        REQUIRE(storage.check_invariants());
    }

    CHECK(storage.text() == input);
}

TEST_CASE("TextStorage maintenance pass preserves text and invariants during many edits") {
    std::string input = "seed";
    vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);

    for (int index = 0; index < 2304; ++index) {
        const std::int64_t offset = storage.byte_count().value / 2;
        const auto inserted = storage.insert(vitality::ByteOffset{offset}, "x");
        REQUIRE(inserted.success);
        input.insert(static_cast<std::size_t>(offset), "x");
    }

    CHECK(storage.text() == input);
    CHECK(storage.check_invariants());
}

TEST_CASE("TextStorage typing forward benchmark pattern preserves text") {
    const auto ops = shortened_typing_script(
        "This is a typing benchmark.\n",
        8,
        "ed ",
        64);
    std::string expected_text = "prefix\nsuffix\n";
    vitality::TextStorage storage = vitality::TextStorage::from_utf8(expected_text);
    int cursor = 7;

    for (const TypingOp op : ops) {
        if (op.kind != TypingOpKind::InsertByte) {
            continue;
        }

        const auto inserted = storage.insert(
            vitality::ByteOffset{cursor},
            std::string_view(&op.byte, 1));
        REQUIRE(inserted.success);
        expected_text.insert(static_cast<std::size_t>(cursor), 1, op.byte);
        ++cursor;
    }

    CHECK(storage.text() == expected_text);
    CHECK(storage.check_invariants());
}

TEST_CASE("TextStorage typing mix benchmark pattern preserves text") {
    const auto ops = shortened_typing_script(
        "// comment about storage typing\n",
        10,
        "ok",
        24);
    std::string expected_text = "prefix\nsuffix\n";
    vitality::TextStorage storage = vitality::TextStorage::from_utf8(expected_text);
    int cursor = 7;

    for (const TypingOp op : ops) {
        if (op.kind == TypingOpKind::InsertByte) {
            const auto inserted = storage.insert(
                vitality::ByteOffset{cursor},
                std::string_view(&op.byte, 1));
            REQUIRE(inserted.success);
            expected_text.insert(static_cast<std::size_t>(cursor), 1, op.byte);
            ++cursor;
            continue;
        }

        if (cursor == 0 || storage.byte_count().value == 0) {
            continue;
        }

        const auto erased = storage.erase(vitality::ByteRange{
            .start = vitality::ByteOffset{cursor - 1},
            .length = vitality::ByteCount{1},
        });
        REQUIRE(erased.success);
        expected_text.erase(static_cast<std::size_t>(cursor - 1), 1);
        --cursor;
    }

    CHECK(storage.text() == expected_text);
    CHECK(storage.check_invariants());
}
