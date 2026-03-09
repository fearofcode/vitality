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
        line.setLineWidth(static_cast<qreal>(std::numeric_limits<int>::max()) / 4.0);
    }
    layout.endLayout();
}

}  // namespace vitality::layout
