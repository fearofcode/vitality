#pragma once

#include <cstdint>
#include <optional>

#include "buffer/TextBuffer.h"
#include "core/CoreTypes.h"

namespace vitality {

enum class NavigationCommand : std::uint8_t {
    Left,
    Right,
    Up,
    Down,
    PageUp,
    PageDown,
    Home,
    End,
};

// This state is view-local navigation intent, not document state. The editor
// stores the logical cursor plus the transient hints needed to make repeated
// vertical movement and bidi-aware visual movement feel like a real editor.
struct EditorNavigationState {
    ByteCursorPos cursor;
    std::optional<PreferredVisualColumn> preferred_column;
    std::optional<VisualCursorX> preferred_visual_x;
};

struct EditorNavigationResult {
    EditorNavigationState state;
    bool handled = false;
};

// EditorNavigationModel owns the keyboard-navigation policy that sits above
// the logical buffer APIs and below the widget's repaint/scrolling concerns.
// It decides which logical cursor should be stored after a key command and how
// the transient preferred-column / preferred-visual-x state should change.
//
// The widget still owns Qt event translation, viewport updates, and scrollbar
// work. The buffer still owns logical document queries. This class is the
// narrow place where those pieces are combined into editor navigation rules.
class EditorNavigationModel {
public:
    explicit EditorNavigationModel(const TextBuffer &buffer);

    [[nodiscard]] EditorNavigationResult navigate(
        EditorNavigationState state,
        NavigationCommand command,
        VisibleLineCount visible_lines) const;

private:
    struct VisualNavigationQuery;

    [[nodiscard]] static ByteCursorPos byte_cursor_from_logical_grapheme_cursor(
        LogicalGraphemeCursorPos cursor);
    [[nodiscard]] std::optional<VisualNavigationQuery> make_visual_navigation_query(
        ByteCursorPos cursor) const;
    [[nodiscard]] PreferredVisualColumn current_preferred_column(
        const EditorNavigationState &state) const;
    void reset_preferred_column_from_cursor(EditorNavigationState &state) const;
    [[nodiscard]] std::optional<VisualCursorX> current_preferred_visual_x(
        const EditorNavigationState &state) const;
    void reset_preferred_visual_x_from_cursor(EditorNavigationState &state) const;
    [[nodiscard]] ByteCursorPos move_vertically_with_preferred_column(
        const EditorNavigationState &state,
        LineIndex target_line) const;

    const TextBuffer &buffer_;
};

}  // namespace vitality
