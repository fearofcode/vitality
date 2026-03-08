#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "core/CoreTypes.h"

namespace vitality::buffer_internal {

enum class BufferKind : std::uint8_t {
    Original,
    Add,
};

struct Piece {
    BufferKind buffer = BufferKind::Original;
    std::size_t start = 0;
    std::size_t length = 0;
};

struct PieceStorage {
    std::string original;
    std::vector<std::uint8_t> add;
};

class PrioritySource {
public:
    explicit PrioritySource(std::uint64_t seed);
    [[nodiscard]] std::uint64_t next();

private:
    std::mt19937_64 rng_;
    std::uniform_int_distribution<std::uint64_t> dist_;
};

class ImplicitTreapStorageCore {
public:
    explicit ImplicitTreapStorageCore(
        std::string_view initial = {},
        std::uint64_t seed = 0xC0FFEEULL);

    [[nodiscard]] bool can_insert(ByteOffset offset) const;
    [[nodiscard]] bool can_erase(ByteRange range) const;
    [[nodiscard]] std::size_t byte_count() const;
    [[nodiscard]] std::size_t line_count() const;
    [[nodiscard]] std::size_t line_start_offset(std::size_t line_index) const;
    [[nodiscard]] std::string substring(std::size_t start, std::size_t length) const;
    [[nodiscard]] std::string text() const;
    [[nodiscard]] std::size_t piece_count() const;
    [[nodiscard]] bool ends_with_newline() const;
    [[nodiscard]] bool check_invariants() const;
    [[nodiscard]] std::size_t compact_with_merge_budget(std::size_t max_merges);

    void insert(ByteOffset offset, std::string_view utf8_text);
    void erase(ByteRange range);

private:
    struct Node {
        Piece piece{};
        std::uint64_t priority = 0;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        std::vector<std::size_t> newline_offsets;
        std::size_t piece_newlines = 0;
        std::size_t subtree_bytes = 0;
        std::size_t subtree_newlines = 0;
    };

    struct ValidateResult {
        bool ok = true;
        std::size_t bytes = 0;
        std::size_t newlines = 0;
    };

    struct RecentContiguousEdit {
        // Tracks one very specific "recent typing run" inside the piece table.
        //
        // Example:
        //   original text: "hello world"
        //   user moves into the middle and types "abc"
        //
        // After the first insert, the document may look like:
        //   [original "hello "] [add "a"] [original "world"]
        //
        // After the next two inserts, we want the cheap path to keep extending
        // that same add-backed piece instead of creating:
        //   [original "hello "] [add "a"] [add "b"] [add "c"] [original "world"]
        //
        // This tracker remembers exactly which add-backed piece currently
        // represents "the thing the user is in the middle of typing". The
        // tracker is intentionally fragile: if we cannot prove that a later
        // insert/erase still refers to that exact piece, we clear it and fall
        // back to the generic treap mutation code.
        bool active = false;
        // Document byte offset where that tracked piece begins.
        std::size_t piece_doc_start = 0;
        // Offset into storage_.add where the tracked piece begins.
        std::size_t piece_add_start = 0;
        // Current full length of the tracked piece. This is the whole piece,
        // not just the newest bytes typed into it.
        std::size_t piece_length = 0;
        // Suffix of that piece that belongs to the current contiguous typing
        // run. This is narrower than piece_length because the tracked piece may
        // already contain older bytes. Immediate backspace is only allowed to
        // trim from this suffix, because that is the only region we know is
        // safe to shrink without changing earlier document structure.
        std::size_t typed_suffix_length = 0;
    };

    PieceStorage storage_;
    std::unique_ptr<Node> root_;
    PrioritySource priorities_;
    RecentContiguousEdit recent_contiguous_edit_;

    [[nodiscard]] static std::size_t node_bytes(const std::unique_ptr<Node> &node);
    [[nodiscard]] static std::size_t node_newlines(const std::unique_ptr<Node> &node);
    [[nodiscard]] static std::size_t node_count(const std::unique_ptr<Node> &node);
    // Return the first piece in document order inside this subtree.
    //
    // Why this exists:
    // after a split we often have two subtrees that represent adjacent
    // document ranges:
    //   left_tree  = everything before some boundary
    //   right_tree = everything after that boundary
    //
    // When we glue them back together, the only possible new merge opportunity
    // is at the outer edge between those two trees. We do not need to inspect
    // every piece in right_tree; we only need to know which piece starts
    // right_tree in document order.
    //
    // This helper gives us that read-only answer cheaply, before we decide
    // whether a more expensive detach/rewrite step is necessary.
    [[nodiscard]] static const Node *leftmost_node(const std::unique_ptr<Node> &node);
    // Return the last piece in document order inside this subtree.
    //
    // This is the symmetric helper for the left side of a split. In the common
    // mutation case we ask:
    //   "what piece currently ends exactly at this document boundary?"
    //
    // Examples:
    // - before joining left_tree and right_tree back together, we check whether
    //   the last piece of left_tree and first piece of right_tree can coalesce
    // - before extending a recent typing run, we check whether the piece that
    //   ends at the cursor is the exact add-backed piece the tracker expects
    //
    // Again, the value here is not that "rightmost node in a tree" is exotic;
    // it is that these optimization paths need a cheap boundary probe before
    // deciding whether to rewrite any tree structure at all.
    [[nodiscard]] static const Node *rightmost_node(const std::unique_ptr<Node> &node);

    [[nodiscard]] std::size_t count_piece_newlines(const Piece &piece) const;
    [[nodiscard]] std::vector<std::size_t> build_newline_offsets(const Piece &piece) const;
    [[nodiscard]] std::unique_ptr<Node> make_node(Piece piece);
    [[nodiscard]] std::unique_ptr<Node> make_node_with_priority(Piece piece, std::uint64_t priority) const;
    [[nodiscard]] std::unique_ptr<Node> make_node_with_offsets(
        const Piece &piece,
        std::uint64_t priority,
        std::vector<std::size_t> newline_offsets) const;
    [[nodiscard]] std::size_t find_byte_after_nth_newline(
        const Node *node,
        std::size_t nth_newline,
        std::size_t base) const;
    [[nodiscard]] std::unique_ptr<Node> merge(std::unique_ptr<Node> left, std::unique_ptr<Node> right);
    [[nodiscard]] std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>> split_by_offset(
        std::unique_ptr<Node> root,
        std::size_t offset);
    [[nodiscard]] static std::pair<Piece, Piece> split_piece_at(const Piece &piece, std::size_t local_offset);
    [[nodiscard]] static bool can_coalesce_pieces(const Piece &left, const Piece &right);
    [[nodiscard]] static Piece coalesce_pieces(const Piece &left, const Piece &right);
    // Remove and return the first piece in document order, plus the remaining
    // subtree.
    //
    // This is the "we now know we really do need to touch the boundary piece"
    // companion to leftmost_node(). The read-only helper lets us cheaply ask
    // whether the boundary matters; detach_leftmost() is what we use once we
    // have decided to actually rewrite that first piece.
    //
    // Typical use:
    //   1. inspect the first piece of right_tree
    //   2. decide it can merge with the piece to its left
    //   3. detach that first node so we can replace it or combine it
    [[nodiscard]] std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>> detach_leftmost(
        std::unique_ptr<Node> root);
    // Remove and return the last piece in document order, plus the remaining
    // subtree.
    //
    // This is used in the symmetric situations on the left side of a split:
    //   - get the exact piece that ends at a document boundary
    //   - mutate or shrink that one piece
    //   - merge the remaining tree back together
    //
    // Recent-typing fast paths depend on this because they need the exact
    // boundary node, not just knowledge that "some boundary piece exists".
    [[nodiscard]] std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>> detach_rightmost(
        std::unique_ptr<Node> root);
    // Join two trees that already represent adjacent document ranges.
    //
    // Without this helper, a mutation would often:
    //   1. split the tree
    //   2. modify something local
    //   3. merge the trees back
    //   4. run a second cleanup pass to see whether the newly adjacent boundary
    //      pieces can be collapsed into one piece
    //
    // This helper folds steps 3 and 4 together. It looks only at the one
    // boundary that could have changed:
    //   last piece of left tree  +  first piece of right tree
    //
    // If those two pieces are adjacent spans in the same backing buffer, it
    // replaces them with one combined piece while performing the join.
    // Otherwise it falls back to a normal treap merge.
    [[nodiscard]] std::unique_ptr<Node> join_with_boundary_coalescing(
        std::unique_ptr<Node> left,
        std::unique_ptr<Node> right);
    void clear_recent_contiguous_edit();
    void record_recent_add_piece(
        std::size_t piece_doc_start,
        std::size_t piece_add_start,
        std::size_t piece_length,
        std::size_t typed_suffix_length);
    // Fast path for "the user typed another byte immediately after the last
    // byte they just typed".
    //
    // The desired effect is:
    //   old: [original left] [add "abc"] [original right]
    //   new: [original left] [add "abcd"] [original right]
    //
    // rather than creating a fresh add piece for every byte.
    //
    // We still have to do local treap work because that tracked add-backed
    // piece may live in the middle of the document. The helper:
    //   - verifies that the insertion point is exactly at the end of the
    //     tracked piece
    //   - isolates that exact node from the tree
    //   - extends its length in place
    //   - joins the surrounding trees back together
    //
    // If any of those assumptions fail, it returns false and the caller falls
    // back to the generic insert path.
    [[nodiscard]] bool try_extend_recent_contiguous_insert(ByteOffset offset, std::string_view utf8_text);
    // Fast path for immediate backspace over the bytes that were just typed.
    //
    // This is intentionally much narrower than generic erase. It handles only:
    //   "remove bytes from the end of the currently tracked typed suffix"
    //
    // In other words, it is for the common editor action:
    //   type type type backspace backspace
    //
    // It is not for arbitrary deletion elsewhere in the document. If the erase
    // range is not exactly the suffix we know how to shrink safely, the helper
    // returns false and the caller uses the generic split/remove/join path.
    [[nodiscard]] bool try_erase_recent_typed_suffix(ByteRange range);
    // Grow an already isolated add-backed node by treating the new bytes as a
    // suffix of that same piece.
    //
    // This is the low-level helper used once the fast path has already proven:
    //   "yes, this exact node is the one we want to extend"
    //
    // It updates both the piece length and the cached newline offsets only for
    // the new suffix, so we avoid rescanning the entire piece contents after
    // each typed character.
    void extend_tail_node_with_inserted_suffix(Node *tail, std::string_view inserted_text);
    void collect_pieces_in_order(const Node *node, std::vector<Piece> &out) const;
    void rebuild_from_piece_sequence(const std::vector<Piece> &pieces);

    void pull(Node *node);
    void in_order_collect(const Node *node, std::string &out) const;
    void collect_range(
        const Node *node,
        std::size_t start,
        std::size_t end,
        std::size_t base,
        std::string &out) const;
    [[nodiscard]] ValidateResult validate(const Node *node, std::uint64_t parent_priority) const;
};

}  // namespace vitality::buffer_internal
