# Text Storage Invariants

This document explains the current `TextStorage` and
`ImplicitTreapStorageCore` implementation in the most didactic way I can manage.
The goal is not merely to restate what the code does, but to explain why the
structure is shaped the way it is, what invariants it relies on, what each
optimization is trying to buy us, and what kinds of mistakes would actually
break the design.

The intended reader already knows, at least loosely, what a piece table is and
what a treap is. What usually remains confusing is the interaction between those
two ideas, especially once optimizations get layered on top. This document is an
attempt to make that interaction explicit enough that you can reason about the
code without having to rediscover the design from scratch each time.

## 1. What `TextStorage` is supposed to be

The public API is in [TextStorage.h](/Users/wkh/code/vitality/src/buffer/TextStorage.h).
At the public boundary, `TextStorage` is a storage and query abstraction. It is
not the full editor model, and it is not the UI. It is responsible for storing
bytes exactly, answering questions about those bytes in a line-oriented way when
the editor needs that view, and supporting byte-based insert and erase
operations. It also provides cursor clamping, but only in the narrow sense of
"given a line index and byte column, return a valid in-bounds position."

That means `TextStorage` is deliberately not responsible for higher-level cursor
policy, visual movement, Unicode grapheme correctness, Qt integration, or
editor modes. It knows how the stored text is shaped as bytes and logical lines.
It does not know how the user should move through that text in a fully
Unicode-correct editor. It also does not leak its internal representation. The
caller can ask for the text of a line, or the full text, or insert and erase
byte ranges, but it cannot inspect pieces, nodes, or tree structure directly.

This is an important architectural choice. The internal storage can evolve from
the current treap-backed piece table to something else later, but the storage
boundary can stay stable if we keep the public contract focused on storage facts
rather than internal mechanics.

## 2. What the internal data structure actually is

The implementation core lives in
[ImplicitTreapStorageCore.h](/Users/wkh/code/vitality/src/buffer/internal/ImplicitTreapStorageCore.h)
and
[ImplicitTreapStorageCore.cpp](/Users/wkh/code/vitality/src/buffer/internal/ImplicitTreapStorageCore.cpp).
Conceptually, it is a piece table represented inside an implicit treap.

The piece-table aspect means that the actual bytes live in backing buffers. The
original file bytes live in `storage_.original`. Newly inserted bytes are
appended to `storage_.add`. The document itself is not stored as one contiguous
string that gets rewritten after every edit. Instead, the document is described
by a sequence of `Piece` descriptors. Each `Piece` says, in effect, "take this
half-open range of bytes from `original`" or "take this half-open range of
bytes from `add`."

The treap aspect means those pieces are stored in a tree whose in-order
traversal is the logical document order. The tree also satisfies the ordinary
heap property on `priority`. Each node additionally stores cached aggregate
metadata for its subtree, especially subtree byte counts and subtree newline
counts. Those aggregates are what let the code answer offset-based and
line-based queries without flattening the whole document on every operation.

One subtle but crucial point is that the tree is not ordered by the `start`
offset inside `original` or `add`. A piece with `start = 1000` in `add` does not
have to come "after" a piece with `start = 10` in `add`. The tree is ordered by
document position. The `start` field tells you where the bytes live in a backing
buffer, not where they appear in the current document.

## 3. The representation invariants

There are several invariants that must hold if the structure is to remain
correct.

The first class of invariants is about individual pieces. Every piece must
identify a valid half-open byte range inside exactly one backing store. If the
piece says `buffer == Original`, then `piece.start` and `piece.length` must
describe a valid range inside `storage_.original`. If the piece says
`buffer == Add`, then those same fields must describe a valid range inside
`storage_.add`. In the tree itself, zero-length pieces should not remain. A
piece with `length == 0` is useful as a transient idea while calculating a
split, but not as a stable element of the representation.

The second class of invariants is about tree order. In-order traversal of the
treap must yield the document from left to right. This is the deepest semantic
meaning of the tree. When the code talks about the "left subtree" of a node, it
means "all document bytes before this piece." When it talks about the "right
subtree," it means "all document bytes after this piece." This is why helpers
such as `split_by_offset(...)`, `leftmost_node(...)`, and `rightmost_node(...)`
are meaningful in document-order terms. They are not just generic BST helpers.
They are ways of reasoning about the current document boundaries induced by the
tree.

The third class of invariants is the ordinary treap heap invariant. A node's
priority must be greater than or equal to the priorities of its children. The
implementation relies on that in `merge(...)`. When two trees are merged, the
root with the higher priority remains the root, and the recursive merge happens
down one side.

The fourth class of invariants is about the cached aggregates. For every node,
`subtree_bytes` must equal the byte count of the left subtree plus the current
piece length plus the byte count of the right subtree. Likewise,
`subtree_newlines` must equal the newline count of the left subtree plus the
newline count of the current piece plus the newline count of the right subtree.
This is maintained by `pull(...)`. Any code that mutates tree structure or
mutates a node's piece length must either call `pull(...)` on the affected nodes
or rebuild through helpers such as `merge(...)` and `split_by_offset(...)` that
already call `pull(...)` as they stitch the tree back together.

This is the point where an important complexity question naturally appears. If
an insertion changes the document, why do we not have to touch every later node
in the tree and update some shifted offset or size field all the way across a
huge suffix of the document? The answer is that the tree does not cache global
document positions in its nodes. It caches only local subtree facts. A node
stores the size and newline count of its own subtree, not the absolute document
offset where that subtree happens to begin today. Because of that choice, a
mutation does not make an O(n) suffix of absolute-position annotations stale.
It makes stale only the nodes whose local child pointers or local piece
descriptors changed.

That is why `pull(...)` can stay cheap. The helper itself is constant time. It
looks at one node, its two children, and the node's own piece data, then
recomputes the cached aggregates for that one subtree root. The real algorithmic
question is therefore not whether `pull(...)` is cheap, but how many nodes need
to be pulled after a mutation. In this implementation the answer is "only the
nodes on the split and merge paths that were structurally rebuilt." A
`split_by_offset(...)` walks from the root down to one boundary and then calls
`pull(...)` while unwinding on just that path. A `merge(...)` walks one treap
spine and again calls `pull(...)` only on nodes whose child pointers changed
during the rejoin. So the number of stale aggregate caches is proportional to
the number of nodes on those rebuilt paths, not to the number of bytes or
pieces after the edit.

In an expected-balanced treap, those paths have expected length O(log n), where
`n` is the number of nodes in the tree. That is the real reason cached
aggregates stay maintainable in expected O(log n) time per mutation. We are not
updating every node "after the insertion point." We are recomputing only the
small set of ancestor nodes whose subtrees were actually changed by the
split-and-rejoin process. If the implementation had instead stored absolute
document offsets in every node, then inserts near the front really would force
broad suffix updates and the whole design would be much worse.

The fifth class of invariants is about newline caching inside a piece. Each node
stores `newline_offsets` and `piece_newlines`. The contract is that
`newline_offsets` contains the positions of `'\n'` bytes relative to the start
of that piece, `piece_newlines` equals `newline_offsets.size()`, and every
offset in that vector is strictly less than `piece.length`. This cache is what
makes line-count and line-start queries efficient enough to use as an editor
primitive rather than as a slow derived query.

If you want the shortest possible version of the correctness story, it is this:
each node must describe a valid piece, the tree must still mean the same
document in-order, and the cached byte and newline counts must agree with that
document.

## 4. Why the public line semantics work

The editor currently wants simple line semantics. An empty document still has
one logical line. Newline bytes terminate lines. A trailing newline does not
create an extra visible line after the end in the way that some naive
"newline-count plus one" formulas would suggest.

The implementation handles this by counting newline bytes in the tree and then
using `ends_with_newline()` to decide how many logical lines that implies. If
the document is empty, the result is one line. If the document is non-empty and
ends with `'\n'`, then the total line count is exactly the number of newline
bytes. If the document does not end with `'\n'`, then the total line count is
the number of newline bytes plus one. `line_start_offset(line_index)` is then
implemented by finding the byte immediately after the `line_index`-th newline.

This is not the only possible line model, but it is the one the current editor
code expects, and the storage layer implements it consistently.

## 5. How substring and line queries work without storing absolute offsets

One of the most important design choices in this implementation is that the tree
stores relative subtree sizes, not absolute document offsets. This matters most
in `collect_range(...)`, which answers absolute substring queries.

The helper carries a parameter called `base`. The best way to think about it is
as "the absolute document offset where this subtree begins." At a given node,
the left subtree occupies `[base, node_begin)`, the current piece occupies
`[node_begin, node_end)`, and the right subtree begins at `node_end`. The code
computes those boundaries by taking `base`, adding the byte size of the left
subtree, and then adding the current piece length.

This means the structure does not need to permanently annotate every node with
an absolute document offset. That would be disastrous, because inserts and
deletes would force broad rewrites of those annotations across many unaffected
nodes. By storing stable relative aggregates and deriving temporary absolute
positions only during a traversal, the code gets the information it needs
without turning every mutation into a global coordinate maintenance problem.

That is one of the quiet but important architectural strengths of the current
design. The storage layer stores only data that stays local under mutation, and
it computes global positions on demand.

## 6. How generic insertion works

The generic insert path has a very standard piece-table shape. The new bytes are
appended to `storage_.add`. A single new `Piece` is created that refers to the
newly appended span. The tree is then split at the insertion offset into a left
tree containing everything before the insertion point and a right tree
containing everything after it. The new piece node is joined between those two
trees. While the trees are being rejoined, boundary coalescing is applied so
that obvious adjacent compatible pieces do not remain needlessly split.

The key property here is that the inserted bytes are not copied again once they
enter `storage_.add`. Only piece descriptors and tree structure move around.

## 7. How generic erasure works

The generic erase path is the corresponding delete shape. The tree is first
split at the erase start. The remainder is then split again to isolate the
erased range. That middle subtree is discarded, and the left and right survivor
trees are joined back together with boundary coalescing. This is exactly what a
piece-table deletion should look like: no backing-buffer bytes are physically
removed, but the logical document stops referencing the erased range.

## 8. Why `split_by_offset(...)` is the central structural helper

If I had to pick the single most important helper in the current code, it would
be `split_by_offset(...)`. Given a tree and a document byte offset, it returns a
pair of trees. The left tree represents every document byte strictly before that
boundary. The right tree represents every document byte from that boundary
onward.

Understanding its branch structure is worth the effort. There are five logical
cases. The split point can lie in the left subtree, in the right subtree,
exactly before the current piece, exactly after the current piece, or inside the
current piece. The first four are conceptually straightforward. The last one is
the interesting one. When the split falls inside the current piece, the code
does not copy bytes. Instead, it turns one piece descriptor into two piece
descriptors that still point into the same backing buffer. The original node
priority is preserved for both resulting nodes so that the local rewrite remains
compatible with the treap ordering above it.

This helper is why so much of the code is written in split-and-rejoin terms.
Once you trust `split_by_offset(...)`, many other operations become local
transformations at known document boundaries.

## 9. Why coalescing exists at all

The representation would become noisy and inefficient if it left obviously
mergeable pieces split forever. Two pieces are mergeable only when they are both
non-empty, they refer to the same backing store, and the byte range of the left
piece ends exactly where the byte range of the right piece begins.

That last condition matters more than it first appears. Two add-backed pieces
are not mergeable merely because they both came from the add buffer. They are
mergeable only if they refer to adjacent spans of the add buffer. Otherwise
merging them would silently invent bytes that do not belong to the document.

So coalescing is not cosmetic. It is a precise statement that two adjacent
document pieces are also adjacent spans in the same backing store, and therefore
can be represented by one larger descriptor without changing meaning.

It is worth being extremely explicit about why those conditions are both
necessary and sufficient. They are necessary because if any one of them fails,
merging would change the represented document. If one piece is empty, then the
representation is already malformed and the right fix is to eliminate the empty
piece rather than to rely on coalescing semantics. If the two pieces come from
different backing stores, then no single `Piece` descriptor can represent both
ranges without also changing the meaning of `buffer`. If they come from the
same backing store but the left range does not end exactly where the right range
begins, then a merged descriptor would cover bytes that were not previously in
the document. The conditions are sufficient because once they all hold, the two
pieces denote exactly one contiguous byte range in one backing store, and a
single larger descriptor denotes exactly the same document bytes in exactly the
same order. There is no additional hidden condition. Treap shape, node
priority, and subtree aggregates matter for how we perform the rewrite, but not
for whether the rewrite is semantically valid.

## 10. Why `join_with_boundary_coalescing(...)` exists

Without `join_with_boundary_coalescing(...)`, a common mutation would be
structured in two phases. First the code would split and locally mutate the
tree. Then it would merge the result back together. After that it would need a
second cleanup pass to see whether the newly adjacent boundary pieces could now
collapse into one. That would be correct, but wasteful.

The current helper folds those phases together. It relies on one crucial
observation: when two already-valid trees are joined, only one new merge
opportunity can appear, namely the boundary between the last piece of the left
tree and the first piece of the right tree. Interior boundaries inside either
tree were already valid before the join and do not need to be re-examined.

That observation is not merely plausible; it is the complete reason the helper
is correct. Before the join, every interior adjacency inside the left tree was
already an adjacency inside one valid document fragment, so if any of those
pieces were mergeable they should already have been merged earlier. The same is
true for the right tree. Joining the trees does not change any of those
interior adjacencies. It creates exactly one new adjacency: the one between the
last piece of the left fragment and the first piece of the right fragment.
Therefore checking that one boundary is not a heuristic. It is both necessary
and sufficient. It is necessary because any newly created merge opportunity must
be there. It is sufficient because there is nowhere else a new merge
opportunity could have appeared.

That is why the helper first does cheap read-only inspection with
`rightmost_node(left)` and `leftmost_node(right)`. If those two boundary pieces
cannot merge, plain `merge(...)` is sufficient. If they can merge, the helper
detaches exactly those two nodes, combines them into one piece, and then merges
the remaining trees back around that new node. The function exists to preserve
the representation invariant that obviously mergeable neighbors do not remain
split, while keeping the work local to the join boundary.

## 11. What the recent-typing optimization is trying to achieve

The typing optimization is narrower than people often imagine. It is not trying
to make insertion "free" or to eliminate tree maintenance entirely. What it is
trying to avoid is the worst representational churn of the naive piece-table
approach to typing one byte at a time.

Suppose the user is typing into the middle of an existing document. After one
insert, the structure might look like `[original left] [add "a"] [original
right]`. If the user then types `b` and `c` immediately after that, the desired
shape is `[original left] [add "abc"] [original right]`, not `[original left]
[add "a"] [add "b"] [add "c"] [original right]`.

So the optimization is trying to keep one recent add-backed piece growing
through a contiguous typing run. That reduces allocation, node churn, and later
coalescing work. It does not remove the need to preserve tree aggregates or
structural correctness.

It is also important to say what this optimization is not. It is not a
replacement for coalescing. It does not establish the general invariant that
adjacent mergeable pieces should collapse. It only recognizes one specific
editing pattern: the user is still extending the same recent add-backed piece,
or immediately deleting bytes from the suffix of that same recent run. In other
words, the fast path is about avoiding needless creation of new tiny add-backed
pieces during one contiguous typing session. Coalescing is the more general
representation cleanup rule that applies after any mutation shape that might
bring two compatible pieces together.

The cleanest way to understand the relationship is this. The recent-typing
optimization operates before the representation has degenerated. It tries to
avoid creating extra pieces in the first place. Coalescing operates after a
structural rewrite has already occurred. It ensures that if two compatible
pieces do end up adjacent, the representation is normalized back to one larger
piece. The two mechanisms therefore solve different problems. One reduces
future cleanup work by preserving a good shape during a typing run. The other
guarantees that all mutation paths, including generic insert, generic erase, and
optimized paths that rejoin trees, end in a representation where adjacent
mergeable pieces do not remain unnecessarily split.

This is why coalescing still matters even when the fast insert path exists. The
fast path can only fire when its tracker remains valid and the edit is exactly
contiguous with the recent run. The moment the tracker becomes stale, the user
edits somewhere else, a generic erase rejoins survivors, or a split/rejoin
creates a new outer boundary, the recent-typing optimization is no longer the
right tool. Coalescing is still needed to normalize those boundaries. Even
inside the fast path, coalescing remains relevant during the final rejoin,
because the locally extended or shrunk piece may now sit next to a compatible
neighbor at the tree boundary.

So the correct exam answer is not that one optimization makes the other
obsolete. The correct answer is that the recent-typing fast path is a narrow
special-case optimization for one very common editing pattern, while coalescing
is the general representational invariant restoration step that all mutation
paths still rely on.

## 12. What `RecentContiguousEdit` actually stores

`RecentContiguousEdit` is not a cached `Node*`. It is a logical description of
one recent add-backed piece. It stores where that piece begins in document
coordinates, where it begins in the add buffer, how long the whole piece is, and
how much of its suffix belongs to the most recent contiguous typing run.

The reason it does not store a raw node pointer is that node identity is not
stable under the current algorithms. `split_by_offset(...)` can replace one node
with two new nodes. Boundary coalescing can replace two nodes with one.
Compaction can rebuild the tree entirely. A cached pointer would therefore be
fragile even if it were carefully invalidated. The current tracker instead
stores logical identity and re-discovers the current node from tree structure
when it needs to apply an optimization.

This is a conservative choice. The tracker is intentionally allowed to become
invalid often. The rule is that the optimization must never guess. If the code
cannot prove that the tracker still describes the exact current piece, it must
fall back.

## 13. Why the fast insert still splits and detaches

This is probably the least intuitive part of the current implementation.

The natural question is: if the user typed one more byte immediately after the
recent typing run, why not simply append that byte to `storage_.add` and
increment the current piece length?

The first answer is that the tracker does not hold a live node pointer. It
holds logical identity only, so the implementation still has to rediscover the
current node from the tree. The second answer is that mutating an interior node
in place would make all ancestor aggregates stale unless the code also had a
way to walk upward and repair them. The current implementation intentionally
does not use parent pointers or upward path caches. Instead it reuses the same
split and merge machinery that already knows how to rebuild aggregates while
stitching the tree back together.

There is also a precise structural reason the split is useful. The fast insert
requires that the insertion point be exactly at the end of the tracked piece.
After calling `split_by_offset(root_, byte_offset)`, everything up to that
boundary is in `left_tree` and everything after it is in `right_tree`. If the
tracker is still correct, then the tracked piece must now be the rightmost node
of `left_tree`, because it is the piece whose end coincides with the insertion
point. That gives the implementation one exact boundary node to detach, verify,
and extend.

The applicability criteria for this optimization are also worth stating as a
proof rather than as a slogan. The optimization is applicable exactly when all
of the following are true: first, the tracker is active; second, the requested
insert offset is equal to the document end of the tracked piece; third, the
tracked piece is add-backed; fourth, the tracked add-backed span still reaches
the current end of `storage_.add`; and fifth, after splitting at the insert
offset, the rightmost node of `left_tree` exactly matches the tracked piece's
buffer, start, and length. Each of those conditions is necessary. If the
tracker is inactive, there is nothing to trust. If the insert offset is not at
the piece end, then the new byte is not extending the current typing run. If
the piece is not add-backed, then extending it would incorrectly rewrite the
meaning of the original buffer. If the piece does not reach the end of
`storage_.add`, then simply appending bytes to `storage_.add` would not make
the piece describe one contiguous add-buffer range. If the boundary node found
after the split does not exactly match the tracker, then the tracker has become
stale or ambiguous.

Those same conditions are sufficient as well. Once they all hold, the semantic
claim of the optimization is completely established: the user is inserting at
the end of exactly one known add-backed piece whose underlying add-buffer span
is currently open-ended at the tail of `storage_.add`. Under those conditions,
appending bytes to `storage_.add` and extending that exact piece by the same
number of bytes preserves the document text, preserves piece meaning, and keeps
the representation equivalent to the generic insert path, just with less node
churn.

So the fast path is not "blind direct mutation." It is "reconfirm the exact
boundary node, then perform a very local rewrite using the normal structural
machinery."

## 14. Why the fast erase is even narrower

`try_erase_recent_typed_suffix(...)` is intentionally stricter than the fast
insert. It handles only immediate backspace over the suffix of the current typed
run. That is, it is for the common case where the user has just typed some bytes
and is now deleting the most recently typed bytes in reverse order.

This is not an arbitrary delete optimization. It does not try to optimize
erasing from the middle of the tracked piece, or erasing earlier bytes that
happen to lie in the same add-backed piece. It only proceeds if the erase range
ends exactly at the end of the tracked piece and lies fully inside
`typed_suffix_length`. That narrower contract is what makes the optimization
safe and local.

Again, the criteria are meant to be necessary and sufficient, not merely
convenient. The optimization is applicable exactly when the tracker is active,
the erase length is positive, the erase range ends at the document end of the
tracked piece, the entire erase range lies inside the currently tracked typed
suffix, and the detached boundary node after splitting still exactly matches the
tracked add-backed piece. These conditions are necessary because deleting
anywhere else would mean we are no longer simply shrinking the most recently
typed suffix. A delete from the middle of the piece would require splitting the
piece semantically, not merely shortening it. A delete that extends outside the
typed suffix would be altering bytes that belong to an older part of the same
piece, which the tracker is not allowed to treat as "recently typed" for this
optimization. A boundary-node mismatch means the tracker no longer proves which
node is safe to shrink. The conditions are sufficient because if they all hold,
then shrinking that one piece by the erase length, trimming any trailing
newline-offset entries that now lie beyond the new end, and rejoining the tree
produces exactly the same document that the generic erase path would produce.

## 15. Why the tracker is intentionally fragile

The recent-edit tracker is cleared aggressively. It is cleared when a generic
mutation path is taken, when compaction runs, and whenever the fast-path
validation does not match the actual boundary node exactly. This is not a
weakness of the design. It is the design. The optimization is allowed to miss
opportunities. It is not allowed to be wrong.

That distinction is worth keeping in mind whenever you read the recent-edit
paths. The code is not trying to maximize the number of times the fast path
fires at any cost. It is trying to make the fast path trustworthy enough that it
can be used without fear of silently corrupting the document.

## 16. What compaction is and is not

The maintenance compaction pass is a storage-level cleanup mechanism. Its job is
to collect piece descriptors, merge some adjacent compatible pieces within a
budget, and rebuild the tree if that is worthwhile. It exists to reduce
fragmentation over longer edit sessions.

The current implementation does not run compaction after every edit. It runs it
through a very specific maintenance policy in `TextStorage`. After every
successful mutation that actually changes storage, the implementation increments
an `edits_since_maintenance` counter. When that counter reaches `512`, it resets
the counter and asks whether the tree currently contains at least `2048`
pieces. If the answer is no, maintenance stops there. If the answer is yes, the
storage core runs bounded compaction with a merge budget of `128`.

Those constants are not sacred, but they do express a clear design intent. The
edit interval of `512` means maintenance is periodic rather than being attached
to the latency of each individual keystroke. The piece threshold of `2048`
means we only pay the compaction cost once fragmentation has become large enough
to matter. The merge budget of `128` means one maintenance pass is allowed to
repair some fragmentation, but not to consume unbounded time trying to fully
normalize a huge tree in one shot. A reasonable budget in this design is not a
budget that finishes all possible cleanup immediately. A reasonable budget is a
budget that makes steady progress on fragmentation while keeping a single
maintenance pass small relative to interactive editor work. That is what `128`
is doing here.

The compaction search is simpler than it may sound. First, the tree is walked in
document order and the current piece sequence is copied into a flat vector. This
produces the exact left-to-right piece order of the current document. Then the
algorithm performs one left-to-right scan over that vector. It builds a second
vector called `compacted`. For each input piece, it checks whether that piece
can coalesce with the last piece already stored in `compacted`. If the answer is
yes and the merge count is still below the budget, the last output piece is
replaced by the merged piece and the merge counter is incremented. Otherwise the
input piece is appended as a separate output piece.

This means the compaction search is greedy, local, and bounded. It does not
perform backtracking. It does not try to choose a globally optimal subset of
merge boundaries. It does not need to. Coalescing is semantically determined by
adjacency and backing-buffer contiguity. If a long run of pieces is pairwise
mergeable in order, then repeatedly merging from left to right yields the same
final piece descriptor that any other legal merge order would yield. The only
effect of the budget is that the pass may stop partway through such cleanup and
leave the rest for a later maintenance cycle.

Once the scan is done, the tree is rebuilt from the compacted piece sequence
only if at least one merge actually occurred. If the scan finds nothing to
merge, rebuilding would be pure churn and is skipped. When rebuilding does
happen, the recent contiguous edit tracker is intentionally invalidated, because
the rebuild may replace every node in the tree even though the document bytes
remain unchanged.

What it is not is just as important. It is not editor caching, cursor caching,
Unicode policy, or line caching. Those concerns belong at other layers. The fact
that compaction remains a storage-local transformation is one reason it fits the
current architecture. It improves the storage representation without importing
higher-level policy into the core data structure.

## 17. What must be true after every successful mutation

If I had to answer an exam question about the mutation correctness contract, I
would state it like this.

After any successful `insert(...)` or `erase(...)`, the logical document bytes
produced by `text()` must be exactly the document the mutation was supposed to
produce. Every surviving piece must still refer to a valid span of `original` or
`add`. In-order traversal must still correspond to document order. Treap heap
ordering by priority must still hold. Every node's cached subtree byte count and
subtree newline count must agree with the actual contents of that subtree. Every
node's newline-offset cache must agree with the actual newline bytes of that
piece. Adjacent mergeable pieces should not remain split unnecessarily. Finally,
the recent contiguous edit tracker must either still describe the exact recent
add-backed piece that survived the mutation, or it must be cleared.

That is the real correctness core. Everything else in the implementation is in
service of preserving those facts cheaply enough to be usable in an editor.

## 18. What each optimization is allowed to assume

It is helpful to phrase the optimization contracts in full sentences instead of
in shorthand.

`join_with_boundary_coalescing(...)` is allowed to assume that the two input
trees were already internally valid before the join. From that premise it may
conclude that the only newly relevant adjacency is the outer boundary between
them. It is not permitted to assume anything weaker, such as "coalescing is
probably only needed at the boundary," because the whole point is that the
boundary-only check is a proof. If the inputs are valid trees, boundary-only is
both the necessary place to look and the sufficient place to look.

`try_extend_recent_contiguous_insert(...)` is allowed to assume only what the
tracker itself records: that there was a recent add-backed piece ending at a
particular document offset, corresponding to a particular add-buffer range, and
that the current insert is intended to extend that run. It is not allowed to
assume that the tracked node still exists, or that the tracked logical piece is
still represented by the same node, or even that the tracker still refers to a
piece at the same boundary. That is why it must re-prove those facts by
splitting and inspecting the boundary node.

`try_erase_recent_typed_suffix(...)` is allowed to assume only that the caller
is attempting to remove bytes from the most recently typed suffix of that same
logical piece. It is not allowed to assume that any delete touching the tracked
piece can be treated as a suffix shrink. It must prove that the erase range is
exactly suffix-shaped with respect to the tracked run, and it must also
re-prove that the boundary node still represents the tracked piece before it
shrinks anything.

The general pattern is that every optimization is narrow in both what it claims
and what it is allowed to trust.

## 19. Architectural reading of the current code

The implementation is doing several things correctly from an architectural point
of view. The storage layer remains independent of Qt. The public `TextStorage`
API does not leak piece-table details. The optimizations stay local and fall
back to generic paths when their assumptions stop being provable. Byte-based
behavior is explicit. Unicode policy has not been smuggled into storage
internals.

The main cost is that the optimization paths are less locally intuitive than a
parent-pointer design would be. They rely on split, detach, and rejoin instead
of direct upward repair from an interior node. That makes the code more
algorithmic and less pointer-oriented. The benefit is that ownership remains
simple through `std::unique_ptr`, parent-pointer bookkeeping is avoided, and the
same structural repair mechanisms are reused across both generic and optimized
paths.

Whether that tradeoff is always the best one is a fair question, but it is the
tradeoff the current implementation is making.

## 20. How much understanding is enough to work safely here

You do not need to memorize every branch of every helper to work safely in this
part of the codebase. But you do need to understand a few things deeply enough
that you can detect when a proposed change is violating the design.

You should understand what a `Piece` means, what the subtree aggregates mean,
what `split_by_offset(...)` guarantees, what conditions make two pieces
mergeable, why the recent-edit tracker stores logical identity rather than node
identity, and when an optimization is required to fall back instead of guessing.

If you can reliably answer the following questions, you are probably close
enough to the code to make good decisions.

- What exact invariant would this change risk breaking?
- If this fast path fails validation, what is the safe fallback?
- Am I changing storage behavior, editor behavior, or UI behavior?

That is the level of understanding I would aim for. You do not need every line
cached in your head. You do need to be able to see which facts the code is
trying to preserve, and which shortcuts are safe only because those facts are
checked aggressively.
