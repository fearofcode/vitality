#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QKeyEvent>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "TestHelpers.h"
#include "ui/StatusBarText.h"
#include "ui/EditorScrollArea.h"

namespace {

[[nodiscard]] QApplication &test_application() {
    static int argc = 1;
    static char app_name[] = "vitality-tests";
    static char *argv[] = {app_name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

void send_key(vitality::EditorScrollArea &editor, const int key) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&editor, &event);
}

}  // namespace

TEST_CASE("vertical movement preserves preferred column across shorter lines") {
    (void)test_application();
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require("abcdef\nx\nabcdef\n"));
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
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require("abcdef\nx\nabcdef\n"));
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
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require("abcdef\nx\nabcdef\nabcdef\n"));
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
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require("e\u0301x\nabcdef\n"));
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
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require(std::string(line)));
    editor.resize(400, 160);

    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 1);
    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 2);
    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 15);

    send_key(editor, Qt::Key_Right);
    CHECK(editor.cursor_for_tests().column.value == 13);

    send_key(editor, Qt::Key_Left);
    CHECK(editor.cursor_for_tests().column.value == 15);
}

TEST_CASE("Home and End use visual line edges on mixed Arabic comment lines") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string(line));
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require(std::string(line)));
    editor.resize(400, 160);

    editor.set_cursor_for_tests(vitality::ByteCursorPos{
        .line = vitality::LineIndex{0},
        .column = vitality::ByteColumn{15},
    });

    send_key(editor, Qt::Key_Home);
    CHECK(editor.cursor_for_tests().column.value == 0);

    const auto logical_end = buffer.move_end(editor.cursor_for_tests());
    send_key(editor, Qt::Key_End);
    CHECK(editor.cursor_for_tests().column.value != logical_end.column.value);
}

TEST_CASE("status bar column stays logical after visual bidi movement") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string(line));
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require(std::string(line)));
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
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string(contents));
    vitality::EditorScrollArea editor(vitality::tests::load_buffer_from_contents_or_require(std::string(contents)));
    editor.resize(900, 160);

    const vitality::ByteCursorPos starting_cursor{
        .line = vitality::LineIndex{1},
        .column = vitality::ByteColumn{15},
    };
    editor.set_cursor_for_tests(starting_cursor);

    const auto logical_preferred_column = buffer.preferred_column(starting_cursor);
    const auto logical_fallback_target = buffer.cursor_for_display_column(
        vitality::LineIndex{0},
        vitality::GraphemeColumn{logical_preferred_column.value});
    REQUIRE(logical_fallback_target.success);

    send_key(editor, Qt::Key_Up);
    CHECK(editor.cursor_for_tests().line.value == 0);
    CHECK(editor.cursor_for_tests().column.value != logical_fallback_target.cursor.column.value);
    CHECK(editor.has_preferred_visual_x_for_tests());
}
