#pragma once

class QTextLayout;

namespace vitality::layout {

// Keep one shared code-editor line layout policy for both rendering and visual
// cursor movement. The helper is Qt-dependent on purpose, but the policy
// itself is narrow:
// - no wrapping
// - left-to-right code-editor paragraph direction
// - visual cursor movement semantics inside Qt
void prepare_code_editor_line_layout(QTextLayout &layout);

}  // namespace vitality::layout
