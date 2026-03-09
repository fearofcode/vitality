#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "file/FilePath.h"

namespace vitality::tests {

class TempFile {
public:
    explicit TempFile(const std::string &contents)
        : path_(std::filesystem::temp_directory_path() / make_name()) {
        std::ofstream output(path_, std::ios::out | std::ios::trunc);
        output << contents;
    }

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
        return "vitality-test-" + std::to_string(seed) + "-" + std::to_string(counter++) + ".txt";
    }

    std::filesystem::path path_;
};

[[nodiscard]] inline BufferLoadResult load_buffer_result_from_contents(const std::string &contents) {
    TempFile temp_file(contents);
    const FilePath file_path = FilePath::from_command_line_arg(temp_file.path().c_str());
    return TextBuffer::load_from_path(file_path);
}

[[nodiscard]] inline BufferLoadResult load_buffer_result_from_lines(const std::vector<std::string> &lines) {
    TempFile temp_file(lines);
    const FilePath file_path = FilePath::from_command_line_arg(temp_file.path().c_str());
    return TextBuffer::load_from_path(file_path);
}

[[nodiscard]] inline TextBuffer load_buffer_from_contents_or_require(const std::string &contents) {
    auto load_result = load_buffer_result_from_contents(contents);
    REQUIRE(load_result.success);
    return std::move(load_result.buffer);
}

[[nodiscard]] inline TextBuffer load_buffer_from_lines_or_require(const std::vector<std::string> &lines) {
    auto load_result = load_buffer_result_from_lines(lines);
    REQUIRE(load_result.success);
    return std::move(load_result.buffer);
}

[[nodiscard]] inline std::vector<std::string> sanitize_lines_for_buffer(std::vector<std::string> lines) {
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

[[nodiscard]] inline VisibleLineCount visible_line_count_for_tests(const std::int64_t count = 3) {
    return VisibleLineCount{count};
}

}  // namespace vitality::tests
