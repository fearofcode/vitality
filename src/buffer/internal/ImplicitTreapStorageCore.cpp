#include "buffer/internal/ImplicitTreapStorageCore.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <span>

namespace vitality::buffer_internal {

namespace {

[[nodiscard]] std::span<const std::uint8_t> piece_bytes(const PieceStorage &storage, const Piece &piece) {
    if (piece.length == 0) {
        return {};
    }

    if (piece.buffer == BufferKind::Original) {
        const auto *ptr = reinterpret_cast<const std::uint8_t *>(storage.original.data());
        return std::span<const std::uint8_t>(ptr + piece.start, piece.length);
    }

    return std::span<const std::uint8_t>(storage.add.data() + piece.start, piece.length);
}

void append_piece_bytes(std::string &out, const PieceStorage &storage, const Piece &piece) {
    const auto bytes = piece_bytes(storage, piece);
    out.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void append_piece_range(
    std::string &out,
    const PieceStorage &storage,
    const Piece &piece,
    const std::size_t begin,
    const std::size_t end) {
    if (begin >= end || begin >= piece.length) {
        return;
    }

    const std::size_t clamped_end = std::min(end, piece.length);
    const std::size_t len = clamped_end - begin;

    Piece sub = piece;
    sub.start = piece.start + begin;
    sub.length = len;
    append_piece_bytes(out, storage, sub);
}

[[nodiscard]] bool piece_ends_with_newline(const PieceStorage &storage, const Piece &piece) {
    if (piece.length == 0) {
        return false;
    }

    const auto bytes = piece_bytes(storage, piece);
    return !bytes.empty() && bytes.back() == static_cast<std::uint8_t>('\n');
}

}  // namespace

PrioritySource::PrioritySource(const std::uint64_t seed)
    : rng_(seed)
    , dist_(0, std::numeric_limits<std::uint64_t>::max()) {
}

std::uint64_t PrioritySource::next() {
    return dist_(rng_);
}

ImplicitTreapStorageCore::ImplicitTreapStorageCore(const std::string_view initial, const std::uint64_t seed)
    : storage_{.original = std::string(initial), .add = {}}
    , priorities_(seed) {
    if (!initial.empty()) {
        root_ = make_node(Piece{
            .buffer = BufferKind::Original,
            .start = 0,
            .length = initial.size(),
        });
    }
}

std::size_t ImplicitTreapStorageCore::node_bytes(const std::unique_ptr<Node> &node) {
    return node ? node->subtree_bytes : 0;
}

std::size_t ImplicitTreapStorageCore::node_newlines(const std::unique_ptr<Node> &node) {
    return node ? node->subtree_newlines : 0;
}

std::size_t ImplicitTreapStorageCore::node_count(const std::unique_ptr<Node> &node) {
    if (!node) {
        return 0;
    }

    return 1 + node_count(node->left) + node_count(node->right);
}

const ImplicitTreapStorageCore::Node *ImplicitTreapStorageCore::leftmost_node(const std::unique_ptr<Node> &node) {
    const Node *current = node.get();
    while (current && current->left) {
        current = current->left.get();
    }
    return current;
}

const ImplicitTreapStorageCore::Node *ImplicitTreapStorageCore::rightmost_node(const std::unique_ptr<Node> &node) {
    const Node *current = node.get();
    while (current && current->right) {
        current = current->right.get();
    }
    return current;
}

std::size_t ImplicitTreapStorageCore::count_piece_newlines(const Piece &piece) const {
    return build_newline_offsets(piece).size();
}

std::vector<std::size_t> ImplicitTreapStorageCore::build_newline_offsets(const Piece &piece) const {
    std::vector<std::size_t> offsets;
    const auto bytes = piece_bytes(storage_, piece);
    offsets.reserve(bytes.size() / 16);

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (bytes[index] == static_cast<std::uint8_t>('\n')) {
            offsets.push_back(index);
        }
    }

    return offsets;
}

std::unique_ptr<ImplicitTreapStorageCore::Node> ImplicitTreapStorageCore::make_node(const Piece piece) {
    return make_node_with_priority(piece, priorities_.next());
}

std::unique_ptr<ImplicitTreapStorageCore::Node> ImplicitTreapStorageCore::make_node_with_priority(
    const Piece piece,
    const std::uint64_t priority) const {
    return make_node_with_offsets(piece, priority, build_newline_offsets(piece));
}

std::unique_ptr<ImplicitTreapStorageCore::Node> ImplicitTreapStorageCore::make_node_with_offsets(
    const Piece piece,
    const std::uint64_t priority,
    std::vector<std::size_t> newline_offsets) const {
    auto node = std::make_unique<Node>();
    node->piece = piece;
    node->priority = priority;
    node->newline_offsets = std::move(newline_offsets);
    node->piece_newlines = node->newline_offsets.size();
    node->subtree_bytes = piece.length;
    node->subtree_newlines = node->piece_newlines;
    return node;
}

void ImplicitTreapStorageCore::pull(Node *const node) {
    if (!node) {
        return;
    }

    node->subtree_bytes = node_bytes(node->left) + node->piece.length + node_bytes(node->right);
    node->subtree_newlines = node_newlines(node->left) + node->piece_newlines + node_newlines(node->right);
}

void ImplicitTreapStorageCore::extend_tail_node_with_inserted_suffix(Node *const tail, const std::string_view inserted_text) {
    assert(tail);

    const std::size_t old_length = tail->piece.length;
    tail->piece.length += inserted_text.size();
    for (std::size_t index = 0; index < inserted_text.size(); ++index) {
        if (inserted_text[index] == '\n') {
            tail->newline_offsets.push_back(old_length + index);
        }
    }
    tail->piece_newlines = tail->newline_offsets.size();
}

std::unique_ptr<ImplicitTreapStorageCore::Node> ImplicitTreapStorageCore::merge(
    std::unique_ptr<Node> left,
    std::unique_ptr<Node> right) {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    if (left->priority >= right->priority) {
        left->right = merge(std::move(left->right), std::move(right));
        pull(left.get());
        return left;
    }

    right->left = merge(std::move(left), std::move(right->left));
    pull(right.get());
    return right;
}

std::pair<Piece, Piece> ImplicitTreapStorageCore::split_piece_at(
    const Piece &piece,
    const std::size_t local_offset) {
    assert(local_offset < piece.length);

    Piece left = piece;
    Piece right = piece;

    left.length = local_offset;
    right.start = piece.start + local_offset;
    right.length = piece.length - local_offset;

    return {left, right};
}

bool ImplicitTreapStorageCore::can_coalesce_pieces(const Piece &left, const Piece &right) {
    if (left.length == 0 || right.length == 0) {
        return false;
    }
    if (left.buffer != right.buffer) {
        return false;
    }
    return left.start + left.length == right.start;
}

Piece ImplicitTreapStorageCore::coalesce_pieces(const Piece &left, const Piece &right) {
    assert(can_coalesce_pieces(left, right));
    return Piece{
        .buffer = left.buffer,
        .start = left.start,
        .length = left.length + right.length,
    };
}

std::pair<std::unique_ptr<ImplicitTreapStorageCore::Node>, std::unique_ptr<ImplicitTreapStorageCore::Node>>
ImplicitTreapStorageCore::detach_leftmost(std::unique_ptr<Node> root) {
    if (!root) {
        return {nullptr, nullptr};
    }

    if (!root->left) {
        auto rest = std::move(root->right);
        root->right.reset();
        pull(root.get());
        return {std::move(root), std::move(rest)};
    }

    auto [leftmost, new_left] = detach_leftmost(std::move(root->left));
    root->left = std::move(new_left);
    pull(root.get());
    return {std::move(leftmost), std::move(root)};
}

std::pair<std::unique_ptr<ImplicitTreapStorageCore::Node>, std::unique_ptr<ImplicitTreapStorageCore::Node>>
ImplicitTreapStorageCore::detach_rightmost(std::unique_ptr<Node> root) {
    if (!root) {
        return {nullptr, nullptr};
    }

    if (!root->right) {
        auto rest = std::move(root->left);
        root->left.reset();
        pull(root.get());
        return {std::move(root), std::move(rest)};
    }

    auto [rightmost, new_right] = detach_rightmost(std::move(root->right));
    root->right = std::move(new_right);
    pull(root.get());
    return {std::move(rightmost), std::move(root)};
}

std::unique_ptr<ImplicitTreapStorageCore::Node> ImplicitTreapStorageCore::join_with_boundary_coalescing(
    std::unique_ptr<Node> left,
    std::unique_ptr<Node> right) {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    const Node *left_tail_view = rightmost_node(left);
    const Node *right_head_view = leftmost_node(right);
    assert(left_tail_view);
    assert(right_head_view);
    if (!can_coalesce_pieces(left_tail_view->piece, right_head_view->piece)) {
        return merge(std::move(left), std::move(right));
    }

    auto [left_tail, left_rest] = detach_rightmost(std::move(left));
    auto [right_head, right_rest] = detach_leftmost(std::move(right));
    assert(left_tail);
    assert(right_head);

    const Piece merged_piece = coalesce_pieces(left_tail->piece, right_head->piece);
    auto merged_node = make_node_with_priority(
        merged_piece,
        std::max(left_tail->priority, right_head->priority));
    auto joined_left = merge(std::move(left_rest), std::move(merged_node));
    return merge(std::move(joined_left), std::move(right_rest));
}

std::pair<std::unique_ptr<ImplicitTreapStorageCore::Node>, std::unique_ptr<ImplicitTreapStorageCore::Node>>
ImplicitTreapStorageCore::split_by_offset(std::unique_ptr<Node> root, const std::size_t offset) {
    if (!root) {
        return {nullptr, nullptr};
    }

    const std::size_t left_bytes = node_bytes(root->left);
    const std::size_t piece_length = root->piece.length;

    if (offset < left_bytes) {
        auto [left_tree, right_tree] = split_by_offset(std::move(root->left), offset);
        root->left = std::move(right_tree);
        pull(root.get());
        return {std::move(left_tree), std::move(root)};
    }

    if (offset > left_bytes + piece_length) {
        auto [left_tree, right_tree] = split_by_offset(
            std::move(root->right),
            offset - left_bytes - piece_length);
        root->right = std::move(left_tree);
        pull(root.get());
        return {std::move(root), std::move(right_tree)};
    }

    if (offset == left_bytes) {
        auto left_tree = std::move(root->left);
        root->left.reset();
        pull(root.get());
        return {std::move(left_tree), std::move(root)};
    }

    if (offset == left_bytes + piece_length) {
        auto right_tree = std::move(root->right);
        root->right.reset();
        pull(root.get());
        return {std::move(root), std::move(right_tree)};
    }

    const std::size_t local_offset = offset - left_bytes;
    auto [left_piece, right_piece] = split_piece_at(root->piece, local_offset);
    std::vector<std::size_t> left_offsets;
    std::vector<std::size_t> right_offsets;
    left_offsets.reserve(root->newline_offsets.size());
    right_offsets.reserve(root->newline_offsets.size());
    for (const std::size_t newline_offset : root->newline_offsets) {
        if (newline_offset < local_offset) {
            left_offsets.push_back(newline_offset);
        } else {
            right_offsets.push_back(newline_offset - local_offset);
        }
    }

    auto left_subtree = std::move(root->left);
    auto right_subtree = std::move(root->right);
    auto left_piece_node = make_node_with_offsets(left_piece, root->priority, std::move(left_offsets));
    auto right_piece_node = make_node_with_offsets(right_piece, root->priority, std::move(right_offsets));

    auto left_tree = merge(std::move(left_subtree), std::move(left_piece_node));
    auto right_tree = merge(std::move(right_piece_node), std::move(right_subtree));
    return {std::move(left_tree), std::move(right_tree)};
}

bool ImplicitTreapStorageCore::can_insert(const ByteOffset offset) const {
    return offset.value >= 0 && static_cast<std::size_t>(offset.value) <= byte_count();
}

bool ImplicitTreapStorageCore::can_erase(const ByteRange range) const {
    if (range.start.value < 0 || range.length.value < 0) {
        return false;
    }

    const std::size_t start = static_cast<std::size_t>(range.start.value);
    const std::size_t length = static_cast<std::size_t>(range.length.value);
    return start <= byte_count() && length <= byte_count() - start;
}

std::size_t ImplicitTreapStorageCore::byte_count() const {
    return node_bytes(root_);
}

bool ImplicitTreapStorageCore::ends_with_newline() const {
    if (!root_) {
        return false;
    }

    const Node *node = root_.get();
    while (node->right) {
        node = node->right.get();
    }

    return piece_ends_with_newline(storage_, node->piece);
}

void ImplicitTreapStorageCore::insert(const ByteOffset offset, const std::string_view utf8_text) {
    assert(can_insert(offset));

    if (utf8_text.empty()) {
        return;
    }

    const std::size_t byte_offset = static_cast<std::size_t>(offset.value);
    const bool append_at_end = byte_offset == byte_count();
    const std::size_t add_start = storage_.add.size();
    storage_.add.insert(storage_.add.end(), utf8_text.begin(), utf8_text.end());

    Piece piece{
        .buffer = BufferKind::Add,
        .start = add_start,
        .length = utf8_text.size(),
    };

    if (append_at_end) {
        auto [tail, rest] = detach_rightmost(std::move(root_));
        if (tail && can_coalesce_pieces(tail->piece, piece)) {
            extend_tail_node_with_inserted_suffix(tail.get(), utf8_text);
            pull(tail.get());
            root_ = merge(std::move(rest), std::move(tail));
            return;
        }

        auto left_tree = merge(std::move(rest), std::move(tail));
        root_ = merge(std::move(left_tree), make_node(piece));
        return;
    }

    auto [left_tree, right_tree] = split_by_offset(std::move(root_), byte_offset);
    auto with_insert = join_with_boundary_coalescing(std::move(left_tree), make_node(piece));
    root_ = join_with_boundary_coalescing(std::move(with_insert), std::move(right_tree));
}

void ImplicitTreapStorageCore::erase(const ByteRange range) {
    assert(can_erase(range));

    if (range.length.value == 0 || range.start.value == byte_count()) {
        return;
    }

    const std::size_t start = static_cast<std::size_t>(range.start.value);
    const std::size_t length = static_cast<std::size_t>(range.length.value);

    auto [left_tree, rest] = split_by_offset(std::move(root_), start);
    auto [removed, right_tree] = split_by_offset(std::move(rest), length);
    (void)removed;

    root_ = join_with_boundary_coalescing(std::move(left_tree), std::move(right_tree));
}

std::size_t ImplicitTreapStorageCore::line_count() const {
    if (!root_) {
        return 1;
    }

    return node_newlines(root_) + (ends_with_newline() ? 0U : 1U);
}

void ImplicitTreapStorageCore::in_order_collect(const Node *const node, std::string &out) const {
    if (!node) {
        return;
    }

    in_order_collect(node->left.get(), out);
    append_piece_bytes(out, storage_, node->piece);
    in_order_collect(node->right.get(), out);
}

std::string ImplicitTreapStorageCore::text() const {
    std::string out;
    out.reserve(byte_count());
    in_order_collect(root_.get(), out);
    return out;
}

void ImplicitTreapStorageCore::collect_range(
    const Node *const node,
    const std::size_t start,
    const std::size_t end,
    const std::size_t base,
    std::string &out) const {
    if (!node || start >= end) {
        return;
    }

    const std::size_t left_bytes = node_bytes(node->left);
    const std::size_t node_begin = base + left_bytes;
    const std::size_t node_end = node_begin + node->piece.length;

    if (start < node_begin) {
        collect_range(node->left.get(), start, end, base, out);
    }

    const std::size_t overlap_start = std::max(start, node_begin);
    const std::size_t overlap_end = std::min(end, node_end);
    if (overlap_start < overlap_end) {
        append_piece_range(out, storage_, node->piece, overlap_start - node_begin, overlap_end - node_begin);
    }

    if (end > node_end) {
        collect_range(node->right.get(), start, end, node_end, out);
    }
}

std::string ImplicitTreapStorageCore::substring(const std::size_t start, const std::size_t length) const {
    if (start >= byte_count() || length == 0) {
        return {};
    }

    const std::size_t clamped = std::min(length, byte_count() - start);
    std::string out;
    out.reserve(clamped);
    collect_range(root_.get(), start, start + clamped, 0, out);
    return out;
}

std::size_t ImplicitTreapStorageCore::find_byte_after_nth_newline(
    const Node *const node,
    const std::size_t nth_newline,
    const std::size_t base) const {
    if (!node) {
        return byte_count();
    }

    const std::size_t left_lines = node_newlines(node->left);
    if (nth_newline <= left_lines) {
        return find_byte_after_nth_newline(node->left.get(), nth_newline, base);
    }

    const std::size_t node_begin = base + node_bytes(node->left);
    if (nth_newline <= left_lines + node->piece_newlines) {
        const std::size_t local_index = nth_newline - left_lines - 1;
        return node_begin + node->newline_offsets[local_index] + 1;
    }

    return find_byte_after_nth_newline(
        node->right.get(),
        nth_newline - left_lines - node->piece_newlines,
        node_begin + node->piece.length);
}

std::size_t ImplicitTreapStorageCore::line_start_offset(const std::size_t line_index) const {
    if (line_index == 0) {
        return 0;
    }

    if (line_index >= line_count()) {
        return byte_count();
    }

    return find_byte_after_nth_newline(root_.get(), line_index, 0);
}

std::size_t ImplicitTreapStorageCore::piece_count() const {
    return node_count(root_);
}

void ImplicitTreapStorageCore::collect_pieces_in_order(const Node *const node, std::vector<Piece> &out) const {
    if (!node) {
        return;
    }

    collect_pieces_in_order(node->left.get(), out);
    out.push_back(node->piece);
    collect_pieces_in_order(node->right.get(), out);
}

void ImplicitTreapStorageCore::rebuild_from_piece_sequence(const std::vector<Piece> &pieces) {
    root_.reset();
    for (const Piece &piece : pieces) {
        root_ = merge(std::move(root_), make_node(piece));
    }
}

std::size_t ImplicitTreapStorageCore::compact_with_merge_budget(const std::size_t max_merges) {
    if (max_merges == 0 || !root_) {
        return 0;
    }

    std::vector<Piece> pieces;
    pieces.reserve(piece_count());
    collect_pieces_in_order(root_.get(), pieces);
    if (pieces.size() < 2) {
        return 0;
    }

    std::size_t merges = 0;
    std::vector<Piece> compacted;
    compacted.reserve(pieces.size());
    compacted.push_back(pieces.front());

    for (std::size_t index = 1; index < pieces.size(); ++index) {
        if (merges < max_merges && can_coalesce_pieces(compacted.back(), pieces[index])) {
            compacted.back() = coalesce_pieces(compacted.back(), pieces[index]);
            ++merges;
        } else {
            compacted.push_back(pieces[index]);
        }
    }

    if (merges > 0) {
        rebuild_from_piece_sequence(compacted);
    }
    return merges;
}

ImplicitTreapStorageCore::ValidateResult ImplicitTreapStorageCore::validate(
    const Node *const node,
    const std::uint64_t parent_priority) const {
    if (!node) {
        return {};
    }

    if (node->priority > parent_priority) {
        return {.ok = false};
    }

    if (node->piece.buffer == BufferKind::Original) {
        if (node->piece.start + node->piece.length > storage_.original.size()) {
            return {.ok = false};
        }
    } else if (node->piece.start + node->piece.length > storage_.add.size()) {
        return {.ok = false};
    }

    const ValidateResult left = validate(node->left.get(), node->priority);
    if (!left.ok) {
        return {.ok = false};
    }

    const ValidateResult right = validate(node->right.get(), node->priority);
    if (!right.ok) {
        return {.ok = false};
    }

    const auto expected_offsets = build_newline_offsets(node->piece);
    if (expected_offsets != node->newline_offsets) {
        return {.ok = false};
    }

    if (count_piece_newlines(node->piece) != node->piece_newlines) {
        return {.ok = false};
    }

    const std::size_t expected_bytes = left.bytes + node->piece.length + right.bytes;
    const std::size_t expected_newlines = left.newlines + node->piece_newlines + right.newlines;
    if (node->subtree_bytes != expected_bytes || node->subtree_newlines != expected_newlines) {
        return {.ok = false};
    }

    return ValidateResult{
        .ok = true,
        .bytes = expected_bytes,
        .newlines = expected_newlines,
    };
}

bool ImplicitTreapStorageCore::check_invariants() const {
    return validate(root_.get(), std::numeric_limits<std::uint64_t>::max()).ok;
}

}  // namespace vitality::buffer_internal
