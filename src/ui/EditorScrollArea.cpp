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

struct VisualNavigationQuery {
    LineText line_text;
    layout::VisualCursorQuery query;
    bool success = false;
};

[[nodiscard]] std::optional<VisualNavigationQuery> make_visual_navigation_query(
    const TextBuffer &buffer,
    const ByteCursorPos cursor) {
    const auto logical_grapheme = buffer.logical_grapheme_cursor(cursor);
    if (!logical_grapheme.success) {
        return std::nullopt;
    }

    LineText line_text = buffer.line_text(logical_grapheme.cursor.line);
    return VisualNavigationQuery{
        .line_text = std::move(line_text),
        .query = layout::VisualCursorQuery{
            .line = logical_grapheme.cursor.line,
            .utf8_line = std::string_view{},
            .logical_cursor = logical_grapheme.cursor,
        },
        .success = true,
    };
}

[[nodiscard]] ByteCursorPos byte_cursor_from_logical_grapheme_cursor(const LogicalGraphemeCursorPos cursor) {
    return ByteCursorPos{
        .line = cursor.line,
        .column = ByteColumn{cursor.column.value},
    };
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
    clear_preferred_column();
    clear_preferred_visual_x();
}

void EditorScrollArea::keyPressEvent(QKeyEvent *event) {
    ByteCursorPos next_cursor = cursor_;

    switch (event->key()) {
    case Qt::Key_Left: {
        auto visual_query = make_visual_navigation_query(buffer_, cursor_);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_left = layout::visual_left_cursor(visual_query->query);
            next_cursor = visual_left.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_left.logical_cursor)
                : buffer_.move_left(cursor_);
        } else {
            next_cursor = buffer_.move_left(cursor_);
        }
        move_cursor(next_cursor);
        clear_preferred_column();
        clear_preferred_visual_x();
        reset_preferred_column_from_cursor();
        reset_preferred_visual_x_from_cursor();
        break;
    }
    case Qt::Key_Right: {
        auto visual_query = make_visual_navigation_query(buffer_, cursor_);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_right = layout::visual_right_cursor(visual_query->query);
            next_cursor = visual_right.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_right.logical_cursor)
                : buffer_.move_right(cursor_);
        } else {
            next_cursor = buffer_.move_right(cursor_);
        }
        move_cursor(next_cursor);
        clear_preferred_column();
        clear_preferred_visual_x();
        reset_preferred_column_from_cursor();
        reset_preferred_visual_x_from_cursor();
        break;
    }
    case Qt::Key_Up: {
        // Vertical navigation preserves the original preferred display column
        // across shorter intermediate lines. The editor owns that transient
        // logical navigation state; the buffer only answers line-local mapping
        // queries. This is still not visual-order cursor behavior in the bidi
        // sense. The stored cursor remains logical and byte-based.
        if (!preferred_column_.has_value()) {
            reset_preferred_column_from_cursor();
        }
        if (!preferred_visual_x_.has_value()) {
            reset_preferred_visual_x_from_cursor();
        }

        const std::int64_t target_line = std::max<std::int64_t>(cursor_.line.value - 1, 0);
        next_cursor = move_vertically_with_preferred_column(LineIndex{target_line});
        move_cursor(next_cursor);
        break;
    }
    case Qt::Key_Down: {
        if (!preferred_column_.has_value()) {
            reset_preferred_column_from_cursor();
        }
        if (!preferred_visual_x_.has_value()) {
            reset_preferred_visual_x_from_cursor();
        }

        const std::int64_t last_line = std::max<std::int64_t>(buffer_.line_count().value - 1, 0);
        const std::int64_t target_line = std::min<std::int64_t>(cursor_.line.value + 1, last_line);
        next_cursor = move_vertically_with_preferred_column(LineIndex{target_line});
        move_cursor(next_cursor);
        break;
    }
    case Qt::Key_PageUp: {
        if (!preferred_column_.has_value()) {
            reset_preferred_column_from_cursor();
        }
        if (!preferred_visual_x_.has_value()) {
            reset_preferred_visual_x_from_cursor();
        }

        const std::int64_t target_line = std::max<std::int64_t>(
            cursor_.line.value - std::max<std::int64_t>(visible_line_count().value - 1, 1),
            0);
        next_cursor = move_vertically_with_preferred_column(LineIndex{target_line});
        move_cursor(next_cursor);
        break;
    }
    case Qt::Key_PageDown: {
        if (!preferred_column_.has_value()) {
            reset_preferred_column_from_cursor();
        }
        if (!preferred_visual_x_.has_value()) {
            reset_preferred_visual_x_from_cursor();
        }

        const std::int64_t last_line = std::max<std::int64_t>(buffer_.line_count().value - 1, 0);
        const std::int64_t target_line = std::min<std::int64_t>(
            cursor_.line.value + std::max<std::int64_t>(visible_line_count().value - 1, 1),
            last_line);
        next_cursor = move_vertically_with_preferred_column(LineIndex{target_line});
        move_cursor(next_cursor);
        break;
    }
    case Qt::Key_Home: {
        auto visual_query = make_visual_navigation_query(buffer_, cursor_);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_home = layout::visual_home_cursor(visual_query->query);
            next_cursor = visual_home.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_home.logical_cursor)
                : buffer_.move_home(cursor_);
        } else {
            next_cursor = buffer_.move_home(cursor_);
        }
        move_cursor(next_cursor);
        clear_preferred_column();
        clear_preferred_visual_x();
        reset_preferred_column_from_cursor();
        reset_preferred_visual_x_from_cursor();
        break;
    }
    case Qt::Key_End: {
        auto visual_query = make_visual_navigation_query(buffer_, cursor_);
        if (visual_query.has_value()) {
            visual_query->query.utf8_line = visual_query->line_text.utf8_text;
            const auto visual_end = layout::visual_end_cursor(visual_query->query);
            next_cursor = visual_end.success
                ? byte_cursor_from_logical_grapheme_cursor(visual_end.logical_cursor)
                : buffer_.move_end(cursor_);
        } else {
            next_cursor = buffer_.move_end(cursor_);
        }
        move_cursor(next_cursor);
        clear_preferred_column();
        clear_preferred_visual_x();
        reset_preferred_column_from_cursor();
        reset_preferred_visual_x_from_cursor();
        break;
    }
    default:
        event->ignore();
        return;
    }

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
                map_utf8_byte_column_to_qt_cursor(line_text.utf8_text, cursor_.column);
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

PreferredVisualColumn EditorScrollArea::current_preferred_column() const {
    if (preferred_column_.has_value()) {
        return *preferred_column_;
    }

    return buffer_.preferred_column(cursor_);
}

void EditorScrollArea::reset_preferred_column_from_cursor() {
    preferred_column_ = buffer_.preferred_column(cursor_);
}

void EditorScrollArea::clear_preferred_column() {
    preferred_column_.reset();
}

std::optional<VisualCursorX> EditorScrollArea::current_preferred_visual_x() const {
    return preferred_visual_x_;
}

void EditorScrollArea::reset_preferred_visual_x_from_cursor() {
    auto visual_query = make_visual_navigation_query(buffer_, cursor_);
    if (!visual_query.has_value()) {
        preferred_visual_x_.reset();
        return;
    }

    visual_query->query.utf8_line = visual_query->line_text.utf8_text;
    const auto visual_cursor = layout::logical_to_visual_cursor(visual_query->query);
    if (!visual_cursor.success) {
        preferred_visual_x_.reset();
        return;
    }

    preferred_visual_x_ = visual_cursor.visual_x;
}

void EditorScrollArea::clear_preferred_visual_x() {
    preferred_visual_x_.reset();
}

ByteCursorPos EditorScrollArea::move_vertically_with_preferred_column(const LineIndex target_line) const {
    if (const auto preferred_visual_x = current_preferred_visual_x(); preferred_visual_x.has_value()) {
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

    const PreferredVisualColumn preferred = current_preferred_column();
    const auto target_cursor = buffer_.cursor_for_display_column(
        target_line,
        GraphemeColumn{preferred.value});
    if (target_cursor.success) {
        return ByteCursorPos{
            .line = target_cursor.cursor.line,
            .column = ByteColumn{target_cursor.cursor.column.value},
        };
    }

    // Malformed UTF-8 or another Unicode mapping failure should not break
    // navigation. Fall back to the old byte-column model on the target line.
    return buffer_.clamp_cursor(ByteCursorPos{
        .line = target_line,
        .column = cursor_.column,
    });
}

}  // namespace vitality
