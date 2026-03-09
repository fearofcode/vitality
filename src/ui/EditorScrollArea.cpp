#include "ui/EditorScrollArea.h"

#include <algorithm>
#include <limits>

#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QTextLayout>

#include "buffer/BufferTypes.h"
#include "layout/LayoutCursorOps.h"
#include "layout/QtLineLayout.h"
#include "unicode/UnicodeLineOps.h"
#include "ui/EditorNavigationModel.h"
#include "ui/StatusBarText.h"
#include "ui/Utf8QtTextMapper.h"

// and now here is some non-Latin text to deal with:
// こんにちは
// 👩‍💻 zero width joiners I guess?
// السلام عليكم

namespace vitality {

namespace {

[[nodiscard]] int clamp_to_qt_int(const std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(
        value,
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}

[[nodiscard]] int qt_utf16_column_to_qt_int(const QtUtf16Column column) {
    return clamp_to_qt_int(column.value);
}

[[nodiscard]] int ibeam_cursor_width() {
    return 2;
}

[[nodiscard]] QRect ibeam_cursor_rect(
    const QTextLine &line,
    const int row_top,
    const int line_height_pixels,
    const QFontMetrics &font_metrics,
    const qreal cursor_x) {
    const int cursor_width = ibeam_cursor_width();
    const int x = static_cast<int>(std::round(cursor_x));
    const int caret_height = std::max(font_metrics.height() - 2, 1);

    if (!line.isValid()) {
        const int fallback_top = row_top + std::max((line_height_pixels - caret_height) / 2, 0);
        return QRect(x, fallback_top, cursor_width, caret_height);
    }

    // Qt widget coordinates are screen-style coordinates: x grows to the
    // right, y grows downward, and row_top is the top edge of this painted
    // editor row. QTextLine::ascent() is measured upward from the text
    // baseline to the top of the main font's glyph box, so:
    //
    //   baseline_y = row_top + line.ascent()
    //
    // gives us the baseline position in widget coordinates.
    //
    // We intentionally keep the caret height fixed from the editor widget's
    // QFontMetrics instead of using per-line fallback glyph metrics. If Qt
    // falls back to another font for emoji or a different script, that line can
    // report a taller glyph box than neighboring lines. Using that taller box
    // directly makes the I-beam change size from row to row, which looks
    // unstable.
    //
    // The stable visual target is: keep one caret height for the whole editor,
    // but place that caret relative to the actual laid-out baseline for this
    // row. font_metrics.ascent() is the height from the editor font's baseline
    // to the top of the normal glyph box, so subtracting a fixed ascent from
    // baseline_y places the caret where a normal text run on this row would
    // start, even if the visible glyph under the caret came from a fallback
    // font.
    //
    // In short:
    // - line.ascent() tells us where this row's baseline ended up in layout
    // - font_metrics.ascent() gives us the editor's stable caret shape/anchor
    // - combining them keeps the caret the same size while aligning it to the
    //   row's real text baseline instead of just centering it in the row box
    const int target_ascent = std::max(font_metrics.ascent() - 1, 1);
    const int baseline_y = row_top + static_cast<int>(std::round(line.ascent()));
    const int caret_top = baseline_y - target_ascent;
    return QRect(x, caret_top, cursor_width, caret_height);
}

}  // namespace

EditorScrollArea::EditorScrollArea(TextBuffer buffer, QWidget *parent)
    : QAbstractScrollArea(parent)
    , buffer_(std::move(buffer))
    , cursor_(ByteCursorPos{
          .line = LineIndex{},
          .column = ByteColumn{},
      }) {
    setWindowTitle(QStringLiteral("Vitality"));
    setFocusPolicy(Qt::StrongFocus);
    auto system_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    system_font.setPointSize(SYSTEM_FONT_SIZE_PT);
    setFont(system_font);
    refresh_scrollbars();
}

ByteCursorPos EditorScrollArea::cursor_for_tests() const {
    return cursor_;
}

bool EditorScrollArea::has_preferred_column_for_tests() const {
    return preferred_column_.has_value();
}

PreferredVisualColumn EditorScrollArea::preferred_column_value_for_tests() const {
    return preferred_column_.value_or(PreferredVisualColumn{-1});
}

bool EditorScrollArea::has_preferred_visual_x_for_tests() const {
    return preferred_visual_x_.has_value();
}

VisualCursorX EditorScrollArea::preferred_visual_x_value_for_tests() const {
    return preferred_visual_x_.value_or(VisualCursorX{-1});
}

void EditorScrollArea::set_cursor_for_tests(const ByteCursorPos cursor) {
    move_cursor(cursor);
    preferred_column_.reset();
    preferred_visual_x_.reset();
}

void EditorScrollArea::keyPressEvent(QKeyEvent *event) {
    std::optional<NavigationCommand> command;

    switch (event->key()) {
    case Qt::Key_Left:
        command = NavigationCommand::Left;
        break;
    case Qt::Key_Right:
        command = NavigationCommand::Right;
        break;
    case Qt::Key_Up:
        command = NavigationCommand::Up;
        break;
    case Qt::Key_Down:
        command = NavigationCommand::Down;
        break;
    case Qt::Key_PageUp:
        command = NavigationCommand::PageUp;
        break;
    case Qt::Key_PageDown:
        command = NavigationCommand::PageDown;
        break;
    case Qt::Key_Home:
        command = NavigationCommand::Home;
        break;
    case Qt::Key_End:
        command = NavigationCommand::End;
        break;
    default:
        event->ignore();
        return;
    }

    const EditorNavigationModel navigation_model(buffer_);
    const auto result = navigation_model.navigate(
        EditorNavigationState{
            .cursor = cursor_,
            .preferred_column = preferred_column_,
            .preferred_visual_x = preferred_visual_x_,
        },
        *command,
        visible_line_count());
    if (!result.handled) {
        event->ignore();
        return;
    }

    apply_navigation_state(result.state);
    event->accept();
}

QColor EditorScrollArea::highlight_cursor_background() const {
    return palette().text().color();
}

void EditorScrollArea::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().base());

    const int line_height_pixels = line_height();
    const int status_height_pixels = status_bar_height();
    const int text_area_height = std::max(viewport()->height() - status_height_pixels, line_height_pixels);
    // We use the vertical scrollbar as a document-line scroll model. refresh_scrollbars()
    // sets its range/page step in line units, so value() is the first visible line index.
    const int top_line = verticalScrollBar()->value();
    const int bottom_line_exclusive = std::min(
        clamp_to_qt_int(static_cast<std::int64_t>(top_line) + visible_line_count().value),
        clamp_to_qt_int(buffer_.line_count().value));
    const int horizontal_offset_pixels = horizontal_scroll_offset_pixels();
    for (int line_index = top_line; line_index < bottom_line_exclusive; ++line_index) {
        const int row = line_index - top_line;
        const int y = row * line_height_pixels;
        if (y >= text_area_height) {
            break;
        }

        const LineText line_text = buffer_.line_text(LineIndex{line_index});
        const QString line_string = utf8_to_qstring(line_text.utf8_text);
        QTextLayout layout(line_string, font());
        layout::prepare_code_editor_line_layout(layout);
        layout.draw(&painter, QPointF(-horizontal_offset_pixels, y));

        if (line_index == cursor_.line.value) {
            const auto [qt_utf16_column, aligned_byte_column, mapping_success, mapping_error] =
                unicode::map_byte_column_to_qt_utf16(line_text.utf8_text, cursor_.column);
            // We expect a single laid-out line here, but keep the invalid case guarded so an
            // empty or unexpectedly failed layout cannot crash cursor painting.
            const QTextLine layout_line = layout.lineCount() > 0 ? layout.lineAt(0) : QTextLine();
            // Qt text positions are UTF-16 columns. Keep the conversion back to
            // Qt's int-shaped APIs local to this UI boundary.
            Q_UNUSED(aligned_byte_column);
            Q_UNUSED(mapping_error);
            const int qt_cursor_position = mapping_success ? qt_utf16_column_to_qt_int(qt_utf16_column) : 0;
            const qreal cursor_x = layout_line.isValid()
                ? layout_line.cursorToX(qt_cursor_position) - horizontal_offset_pixels
                : -horizontal_offset_pixels;

            const QRect cursor_rect = ibeam_cursor_rect(
                layout_line,
                y,
                line_height_pixels,
                fontMetrics(),
                cursor_x);

            painter.fillRect(cursor_rect, highlight_cursor_background());
        }
    }

    const QRect status_rect(0, viewport()->height() - status_height_pixels, viewport()->width(), status_height_pixels);
    painter.fillRect(status_rect, palette().alternateBase());
    painter.setPen(palette().windowText().color());
    // adjusted() gives the text a small left/right inset while keeping the same status bar box.
    // We could bake that padding into a second QRect, but deriving it from status_rect keeps the
    // fill rect and text rect tied together.
    painter.drawText(
        status_rect.adjusted(STATUS_BAR_INSET, 0, -STATUS_BAR_INSET, 0),
        Qt::AlignVCenter | Qt::AlignLeft,
        make_status_bar_text(buffer_, cursor_));
}

void EditorScrollArea::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    refresh_scrollbars();
    ensure_cursor_visible();
    viewport()->update();
}

void EditorScrollArea::scrollContentsBy(const int dx, const int dy) {
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    // The viewport is fully custom-painted from the current scrollbar state, so
    // scrolling needs to trigger a repaint of the visible document region.
    viewport()->update();
}

int EditorScrollArea::line_height() const {
    return std::max(fontMetrics().height(), 1);
}

int EditorScrollArea::status_bar_height() const {
    return line_height();
}

VisibleLineCount EditorScrollArea::visible_line_count() const {
    const int available_height = std::max(viewport()->height() - status_bar_height(), line_height());
    const int line_count = std::max(available_height / line_height(), 1);
    return VisibleLineCount{line_count};
}

int EditorScrollArea::visible_column_count() const {
    const int average_char_width = std::max(fontMetrics().horizontalAdvance(QLatin1Char(' ')), 1);
    return std::max(viewport()->width() / average_char_width, 1);
}

int EditorScrollArea::horizontal_scroll_offset_pixels() const {
    const int average_char_width = std::max(fontMetrics().horizontalAdvance(QLatin1Char(' ')), 1);
    return horizontalScrollBar()->value() * average_char_width;
}

int EditorScrollArea::document_max_line_length() const {
    int max_length = 0;
    for (std::int64_t line_index = 0; line_index < buffer_.line_count().value; ++line_index) {
        const auto line_length = buffer_.line_length(LineIndex{line_index}).value;
        const int clamped_line_length = static_cast<int>(
            std::min<std::int64_t>(line_length, std::numeric_limits<int>::max()));
        max_length = std::max(max_length, clamped_line_length);
    }

    return max_length;
}

void EditorScrollArea::refresh_scrollbars() const {
    const int visible_lines = clamp_to_qt_int(visible_line_count().value);
    const int total_lines = clamp_to_qt_int(buffer_.line_count().value);
    verticalScrollBar()->setPageStep(visible_lines);
    verticalScrollBar()->setRange(0, std::max(total_lines - visible_lines, 0));

    const int visible_columns = visible_column_count();
    const int max_line_length = document_max_line_length();
    horizontalScrollBar()->setPageStep(visible_columns);
    horizontalScrollBar()->setRange(0, std::max(max_line_length - visible_columns, 0));
}

void EditorScrollArea::ensure_cursor_visible() const {
    const int visible_lines = clamp_to_qt_int(visible_line_count().value);
    const int top_line = verticalScrollBar()->value();
    const int bottom_line = top_line + visible_lines - 1;

    if (cursor_.line.value < top_line) {
        verticalScrollBar()->setValue(clamp_to_qt_int(cursor_.line.value));
    } else if (cursor_.line.value > bottom_line) {
        verticalScrollBar()->setValue(
            clamp_to_qt_int(cursor_.line.value - static_cast<std::int64_t>(visible_lines) + 1));
    }

    const int visible_columns = visible_column_count();
    const int left_column = horizontalScrollBar()->value();
    const int right_column = left_column + visible_columns - 1;

    if (cursor_.column.value < left_column) {
        horizontalScrollBar()->setValue(clamp_to_qt_int(cursor_.column.value));
    } else if (cursor_.column.value > right_column) {
        horizontalScrollBar()->setValue(
            clamp_to_qt_int(cursor_.column.value - static_cast<std::int64_t>(visible_columns) + 1));
    }
}

void EditorScrollArea::move_cursor(const ByteCursorPos cursor) {
    cursor_ = buffer_.clamp_cursor(cursor);
    refresh_scrollbars();
    ensure_cursor_visible();
    viewport()->update();
}

void EditorScrollArea::apply_navigation_state(const EditorNavigationState &state) {
    preferred_column_ = state.preferred_column;
    preferred_visual_x_ = state.preferred_visual_x;
    move_cursor(state.cursor);
}

}  // namespace vitality
