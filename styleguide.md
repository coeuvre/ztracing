# ztracing: C23 Style Guide & Coding Standards

This document defines the coding standards, style guidelines, and mandates for the C23 rewrite of the `ztracing` project. All C code in this repository must strictly adhere to these rules.

---

## 1. Language & Compiler Mandates

*   **Standard**: Pure **C23** is required for all application logic. Compiles with `-std=c23`.
*   **Null Pointers**: Always use the native C23 keyword `nullptr` for null pointer constants. The old C-style macro `NULL` and raw `0` are strictly prohibited for pointer values.
*   **Constants**: Prefer the native C23 `constexpr` keyword over preprocessor `#define` macros for declaring typed constants. This provides compile-time type safety and scope control.
    ```c
    // Correct (C23 Style)
    constexpr size_t NUM_WORKERS = 2;

    // Incorrect (Obsolete preprocessor macro)
    #define NUM_WORKERS 2
    ```
*   **Warnings**: Strict warnings are enabled for all local code (`-Wall -Wextra -Werror` etc.).
*   **Include Guards**: All internal headers must use include guards matching the pattern `ZTRACING_SRC_<FILE>_H_` (not `#pragma once`).
    ```c
    #ifndef ZTRACING_SRC_STRING_H_
    #define ZTRACING_SRC_STRING_H_
    // ...
    #endif // ZTRACING_SRC_STRING_H_
    ```

---

## 2. Naming Conventions

*   **Type Naming**: All struct and enum types must use the `_t` suffix and be declared via typedef.
    ```c
    typedef struct array_list {
      void* data;
      size_t size;
      // ...
    } array_list_t;

    typedef enum trace_parser_state {
      TRACE_PARSER_STATE_INITIAL,
      // ...
    } trace_parser_state_t;
    ```
*   **Functions**: Must use `snake_case` (e.g., `array_list_push_back`).
*   **Constants**: Must use `SCREAMING_CASE` (e.g., `VERTICAL_MINIMAP_WIDTH`).

---

## 3. Structured Programming (Single Entry Single Exit)

*   **Rule**: Every function must strictly follow the **Single Entry Single Exit** (SESE) rule. 
*   **Enforcement**: There must be **exactly one** `return` statement in a function, located at the very end of the function. Early returns (e.g., returning from inside an `if` block) are strictly prohibited.
*   **Rationale**: Simplifies flow analysis, makes debugging straightforward, and ensures reliable resource cleanup (e.g., freeing memory or unlocking mutexes) without duplicating code.
*   **Mitigating Deep Nesting**: A common challenge with SESE is that avoiding early returns can lead to deep nesting of `if` blocks. If you find your code exceeding **3-4 levels of nesting**, you must **consider extracting the nested blocks into smaller, well-named helper functions**. This keeps both the caller and callee functions flat, highly readable, and perfectly SESE-compliant.

*   *Example*:
    ```c
    // Correct (Single Entry Single Exit)
    inline bool string_eq(string_t a, string_t b) {
      bool result = false;
      if (a.len == b.len) {
        result = memcmp(a.ptr, b.ptr, a.len) == 0;
      }
      return result;
    }
    ```

---

## 4. Zero-Is-Initialization (ZII) & Initialization

*   **Pattern**: Prefer Zero-Is-Initialization (ZII). A zeroed-out struct must represent a valid, default-initialized state.
*   **Empty Initializer**: Use the C23 empty initializer `{}` to zero-initialize any struct, array, or union. Do **not** use the obsolete C11-style `{0}`:
    ```c
    // Correct (C23 Style)
    array_list_t al = {};
    int arr[10] = {};

    // Incorrect (Obsolete C11 Style)
    array_list_t al = {0};
    int arr[10] = {0};
    ```
*   **No `memset`**: Never use `memset(ptr, 0, sizeof(T))` for struct initialization. Use `{}` instead.
*   **Designated Initializers**: For "ZII + setting" operations, use designated initializers. Redundant `= 0` or `= nullptr` fields must be omitted to keep the code concise:
    ```c
    // Correct
    array_list_t al = { .element_size = sizeof(int) };
    ```
*   **Declaration Order**: Fields in a designated initializer must be listed in their exact declaration order to comply with standard requirements.

---

## 5. Generic Containers (Non-Macro)

*   **Design**: Containers like `array_list_t` and `hash_table_t` are implemented as type-generic C structures using `void*` data and storing element/key/value sizes in bytes at runtime. We **do not** use macro-based code-generation templates.
*   **ArrayList**: Stores `element_size` in the struct. Operations copy items via `memcpy`.
*   **HashTable**: Stores `key_size` and `value_size` along with function pointers for hashing/equality and a `context` pointer to support stateful operations (like `TraceData*` context).
*   **Type-Safe Access**: To access elements with zero runtime overhead and without macros, cast the `void* data` to a typed pointer once at the usage site, then use standard C array indexing:
    ```c
    // Cast once
    TraceEvent* events = (TraceEvent*)td->events.data;
    // Use normal array indexing
    TraceEvent event = events[index];
    ```

---

## 6. String Handling (`string_t`)

*   **Type**: Use `string_t` (containing `const char* ptr` and `size_t len`) for read-only string references.
*   **Compile-Time Literals**: Always use the `string_lit(lit)` macro helper to construct a `string_t` from a compile-time string literal with **zero runtime overhead**:
    ```c
    string_t s = string_lit("hello");
    ```
*   **Literal Safety**: The `string_lit` macro compile-time enforces that its argument is indeed a string literal (using the concatenation trick `(lit "")`). Passing a pointer will cause a compile-time syntax error:
    ```c
    #define string_lit(lit) ((string_t){(lit ""), sizeof(lit "") - 1})
    ```
*   **Dynamic Strings**: Use `string_from_parts(ptr, len)` for non-literal substrings, and `string_from_cstr(str)` for null-terminated C strings.

---

## 7. Dear ImGui C Bindings

*   All UI code must interact with Dear ImGui strictly through a custom, hand-written C-compatible wrapper (`src/imgui_c.h`).
*   Direct inclusion of the `<imgui.h>` header is strictly prohibited in C23 files.

---

## 8. Formatting Tooling

To ensure consistent code style, a helper script is provided in the repository root:

*   **Script**: `./format.sh` (executable).
*   **Purpose**: Automatically formats changed C/C++ files (modified, added, or renamed) in the Jujutsu (`jj`) working copy using `clang-format` based on the `.clang-format` configuration.
*   **Usage**: Run it before committing or sharing code:
    ```bash
    ./format.sh
    ```
    This script automatically queries Jujutsu, filters for C/C++ files, and formats them in-place, ensuring strict compliance with this style guide.
