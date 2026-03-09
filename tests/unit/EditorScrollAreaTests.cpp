#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QKeyEvent>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "file/FilePath.h"
#include "layout/LayoutCursorOps.h"
#include "ui/StatusBarText.h"
#include "ui/EditorScrollArea.h"

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
        return "vitality-editor-" + std::to_string(seed) + "-" + std::to_string(counter++) + ".txt";
    }

    std::filesystem::path path_;
};

[[nodiscard]] QApplication &test_application() {
    static int argc = 1;
    static char app_name[] = "vitality-tests";
    static char *argv[] = {app_name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

[[nodiscard]] vitality::TextBuffer load_buffer_from_contents(const std::string &contents) {
    TempFile temp_file(contents);
    const vitality::FilePath file_path = vitality::FilePath::from_command_line_arg(temp_file.path().c_str());
    auto load_result = vitality::TextBuffer::load_from_path(file_path);
    REQUIRE(load_result.success);
    return std::move(load_result.buffer);
}

void send_key(vitality::EditorScrollArea &editor, const int key) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&editor, &event);
}

}  // namespace

TEST_CASE("vertical movement preserves preferred column across shorter lines") {
    (void)test_application();
    vitality::EditorScrollArea editor(load_buffer_from_contents("abcdef\nx\nabcdef\n"));
    editor.resize(400, 160);

    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    REQUIRE(editor.cursor_for_tests().column.value == 4);

    send_key(editor, Qt::Key_Down);
    CHECK(editor.cursor_for_tests().line.value == 1);
    CHECK(editor.cursor_for_tests().column.value == 1);
    REQUIRE(editor.has_preferred_column_for_tests());
    const std::int64_t preferred_after_first_down_value = editor.preferred_column_value_for_tests().value;
    CHECK(preferred_after_first_down_value == 4);

    send_key(editor, Qt::Key_Down);
    CHECK(editor.cursor_for_tests().line.value == 2);
    CHECK(editor.cursor_for_tests().column.value == 4);
    REQUIRE(editor.has_preferred_column_for_tests());
    const std::int64_t preferred_after_second_down_value = editor.preferred_column_value_for_tests().value;
    CHECK(preferred_after_second_down_value == 4);
}

TEST_CASE("horizontal movement resets preferred column state") {
    (void)test_application();
    vitality::EditorScrollArea editor(load_buffer_from_contents("abcdef\nx\nabcdef\n"));
    editor.resize(400, 160);

    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Down);
    REQUIRE(editor.has_preferred_column_for_tests());
    const std::int64_t preferred_after_down_value = editor.preferred_column_value_for_tests().value;
    CHECK(preferred_after_down_value == 4);

    send_key(editor, Qt::Key_Left);
    REQUIRE(editor.has_preferred_column_for_tests());
    const std::int64_t preferred_after_left_value = editor.preferred_column_value_for_tests().value;
    CHECK(preferred_after_left_value == 0);

    send_key(editor, Qt::Key_Down);
    CHECK(editor.cursor_for_tests().line.value == 2);
    CHECK(editor.cursor_for_tests().column.value == 0);
}

TEST_CASE("page movement preserves preferred column") {
    (void)test_application();
    vitality::EditorScrollArea editor(load_buffer_from_contents("abcdef\nx\nabcdef\nabcdef\n"));
    editor.resize(400, 80);

    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_PageDown);
    REQUIRE(editor.has_preferred_column_for_tests());
    const std::int64_t preferred_after_page_down_value = editor.preferred_column_value_for_tests().value;
    CHECK(preferred_after_page_down_value == 3);
    CHECK(editor.cursor_for_tests().line.value >= 1);

    send_key(editor, Qt::Key_PageUp);
    CHECK(editor.cursor_for_tests().line.value == 0);
    CHECK(editor.cursor_for_tests().column.value == 3);
}

TEST_CASE("vertical movement derives preferred column from containing grapheme cluster") {
    (void)test_application();
    vitality::EditorScrollArea editor(load_buffer_from_contents("e\u0301x\nabcdef\n"));
    editor.resize(400, 160);

    editor.set_cursor_for_tests(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{2},
    });

    send_key(editor, Qt::Key_Down);
    REQUIRE(editor.has_preferred_column_for_tests());
    const std::int64_t preferred_after_unicode_down_value = editor.preferred_column_value_for_tests().value;
    CHECK(preferred_after_unicode_down_value == 0);
    CHECK(editor.cursor_for_tests().line.value == 1);
    CHECK(editor.cursor_for_tests().column.value == 0);
}

TEST_CASE("mixed Arabic comment line uses visual Left and Right in the editor path") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    vitality::EditorScrollArea editor(load_buffer_from_contents(std::string(line)));
    editor.resize(400, 160);

    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 1);
    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 2);
    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 15);

    const vitality::TextBuffer buffer = load_buffer_from_contents(std::string(line));
    const auto logical_grapheme = buffer.logical_grapheme_cursor(editor.cursor_for_tests());
    REQUIRE(logical_grapheme.success);
    const auto expected_right = vitality::layout::visual_right_cursor(vitality::layout::VisualCursorQuery{
        .line = vitality::LineIndex{0},
        .utf8_line = buffer.line_text(vitality::LineIndex{0}).utf8_text,
        .logical_cursor = logical_grapheme.cursor,
    });
    REQUIRE(expected_right.success);

    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == expected_right.logical_cursor.column.value);
    CHECK(editor.cursor_for_tests().column.value != 5);

    send_key(editor, Qt::Key_Left);
    CHECK(editor.cursor_for_tests().column.value == 15);
}

TEST_CASE("Home and End use visual line edges on mixed Arabic comment lines") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    const vitality::TextBuffer buffer = load_buffer_from_contents(std::string(line));
    vitality::EditorScrollArea editor(load_buffer_from_contents(std::string(line)));
    editor.resize(400, 160);

    editor.set_cursor_for_tests(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{15},
    });

    auto logical_grapheme = buffer.logical_grapheme_cursor(editor.cursor_for_tests());
    REQUIRE(logical_grapheme.success);
    const auto expected_home = vitality::layout::visual_home_cursor(vitality::layout::VisualCursorQuery{
        .line = vitality::LineIndex{0},
        .utf8_line = buffer.line_text(vitality::LineIndex{0}).utf8_text,
        .logical_cursor = logical_grapheme.cursor,
    });
    REQUIRE(expected_home.success);

    send_key(editor, Qt::Key_Home);
    CHECK(editor.cursor_for_tests().column.value == expected_home.logical_cursor.column.value);

    logical_grapheme = buffer.logical_grapheme_cursor(editor.cursor_for_tests());
    REQUIRE(logical_grapheme.success);
    const auto expected_end = vitality::layout::visual_end_cursor(vitality::layout::VisualCursorQuery{
        .line = vitality::LineIndex{0},
        .utf8_line = buffer.line_text(vitality::LineIndex{0}).utf8_text,
        .logical_cursor = logical_grapheme.cursor,
    });
    REQUIRE(expected_end.success);

    send_key(editor, Qt::Key_End);
    CHECK(editor.cursor_for_tests().column.value == expected_end.logical_cursor.column.value);
}

TEST_CASE("status bar column stays logical after visual bidi movement") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    const vitality::TextBuffer buffer = load_buffer_from_contents(std::string(line));
    vitality::EditorScrollArea editor(load_buffer_from_contents(std::string(line)));
    editor.resize(400, 160);

    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);
    send_key(editor, Qt::Key_Right);

    const auto status_text = make_status_bar_text(buffer, editor.cursor_for_tests());
    const auto logical_column = buffer.display_column(editor.cursor_for_tests());
    REQUIRE(logical_column.success);
    CHECK(status_text.contains(QStringLiteral("Col %1").arg(logical_column.column.value + 1)));
}

TEST_CASE("vertical movement from a visual bidi stop follows visual x rather than logical display column") {
    (void)test_application();
    constexpr std::string_view contents =
        "// 👩‍💻 zero width joiners I guess?\n"
        "// السلام عليكم\n";
    const vitality::TextBuffer buffer = load_buffer_from_contents(std::string(contents));
    vitality::EditorScrollArea editor(load_buffer_from_contents(std::string(contents)));
    editor.resize(900, 160);

    const auto starting_cursor = vitality::layout::visual_right_cursor(vitality::layout::VisualCursorQuery{
        .line = vitality::LineIndex{1},
        .utf8_line = buffer.line_text(vitality::LineIndex{1}).utf8_text,
        .logical_cursor = vitality::LogicalGraphemeCursorPos{
            .line = vitality::LineIndex{1},
            .column = vitality::GraphemeBoundaryByteColumn{2},
        },
    });
    REQUIRE(starting_cursor.success);
    editor.set_cursor_for_tests(vitality::ByteCursorPos{
        .line = starting_cursor.logical_cursor.line,
        .column = vitality::ByteColumn{starting_cursor.logical_cursor.column.value},
    });

    const auto source_visual = vitality::layout::logical_to_visual_cursor(vitality::layout::VisualCursorQuery{
        .line = vitality::LineIndex{1},
        .utf8_line = buffer.line_text(vitality::LineIndex{1}).utf8_text,
        .logical_cursor = starting_cursor.logical_cursor,
    });
    REQUIRE(source_visual.success);
    const auto expected_target = vitality::layout::logical_cursor_for_visual_x(
        vitality::LineIndex{0},
        buffer.line_text(vitality::LineIndex{0}).utf8_text,
        source_visual.visual_x);
    REQUIRE(expected_target.success);

    send_key(editor, Qt::Key_Up);
    CHECK(editor.cursor_for_tests().line.value == 0);
    CHECK(editor.cursor_for_tests().column.value == expected_target.logical_cursor.column.value);
    CHECK(editor.has_preferred_visual_x_for_tests());
}
