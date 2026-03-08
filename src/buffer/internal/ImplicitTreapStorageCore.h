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

    PieceStorage storage_;
    std::unique_ptr<Node> root_;
    PrioritySource priorities_;

    [[nodiscard]] static std::size_t node_bytes(const std::unique_ptr<Node> &node);
    [[nodiscard]] static std::size_t node_newlines(const std::unique_ptr<Node> &node);
    [[nodiscard]] static std::size_t node_count(const std::unique_ptr<Node> &node);
    [[nodiscard]] static const Node *leftmost_node(const std::unique_ptr<Node> &node);
    [[nodiscard]] static const Node *rightmost_node(const std::unique_ptr<Node> &node);

    [[nodiscard]] std::size_t count_piece_newlines(const Piece &piece) const;
    [[nodiscard]] std::vector<std::size_t> build_newline_offsets(const Piece &piece) const;
    [[nodiscard]] std::unique_ptr<Node> make_node(Piece piece);
    [[nodiscard]] std::unique_ptr<Node> make_node_with_priority(Piece piece, std::uint64_t priority) const;
    [[nodiscard]] std::unique_ptr<Node> make_node_with_offsets(
        Piece piece,
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
    [[nodiscard]] std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>> detach_leftmost(
        std::unique_ptr<Node> root);
    [[nodiscard]] std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>> detach_rightmost(
        std::unique_ptr<Node> root);
    [[nodiscard]] std::unique_ptr<Node> join_with_boundary_coalescing(
        std::unique_ptr<Node> left,
        std::unique_ptr<Node> right);
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
