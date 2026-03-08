#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "buffer/internal/ImplicitTreapStorageCore.h"

TEST_CASE("ImplicitTreapStorageCore handles empty input") {
    const vitality::buffer_internal::ImplicitTreapStorageCore core;

    CHECK(core.byte_count() == 0);
    CHECK(core.line_count() == 1);
    CHECK(core.line_start_offset(0) == 0);
    CHECK(core.line_start_offset(1) == 0);
    CHECK(core.text().empty());
    CHECK(core.substring(0, 10).empty());
    CHECK(core.piece_count() == 0);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore reconstructs exact bytes") {
    const std::string input = "alpha\nbeta\n";
    const vitality::buffer_internal::ImplicitTreapStorageCore core(input);

    CHECK(core.byte_count() == input.size());
    CHECK(core.text() == input);
    CHECK(core.piece_count() == 1);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore line count and starts follow editor semantics") {
    const vitality::buffer_internal::ImplicitTreapStorageCore core("a\n\nb\n");

    CHECK(core.line_count() == 3);
    CHECK(core.line_start_offset(0) == 0);
    CHECK(core.line_start_offset(1) == 2);
    CHECK(core.line_start_offset(2) == 3);
    CHECK(core.line_start_offset(3) == 5);
}

TEST_CASE("ImplicitTreapStorageCore substring collects arbitrary ranges") {
    const vitality::buffer_internal::ImplicitTreapStorageCore core("0123456789");

    CHECK(core.substring(0, 4) == std::string_view("0123"));
    CHECK(core.substring(3, 4) == std::string_view("3456"));
    CHECK(core.substring(8, 10) == std::string_view("89"));
    CHECK(core.substring(99, 2).empty());
}

TEST_CASE("ImplicitTreapStorageCore supports insert and erase mutations") {
    vitality::buffer_internal::ImplicitTreapStorageCore core;

    CHECK(core.can_insert(vitality::ByteOffset{0}));
    CHECK(!core.can_insert(vitality::ByteOffset{-1}));

    core.insert(vitality::ByteOffset{0}, "abc");
    CHECK(core.text() == std::string_view("abc"));
    CHECK(core.line_count() == 1);
    CHECK(core.piece_count() == 1);
    CHECK(core.check_invariants());

    core.insert(vitality::ByteOffset{3}, "de");
    CHECK(core.text() == std::string_view("abcde"));
    CHECK(core.piece_count() == 1);
    CHECK(core.check_invariants());

    core.insert(vitality::ByteOffset{2}, "\nZ");
    CHECK(core.text() == std::string_view("ab\nZcde"));
    CHECK(core.line_count() == 2);
    CHECK(core.line_start_offset(1) == 3);
    CHECK(core.check_invariants());

    CHECK(core.can_erase(vitality::ByteRange{
        .start = vitality::ByteOffset{2},
        .length = vitality::ByteCount{2},
    }));
    CHECK(!core.can_erase(vitality::ByteRange{
        .start = vitality::ByteOffset{-1},
        .length = vitality::ByteCount{1},
    }));

    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{2},
        .length = vitality::ByteCount{2},
    });
    CHECK(core.text() == std::string_view("abcde"));
    CHECK(core.line_count() == 1);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore middle erase keeps the minimal non-contiguous original pieces") {
    vitality::buffer_internal::ImplicitTreapStorageCore core("abcdef");

    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{2},
        .length = vitality::ByteCount{2},
    });

    CHECK(core.text() == std::string_view("abef"));
    CHECK(core.piece_count() == 2);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore insert then erase restores a single original piece") {
    vitality::buffer_internal::ImplicitTreapStorageCore core("abcdef");

    core.insert(vitality::ByteOffset{3}, "XYZ");
    CHECK(core.text() == std::string_view("abcXYZdef"));
    CHECK(core.check_invariants());

    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{3},
        .length = vitality::ByteCount{3},
    });

    CHECK(core.text() == std::string_view("abcdef"));
    CHECK(core.piece_count() == 1);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore repeated insert erase cycles keep piece count bounded") {
    vitality::buffer_internal::ImplicitTreapStorageCore core("single line text");

    for (int index = 0; index < 64; ++index) {
        core.insert(vitality::ByteOffset{7}, "z");
        CHECK(core.check_invariants());
        core.erase(vitality::ByteRange{
            .start = vitality::ByteOffset{7},
            .length = vitality::ByteCount{1},
        });
        CHECK(core.text() == std::string_view("single line text"));
        CHECK(core.check_invariants());
        CHECK(core.piece_count() <= 1);
    }
}

TEST_CASE("ImplicitTreapStorageCore repeated append stays in one add-backed tail piece") {
    vitality::buffer_internal::ImplicitTreapStorageCore core;
    std::string expected_text;

    for (int index = 0; index < 32; ++index) {
        core.insert(vitality::ByteOffset{static_cast<int>(core.byte_count())}, "xy");
        expected_text += "xy";
        CHECK(core.check_invariants());
    }

    CHECK(core.text() == expected_text);
    CHECK(core.piece_count() == 1);
}

TEST_CASE("ImplicitTreapStorageCore compaction keeps text and invariants for an already compact tree") {
    vitality::buffer_internal::ImplicitTreapStorageCore core("alpha\nbeta\n");

    CHECK(core.compact_with_merge_budget(128) == 0);
    CHECK(core.text() == std::string_view("alpha\nbeta\n"));
    CHECK(core.piece_count() == 1);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore recent contiguous insert appends to the same add piece") {
    vitality::buffer_internal::ImplicitTreapStorageCore core;

    core.insert(vitality::ByteOffset{0}, "a");
    core.insert(vitality::ByteOffset{1}, "b");
    core.insert(vitality::ByteOffset{2}, "c");

    CHECK(core.text() == std::string_view("abc"));
    CHECK(core.piece_count() == 1);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore recent contiguous insert works in the middle of existing text") {
    vitality::buffer_internal::ImplicitTreapStorageCore core("XXYY");

    core.insert(vitality::ByteOffset{2}, "a");
    core.insert(vitality::ByteOffset{3}, "b");
    core.insert(vitality::ByteOffset{4}, "c");

    CHECK(core.text() == std::string_view("XXabcYY"));
    CHECK(core.piece_count() <= 3);
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore immediate backspace shrinks the recent typed suffix") {
    vitality::buffer_internal::ImplicitTreapStorageCore core;

    core.insert(vitality::ByteOffset{0}, "a");
    core.insert(vitality::ByteOffset{1}, "b");
    core.insert(vitality::ByteOffset{2}, "c");

    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{2},
        .length = vitality::ByteCount{1},
    });
    CHECK(core.text() == std::string_view("ab"));
    CHECK(core.check_invariants());

    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{1},
        .length = vitality::ByteCount{1},
    });
    CHECK(core.text() == std::string_view("a"));
    CHECK(core.check_invariants());

    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{0},
        .length = vitality::ByteCount{1},
    });
    CHECK(core.text().empty());
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore non contiguous insert invalidates recent edit state safely") {
    vitality::buffer_internal::ImplicitTreapStorageCore core;

    core.insert(vitality::ByteOffset{0}, "a");
    core.insert(vitality::ByteOffset{1}, "b");
    core.insert(vitality::ByteOffset{0}, "Z");
    core.insert(vitality::ByteOffset{3}, "c");

    CHECK(core.text() == std::string_view("Zabc"));
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore erase outside recent typed suffix falls back safely") {
    vitality::buffer_internal::ImplicitTreapStorageCore core;

    core.insert(vitality::ByteOffset{0}, "a");
    core.insert(vitality::ByteOffset{1}, "b");
    core.insert(vitality::ByteOffset{2}, "c");
    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{0},
        .length = vitality::ByteCount{1},
    });

    CHECK(core.text() == std::string_view("bc"));
    CHECK(core.check_invariants());
}

TEST_CASE("ImplicitTreapStorageCore compaction clears recent edit tracking safely") {
    vitality::buffer_internal::ImplicitTreapStorageCore core("XXYY");

    for (int index = 0; index < 3; ++index) {
        core.insert(vitality::ByteOffset{2 + index}, "a");
    }
    CHECK(core.text() == std::string_view("XXaaaYY"));
    CHECK(core.check_invariants());

    CHECK(core.compact_with_merge_budget(128) == 0);

    core.insert(vitality::ByteOffset{5}, "b");
    core.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{5},
        .length = vitality::ByteCount{1},
    });

    CHECK(core.text() == std::string_view("XXaaaYY"));
    CHECK(core.check_invariants());
}
