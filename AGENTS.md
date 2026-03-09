You are an experienced C++ developer working on Vitality, a text editor written in C++20 and Qt 6.

This project should lean toward domain-driven design. Model important concepts as explicit types, enforce invariants at the type level where practical, and keep dependencies between subsystems narrow and intentional. Prefer code that makes invalid states hard to represent and hard to pass across module boundaries.

## Language and toolchain constraints

- Target C++20, but stay conservative. Avoid features with uneven compiler support, especially modules.
- Do not lean heavily on template metaprogramming. Ordinary templates and common STL algorithms are fine.
- Avoid bleeding-edge C++20 features such as concepts, ranges/views, and coroutines unless there is a strong, concrete need.
- Prefer straightforward, readable code over cleverness.

## Architecture priorities

- Design around clear module boundaries and information hiding.
- Code to interfaces at subsystem boundaries when there is a real subsystem boundary. Do not introduce interface/factory indirection prematurely for hot-path or still-evolving internals.
- Expose stable abstractions in headers and keep implementation details private.
- Do not expose internal representations that are likely to change, especially editor buffer internals, line storage, layout data, and similar structures.
- Prefer one-way dependencies. Higher-level layers may depend on lower-level domain types and interfaces, but do not let unrelated parts of the system reach into each other.
- When two parts of the system need different representations, introduce an explicit conversion boundary instead of letting representations leak across layers.

## Module boundaries

- Keep the dependency graph simple and one-way.
- Default dependency direction:
  - `core` depends only on the standard library.
  - `file` may depend on `core` and the standard library.
  - `buffer` may depend on `core`, `file`, and the standard library.
  - `ui` may depend on `core`, `buffer`, and Qt.
  - `app` may compose everything, but should contain as little logic as possible.
- Do not introduce Qt types into `core`, `file`, or `buffer`.
- Do not let `ui` reach into buffer internals or depend on storage details.
- Prefer concrete types with narrow public headers for editor internals that are performance-sensitive.
- Use virtual interfaces selectively at system boundaries such as platform integration, persistence backends, or other seams that are genuinely useful to substitute.
- Qt widget and painting APIs are often `int`-shaped. Do not let those UI/API limits force 32-bit coordinate, offset, line-count, or file-size limits back into `core`, `buffer`, or storage-facing domain types.
- When a Qt boundary needs `int`, clamp or map explicitly at the UI edge. Keep core/storage semantics large-file-friendly even if the current widget layer still needs a narrower representation.

## Domain modeling

- Avoid passing primitive types where a domain type would communicate intent or enforce an invariant.
- Introduce small value types in a core/domain module for important concepts such as cursor positions, offsets, ranges, line indices, grapheme-aligned positions, and similar editor concepts.
- Distinguish semantically different values with different types even if they currently share the same underlying representation.
- If a function requires a stronger invariant, reflect that in the type instead of relying on comments or call-site discipline.
- Prefer dedicated conversion functions between related domain types. Do not mix multiple representations inside one function unless there is a compelling reason.
- Do not add convenience overloads that fall back to primitives if a domain type already exists. If a function conceptually takes a `CursorPos`, do not also add an `(int line, int column)` overload.

## Representation rules

- Encode units and coordinate spaces in type names when they differ in meaning.
- Be explicit about whether a value is byte-based, code-point-based, grapheme-based, pixel-based, document-line-based, or screen-line-based.
- Prefer names such as `ByteColumn`, `GraphemeColumn`, `DisplayColumn`, `DocumentLine`, `ScreenLine`, or `PixelX` over ambiguous generic names once multiple coordinate systems exist.
- Distinguish user-facing values from internal values when they have different indexing or formatting rules.
- If a temporary simplification exists, name it explicitly in the code and comments. For example: "this column is currently a UTF-8 byte offset."
- When introducing a new representation, also introduce the conversion boundary explicitly. Do not silently mix coordinate systems in one API.

## Unicode handling

- Do not write ad hoc Unicode processing logic when correctness depends on real text shaping, segmentation, normalization, bidirectional behavior, or grapheme boundaries.
- Do not scatter Unicode-sensitive logic across the buffer, UI, and utility code. Prefer a dedicated boundary for Unicode-aware behavior.
- If Unicode-correct behavior requires a library such as HarfBuzz, ICU, or another specialized dependency, do not introduce that dependency into a layer that is not supposed to depend on it without asking the user first.
- If needed, introduce a dedicated `unicode` module that abstracts the underlying library choice and exposes narrow domain-oriented operations to the rest of the system.
- Prefer layering Unicode-aware behavior on top of the buffer through explicit types and conversion functions rather than baking library-specific logic directly into buffer internals.
- Keep the rest of the codebase insulated from library-specific Unicode APIs as much as practical.
- For the actual Unicode and bidirectional-text behavior the editor implements today, see `docs/unicode-current-state.md`. Prefer that document when deciding whether a behavior is already complete versus still planned.

## Public API expectations

- Public headers should make the contract easy to understand locally.
- For important types and functions, state the invariant, unit, indexing basis, ownership, and failure behavior in the header when it is not obvious from the type alone.
- Prefer APIs that are hard to misuse over APIs that are briefly convenient.
- If an API can only be called with a valid or aligned value, prefer a stronger input type over runtime comments and defensive checks scattered across callers.

Example:

```cpp
struct CursorPos {
    int byte_offset;
};

struct GraphemeAlignedCursorPos {
    int byte_offset;
};
```

These may look similar internally, but they represent different guarantees and should not be treated as interchangeable.

## Interfaces and performance

- Use virtual interfaces as system-boundary tools, not as a default for every type.
- Avoid virtual dispatch on hot paths that are called at very high frequency.
- A typical pattern at a true subsystem boundary is: expose a small abstract interface in the header, hide the concrete implementation in the source file, and provide a factory function for the default implementation.
- For core editor data structures, prefer a concrete type with a narrow API unless there is a demonstrated need for substitution.
- Keep performance in mind, but do not sacrifice modularity for speculative micro-optimizations.

## Error handling

- Prefer not to throw exceptions.
- When an operation can fail, return an explicit result or status type instead.
- Mark such functions `[[nodiscard]]` when ignoring the result would be a mistake.
- Small result structs are preferred over exception-based control flow.

Example:

```cpp
auto [result, success] = some_func();
if (!success) {
    // handle error
}
```

If a richer error type is warranted, use one.

## General C++ practices

- Follow common modern C++ practices.
- Prefer RAII, `std::unique_ptr`, move semantics, and explicit ownership.
- Avoid raw owning pointers.
- Keep lifetimes and ownership obvious from the API.
- Favor simple data flow and local reasoning.

## Testing and validation

- Write tests for meaningful changes whenever it makes sense.
- Bug fixes should usually come with tests.
- Prefer a mix of unit tests, fuzz tests, and property-based tests where appropriate.
- Domain and buffer invariants should usually be tested below the UI layer.
- When behavior is mostly about cursor movement, clamping, indexing, or representation conversion, prefer unit tests and property-based tests before adding UI-heavy coverage.
- 100% coverage is not required, but untested behavior should be the exception, not the default.
- Run the test suite after each change when possible.
- Run `clang-tidy` after each change when possible.
- Prefer the project command that uses a no-PCH compile database plus the
  checked-in `.clang-tidy` config so diagnostics are concrete and do not fight
  compiler-specific precompiled header artifacts:

```bash
cmake -S . -B build-clang-tidy -DVITALITY_ENABLE_PCH=OFF
/opt/homebrew/opt/llvm/bin/run-clang-tidy \
  -p build-clang-tidy \
  -quiet \
  -source-filter="^$(pwd)/(src|tests|benchmarks)/" \
  -header-filter="^$(pwd)/(src|tests|benchmarks)/" \
  -exclude-header-filter="^$(pwd)/(build|build-clang-tidy)/" \
  "^$(pwd)/(src|tests|benchmarks)/"
```

- If you need a one-off focused run for a single translation unit, use:

```bash
clang-tidy -p build-clang-tidy path/to/file.cpp
```

## Refactoring expectations

- Look for refactorings that improve boundaries, naming, and type safety before starting a feature and after completing it.
- Prefer small, steady improvements over large speculative rewrites.
- If a design starts to blur invariants or couple subsystems too tightly, stop and introduce a clearer boundary.

When making decisions, optimize for these outcomes:

- domain concepts are explicit in the code
- invariants are enforced by types or narrow APIs
- modules can evolve without exposing their internals
- dependencies stay intentional and limited
- different representations are difficult to confuse
- public APIs are difficult to misuse
- the code remains practical, readable, and efficient
