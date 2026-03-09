#include "ui/EditorNavigationModel.h"

#include <algorithm>

#include "buffer/BufferTypes.h"
#include "layout/LayoutCursorOps.h"

namespace vitality {

struct EditorNavigationModel::VisualNavigationQuery {
    LineText line_text;
    layout::VisualCursorQuery query;
};

EditorNavigationModel::EditorNavigationModel(const TextBuffer &buffer)
    : buffer_(buffer) {}

EditorNavigationResult EditorNavigationModel::navigate(
    EditorNavigationState state,
    const NavigationCommand command,
    const VisibleLineCount visible_lines) const {
    switch (command) {
    case NavigationCommand::Left: {
        auto visual_query = make_visual_navigation_query(state.cursor);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_left = layout::visual_left_cursor(visual_query->query);
            state.cursor = visual_left.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_left.logical_cursor)
                : buffer_.move_left(state.cursor);
        } else {
            state.cursor = buffer_.move_left(state.cursor);
        }
        state.preferred_column.reset();
        state.preferred_visual_x.reset();
        reset_preferred_column_from_cursor(state);
        reset_preferred_visual_x_from_cursor(state);
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::Right: {
        auto visual_query = make_visual_navigation_query(state.cursor);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_right = layout::visual_right_cursor(visual_query->query);
            state.cursor = visual_right.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_right.logical_cursor)
                : buffer_.move_right(state.cursor);
        } else {
            state.cursor = buffer_.move_right(state.cursor);
        }
        state.preferred_column.reset();
        state.preferred_visual_x.reset();
        reset_preferred_column_from_cursor(state);
        reset_preferred_visual_x_from_cursor(state);
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::Up: {
        if (!state.preferred_column.has_value()) {
            reset_preferred_column_from_cursor(state);
        }
        if (!state.preferred_visual_x.has_value()) {
            reset_preferred_visual_x_from_cursor(state);
        }

        const std::int64_t target_line = std::max<std::int64_t>(state.cursor.line.value - 1, 0);
        state.cursor = move_vertically_with_preferred_column(state, LineIndex{target_line});
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::Down: {
        if (!state.preferred_column.has_value()) {
            reset_preferred_column_from_cursor(state);
        }
        if (!state.preferred_visual_x.has_value()) {
            reset_preferred_visual_x_from_cursor(state);
        }

        const std::int64_t last_line = std::max<std::int64_t>(buffer_.line_count().value - 1, 0);
        const std::int64_t target_line = std::min<std::int64_t>(state.cursor.line.value + 1, last_line);
        state.cursor = move_vertically_with_preferred_column(state, LineIndex{target_line});
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::PageUp: {
        if (!state.preferred_column.has_value()) {
            reset_preferred_column_from_cursor(state);
        }
        if (!state.preferred_visual_x.has_value()) {
            reset_preferred_visual_x_from_cursor(state);
        }

        const std::int64_t target_line = std::max<std::int64_t>(
            state.cursor.line.value - std::max<std::int64_t>(visible_lines.value - 1, 1),
            0);
        state.cursor = move_vertically_with_preferred_column(state, LineIndex{target_line});
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::PageDown: {
        if (!state.preferred_column.has_value()) {
            reset_preferred_column_from_cursor(state);
        }
        if (!state.preferred_visual_x.has_value()) {
            reset_preferred_visual_x_from_cursor(state);
        }

        const std::int64_t last_line = std::max<std::int64_t>(buffer_.line_count().value - 1, 0);
        const std::int64_t target_line = std::min<std::int64_t>(
            state.cursor.line.value + std::max<std::int64_t>(visible_lines.value - 1, 1),
            last_line);
        state.cursor = move_vertically_with_preferred_column(state, LineIndex{target_line});
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::Home: {
        auto visual_query = make_visual_navigation_query(state.cursor);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_home = layout::visual_home_cursor(visual_query->query);
            state.cursor = visual_home.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_home.logical_cursor)
                : buffer_.move_home(state.cursor);
        } else {
            state.cursor = buffer_.move_home(state.cursor);
        }
        state.preferred_column.reset();
        state.preferred_visual_x.reset();
        reset_preferred_column_from_cursor(state);
        reset_preferred_visual_x_from_cursor(state);
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    case NavigationCommand::End: {
        auto visual_query = make_visual_navigation_query(state.cursor);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_end = layout::visual_end_cursor(visual_query->query);
            state.cursor = visual_end.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_end.logical_cursor)
                : buffer_.move_end(state.cursor);
        } else {
            state.cursor = buffer_.move_end(state.cursor);
        }
        state.preferred_column.reset();
        state.preferred_visual_x.reset();
        reset_preferred_column_from_cursor(state);
        reset_preferred_visual_x_from_cursor(state);
        return EditorNavigationResult{
            .state = state,
            .handled = true,
        };
    }
    }

    return EditorNavigationResult{
        .state = state,
        .handled = false,
    };
}

ByteCursorPos EditorNavigationModel::byte_cursor_from_logical_grapheme_cursor(
    const LogicalGraphemeCursorPos cursor) {
    return ByteCursorPos{
        .line = cursor.line,
        .column = ByteColumn{cursor.column.value},
    };
}

std::optional<EditorNavigationModel::VisualNavigationQuery> EditorNavigationModel::make_visual_navigation_query(
    const ByteCursorPos cursor) const {
    const auto logical_grapheme = buffer_.logical_grapheme_cursor(cursor);
    if (!logical_grapheme.success) {
        return std::nullopt;
    }

    LineText line_text = buffer_.line_text(logical_grapheme.cursor.line);
    return VisualNavigationQuery{
        .line_text = std::move(line_text),
        .query = layout::VisualCursorQuery{
            .line = logical_grapheme.cursor.line,
            .utf8_line = std::string_view{},
            .logical_cursor = logical_grapheme.cursor,
        },
    };
}

PreferredVisualColumn EditorNavigationModel::current_preferred_column(const EditorNavigationState &state) const {
    if (state.preferred_column.has_value()) {
        return *state.preferred_column;
    }

    return buffer_.preferred_column(state.cursor);
}

void EditorNavigationModel::reset_preferred_column_from_cursor(EditorNavigationState &state) const {
    state.preferred_column = buffer_.preferred_column(state.cursor);
}

std::optional<VisualCursorX> EditorNavigationModel::current_preferred_visual_x(
    const EditorNavigationState &state) const {
    return state.preferred_visual_x;
}

void EditorNavigationModel::reset_preferred_visual_x_from_cursor(EditorNavigationState &state) const {
    auto visual_query = make_visual_navigation_query(state.cursor);
    if (!visual_query.has_value()) {
        state.preferred_visual_x.reset();
        return;
    }

    visual_query->query.utf8_line = visual_query->line_text.utf8_text;
    const auto visual_cursor = layout::logical_to_visual_cursor(visual_query->query);
    if (!visual_cursor.success) {
        state.preferred_visual_x.reset();
        return;
    }

    state.preferred_visual_x = visual_cursor.visual_x;
}

ByteCursorPos EditorNavigationModel::move_vertically_with_preferred_column(
    const EditorNavigationState &state,
    const LineIndex target_line) const {
    if (const auto preferred_visual_x = current_preferred_visual_x(state); preferred_visual_x.has_value()) {
        const LineText line_text = buffer_.line_text(target_line);
        const auto visual_target = layout::logical_cursor_for_visual_x(
            target_line,
            line_text.utf8_text,
            *preferred_visual_x);
        if (visual_target.success) {
            return ByteCursorPos{
                .line = visual_target.logical_cursor.line,
                .column = ByteColumn{visual_target.logical_cursor.column.value},
            };
        }
    }

    const PreferredVisualColumn preferred = current_preferred_column(state);
    const auto target_cursor = buffer_.cursor_for_display_column(
        target_line,
        GraphemeColumn{preferred.value});
    if (target_cursor.success) {
        return ByteCursorPos{
            .line = target_cursor.cursor.line,
            .column = ByteColumn{target_cursor.cursor.column.value},
        };
    }

    return buffer_.clamp_cursor(ByteCursorPos{
        .line = target_line,
        .column = state.cursor.column,
    });
}

}  // namespace vitality
