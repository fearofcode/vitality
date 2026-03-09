#include "layout/QtLineLayout.h"

#include <limits>

#include <QPointF>
#include <QTextLayout>
#include <QTextOption>

namespace vitality::layout {

void prepare_code_editor_line_layout(QTextLayout &layout) {
    QTextOption option = layout.textOption();
    option.setWrapMode(QTextOption::NoWrap);
    option.setTextDirection(Qt::LeftToRight);
    layout.setTextOption(option);
    layout.setCursorMoveStyle(Qt::VisualMoveStyle);

    layout.beginLayout();
    if (QTextLine line = layout.createLine(); line.isValid()) {
        line.setPosition(QPointF(0.0, 0.0));
        // QTextLayout still expects a finite line width even when we want
        // code-editor-style "no wrapping" behavior. Giving it a very large
        // but still ordinary finite width keeps the entire logical line on one
        // QTextLine without depending on the viewport width, and staying well
        // below the absolute numeric limit avoids inviting odd overflow or
        // precision behavior inside Qt's layout math.
        line.setLineWidth(static_cast<qreal>(std::numeric_limits<int>::max()) / 4.0);
    }
    layout.endLayout();
}

}  // namespace vitality::layout
