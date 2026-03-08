#include <chrono>
#include <cstdint>
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

void print_timing(const std::string_view name, const std::uint64_t checksum, const std::chrono::nanoseconds elapsed) {
    std::cout << name << " checksum=" << checksum << " ns=" << elapsed.count() << '\n';
}

}  // namespace

int run_benchmarks() {
    using clock = std::chrono::steady_clock;

    std::uint64_t checksum = 0;
    const auto inputs = benchmark_inputs();

    auto start = clock::now();
    std::vector<vitality::TextStorage> storages;
    storages.reserve(inputs.size());
    for (const std::string &input : inputs) {
        storages.push_back(vitality::TextStorage::from_utf8(input));
        checksum += storages.back().text().size();
    }
    print_timing("construct", checksum, clock::now() - start);

    start = clock::now();
    for (const vitality::TextStorage &storage : storages) {
        checksum += static_cast<std::uint64_t>(storage.line_count().value);
    }
    print_timing("line_count", checksum, clock::now() - start);

    start = clock::now();
    for (const vitality::TextStorage &storage : storages) {
        const int lines = storage.line_count().value;
        for (int index = 0; index < lines; index += std::max(lines / 32, 1)) {
            checksum += static_cast<std::uint64_t>(storage.line_text(vitality::LineIndex{index}).utf8_text.size());
        }
    }
    print_timing("random_line_text", checksum, clock::now() - start);

    start = clock::now();
    for (const vitality::TextStorage &storage : storages) {
        for (int index = 0; index < storage.line_count().value; ++index) {
            checksum += static_cast<std::uint64_t>(storage.line_length(vitality::LineIndex{index}).value);
        }
    }
    print_timing("sequential_line_scan", checksum, clock::now() - start);

    start = clock::now();
    for (const vitality::TextStorage &storage : storages) {
        checksum += static_cast<std::uint64_t>(storage.text().size());
    }
    print_timing("text_materialization", checksum, clock::now() - start);

    start = clock::now();
    for (const vitality::TextStorage &storage : storages) {
        const int lines = storage.line_count().value;
        for (int index = -3; index < lines + 3; ++index) {
            checksum += static_cast<std::uint64_t>(
                storage.clamp_cursor(vitality::ByteCursorPos{
                    .line = vitality::LineIndex{index},
                    .column = vitality::ByteColumn{index * 7},
                }).column.value);
        }
    }
    print_timing("clamp_cursor", checksum, clock::now() - start);

    start = clock::now();
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128; ++index) {
            const auto result = storage.insert(vitality::ByteOffset{storage.byte_count().value}, "xy");
            checksum += static_cast<std::uint64_t>(result.inserted_range.length.value);
        }
    }
    print_timing("append_insert", checksum, clock::now() - start);

    start = clock::now();
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128; ++index) {
            const int offset = storage.byte_count().value / 2;
            const auto result = storage.insert(vitality::ByteOffset{offset}, "middle");
            checksum += static_cast<std::uint64_t>(result.inserted_range.start.value);
        }
    }
    print_timing("middle_insert", checksum, clock::now() - start);

    start = clock::now();
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128 && storage.byte_count().value > 0; ++index) {
            const int start_offset = std::max(storage.byte_count().value / 2 - 1, 0);
            const int length = std::min(2, storage.byte_count().value - start_offset);
            const auto result = storage.erase(vitality::ByteRange{
                .start = vitality::ByteOffset{start_offset},
                .length = vitality::ByteCount{length},
            });
            checksum += static_cast<std::uint64_t>(result.erased_range.length.value);
        }
    }
    print_timing("middle_erase", checksum, clock::now() - start);

    start = clock::now();
    for (const std::string &input : inputs) {
        vitality::TextStorage storage = vitality::TextStorage::from_utf8(input);
        for (int index = 0; index < 128; ++index) {
            const auto inserted = storage.insert(vitality::ByteOffset{storage.byte_count().value / 2}, "z");
            const auto erased = storage.erase(inserted.inserted_range);
            checksum += static_cast<std::uint64_t>(erased.erased_range.length.value);
        }
    }
    print_timing("insert_erase_cycle", checksum, clock::now() - start);

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
