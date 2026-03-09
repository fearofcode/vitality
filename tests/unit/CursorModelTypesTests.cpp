#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "core/CoreTypes.h"

namespace {

using vitality::ByteColumn;
using vitality::ByteCursorPos;
using vitality::GraphemeBoundaryByteColumn;
using vitality::LineIndex;
using vitality::LogicalByteCursorPos;
using vitality::LogicalGraphemeCursorPos;
using vitality::VisualCursorPos;

static_assert(!std::is_same_v<LogicalByteCursorPos, VisualCursorPos>);
static_assert(!std::is_same_v<LogicalGraphemeCursorPos, vitality::GraphemeBoundaryCursorPos>);
static_assert(!std::is_convertible_v<VisualCursorPos, LogicalByteCursorPos>);
static_assert(!std::is_convertible_v<LogicalByteCursorPos, VisualCursorPos>);

}  // namespace

TEST_CASE("logical and visual cursor model types remain distinct") {
    const LogicalByteCursorPos logical_byte{
        .line = LineIndex{2},
        .column = ByteColumn{5},
    };
    const LogicalGraphemeCursorPos logical_grapheme{
        .line = LineIndex{3},
        .column = GraphemeBoundaryByteColumn{7},
    };
    const VisualCursorPos visual{
        .line = LineIndex{4},
        .column = vitality::VisualCursorColumn{9},
    };

    CHECK(logical_byte.line.value == 2);
    CHECK(logical_grapheme.column.value == 7);
    CHECK(visual.column.value == 9);
}
