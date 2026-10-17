# Code style

How Emerick is written. A change that does not follow this is not done.

We borrow on purpose from projects we admire:

- **mux's [CODE_STYLE.md](https://github.com/j4niwzis/mux/blob/main/CODE_STYLE.md)**: types at the boundary, no branching on strings, variants handled by visiting.
- **[LLVM](https://llvm.org/docs/CodingStandards.html)**: small libraries with strict layering.
- **[SerenityOS](https://github.com/SerenityOS/serenity/blob/master/Documentation/CodingStyle.md)**: one readable style everywhere.
- **[Chromium base](https://chromium.googlesource.com/chromium/src/+/main/docs/threading_and_tasks.md)**: threads and ownership stated plainly.
- **[Ardour](https://github.com/Ardour/ardour) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine)**: realtime discipline.
- **The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)**: ownership and resource rules.

## Language and build

- C++23 with headers. We move to modules once Apple Clang, GCC, MSVC and Qt's moc all handle them; until then every library keeps its public headers in `include/emerick/<library>/` and its sources beside them.
- One CMake target per layer (`docs/architecture.md`), each with `include/`, sources and `tests/`. A layer never includes a layer below it in that table.
- Warnings are errors, and `make quality` is the only gate. Where a compiler has a bug, work around it in the code and say why in one sentence.

## Types at the boundary

- Everything that comes in as text is read once, where it comes in, into structs, enums and variants: MCP params, the project file, a MIDI message, a sysex, a settings value. Code past that point works on the types and never digs through JSON or bytes again.
- Data we do not understand stays opaque. A plugin track's `data` is JSON to the model and is only parsed by the plugin that owns it, at its own boundary.
- One type becomes another through a plain function. Never go typed to JSON to typed.

## No branching on strings

- Text that decides what code does (an action id, a track kind, a section name, a chord type) goes through a lookup table into a type or a handler. No `name == "..."` chains past the boundary. The registry is that table for action and track kind ids.

## Variants and dispatch

- Handle a `std::variant` with `std::visit` and an overload set, so adding an alternative breaks every place that must handle it. No `holds_alternative` or `get_if` chains.

## Ownership and calls

- Values first, `std::unique_ptr` for ownership, references for borrowing. `std::shared_ptr` only where lifetime is really shared, with a comment naming the owners.
- `std::function` is fine for registry handlers and other UI-thread glue. It never appears on the audio thread.
- A lower layer tells a higher one things as data the higher layer reads (a take it polls, a change it receives), not by calling back into it.
- No macros where the language can do it. A `#define` only where a library requires one.

## Realtime

- The audio callback and everything it calls never allocate, lock, log, throw, or wait. Data crosses threads through the SPSC queues and atomic snapshot swaps in `src/engine`.
- Each thread's job is written at the top of the file that starts it.
- A realtime function says so in its name or its file (`processBlock`, `rt_*.cpp`).

## Errors

- Parsing returns `std::optional` or `std::expected`. A failure the user or agent caused throws `app::AppError` with a message that says what to do; the action boundary turns it into an MCP or UI error.
- Catch only the exceptions that can happen. Never `catch (...)`.

## Declarative code

- Build or scan a sequence with ranges and algorithms (`std::views::transform`, `filter`, `std::ranges::to`, `find_if`) rather than a loop pushing into an output.
- Prefer `constexpr` tables to code that builds the same table at runtime: chord types and the CC map are `constexpr` arrays.

## Bytes and casts

- No `reinterpret_cast` and no C-style casts. Bytes stay `std::uint8_t` from the moment they arrive. Where a C API (CoreAudio, CoreMIDI) demands another pointer type, one small typed wrapper does the narrowest cast and says why.

## QML

- QML shows state and calls registry actions. Logic beyond binding and layout lives in C++.
- Colors, sizes and fonts come only from `Theme`.
- Every interactive control sets `objectName` to its registry id, and per-item controls append the item's id (`track.setMute:<trackId>`).
- Reuse an existing component (button, row, header) rather than copying it; make the existing one take what differs as a property.

## Names and files

- Types `PascalCase`, functions and variables `camelCase`, constants `kPascalCase`, namespaces and files `snake_case`. Names say what a thing is in the user's words: `Take`, `Clip`, `Track`, not `Item` or `Node`.
- One concept per file. A file over 400 lines is asking to be split.

## Tests

- Test behavior through the public API, with test names written as sentences (`"a take never overwrites an earlier take"`).
- Engine tests run on `OfflineBackend`, never real hardware. A bug fix comes with the test that would have caught it.

## Comments, docs and commits

- Default to no comment. Keep one only for a non-obvious why, as one plain sentence in first person plural.
- Docs describe what exists now, in plain declarative sentences, with no em dashes.
- Commits are one line in conventional form (`fix: keep takes when overlap would replace them`).
