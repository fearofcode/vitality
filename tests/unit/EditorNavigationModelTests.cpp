#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <QApplication>

#include "buffer/BufferTypes.h"
#include "buffer/TextBuffer.h"
#include "TestHelpers.h"
#include "ui/EditorNavigationModel.h"

namespace {

[[nodiscard]] QApplication &test_application() {
    static int argc = 1;
    static char app_name[] = "vitality-tests";
    static char *argv[] = {app_name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

}  // namespace

TEST_CASE("EditorNavigationModel uses visual left and right on mixed Arabic comment lines") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string(line));
    const vitality::EditorNavigationModel model(buffer);

    vitality::EditorNavigationState state{
        .cursor = vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{0},
        },
    };

    auto result = model.navigate(state, vitality::NavigationCommand::Right, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value == 1);
    REQUIRE(result.state.preferred_column.has_value());

    result = model.navigate(result.state, vitality::NavigationCommand::Right, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value == 2);

    result = model.navigate(result.state, vitality::NavigationCommand::Right, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value == 15);

    result = model.navigate(result.state, vitality::NavigationCommand::Right, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value == 13);

    result = model.navigate(result.state, vitality::NavigationCommand::Left, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value == 15);
}

TEST_CASE("EditorNavigationModel uses visual home and end instead of the logical fallback on mixed Arabic comment lines") {
    (void)test_application();
    constexpr std::string_view line = "// السلام\n";
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string(line));
    const vitality::EditorNavigationModel model(buffer);

    vitality::EditorNavigationState state{
        .cursor = vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{15},
        },
        .preferred_column = vitality::PreferredVisualColumn{99},
        .preferred_visual_x = vitality::VisualCursorX{99},
    };

    auto result = model.navigate(state, vitality::NavigationCommand::Home, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value == 0);
    REQUIRE(result.state.preferred_column.has_value());
    const auto home_preferred_column = result.state.preferred_column.value();  // NOLINT(bugprone-unchecked-optional-access)
    CHECK(home_preferred_column.value == buffer.preferred_column(result.state.cursor).value);

    const auto logical_end = buffer.move_end(result.state.cursor);
    result = model.navigate(result.state, vitality::NavigationCommand::End, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.column.value != logical_end.column.value);
    REQUIRE(result.state.preferred_column.has_value());
    const auto end_preferred_column = result.state.preferred_column.value();  // NOLINT(bugprone-unchecked-optional-access)
    CHECK(end_preferred_column.value == buffer.preferred_column(result.state.cursor).value);
}

TEST_CASE("EditorNavigationModel preserves preferred column across shorter lines") {
    (void)test_application();
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require("abcdef\nx\nabcdef\n");
    const vitality::EditorNavigationModel model(buffer);

    vitality::EditorNavigationState state{
        .cursor = vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{4},
        },
    };

    auto result = model.navigate(state, vitality::NavigationCommand::Down, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.line.value == 1);
    CHECK(result.state.cursor.column.value == 1);
    REQUIRE(result.state.preferred_column.has_value());
    const auto first_down_preferred_column = result.state.preferred_column.value();  // NOLINT(bugprone-unchecked-optional-access)
    CHECK(first_down_preferred_column.value == 4);

    result = model.navigate(result.state, vitality::NavigationCommand::Down, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.line.value == 2);
    CHECK(result.state.cursor.column.value == 4);
    REQUIRE(result.state.preferred_column.has_value());
    const auto second_down_preferred_column = result.state.preferred_column.value();  // NOLINT(bugprone-unchecked-optional-access)
    CHECK(second_down_preferred_column.value == 4);
}

TEST_CASE("EditorNavigationModel uses visual x for bidi vertical movement before the logical preferred-column fallback") {
    (void)test_application();
    constexpr std::string_view contents =
        "// 👩‍💻 zero width joiners I guess?\n"
        "// السلام عليكم\n";
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(std::string(contents));
    const vitality::EditorNavigationModel model(buffer);

    const vitality::ByteCursorPos starting_cursor{
        .line = vitality::LineIndex{1},
        .column = vitality::ByteColumn{15},
    };
    const auto logical_fallback_target = buffer.cursor_for_display_column(
        vitality::LineIndex{0},
        vitality::GraphemeColumn{buffer.preferred_column(starting_cursor).value});
    REQUIRE(logical_fallback_target.success);

    const auto result = model.navigate(
        vitality::EditorNavigationState{
            .cursor = starting_cursor,
        },
        vitality::NavigationCommand::Up,
        vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    CHECK(result.state.cursor.line.value == 0);
    CHECK(result.state.cursor.column.value != logical_fallback_target.cursor.column.value);
    CHECK(result.state.preferred_visual_x.has_value());
    REQUIRE(result.state.preferred_column.has_value());
}

TEST_CASE("EditorNavigationModel falls back to byte-based horizontal movement on malformed UTF-8") {
    (void)test_application();
    const std::string malformed_line("\x80x\n", 3);
    const vitality::TextBuffer buffer = vitality::tests::load_buffer_from_contents_or_require(malformed_line);
    const vitality::EditorNavigationModel model(buffer);

    const vitality::EditorNavigationState state{
        .cursor = vitality::ByteCursorPos{
            .line = vitality::LineIndex{0},
            .column = vitality::ByteColumn{0},
        },
    };

    const auto result = model.navigate(state, vitality::NavigationCommand::Right, vitality::tests::visible_line_count_for_tests());
    REQUIRE(result.handled);
    const auto expected = buffer.move_right(state.cursor);
    CHECK(result.state.cursor.line.value == expected.line.value);
    CHECK(result.state.cursor.column.value == expected.column.value);
}
