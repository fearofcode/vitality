#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "file/FilePath.h"
#include "ui/StatusBarText.h"

namespace {

class TempFile {
public:
    explicit TempFile(const std::string &contents)
        : path_(std::filesystem::temp_directory_path() / make_name()) {
        std::ofstream output(path_, std::ios::out | std::ios::trunc);
        output << contents;
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
        return "vitality-status-" + std::to_string(seed) + "-" + std::to_string(counter++) + ".txt";
    }

    std::filesystem::path path_;
};

[[nodiscard]] vitality::TextBuffer load_buffer_from_contents(const std::string &contents) {
    TempFile temp_file(contents);
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());
    auto load_result = vitality::TextBuffer::load_from_path(file_path);
    REQUIRE(load_result.success);
    return std::move(load_result.buffer);
}

}  // namespace

TEST_CASE("status bar reports grapheme-based columns for valid UTF-8 text") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("こんにちは\n");

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
    const vitality::TextBuffer buffer = load_buffer_from_contents(std::string("\x80x\n", 3));

    const QString status = vitality::make_status_bar_text(
        buffer,
        vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{1},
        });

    CHECK(status.contains(QStringLiteral("Col 2")));
}

TEST_CASE("status bar keeps combining sequences to a single displayed column") {
    const vitality::TextBuffer buffer = load_buffer_from_contents("e\u0301x\n");

    const QString status = vitality::make_status_bar_text(
        buffer,
        vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{2},
        });

    CHECK(status.contains(QStringLiteral("Col 1")));
}
