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

    // end is caller-derived overlap math and should normally already be inside
    // the piece, but clamp it defensively so this helper always enforces the
    // legal local half-open interval:
    //   [begin, end) intersect [0, piece.length) -> [begin, clamped_end)
    //
    // That keeps this helper robust even if a caller hands us a range that
    // would otherwise describe bytes past the piece boundary.
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

void trim_piece_newline_offsets_to_length(std::vector<std::size_t> &newline_offsets, const std::size_t new_length) {
    while (!newline_offsets.empty() && newline_offsets.back() >= new_length) {
        newline_offsets.pop_back();
    }
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
    const Piece &piece,
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

// Optimization helper: grow one existing add-backed piece in place after new
// bytes were appended to storage_.add. This keeps typing and append-style edits
// from creating a fresh node for every byte.
void ImplicitTreapStorageCore::extend_tail_node_with_inserted_suffix(Node *const tail, const std::string_view inserted_text) {
    assert(tail);

    // "Tail" here means the piece node we already know should absorb the new
    // bytes. This helper performs the cheap in-place update: grow the piece
    // length and append newline offsets only for the newly inserted suffix.
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
    // Branch 1: one side is empty, so the other side is already the full
    // merged result.
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    // Branch 2: left root wins the heap/priority comparison, so it stays the
    // root and we recursively merge into its right subtree.
    if (left->priority >= right->priority) {
        left->right = merge(std::move(left->right), std::move(right));
        pull(left.get());
        return left;
    }

    // Branch 3: right root wins the priority comparison, so it stays the root
    // and we recursively merge into its left subtree.
    right->left = merge(std::move(left), std::move(right->left));
    pull(right.get());
    return right;
}

std::pair<Piece, Piece> ImplicitTreapStorageCore::split_piece_at(
    const Piece &piece,
    const std::size_t local_offset) {
    assert(local_offset < piece.length);

    // Splitting a piece does not copy bytes. Both resulting descriptors still
    // point into the same backing buffer; they just cover disjoint subranges of
    // the original span.
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
    // Two pieces are mergeable only when they refer to adjacent byte ranges in
    // the same backing store. If there is any gap, overlap, or buffer change,
    // merging would change the represented text.
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

// Optimization helper: detach exactly the first in-document node from a tree so
// boundary-aware join logic can inspect or rewrite that one piece without
// flattening the whole subtree.
std::pair<std::unique_ptr<ImplicitTreapStorageCore::Node>, std::unique_ptr<ImplicitTreapStorageCore::Node>>
ImplicitTreapStorageCore::detach_leftmost(std::unique_ptr<Node> root) {
    if (!root) {
        return {nullptr, nullptr};
    }

    if (!root->left) {
        // This node is already the first in document order. Return it and keep
        // its right subtree as the remaining tree.
        auto rest = std::move(root->right);
        root->right.reset();
        pull(root.get());
        return {std::move(root), std::move(rest)};
    }

    // Recurse down the left spine until we find the first in-order node, then
    // rebuild metadata on the way back up.
    auto [leftmost, new_left] = detach_leftmost(std::move(root->left));
    root->left = std::move(new_left);
    pull(root.get());
    return {std::move(leftmost), std::move(root)};
}

// Optimization helper: detach exactly the last in-document node from a tree so
// fast insert/erase paths can mutate the boundary piece that touches a split
// point, then stitch the remaining tree back together.
std::pair<std::unique_ptr<ImplicitTreapStorageCore::Node>, std::unique_ptr<ImplicitTreapStorageCore::Node>>
ImplicitTreapStorageCore::detach_rightmost(std::unique_ptr<Node> root) {
    if (!root) {
        return {nullptr, nullptr};
    }

    if (!root->right) {
        // This node is already the last in document order. Return it and keep
        // its left subtree as the remaining tree.
        auto rest = std::move(root->left);
        root->left.reset();
        pull(root.get());
        return {std::move(root), std::move(rest)};
    }

    // Recurse down the right spine until we find the last in-order node, then
    // rebuild metadata on the way back up.
    auto [rightmost, new_right] = detach_rightmost(std::move(root->right));
    root->right = std::move(new_right);
    pull(root.get());
    return {std::move(rightmost), std::move(root)};
}

// Optimization helper: join adjacent document subtrees while opportunistically
// collapsing only the single boundary pair that could have become mergeable.
// This avoids a separate post-hoc coalescing pass after common mutations.
std::unique_ptr<ImplicitTreapStorageCore::Node> ImplicitTreapStorageCore::join_with_boundary_coalescing(
    std::unique_ptr<Node> left,
    std::unique_ptr<Node> right) {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    // Only the document boundary between the two trees can create a new
    // merge opportunity. Internal boundaries inside each tree were already
    // valid before this join, so we inspect the rightmost piece from the left
    // tree and the leftmost piece from the right tree.
    const Node *left_tail_view = rightmost_node(left);
    const Node *right_head_view = leftmost_node(right);
    assert(left_tail_view);
    assert(right_head_view);

    // Most joins do not actually create mergeable neighbors. In that common
    // case, avoid detaching and rebuilding boundary nodes and just perform the
    // ordinary treap merge.
    if (!can_coalesce_pieces(left_tail_view->piece, right_head_view->piece)) {
        return merge(std::move(left), std::move(right));
    }

    // If the boundary pieces are contiguous in the same backing buffer, detach
    // just those two edge nodes, replace them with one combined piece, then
    // stitch the trees back together.
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

// Optimization helper: drop the recent typing tracker whenever a mutation path
// can no longer prove which add-backed piece represents the current typing run.
void ImplicitTreapStorageCore::clear_recent_contiguous_edit() {
    recent_contiguous_edit_ = RecentContiguousEdit{};
}

// Optimization helper: remember one exact add-backed piece as the current
// contiguous typing run so the next insert/backspace can try a narrower fast
// path instead of the generic split/join mutation path.
void ImplicitTreapStorageCore::record_recent_add_piece(
    const std::size_t piece_doc_start,
    const std::size_t piece_add_start,
    const std::size_t piece_length,
    const std::size_t typed_suffix_length) {
    recent_contiguous_edit_ = RecentContiguousEdit{
        .active = true,
        .piece_doc_start = piece_doc_start,
        .piece_add_start = piece_add_start,
        .piece_length = piece_length,
        .typed_suffix_length = typed_suffix_length,
    };
}

// Optimization helper: fast path for "type another byte right where the last
// typing run ended". It isolates the tracked add-backed piece, grows it in
// place, and avoids creating a brand-new node for each keystroke.
bool ImplicitTreapStorageCore::try_extend_recent_contiguous_insert(
    const ByteOffset offset,
    const std::string_view utf8_text) {
    if (!recent_contiguous_edit_.active || utf8_text.empty() || offset.value < 0) {
        return false;
    }

    const std::size_t byte_offset = static_cast<std::size_t>(offset.value);
    if (byte_offset != recent_contiguous_edit_.piece_doc_start + recent_contiguous_edit_.piece_length) {
        return false;
    }
    if (recent_contiguous_edit_.piece_add_start + recent_contiguous_edit_.piece_length != storage_.add.size()) {
        return false;
    }

    // Even on the fast path we still split the tree.
    //
    // A tempting design would be: store a Node* in RecentContiguousEdit and,
    // when the user types another byte, just dereference that pointer and
    // increment piece.length.
    //
    // We do not do that because treap mutations routinely move, replace, or
    // destroy nodes:
    // - split_by_offset() can replace one node with two new piece nodes
    // - join/coalescing can remove two boundary nodes and replace them with one
    // - compaction can rebuild the tree from a new piece sequence
    //
    // Now at this point you might be thinking "well just do like this":
    //
    //     if (recent_contiguous_edit_.node != null &&
    //        recent_contiguous_edit_.node->start + recent_contiguous_edit_.node->length + 1 == byte_offset) {
    //         storage_.add.insert(storage_.add.end(), utf8_text.begin(), utf8_text.end());
    //         recent_contiguous_edit_.node->length++; // ha ha! we are so smart!
    //         return true;
    //     } else {
    //         recent_contiguous_edit_.node = null;
    //         return false;
    //     }
    //
    // Yes, if we made sure to invalidate recent_contiguous_edit_.node properly. But we'd also have to update the node's
    // ancestors' subtree counts which would require either a parent pointer or a path to the node that would also have
    // to be properly invalidated. This breaks our conceptual ownership model that a node owns its subtree unless we use
    // raw pointers and introduces new potential bugs around stale parent pointers.
    //
    // This code path still avoids allocating a new piece that would have to be coalesced.
    //
    // So a cached pointer would be fragile and easy to turn into a dangling or
    // stale reference. The tracker therefore stores the *logical identity* of
    // the recent piece (where it starts in the document, where it starts in the
    // add buffer, and how long it is), not a direct pointer to a mutable node.
    // We then rediscover the current node from tree structure at the moment we
    // need to edit it. The tracked add-backed piece may currently be surrounded
    // by other pieces:
    //   [original left] [tracked add piece] [original or add right]
    //
    // The tree is organized by document order plus treap priorities, not by
    // "give me the node that ends at offset N". Splitting at the insertion
    // point turns the problem into:
    //   left_tree = everything up to the cursor
    //   right_tree = everything after the cursor
    //
    // If the tracker is still correct, the tracked piece must now be the
    // rightmost node of left_tree.
    //
    // Why:
    // - the insertion point is required to be exactly at the end of the
    //   tracked piece
    // - split_by_offset(root_, byte_offset) puts every byte strictly before or
    //   at that insertion point into left_tree
    // - everything after that insertion point goes into right_tree
    //
    // So after the split, the piece that ends exactly at the insertion point
    // is the last piece in document order inside left_tree. If some other node
    // appears there instead, then our tracker no longer describes the current
    // tree shape correctly.
    //
    // Detaching that one boundary node gives us the exact current node to
    // extend, without assuming an old pointer is still valid.
    auto [left_tree, right_tree] = split_by_offset(std::move(root_), byte_offset);
    auto [left_tail, left_rest] = detach_rightmost(std::move(left_tree));
    if (!left_tail ||
        left_tail->piece.buffer != BufferKind::Add ||
        left_tail->piece.start != recent_contiguous_edit_.piece_add_start ||
        left_tail->piece.length != recent_contiguous_edit_.piece_length) {
        // The tracker was stale or ambiguous.
        //
        // Examples:
        // - another edit changed the surrounding document structure
        // - the tracked piece got merged, split, or removed
        // - the cursor offset still looks plausible, but the boundary node we
        //   found is not the exact add-backed piece the tracker describes
        //
        // At that point we no longer have enough information to say "extend
        // this node in place" safely. Put the tree back together exactly as it
        // was, clear the tracker, and let the generic insert path rediscover
        // the correct structure.
        auto rebuilt_left = merge(std::move(left_rest), std::move(left_tail));
        root_ = merge(std::move(rebuilt_left), std::move(right_tree));
        clear_recent_contiguous_edit();
        return false;
    }

    // Now that we have proven we are holding the exact tracked add-backed node,
    // append the new bytes to storage_.add and then extend the piece to cover
    // them. Doing this after validation avoids leaving dead bytes behind in the
    // add buffer when the tracker turns out to be stale and we have to fall
    // back to the generic insert path.
    storage_.add.insert(storage_.add.end(), utf8_text.begin(), utf8_text.end());

    extend_tail_node_with_inserted_suffix(left_tail.get(), utf8_text);
    pull(left_tail.get());

    recent_contiguous_edit_.piece_length += utf8_text.size();
    recent_contiguous_edit_.typed_suffix_length += utf8_text.size();

    // We rejoin through boundary-aware coalescing so that, if the extended add
    // piece now touches a compatible neighbor on the right, we preserve the
    // normal piece-collapsing invariants instead of leaving an avoidable split.
    auto rebuilt_left = merge(std::move(left_rest), std::move(left_tail));
    root_ = join_with_boundary_coalescing(std::move(rebuilt_left), std::move(right_tree));
    return true;
}

// Optimization helper: fast path for immediate backspace over recently typed
// bytes. It shrinks only the tracked suffix of the current add-backed piece and
// falls back immediately if the erase is not exactly that local case.
bool ImplicitTreapStorageCore::try_erase_recent_typed_suffix(const ByteRange range) {
    if (!recent_contiguous_edit_.active || range.start.value < 0 || range.length.value <= 0) {
        return false;
    }

    const std::size_t start = static_cast<std::size_t>(range.start.value);
    const std::size_t length = static_cast<std::size_t>(range.length.value);
    const std::size_t piece_end = recent_contiguous_edit_.piece_doc_start + recent_contiguous_edit_.piece_length;
    const std::size_t typed_suffix_start = piece_end - recent_contiguous_edit_.typed_suffix_length;
    if (start + length != piece_end || start < typed_suffix_start) {
        return false;
    }

    // Immediate backspace only needs to touch the tracked add-backed piece, but
    // that piece can still live inside the middle of the document. Split at the
    // known end position so the tracked node becomes the rightmost node of the
    // left tree, then detach just that node for in-place shrinking.
    auto [left_tree, right_tree] = split_by_offset(std::move(root_), piece_end);
    auto [left_tail, left_rest] = detach_rightmost(std::move(left_tree));
    if (!left_tail ||
        left_tail->piece.buffer != BufferKind::Add ||
        left_tail->piece.start != recent_contiguous_edit_.piece_add_start ||
        left_tail->piece.length != recent_contiguous_edit_.piece_length) {
        // The tracker no longer describes the actual boundary piece. Put the
        // tree back together and let the generic erase path handle it.
        auto rebuilt_left = merge(std::move(left_rest), std::move(left_tail));
        root_ = merge(std::move(rebuilt_left), std::move(right_tree));
        clear_recent_contiguous_edit();
        return false;
    }

    const std::size_t new_length = left_tail->piece.length - length;
    if (new_length == 0) {
        // If backspace removes the entire tracked add piece, the left and right
        // sides may become directly mergeable again, so use the same boundary
        // coalescing join as the generic erase path.
        recent_contiguous_edit_.piece_length = 0;
        recent_contiguous_edit_.typed_suffix_length -= length;
        clear_recent_contiguous_edit();
        root_ = join_with_boundary_coalescing(std::move(left_rest), std::move(right_tree));
        return true;
    }

    left_tail->piece.length = new_length;
    trim_piece_newline_offsets_to_length(left_tail->newline_offsets, new_length);
    left_tail->piece_newlines = left_tail->newline_offsets.size();
    pull(left_tail.get());

    recent_contiguous_edit_.piece_length = new_length;
    recent_contiguous_edit_.typed_suffix_length -= length;

    // Shrinking the tracked node keeps the fast path local: we only rebuild the
    // minimal path needed to put that one node back into the treap.
    auto rebuilt_left = merge(std::move(left_rest), std::move(left_tail));
    root_ = join_with_boundary_coalescing(std::move(rebuilt_left), std::move(right_tree));
    return true;
}

std::pair<std::unique_ptr<ImplicitTreapStorageCore::Node>, std::unique_ptr<ImplicitTreapStorageCore::Node>>
ImplicitTreapStorageCore::split_by_offset(std::unique_ptr<Node> root, const std::size_t offset) {
    if (!root) {
        return {nullptr, nullptr};
    }

    const std::size_t left_bytes = node_bytes(root->left);
    const std::size_t piece_length = root->piece.length;

    // Branch 1: the split point lands strictly inside the left subtree.
    if (offset < left_bytes) {
        auto [left_tree, right_tree] = split_by_offset(std::move(root->left), offset);
        root->left = std::move(right_tree);
        pull(root.get());
        return {std::move(left_tree), std::move(root)};
    }

    // Branch 2: the split point lands strictly inside the right subtree.
    // Rebase the offset so the recursive call sees it relative to the start of
    // that subtree, not the start of the current node's full range.
    if (offset > left_bytes + piece_length) {
        auto [left_tree, right_tree] = split_by_offset(
            std::move(root->right),
            offset - left_bytes - piece_length);
        root->right = std::move(left_tree);
        pull(root.get());
        return {std::move(root), std::move(right_tree)};
    }

    // Branch 3: split exactly before the current piece.
    if (offset == left_bytes) {
        auto left_tree = std::move(root->left);
        root->left.reset();
        pull(root.get());
        return {std::move(left_tree), std::move(root)};
    }

    // Branch 4: split exactly after the current piece.
    if (offset == left_bytes + piece_length) {
        auto right_tree = std::move(root->right);
        root->right.reset();
        pull(root.get());
        return {std::move(root), std::move(right_tree)};
    }

    // Branch 5: the split lands inside the current piece, so one piece
    // descriptor becomes two descriptors that still point into the same
    // backing buffer.
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
    // Keep the original node priority for both fragments so the untouched
    // ancestors above this local rewrite still satisfy the treap priority
    // ordering.
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

    if (try_extend_recent_contiguous_insert(offset, utf8_text)) {
        return;
    }

    clear_recent_contiguous_edit();

    const std::size_t byte_offset = static_cast<std::size_t>(offset.value);
    const bool append_at_end = byte_offset == byte_count();
    const std::size_t add_start = storage_.add.size();
    storage_.add.insert(storage_.add.end(), utf8_text.begin(), utf8_text.end());

    const Piece piece{
        .buffer = BufferKind::Add,
        .start = add_start,
        .length = utf8_text.size(),
    };

    if (append_at_end) {
        auto [tail, rest] = detach_rightmost(std::move(root_));
        if (tail && can_coalesce_pieces(tail->piece, piece)) {
            const std::size_t existing_length = tail->piece.length;
            extend_tail_node_with_inserted_suffix(tail.get(), utf8_text);
            pull(tail.get());
            root_ = merge(std::move(rest), std::move(tail));
            record_recent_add_piece(
                byte_offset - existing_length,
                add_start - existing_length,
                existing_length + utf8_text.size(),
                utf8_text.size());
            return;
        }

        auto left_tree = merge(std::move(rest), std::move(tail));
        root_ = merge(std::move(left_tree), make_node(piece));
        record_recent_add_piece(byte_offset, add_start, utf8_text.size(), utf8_text.size());
        return;
    }

    auto [left_tree, right_tree] = split_by_offset(std::move(root_), byte_offset);
    const Node *left_tail_view = rightmost_node(left_tree);
    const Node *right_head_view = leftmost_node(right_tree);
    // We record whether the newly inserted add-backed piece will merge with an
    // existing neighbor so we can decide whether the recent-edit tracker can be
    // re-established exactly after the generic insertion finishes.
    const bool coalesces_left = left_tail_view && can_coalesce_pieces(left_tail_view->piece, piece);
    const bool coalesces_right = right_head_view && can_coalesce_pieces(piece, right_head_view->piece);
    auto with_insert = join_with_boundary_coalescing(std::move(left_tree), make_node(piece));
    root_ = join_with_boundary_coalescing(std::move(with_insert), std::move(right_tree));

    if (left_tail_view != nullptr && coalesces_left && !coalesces_right) {
        record_recent_add_piece(
            byte_offset - left_tail_view->piece.length,
            left_tail_view->piece.start,
            left_tail_view->piece.length + utf8_text.size(),
            utf8_text.size());
    } else if (!coalesces_left && !coalesces_right) {
        record_recent_add_piece(byte_offset, add_start, utf8_text.size(), utf8_text.size());
    } else {
        // If the inserted piece merged on the right, or on both sides, the
        // resulting piece identity is no longer cheap to reconstruct from the
        // pre-join boundary views. Prefer dropping the tracker to recording the
        // wrong piece and corrupting later fast-path edits.
        clear_recent_contiguous_edit();
    }
}

void ImplicitTreapStorageCore::erase(const ByteRange range) {
    assert(can_erase(range));

    if (range.length.value == 0 || range.start.value == byte_count()) {
        return;
    }

    if (try_erase_recent_typed_suffix(range)) {
        return;
    }

    clear_recent_contiguous_edit();

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

    // base is the running absolute origin for this recursive frame: "the first
    // byte represented by this subtree starts at absolute document offset
    // base". We intentionally carry that down the traversal instead of storing
    // absolute offsets on every node, because absolute offsets would become
    // stale after inserts/deletes/rebalancing and would require broad updates.
    const std::size_t left_bytes = node_bytes(node->left);
    const std::size_t node_begin = base + left_bytes;
    const std::size_t node_end = node_begin + node->piece.length;

    // Branch 1: recurse left only if the requested absolute range reaches into
    // the left subtree interval [base, node_begin).
    if (start < node_begin) {
        collect_range(node->left.get(), start, end, base, out);
    }

    const std::size_t overlap_start = std::max(start, node_begin);
    const std::size_t overlap_end = std::min(end, node_end);
    // Branch 2: append the overlap from the current piece if the query range
    // intersects [node_begin, node_end).
    if (overlap_start < overlap_end) {
        append_piece_range(out, storage_, node->piece, overlap_start - node_begin, overlap_end - node_begin);
    }

    // Branch 3: recurse right only if the requested range extends beyond the
    // current piece. The right subtree begins at node_end in absolute document
    // coordinates.
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
        clear_recent_contiguous_edit();
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
