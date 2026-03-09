#include "layout/LayoutCursorOps.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <QString>
#include <QTextLayout>

#include "layout/QtLineLayout.h"
#include "unicode/UnicodeLineOps.h"

namespace vitality::layout {

namespace {

struct VisualCursorStop {
    LogicalGraphemeCursorPos logical_cursor;
    QtUtf16Column qt_column;
    qreal x = 0.0;
};

struct VisualCursorModel {
    std::vector<VisualCursorStop> stops;
    std::vector<std::size_t> visual_order;
    bool success = false;
    UnicodeError error = UnicodeError::None;
};

[[nodiscard]] int clamp_to_qt_int(const std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(
        value,
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}

[[nodiscard]] QString utf8_to_qstring(const std::string_view utf8_text) {
    return QString::fromUtf8(
        utf8_text.data(),
        static_cast<qsizetype>(utf8_text.size()));
}

[[nodiscard]] VisualCursorModel invalid_visual_model(const UnicodeError error) {
    return VisualCursorModel{
        .success = false,
        .error = error,
    };
}

[[nodiscard]] LogicalVisualCursorResult invalid_logical_visual_result(const UnicodeError error) {
    return LogicalVisualCursorResult{
        .success = false,
        .error = error,
    };
}

[[nodiscard]] LogicalCursorMappingResult invalid_logical_cursor_result(const UnicodeError error) {
    return LogicalCursorMappingResult{
        .success = false,
        .error = error,
    };
}

[[nodiscard]] std::int64_t visual_x_to_storage(const qreal x) {
    return static_cast<std::int64_t>(std::llround(x));
}

[[nodiscard]] std::optional<std::size_t> stop_index_for_qt_cursor(
    const std::vector<VisualCursorStop> &stops,
    const std::vector<bool> &visited,
    const int qt_cursor) {
    for (std::size_t index = 0; index < stops.size(); ++index) {
        if (!visited[index] && clamp_to_qt_int(stops[index].qt_column.value) == qt_cursor) {
            return index;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> current_stop_index(
    const VisualCursorModel &model,
    const VisualCursorQuery &query) {
    for (std::size_t index = 0; index < model.stops.size(); ++index) {
        if (model.stops[index].logical_cursor.line.value == query.logical_cursor.line.value
            && model.stops[index].logical_cursor.column.value == query.logical_cursor.column.value) {
            return index;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> visual_index_for_stop(
    const VisualCursorModel &model,
    const std::size_t stop_index) {
    const auto it = std::find(model.visual_order.begin(), model.visual_order.end(), stop_index);
    if (it == model.visual_order.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(model.visual_order.begin(), it));
}

[[nodiscard]] VisualCursorModel build_visual_cursor_model(const VisualCursorQuery &query) {
    VisualCursorModel model;

    std::int64_t byte_boundary = 0;
    while (true) {
        const auto qt_mapping = unicode::map_byte_column_to_qt_utf16(
            query.utf8_line,
            ByteColumn{byte_boundary});
        if (!qt_mapping.success || qt_mapping.aligned_byte_column.value != byte_boundary) {
            return invalid_visual_model(qt_mapping.error);
        }

        model.stops.push_back(VisualCursorStop{
            .logical_cursor = LogicalGraphemeCursorPos{
                .line = query.line,
                .column = GraphemeBoundaryByteColumn{byte_boundary},
            },
            .qt_column = qt_mapping.qt_column,
        });

        if (byte_boundary >= static_cast<std::int64_t>(query.utf8_line.size())) {
            break;
        }

        const auto next = unicode::next_grapheme_boundary(
            query.utf8_line,
            ByteColumn{byte_boundary});
        if (!next.success) {
            return invalid_visual_model(next.error);
        }

        if (next.column.value <= byte_boundary) {
            return invalid_visual_model(UnicodeError::DependencyFailure);
        }

        byte_boundary = next.column.value;
    }

    QTextLayout layout(utf8_to_qstring(query.utf8_line));
    prepare_code_editor_line_layout(layout);
    const QTextLine line = layout.lineCount() > 0 ? layout.lineAt(0) : QTextLine();

    for (VisualCursorStop &stop : model.stops) {
        stop.x = line.isValid() ? line.cursorToX(clamp_to_qt_int(stop.qt_column.value)) : 0.0;
    }

    std::vector<std::size_t> sorted_by_x(model.stops.size());
    for (std::size_t index = 0; index < sorted_by_x.size(); ++index) {
        sorted_by_x[index] = index;
    }
    std::stable_sort(
        sorted_by_x.begin(),
        sorted_by_x.end(),
        [&model](const std::size_t left, const std::size_t right) {
            const qreal left_x = model.stops[left].x;
            const qreal right_x = model.stops[right].x;
            if (left_x == right_x) {
                return model.stops[left].qt_column.value < model.stops[right].qt_column.value;
            }
            return left_x < right_x;
        });

    std::vector<std::size_t> sorted_position(model.stops.size(), 0);
    for (std::size_t index = 0; index < sorted_by_x.size(); ++index) {
        sorted_position[sorted_by_x[index]] = index;
    }

    if (sorted_by_x.empty()) {
        return invalid_visual_model(UnicodeError::DependencyFailure);
    }

    model.visual_order.push_back(sorted_by_x.front());
    std::vector<bool> visited(model.stops.size(), false);
    visited[sorted_by_x.front()] = true;

    while (model.visual_order.size() < model.stops.size()) {
        const std::size_t current_index = model.visual_order.back();
        int candidate = clamp_to_qt_int(model.stops[current_index].qt_column.value);
        bool found = false;
        const int max_steps = std::max<int>(clamp_to_qt_int(layout.text().size()) + 4, 4);

        for (int step = 0; step < max_steps; ++step) {
            const int next_candidate = layout.rightCursorPosition(candidate);
            if (next_candidate == candidate) {
                break;
            }

            candidate = next_candidate;
            const auto next_index = stop_index_for_qt_cursor(model.stops, visited, candidate);
            if (next_index.has_value()) {
                visited[*next_index] = true;
                model.visual_order.push_back(*next_index);
                found = true;
                break;
            }
        }

        if (!found) {
            const std::size_t current_sorted_index = sorted_position[current_index];
            for (std::size_t scan = current_sorted_index + 1; scan < sorted_by_x.size(); ++scan) {
                const std::size_t fallback_index = sorted_by_x[scan];
                if (!visited[fallback_index]) {
                    visited[fallback_index] = true;
                    model.visual_order.push_back(fallback_index);
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            return invalid_visual_model(UnicodeError::DependencyFailure);
        }
    }

    model.success = true;
    return model;
}

}  // namespace

LogicalVisualCursorResult logical_to_visual_cursor(const VisualCursorQuery &query) {
    const VisualCursorModel model = build_visual_cursor_model(query);
    if (!model.success) {
        return invalid_logical_visual_result(model.error);
    }

    const auto stop_index = current_stop_index(model, query);
    if (!stop_index.has_value()) {
        return invalid_logical_visual_result(UnicodeError::DependencyFailure);
    }

    const auto visual_index = visual_index_for_stop(model, *stop_index);
    if (!visual_index.has_value()) {
        return invalid_logical_visual_result(UnicodeError::DependencyFailure);
    }

    return LogicalVisualCursorResult{
        .logical_cursor = query.logical_cursor,
        .visual_cursor = VisualCursorPos{
            .line = query.line,
            .column = VisualCursorColumn{static_cast<std::int64_t>(*visual_index)},
        },
        .visual_x = VisualCursorX{
            .value = visual_x_to_storage(model.stops[*stop_index].x),
        },
        .success = true,
        .error = UnicodeError::None,
    };
}

LogicalCursorMappingResult logical_cursor_for_visual_x(
    const LineIndex line,
    const std::string_view utf8_line,
    const VisualCursorX visual_x) {
    const VisualCursorQuery query{
        .line = line,
        .utf8_line = utf8_line,
        .logical_cursor = LogicalGraphemeCursorPos{
            .line = line,
            .column = GraphemeBoundaryByteColumn{0},
        },
    };
    const VisualCursorModel model = build_visual_cursor_model(query);
    if (!model.success) {
        return invalid_logical_cursor_result(model.error);
    }

    if (model.stops.empty()) {
        return invalid_logical_cursor_result(UnicodeError::DependencyFailure);
    }

    std::size_t best_index = model.visual_order.front();
    std::int64_t best_distance = std::llabs(
        visual_x_to_storage(model.stops[best_index].x) - visual_x.value);

    for (const std::size_t stop_index : model.visual_order) {
        const std::int64_t distance = std::llabs(
            visual_x_to_storage(model.stops[stop_index].x) - visual_x.value);
        if (distance < best_distance) {
            best_index = stop_index;
            best_distance = distance;
            continue;
        }

        if (distance == best_distance
            && visual_x_to_storage(model.stops[stop_index].x)
                < visual_x_to_storage(model.stops[best_index].x)) {
            best_index = stop_index;
        }
    }

    return LogicalCursorMappingResult{
        .logical_cursor = model.stops[best_index].logical_cursor,
        .success = true,
        .error = UnicodeError::None,
    };
}

LogicalCursorMappingResult visual_left_cursor(const VisualCursorQuery &query) {
    const VisualCursorModel model = build_visual_cursor_model(query);
    if (!model.success) {
        return invalid_logical_cursor_result(model.error);
    }

    const auto stop_index = current_stop_index(model, query);
    const auto visual_index = stop_index.has_value() ? visual_index_for_stop(model, *stop_index) : std::nullopt;
    if (!visual_index.has_value()) {
        return invalid_logical_cursor_result(UnicodeError::DependencyFailure);
    }

    const std::size_t target_index = *visual_index == 0 ? 0 : (*visual_index - 1);
    const VisualCursorStop &target_stop = model.stops[model.visual_order[target_index]];
    return LogicalCursorMappingResult{
        .logical_cursor = target_stop.logical_cursor,
        .success = true,
        .error = UnicodeError::None,
    };
}

LogicalCursorMappingResult visual_right_cursor(const VisualCursorQuery &query) {
    const VisualCursorModel model = build_visual_cursor_model(query);
    if (!model.success) {
        return invalid_logical_cursor_result(model.error);
    }

    const auto stop_index = current_stop_index(model, query);
    const auto visual_index = stop_index.has_value() ? visual_index_for_stop(model, *stop_index) : std::nullopt;
    if (!visual_index.has_value()) {
        return invalid_logical_cursor_result(UnicodeError::DependencyFailure);
    }

    const std::size_t last_index = model.visual_order.size() - 1;
    const std::size_t target_index = std::min<std::size_t>(*visual_index + 1, last_index);
    const VisualCursorStop &target_stop = model.stops[model.visual_order[target_index]];
    return LogicalCursorMappingResult{
        .logical_cursor = target_stop.logical_cursor,
        .success = true,
        .error = UnicodeError::None,
    };
}

LogicalCursorMappingResult visual_home_cursor(const VisualCursorQuery &query) {
    const VisualCursorModel model = build_visual_cursor_model(query);
    if (!model.success) {
        return invalid_logical_cursor_result(model.error);
    }

    const VisualCursorStop &target_stop = model.stops[model.visual_order.front()];
    return LogicalCursorMappingResult{
        .logical_cursor = target_stop.logical_cursor,
        .success = true,
        .error = UnicodeError::None,
    };
}

LogicalCursorMappingResult visual_end_cursor(const VisualCursorQuery &query) {
    const VisualCursorModel model = build_visual_cursor_model(query);
    if (!model.success) {
        return invalid_logical_cursor_result(model.error);
    }

    const VisualCursorStop &target_stop = model.stops[model.visual_order.back()];
    return LogicalCursorMappingResult{
        .logical_cursor = target_stop.logical_cursor,
        .success = true,
        .error = UnicodeError::None,
    };
}

}  // namespace vitality::layout
