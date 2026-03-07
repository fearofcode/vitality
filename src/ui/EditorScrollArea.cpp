#include "ui/EditorScrollArea.h"

#include <algorithm>
#include <limits>

#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QTextLayout>

#include "buffer/BufferTypes.h"
#include "ui/StatusBarText.h"
#include "ui/Utf8QtTextMapper.h"

namespace vitality {

namespace {

void prepare_layout(QTextLayout &layout) {
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(option);

    layout.beginLayout();
    if (QTextLine line = layout.createLine(); line.isValid()) {
        line.setPosition(QPointF(0.0, 0.0));
        line.setLineWidth(std::numeric_limits<int>::max() / 4);
    }
    layout.endLayout();
}

[[nodiscard]] int block_cursor_width(const QTextLine &line, const int cursor_position, const int text_length, const int fallback_width) {
    if (!line.isValid()) {
        return fallback_width;
    }

    if (cursor_position < text_length) {
        const qreal current_x = line.cursorToX(cursor_position);
        const qreal next_x = line.cursorToX(cursor_position + 1);
        if (const int width = static_cast<int>(std::round(next_x - current_x)); width > 0) {
            return width;
        }
    }

    return fallback_width;
}

}  // namespace

EditorScrollArea::EditorScrollArea(TextBuffer buffer, QWidget *parent)
    : QAbstractScrollArea(parent)
    , buffer_(std::move(buffer))
    , cursor_(CursorPos{
          .line = LineIndex{},
          .column = ColumnIndex{},
      }) {
    setWindowTitle(QStringLiteral("Vitality"));
    setFocusPolicy(Qt::StrongFocus);
    auto system_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    system_font.setPointSize(SYSTEM_FONT_SIZE_PT);
    setFont(system_font);

    refresh_scrollbars();
}

void EditorScrollArea::keyPressEvent(QKeyEvent *event) {
    CursorPos next_cursor = cursor_;

    switch (event->key()) {
    case Qt::Key_Up:
        next_cursor = buffer_.move_up(cursor_);
        break;
    case Qt::Key_Down:
        next_cursor = buffer_.move_down(cursor_);
        break;
    case Qt::Key_Left:
        next_cursor = buffer_.move_left(cursor_);
        break;
    case Qt::Key_Right:
        next_cursor = buffer_.move_right(cursor_);
        break;
    case Qt::Key_PageUp:
        next_cursor = buffer_.move_page_up(cursor_, visible_line_count());
        break;
    case Qt::Key_PageDown:
        next_cursor = buffer_.move_page_down(cursor_, visible_line_count());
        break;
    case Qt::Key_Home:
        next_cursor = buffer_.move_home(cursor_);
        break;
    case Qt::Key_End:
        next_cursor = buffer_.move_end(cursor_);
        break;
    default:
        event->ignore();
        return;
    }

    move_cursor(next_cursor);
    event->accept();
}

QColor EditorScrollArea::highlight_cursor_background() const {
    return palette().highlight().color().lighter(125);
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
        top_line + visible_line_count().value,
        buffer_.line_count().value);
    const int horizontal_offset_pixels = horizontal_scroll_offset_pixels();
    const int fallback_cursor_width = std::max(fontMetrics().horizontalAdvance(QLatin1Char(' ')), 1);

    for (int line_index = top_line; line_index < bottom_line_exclusive; ++line_index) {
        const int row = line_index - top_line;
        const int y = row * line_height_pixels;
        if (y >= text_area_height) {
            break;
        }

        const auto [utf8_text] = buffer_.line_text(LineIndex{line_index});
        const QString line_string = utf8_to_qstring(utf8_text);
        QTextLayout layout(line_string, font());
        prepare_layout(layout);
        layout.draw(&painter, QPointF(-horizontal_offset_pixels, y));

        if (line_index == cursor_.line.value) {
            const auto [qt_cursor_position, aligned_byte_column] =
                map_utf8_byte_column_to_qt_cursor(utf8_text, cursor_.column);
            // We expect a single laid-out line here, but keep the invalid case guarded so an
            // empty or unexpectedly failed layout cannot crash cursor painting.
            const QTextLine layout_line = layout.lineCount() > 0 ? layout.lineAt(0) : QTextLine();
            const qreal cursor_x = layout_line.isValid()
                ? layout_line.cursorToX(qt_cursor_position) - horizontal_offset_pixels
                : -horizontal_offset_pixels;
            const int cursor_width = block_cursor_width(
                layout_line,
                qt_cursor_position,
                line_string.size(),
                fallback_cursor_width);

            QRect cursor_rect(
                static_cast<int>(std::round(cursor_x)),
                y,
                cursor_width,
                line_height_pixels);

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
    for (int line_index = 0; line_index < buffer_.line_count().value; ++line_index) {
        max_length = std::max(max_length, buffer_.line_length(LineIndex{line_index}).value);
    }

    return max_length;
}

void EditorScrollArea::refresh_scrollbars() const {
    const int visible_lines = visible_line_count().value;
    const int total_lines = buffer_.line_count().value;
    verticalScrollBar()->setPageStep(visible_lines);
    verticalScrollBar()->setRange(0, std::max(total_lines - visible_lines, 0));

    const int visible_columns = visible_column_count();
    const int max_line_length = document_max_line_length();
    horizontalScrollBar()->setPageStep(visible_columns);
    horizontalScrollBar()->setRange(0, std::max(max_line_length - visible_columns, 0));
}

void EditorScrollArea::ensure_cursor_visible() const {
    const int visible_lines = visible_line_count().value;
    const int top_line = verticalScrollBar()->value();
    const int bottom_line = top_line + visible_lines - 1;

    if (cursor_.line.value < top_line) {
        verticalScrollBar()->setValue(cursor_.line.value);
    } else if (cursor_.line.value > bottom_line) {
        verticalScrollBar()->setValue(cursor_.line.value - visible_lines + 1);
    }

    const int visible_columns = visible_column_count();
    const int left_column = horizontalScrollBar()->value();
    const int right_column = left_column + visible_columns - 1;

    if (cursor_.column.value < left_column) {
        horizontalScrollBar()->setValue(cursor_.column.value);
    } else if (cursor_.column.value > right_column) {
        horizontalScrollBar()->setValue(cursor_.column.value - visible_columns + 1);
    }
}

void EditorScrollArea::move_cursor(const CursorPos cursor) {
    cursor_ = buffer_.clamp_cursor(cursor);
    refresh_scrollbars();
    ensure_cursor_visible();
    viewport()->update();
}

}  // namespace vitality
