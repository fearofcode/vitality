#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFontDatabase>
#include <QTextLayout>

#include "core/CoreTypes.h"
#include "layout/LayoutCursorOps.h"
#include "layout/QtLineLayout.h"

namespace {

[[nodiscard]] QApplication &test_application() {
    static int argc = 1;
    static char app_name[] = "vitality-layout-tests";
    static char *argv[] = {app_name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

[[nodiscard]] vitality::layout::VisualCursorQuery query_from(
    const vitality::LineIndex line,
    const std::string_view utf8_line,
    const std::int64_t byte_column) {
    return vitality::layout::VisualCursorQuery{
        .line = line,
        .utf8_line = utf8_line,
        .logical_cursor = vitality::LogicalGraphemeCursorPos{
            .line = line,
            .column = vitality::GraphemeBoundaryByteColumn{byte_column},
        },
    };
}

}  // namespace

TEST_CASE("QtLineLayout forces the code-editor line layout policy") {
    (void)test_application();

    QTextLayout layout(QString::fromUtf8("// السلام"), QFontDatabase::systemFont(QFontDatabase::FixedFont));
    vitality::layout::prepare_code_editor_line_layout(layout);

    CHECK(layout.textOption().textDirection() == Qt::LeftToRight);
    CHECK(layout.textOption().wrapMode() == QTextOption::NoWrap);
    CHECK(layout.cursorMoveStyle() == Qt::VisualMoveStyle);
}

TEST_CASE("LayoutCursorOps matches logical movement on ASCII lines") {
    (void)test_application();
    constexpr std::string_view line = "abc";

    const auto mapped = vitality::layout::logical_to_visual_cursor(query_from(vitality::LineIndex{0}, line, 1));
    REQUIRE(mapped.success);
    CHECK(mapped.visual_cursor.column.value == 1);

    const auto left = vitality::layout::visual_left_cursor(query_from(vitality::LineIndex{0}, line, 1));
    REQUIRE(left.success);
    CHECK(left.logical_cursor.column.value == 0);

    const auto right = vitality::layout::visual_right_cursor(query_from(vitality::LineIndex{0}, line, 1));
    REQUIRE(right.success);
    CHECK(right.logical_cursor.column.value == 2);

    const auto home = vitality::layout::visual_home_cursor(query_from(vitality::LineIndex{0}, line, 2));
    REQUIRE(home.success);
    CHECK(home.logical_cursor.column.value == 0);

    const auto end = vitality::layout::visual_end_cursor(query_from(vitality::LineIndex{0}, line, 1));
    REQUIRE(end.success);
    CHECK(end.logical_cursor.column.value == 3);
}

TEST_CASE("LayoutCursorOps uses visual cursor-stop ordering on mixed-direction code lines") {
    (void)test_application();
    constexpr std::string_view line = "// السلام";

    const std::vector<std::int64_t> boundaries{0, 1, 2, 3, 5, 7, 9, 11, 13, 15};
    std::vector<std::int64_t> visual_columns;
    visual_columns.reserve(boundaries.size());
    for (const std::int64_t boundary : boundaries) {
        const auto mapped = vitality::layout::logical_to_visual_cursor(
            query_from(vitality::LineIndex{0}, line, boundary));
        REQUIRE(mapped.success);
        visual_columns.push_back(mapped.visual_cursor.column.value);
    }

    std::vector<std::int64_t> expected_columns(boundaries.size());
    for (std::size_t index = 0; index < expected_columns.size(); ++index) {
        expected_columns[index] = static_cast<std::int64_t>(index);
    }
    std::sort(visual_columns.begin(), visual_columns.end());
    CHECK(visual_columns == expected_columns);

    const auto right_from_prefix = vitality::layout::visual_right_cursor(
        query_from(vitality::LineIndex{0}, line, 2));
    REQUIRE(right_from_prefix.success);
    CHECK(right_from_prefix.logical_cursor.column.value != 5);
    CHECK(right_from_prefix.logical_cursor.column.value == 15);

    const auto left_back = vitality::layout::visual_left_cursor(vitality::layout::VisualCursorQuery{
        .line = vitality::LineIndex{0},
        .utf8_line = line,
        .logical_cursor = right_from_prefix.logical_cursor,
    });
    REQUIRE(left_back.success);
    CHECK(left_back.logical_cursor.column.value == 2);

    const auto home = vitality::layout::visual_home_cursor(query_from(vitality::LineIndex{0}, line, 15));
    REQUIRE(home.success);
    CHECK(home.logical_cursor.column.value == 0);
}

TEST_CASE("LayoutCursorOps remains total on pure Arabic and respects grapheme boundaries") {
    (void)test_application();
    constexpr std::string_view arabic = "السلام";
    constexpr std::string_view emoji = "👩‍💻x";

    const auto arabic_home = vitality::layout::visual_home_cursor(
        query_from(vitality::LineIndex{0}, arabic, 0));
    REQUIRE(arabic_home.success);

    const auto arabic_end = vitality::layout::visual_end_cursor(
        query_from(vitality::LineIndex{0}, arabic, 0));
    REQUIRE(arabic_end.success);
    CHECK(arabic_end.logical_cursor.column.value != arabic_home.logical_cursor.column.value);

    const auto emoji_right = vitality::layout::visual_right_cursor(
        query_from(vitality::LineIndex{0}, emoji, 0));
    REQUIRE(emoji_right.success);
    CHECK(emoji_right.logical_cursor.column.value == 11);
}

TEST_CASE("LayoutCursorOps reports malformed UTF-8 failure explicitly") {
    (void)test_application();
    const std::string malformed("\x80x", 2);

    const auto result = vitality::layout::visual_right_cursor(
        query_from(vitality::LineIndex{0}, malformed, 0));
    CHECK(!result.success);
    CHECK(result.error == vitality::UnicodeError::InvalidUtf8);
}

TEST_CASE("LayoutCursorOps maps visual x positions back to logical grapheme cursors") {
    (void)test_application();
    constexpr std::string_view source = "// السلام";
    constexpr std::string_view target = "// 👩‍💻 zero width";

    const auto source_visual = vitality::layout::logical_to_visual_cursor(
        query_from(vitality::LineIndex{1}, source, 15));
    REQUIRE(source_visual.success);

    const auto target_cursor = vitality::layout::logical_cursor_for_visual_x(
        vitality::LineIndex{0},
        target,
        source_visual.visual_x);
    REQUIRE(target_cursor.success);
    CHECK(target_cursor.logical_cursor.column.value == 3);
}
