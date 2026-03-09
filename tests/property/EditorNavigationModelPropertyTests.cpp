#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <rapidcheck/catch.h>
#include <rapidcheck/gen/Container.h>
#include <rapidcheck/gen/Text.h>

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

[[nodiscard]] vitality::ByteCursorPos make_cursor(const vitality::TextBuffer &buffer, const int line, const int column) {
    return buffer.clamp_cursor(vitality::ByteCursorPos{
        .line = vitality::LineIndex{line},
        .column = vitality::ByteColumn{column},
    });
}

[[nodiscard]] bool is_valid_cursor(const vitality::TextBuffer &buffer, const vitality::ByteCursorPos cursor) {
    if (cursor.line.value < 0 || cursor.line.value >= buffer.line_count().value) {
        return false;
    }

    const std::int64_t line_length = buffer.line_length(cursor.line).value;
    return cursor.column.value >= 0 && cursor.column.value <= line_length;
}

[[nodiscard]] vitality::NavigationCommand command_from_int(const int value) {
    switch (value % 8) {
    case 0:
        return vitality::NavigationCommand::Left;
    case 1:
        return vitality::NavigationCommand::Right;
    case 2:
        return vitality::NavigationCommand::Up;
    case 3:
        return vitality::NavigationCommand::Down;
    case 4:
        return vitality::NavigationCommand::PageUp;
    case 5:
        return vitality::NavigationCommand::PageDown;
    case 6:
        return vitality::NavigationCommand::Home;
    default:
        return vitality::NavigationCommand::End;
    }
}

}  // namespace

TEST_CASE("EditorNavigationModel command sequences remain total and keep the cursor in bounds") {
    (void)test_application();
    rc::prop("every command sequence yields a handled result and a valid cursor", [] {
        auto raw_lines = *rc::gen::container<std::vector<std::string>>(
            *rc::gen::inRange<std::size_t>(1, static_cast<std::size_t>(8)),
            rc::gen::string<std::string>());
        raw_lines = vitality::tests::sanitize_lines_for_buffer(std::move(raw_lines));

        auto load_result = vitality::tests::load_buffer_result_from_lines(raw_lines);
        RC_ASSERT(load_result.success);
        const vitality::TextBuffer buffer = std::move(load_result.buffer);
        const vitality::EditorNavigationModel model(buffer);
        vitality::EditorNavigationState state{
            .cursor = make_cursor(
                buffer,
                *rc::gen::arbitrary<int>(),
                *rc::gen::arbitrary<int>()),
        };

        const auto raw_commands = *rc::gen::container<std::vector<int>>(
            *rc::gen::inRange<std::size_t>(1, static_cast<std::size_t>(32)),
            rc::gen::arbitrary<int>());
        const vitality::VisibleLineCount visible_lines{
            *rc::gen::inRange<std::int64_t>(1, 6),
        };

        for (const int raw_command : raw_commands) {
            const vitality::NavigationCommand command = command_from_int(raw_command);
            const auto result = model.navigate(state, command, visible_lines);

            RC_ASSERT(result.handled);
            RC_ASSERT(is_valid_cursor(buffer, result.state.cursor));

            if (command == vitality::NavigationCommand::Left
                || command == vitality::NavigationCommand::Right
                || command == vitality::NavigationCommand::Home
                || command == vitality::NavigationCommand::End) {
                RC_ASSERT(result.state.preferred_column.has_value());
            }

            if (command == vitality::NavigationCommand::Up
                || command == vitality::NavigationCommand::Down
                || command == vitality::NavigationCommand::PageUp
                || command == vitality::NavigationCommand::PageDown) {
                RC_ASSERT(result.state.preferred_column.has_value());
            }

            state = result.state;
        }
    });
}
