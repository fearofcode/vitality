#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rapidcheck/catch.h>
#include <rapidcheck/gen/Container.h>

#include "buffer/TextStorage.h"

namespace {

struct ReferenceMutationState {
    std::string text;
};

[[nodiscard]] std::vector<std::string> split_reference_lines(const std::string_view text) {
    std::vector<std::string> lines;
    if (text.empty()) {
        lines.emplace_back();
        return lines;
    }

    std::size_t line_start = 0;
    while (line_start < text.size()) {
        const std::size_t line_end = text.find('\n', line_start);
        const std::size_t end = (line_end == std::string_view::npos) ? text.size() : line_end;
        lines.emplace_back(text.substr(line_start, end - line_start));
        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (lines.empty()) {
        lines.emplace_back();
    }

    return lines;
}

}  // namespace

TEST_CASE("editable TextStorage matches a string reference model") {
    rc::prop("insert and erase keep storage in sync with the reference text", [] {
        const auto initial_text = *rc::gen::container<std::string>(
            *rc::gen::inRange<std::size_t>(0, static_cast<std::size_t>(64)),
            rc::gen::arbitrary<char>());
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(initial_text);
        ReferenceMutationState reference{.text = initial_text};

        const auto operations = *rc::gen::container<std::vector<int>>(
            *rc::gen::inRange<std::size_t>(1, static_cast<std::size_t>(40)),
            rc::gen::arbitrary<int>());

        for (const int op_seed : operations) {
            if ((op_seed & 1) == 0) {
                const int size = static_cast<int>(reference.text.size());
                const int normalized = size == 0 ? 0 : std::abs(op_seed) % (size + 1);
                const auto payload = *rc::gen::container<std::string>(
                    *rc::gen::inRange<std::size_t>(0, static_cast<std::size_t>(8)),
                    rc::gen::arbitrary<char>());

                const auto result = storage.insert(vitality::ByteOffset{normalized}, payload);
                RC_ASSERT(result.success);
                reference.text.insert(static_cast<std::size_t>(normalized), payload);
            } else {
                const int size = static_cast<int>(reference.text.size());
                const int normalized_start = size == 0 ? 0 : std::abs(op_seed) % (size + 1);
                const int max_length = size - normalized_start;
                const int normalized_length = max_length == 0 ? 0 : (std::abs(op_seed / 3) % (max_length + 1));

                const auto result = storage.erase(vitality::ByteRange{
                    .start = vitality::ByteOffset{normalized_start},
                    .length = vitality::ByteCount{normalized_length},
                });
                RC_ASSERT(result.success);
                reference.text.erase(
                    static_cast<std::size_t>(normalized_start),
                    static_cast<std::size_t>(normalized_length));
            }

            const auto reference_lines = split_reference_lines(reference.text);
            RC_ASSERT(storage.text() == reference.text);
            RC_ASSERT(storage.line_count().value == static_cast<int>(reference_lines.size()));
            RC_ASSERT(storage.check_invariants());

            for (int line_index = 0; line_index < storage.line_count().value; ++line_index) {
                RC_ASSERT(storage.line_text(vitality::LineIndex{line_index}).utf8_text ==
                          reference_lines[line_index]);
                RC_ASSERT(storage.line_length(vitality::LineIndex{line_index}).value ==
                          static_cast<int>(reference_lines[line_index].size()));
            }
        }
    });
}

TEST_CASE("editable TextStorage rejects invalid mutation requests") {
    vitality::TextStorage storage = vitality::TextStorage::from_utf8("abc");

    const auto bad_insert = storage.insert(vitality::ByteOffset{99}, "x");
    CHECK(!bad_insert.success);
    CHECK(storage.text() == std::string_view("abc"));

    const auto bad_erase = storage.erase(vitality::ByteRange{
        .start = vitality::ByteOffset{2},
        .length = vitality::ByteCount{9},
    });
    CHECK(!bad_erase.success);
    CHECK(storage.text() == std::string_view("abc"));
}
