#include "buffer/TextStorage.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>

#include "buffer/internal/ImplicitTreapStorageCore.h"

namespace vitality {

namespace {

constexpr std::size_t kMaintenanceEditInterval = 512;
constexpr std::size_t kMaintenancePieceThreshold = 2048;
constexpr std::size_t kMaintenanceMergeBudget = 128;

[[nodiscard]] bool mutation_changed_storage(const std::string_view utf8_text) {
    return !utf8_text.empty();
}

[[nodiscard]] bool mutation_changed_storage(const ByteRange range) {
    return range.length.value > 0;
}

}  // namespace

struct TextStorage::Impl {
    buffer_internal::ImplicitTreapStorageCore core;
    std::size_t edits_since_maintenance = 0;

    explicit Impl(buffer_internal::ImplicitTreapStorageCore core_to_use)
        : core(std::move(core_to_use)) {
    }

    void run_maintenance_pass() {
        ++edits_since_maintenance;
        if (edits_since_maintenance < kMaintenanceEditInterval) {
            return;
        }
        edits_since_maintenance = 0;

        if (core.piece_count() < kMaintenancePieceThreshold) {
            return;
        }

        (void)core.compact_with_merge_budget(kMaintenanceMergeBudget);
    }
};

namespace {

[[nodiscard]] std::int64_t clamp_int(const std::int64_t value, const std::int64_t minimum, const std::int64_t maximum) {
    return std::clamp(value, minimum, maximum);
}

struct LineByteRange {
    std::size_t start = 0;
    std::size_t end = 0;
    bool valid = false;
};

[[nodiscard]] LineByteRange line_byte_range(
    const buffer_internal::ImplicitTreapStorageCore &core,
    const LineIndex line) {
    const std::int64_t total_lines = static_cast<std::int64_t>(core.line_count());
    if (line.value < 0 || line.value >= total_lines) {
        return {};
    }

    const std::size_t line_index = static_cast<std::size_t>(line.value);
    const std::size_t start = core.line_start_offset(line_index);
    std::size_t end = core.byte_count();

    if (line.value + 1 < total_lines) {
        end = core.line_start_offset(line_index + 1);
        if (end > start) {
            --end;
        }
    } else if (core.ends_with_newline() && end > start) {
        --end;
    }

    return LineByteRange{
        .start = start,
        .end = end,
        .valid = true,
    };
}

}  // namespace

TextStorage TextStorage::make_empty() {
    auto impl = std::make_unique<Impl>(buffer_internal::ImplicitTreapStorageCore());
    return TextStorage(std::move(impl));
}

TextStorage TextStorage::from_utf8(const std::string_view utf8_text) {
    auto impl = std::make_unique<Impl>(buffer_internal::ImplicitTreapStorageCore(utf8_text));
    return TextStorage(std::move(impl));
}

TextStorage TextStorage::load_from_stream(std::istream &input) {
    std::string utf8_text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return from_utf8(utf8_text);
}

TextStorage::TextStorage(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
}

TextStorage::TextStorage(TextStorage &&other) noexcept = default;

TextStorage &TextStorage::operator=(TextStorage &&other) noexcept = default;

TextStorage::~TextStorage() = default;

ByteCount TextStorage::byte_count() const {
    return ByteCount{static_cast<std::int64_t>(impl_->core.byte_count())};
}

LineCount TextStorage::line_count() const {
    return LineCount{static_cast<std::int64_t>(impl_->core.line_count())};
}

LineText TextStorage::line_text(const LineIndex line) const {
    const auto [start, end, valid] = line_byte_range(impl_->core, line);
    if (!valid) {
        return LineText{};
    }

    return LineText{
        .utf8_text = impl_->core.substring(start, end - start),
    };
}

ByteColumn TextStorage::line_length(const LineIndex line) const {
    const LineByteRange range = line_byte_range(impl_->core, line);
    if (!range.valid) {
        return ByteColumn{};
    }

    return ByteColumn{static_cast<std::int64_t>(range.end - range.start)};
}

ByteColumn TextStorage::clamp_line_byte_column(const LineIndex line, const ByteColumn column) const {
    return clamp_cursor(ByteCursorPos{
        .line = line,
        .column = column,
    }).column;
}

ByteCursorPos TextStorage::clamp_cursor(const ByteCursorPos cursor) const {
    const std::int64_t last_line_index = line_count().value - 1;
    const std::int64_t clamped_line = clamp_int(cursor.line.value, 0, last_line_index);
    const std::int64_t max_column = line_length(LineIndex{clamped_line}).value;
    const std::int64_t clamped_column = clamp_int(cursor.column.value, 0, max_column);

    return ByteCursorPos{
        .line = LineIndex{clamped_line},
        .column = ByteColumn{clamped_column},
    };
}

std::string TextStorage::text() const {
    return impl_->core.text();
}

bool TextStorage::check_invariants() const {
    return impl_->core.check_invariants();
}

InsertTextResult TextStorage::insert(const ByteOffset offset, const std::string_view utf8_text) {
    if (!impl_->core.can_insert(offset)) {
        return InsertTextResult{};
    }

    impl_->core.insert(offset, utf8_text);
    if (mutation_changed_storage(utf8_text)) {
        impl_->run_maintenance_pass();
    }
    return InsertTextResult{
        .inserted_range = ByteRange{
            .start = offset,
            .length = ByteCount{static_cast<std::int64_t>(utf8_text.size())},
        },
        .success = true,
    };
}

EraseTextResult TextStorage::erase(const ByteRange range) {
    if (!impl_->core.can_erase(range)) {
        return EraseTextResult{};
    }

    impl_->core.erase(range);
    if (mutation_changed_storage(range)) {
        impl_->run_maintenance_pass();
    }
    return EraseTextResult{
        .erased_range = range,
        .success = true,
    };
}

}  // namespace vitality
