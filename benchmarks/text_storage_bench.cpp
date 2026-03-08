#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "buffer/TextStorage.h"

namespace {

[[nodiscard]] std::string repeat_lines(const std::string_view pattern, const std::size_t count) {
    std::string text;
    text.reserve(pattern.size() * count);
    for (std::size_t index = 0; index < count; ++index) {
        text.append(pattern);
    }
    return text;
}

[[nodiscard]] std::vector<std::string> benchmark_inputs() {
    return {
        repeat_lines("alpha beta gamma delta epsilon\n", 512),
        repeat_lines("こんにちは\n👩‍💻\nالسلام عليكم\n", 1024),
        repeat_lines("x\n", 50000),
        repeat_lines("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\n", 4096),
    };
}

enum class TypingOpKind : std::uint8_t {
    InsertByte,
    Backspace,
};

struct TypingOp {
    TypingOpKind kind = TypingOpKind::InsertByte;
    char byte = '\0';
};

struct TypingScenario {
    std::string initial_text;
    std::int64_t initial_cursor = 0;
    std::vector<TypingOp> ops;
};

[[nodiscard]] std::vector<TypingOp> build_typing_script(
    const std::vector<std::string_view> &fragments,
    const std::size_t total_operations,
    const std::string_view replacement_bytes,
    const std::size_t correction_interval) {
    std::vector<TypingOp> ops;
    ops.reserve(total_operations);

    std::size_t fragment_index = 0;
    std::size_t char_index = 0;
    std::size_t inserted_since_correction = 0;

    while (ops.size() < total_operations) {
        const std::string_view fragment = fragments[fragment_index % fragments.size()];
        if (fragment.empty()) {
            ++fragment_index;
            char_index = 0;
            continue;
        }

        if (char_index == fragment.size()) {
            ++fragment_index;
            char_index = 0;
            continue;
        }

        ops.push_back(TypingOp{
            .kind = TypingOpKind::InsertByte,
            .byte = fragment[char_index],
        });
        ++char_index;
        ++inserted_since_correction;

        const std::size_t correction_cost = replacement_bytes.size() * 2;
        if (inserted_since_correction < correction_interval || correction_cost == 0) {
            continue;
        }
        if (ops.size() + correction_cost > total_operations) {
            continue;
        }

        inserted_since_correction = 0;
        for (std::size_t index = 0; index < replacement_bytes.size(); ++index) {
            ops.push_back(TypingOp{
                .kind = TypingOpKind::Backspace,
            });
        }
        for (const char byte : replacement_bytes) {
            ops.push_back(TypingOp{
                .kind = TypingOpKind::InsertByte,
                .byte = byte,
            });
        }
    }

    return ops;
}

[[nodiscard]] std::vector<TypingOp> essay_script() {
    return build_typing_script(
        {
            "This is a short essay sentence about editor structure.",
            " It keeps typing one byte at a time.",
            "\n",
        },
        4096,
        "ed ",
        72);
}

[[nodiscard]] std::vector<TypingOp> comment_script() {
    return build_typing_script(
        {
            "// benchmark comment about typing behavior\n",
            "/* detail: storage fast path and backspace */\n",
        },
        4096,
        "ok",
        48);
}

[[nodiscard]] std::vector<TypingOp> documentation_script() {
    return build_typing_script(
        {
            "## Notes\n",
            "- typing benchmark for contiguous edits\n",
            "The storage layer should stay internal.\n",
        },
        4096,
        "text",
        96);
}

[[nodiscard]] std::vector<TypingOp> forward_only_script(const std::vector<TypingOp> &ops) {
    std::vector<TypingOp> forward;
    forward.reserve(ops.size());
    for (const TypingOp op : ops) {
        if (op.kind == TypingOpKind::InsertByte) {
            forward.push_back(op);
        }
    }
    return forward;
}

[[nodiscard]] TypingScenario make_typing_scenario(
    const std::string_view prefix,
    std::vector<TypingOp> ops,
    const std::string_view suffix) {
    TypingScenario scenario;
    scenario.initial_text = std::string(prefix);
    scenario.initial_text += suffix;
    scenario.initial_cursor = static_cast<std::int64_t>(prefix.size());
    scenario.ops = std::move(ops);
    return scenario;
}

[[nodiscard]] std::vector<TypingScenario> typing_scenarios() {
    return {
        make_typing_scenario(
            "Dear reader,\n\n",
            essay_script(),
            "\n\nSincerely,\nSomeone\n"),
        make_typing_scenario(
            "int main() {\n    ",
            comment_script(),
            "\n    return 0;\n}\n"),
        make_typing_scenario(
            "# Title\n\n",
            documentation_script(),
            "\n\n## Next\nMore text follows.\n"),
    };
}

void print_timing(
    const std::string_view name,
    const std::uint64_t checksum,
    const std::uint64_t operations,
    const std::chrono::nanoseconds elapsed) {
    const double microseconds_per_operation = operations == 0
        ? 0.0
        : static_cast<double>(elapsed.count()) / 1000.0 / static_cast<double>(operations);
    std::cout << std::fixed << std::setprecision(4)
              << name
              << " checksum=" << checksum
              << " ops=" << operations
              << " ns=" << elapsed.count()
              << " us_per_op=" << microseconds_per_operation
              << '\n';
}

void print_cycle_timing(
    const std::string_view name,
    const std::uint64_t checksum,
    const std::uint64_t cycles,
    const std::uint64_t edits,
    const std::chrono::nanoseconds elapsed) {
    const double microseconds_per_cycle = cycles == 0
        ? 0.0
        : static_cast<double>(elapsed.count()) / 1000.0 / static_cast<double>(cycles);
    const double microseconds_per_edit = edits == 0
        ? 0.0
        : static_cast<double>(elapsed.count()) / 1000.0 / static_cast<double>(edits);
    std::cout << std::fixed << std::setprecision(4)
              << name
              << " checksum=" << checksum
              << " cycles=" << cycles
              << " edits=" << edits
              << " ns=" << elapsed.count()
              << " us_per_cycle=" << microseconds_per_cycle
              << " us_per_edit=" << microseconds_per_edit
              << '\n';
}

}  // namespace

int run_benchmarks() {
    using clock = std::chrono::steady_clock;

    std::uint64_t checksum = 0;
    const auto inputs = benchmark_inputs();

    auto start = clock::now();
    std::vector<vitality::TextStorage> storages;
    storages.reserve(inputs.size());
    std::uint64_t operations = 0;
    for (const std::string &input : inputs) {
        storages.push_back(vitality::TextStorage::from_utf8(input));
        checksum += storages.back().text().size();
        ++operations;
    }
    print_timing("construct", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const vitality::TextStorage &storage : storages) {
        checksum += static_cast<std::uint64_t>(storage.line_count().value);
        ++operations;
    }
    print_timing("line_count", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const vitality::TextStorage &storage : storages) {
        const std::int64_t lines = storage.line_count().value;
        for (std::int64_t index = 0; index < lines; index += std::max<std::int64_t>(lines / 32, 1)) {
            checksum += static_cast<std::uint64_t>(storage.line_text(vitality::LineIndex{index}).utf8_text.size());
            ++operations;
        }
    }
    print_timing("random_line_text", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const vitality::TextStorage &storage : storages) {
        for (std::int64_t index = 0; index < storage.line_count().value; ++index) {
            checksum += static_cast<std::uint64_t>(storage.line_length(vitality::LineIndex{index}).value);
            ++operations;
        }
    }
    print_timing("sequential_line_scan", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const vitality::TextStorage &storage : storages) {
        checksum += static_cast<std::uint64_t>(storage.text().size());
        ++operations;
    }
    print_timing("text_materialization", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const vitality::TextStorage &storage : storages) {
        const std::int64_t lines = storage.line_count().value;
        for (std::int64_t index = -3; index < lines + 3; ++index) {
            checksum += static_cast<std::uint64_t>(
                storage.clamp_cursor(vitality::ByteCursorPos{
                    .line = vitality::LineIndex{index},
                    .column = vitality::ByteColumn{index * 7},
                }).column.value);
            ++operations;
        }
    }
    print_timing("clamp_cursor", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128; ++index) {
            const auto result = storage.insert(vitality::ByteOffset{storage.byte_count().value}, "xy");
            checksum += static_cast<std::uint64_t>(result.inserted_range.length.value);
            ++operations;
        }
    }
    print_timing("append_insert", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128; ++index) {
            const std::int64_t offset = storage.byte_count().value / 2;
            const auto result = storage.insert(vitality::ByteOffset{offset}, "middle");
            checksum += static_cast<std::uint64_t>(result.inserted_range.start.value);
            ++operations;
        }
    }
    print_timing("middle_insert", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128 && storage.byte_count().value > 0; ++index) {
            const std::int64_t start_offset = std::max<std::int64_t>(storage.byte_count().value / 2 - 1, 0);
            const std::int64_t length = std::min<std::int64_t>(2, storage.byte_count().value - start_offset);
            const auto result = storage.erase(vitality::ByteRange{
                .start = vitality::ByteOffset{start_offset},
                .length = vitality::ByteCount{length},
            });
            checksum += static_cast<std::uint64_t>(result.erased_range.length.value);
            ++operations;
        }
    }
    print_timing("middle_erase", checksum, operations, clock::now() - start);

    start = clock::now();
    std::uint64_t cycles = 0;
    std::uint64_t edits = 0;
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128; ++index) {
            const auto inserted = storage.insert(vitality::ByteOffset{storage.byte_count().value / 2}, "z");
            const auto erased = storage.erase(inserted.inserted_range);
            checksum += static_cast<std::uint64_t>(erased.erased_range.length.value);
            ++cycles;
            edits += 2;
        }
    }
    print_cycle_timing("insert_erase_cycle", checksum, cycles, edits, clock::now() - start);

    const std::vector<TypingScenario> typing_inputs = typing_scenarios();

    start = clock::now();
    operations = 0;
    for (const auto &scenario : typing_inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(scenario.initial_text);
        std::int64_t cursor = scenario.initial_cursor;
        for (const TypingOp op : forward_only_script(scenario.ops)) {
            const auto result = storage.insert(
                vitality::ByteOffset{cursor},
                std::string_view(&op.byte, 1));
            checksum += static_cast<std::uint64_t>(result.inserted_range.length.value);
            ++cursor;
            ++operations;
        }
        checksum += static_cast<std::uint64_t>(storage.text().size());
    }
    print_timing("typing_forward", checksum, operations, clock::now() - start);

    start = clock::now();
    operations = 0;
    for (const auto &scenario : typing_inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(scenario.initial_text);
        std::int64_t cursor = scenario.initial_cursor;
        for (const TypingOp op : scenario.ops) {
            if (op.kind == TypingOpKind::InsertByte) {
                const auto result = storage.insert(
                    vitality::ByteOffset{cursor},
                    std::string_view(&op.byte, 1));
                checksum += static_cast<std::uint64_t>(result.inserted_range.length.value);
                ++cursor;
                ++operations;
                continue;
            }

            if (cursor == 0 || storage.byte_count().value == 0) {
                continue;
            }

            const auto result = storage.erase(vitality::ByteRange{
                .start = vitality::ByteOffset{cursor - 1},
                .length = vitality::ByteCount{1},
            });
            checksum += static_cast<std::uint64_t>(result.erased_range.length.value);
            --cursor;
            ++operations;
        }
        checksum += static_cast<std::uint64_t>(storage.text().size());
    }
    print_timing("typing_mix", checksum, operations, clock::now() - start);

    return static_cast<int>(checksum == 0);
}

int main() {
    try {
        return run_benchmarks();
    } catch (const std::exception &error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "benchmark failed: unknown exception\n";
        return 1;
    }
}
