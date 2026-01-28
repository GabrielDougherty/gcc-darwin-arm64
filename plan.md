# Simplified Plugin API for Custom Format Specifiers

## Goal

Provide an attribute-based interface for adding custom format specifiers to
existing format types (printf, scanf, etc.). This is implemented as a GCC
plugin for faster development and testing, following Manuel López-Ibáñez's
suggestion from the bug discussion.

The attribute-based approach allows users to declare custom specifiers
directly in their source code, without writing plugin code themselves.

**Scope:** Adding specifiers to existing format types only. Creating entirely
new format types is out of scope for this design.

---

## User-Facing Attribute Syntax

Users declare custom format specifiers via an attribute in their source code:

```c
/* Declare that %T in printf format strings accepts an int (for bool) */
__attribute__((format_specifier(printf, "T", int)))

/* Declare that %Q accepts a void* */
__attribute__((format_specifier(printf, "Q", void *)))

/* With length modifier support: %V=int, %lV=long, %llV=long long */
__attribute__((format_specifier(printf, "V", int, long, long long)))
```

The attribute can be attached to:
- A function declaration (applies to that translation unit)
- A type declaration
- Or used standalone with a dummy declaration

### Attribute Parameters

```
format_specifier(format_type, specifier_char, type [, long_type [, long_long_type]])
```

| Parameter | Description |
|-----------|-------------|
| `format_type` | `printf`, `scanf`, `strftime`, etc. |
| `specifier_char` | Single character like `"T"`, `"Q"`, `"V"` |
| `type` | The C type accepted by bare `%T` |
| `long_type` | (optional) Type for `%lT` |
| `long_long_type` | (optional) Type for `%llT` |

### Example Usage

```c
/* In a header file included before using custom specifiers */
__attribute__((format_specifier(printf, "T", int)))
__attribute__((format_specifier(printf, "Q", void *)))
extern int my_printf(const char *, ...) __attribute__((format(printf, 1, 2)));

/* Now these are checked correctly */
void test(void) {
    my_printf("%T %Q\n", 1, ptr);     /* OK */
    my_printf("%T\n", "wrong");        /* warning: %T expects int */
}
```

---

## Plugin Implementation

The plugin:
1. Registers a handler for the `format_specifier` attribute
2. When the attribute is encountered, calls the internal API to register the specifier
3. Uses existing GCC format checking infrastructure

---

## Internal API (c-format.h)

### Specifier Definition Structure

```c
/* User-facing specifier definition.
   All fields have sensible defaults when zero-initialized.  */
struct format_specifier_def {
  /* Specifier character(s), e.g. "T" or "pS".  Required.  */
  const char *chars;

  /* Pointer indirection level:
     0 = value (e.g., %d takes int)
     1 = pointer (e.g., %s takes char*)
     2 = pointer-to-pointer (rare)  */
  int pointer_count;

  /* Type accepted for each length modifier.
     Use existing FMT_LEN_* constants from c-format.h.
     Example: { [FMT_LEN_none] = integer_type_node, [FMT_LEN_l] = long_integer_type_node }
     NULL entries mean that length modifier is not allowed.  */
  tree types[FMT_LEN_MAX];

  /* Allowed flags as a string, e.g. "-+0 #".
     'w' = width allowed, 'p' = precision allowed.
     NULL = default flags "-w" (left-align and width).  */
  const char *flags;
};
```

### Core API Functions

```c
/* Add a format specifier to an existing format type.

   format_name: Name of existing format type, e.g.:
     - "printf", "gnu_printf" - standard printf
     - "scanf", "gnu_scanf"   - standard scanf
     - "strftime"             - time formatting
     - etc.

   Returns true on success, false on failure.
   The format_specifier_def is copied; caller retains ownership.  */
extern bool register_format_specifier (const char *format_name,
                                       const struct format_specifier_def *spec);
```

---

## Example Plugin (Simplified)

```c
#include "gcc-plugin.h"
#include "tree.h"
#include "c-family/c-format.h"

int plugin_is_GPL_compatible;

static void
register_formats (void *event_data, void *data)
{
  /* %T takes int (for bool - original PR c/47781 use case) */
  register_format_specifier ("gnu_printf", &(struct format_specifier_def){
    .chars = "T",
    .pointer_count = 0,
    .types = { [FMT_LEN_none] = integer_type_node },
  });

  /* %Q takes void* */
  register_format_specifier ("gnu_printf", &(struct format_specifier_def){
    .chars = "Q",
    .pointer_count = 1,
    .types = { [FMT_LEN_none] = void_type_node },
  });

  /* %V takes int, %lV takes long, %llV takes long long */
  register_format_specifier ("gnu_printf", &(struct format_specifier_def){
    .chars = "V",
    .pointer_count = 0,
    .types = {
      [FMT_LEN_none] = integer_type_node,
      [FMT_LEN_l] = long_integer_type_node,
      [FMT_LEN_ll] = long_long_integer_type_node,
    },
    .flags = "-+0 wp",  /* allow width and precision */
  });
}

int
plugin_init (struct plugin_name_args *info,
             struct plugin_gcc_version *version)
{
  register_callback (info->base_name, PLUGIN_ATTRIBUTES,
                     register_formats, NULL);
  return 0;
}
```

---

## Implementation Details (c-format.cc)

### Internal Translation

The implementation translates the user-facing structures to GCC's internal
format_char_info:

1. **Build format_type_detail array**
   - For each entry in `spec->types[]`:
     - If non-NULL: create `{ STD_EXT, NULL, &type }`
     - If NULL: use `BADLEN`

2. **Build format_char_info**
   ```c
   format_char_info internal = {
     .format_chars = xstrdup (spec->chars),
     .pointer_count = spec->pointer_count,
     .std = STD_EXT,
     .types = /* translated from spec->types[] */,
     .flag_chars = spec->flags ? xstrdup (spec->flags) : "-w",
     .flags2 = "",
     .chain = NULL,
   };
   ```

3. **Register using existing internal machinery**

---

## What Gets Hidden from Plugin Authors

| Hidden Detail | How It's Handled |
|--------------|------------------|
| `BADLEN` constant | Automatically filled for NULL entries in types[] |
| `format_type_detail` wrapper | Built internally from tree nodes |
| `STD_C89`, `STD_EXT` constants | Default to `STD_EXT` for extensions |
| Terminator entries | Added automatically |
| Deep copying | Handled in implementation |
| `format_char_info` structure | Never exposed; built from format_specifier_def |

## What Plugin Authors Still Use Directly

| Exposed Detail | Reason |
|----------------|--------|
| `FMT_LEN_*` constants | No value in duplicating; array indices |
| `tree` type nodes | Standard GCC plugin interface |
| `PLUGIN_ATTRIBUTES` event | Standard GCC plugin callback |

---

## API Comparison

| Aspect | Current API | New API |
|--------|-------------|---------|
| Plugin lines of code | ~100 | ~30 |
| Headers required | 11 | 3 |
| Must understand BADLEN | Yes | No |
| Must understand FMT_LEN_* | Yes | Yes (same constants, simpler usage) |
| Must build format_char_info | Yes | No |
| Must build format_type_detail | Yes | No |
| Must define helper macros | Yes | No |
| Designated initializers | No | Yes (sparse array) |
| Terminator entries | Manual | Automatic |
| Deep copy handling | Manual | Automatic |

---

## Migration Path

1. Keep existing low-level functions as internal (`static`)
2. Add new public API as described above
3. Update test plugin to use new API
4. Document new API in gcc/doc/plugins.texi

---

## Open Questions

1. **Should we support multi-argument specifiers?**
   - Example: `%.*s` takes int (precision) then char*
   - Current internal API supports this via `chain` pointer
   - Could add `struct format_specifier_def *next_arg` field

2. **Should flags be a string or a flags enum/bitmask?**
   - String: `"-+0 wp"` - familiar, matches printf
   - Bitmask: `FMT_FLAG_MINUS | FMT_FLAG_WIDTH` - type-safe
   - Recommendation: string for simplicity

3. **Error reporting: return bool vs error enum?**
   - Current: returns bool, uses `error()` internally
   - Alternative: return enum with specific error codes
   - Recommendation: keep bool, errors go to diagnostics

4. **Should we validate type nodes?**
   - Check that `integer_type_node` etc. are valid
   - Reject obviously wrong types (e.g., FUNCTION_TYPE for %d)
   - Recommendation: basic validation, warn on suspicious types

5. **FMT_LEN_MAX in struct - ABI stability concern?**
   - If GCC adds new length modifiers, FMT_LEN_MAX changes
   - Plugins compiled against old headers have wrong struct size
   - Options:
     a. Accept it (plugins already tied to GCC versions)
     b. Use pointer to array + count instead of fixed array
     c. Use list of (length, type) pairs
   - Recommendation: TBD

---

## Files to Modify

### Core API (in GCC)
- `gcc/c-family/c-format.h` - Add `format_specifier_def` struct and `register_format_specifier()` declaration
- `gcc/c-family/c-format.cc` - Add implementation that translates to internal structures

### Plugin (separate, for faster iteration)
- `format_specifier_plugin.cc` - The plugin that:
  - Registers the `format_specifier` attribute handler
  - Parses attribute arguments (format_type, specifier, types...)
  - Calls `register_format_specifier()` with appropriate parameters

### Testing
- `gcc/testsuite/g++.dg/plugin/format_specifier_plugin.cc` - Plugin source
- `gcc/testsuite/g++.dg/plugin/format_specifier-test-1.C` - Test cases

### Documentation
- `gcc/doc/plugins.texi` - Document the internal API
- Plugin README - Document the attribute syntax for end users

---

## Development Approach

Following Manuel's suggestion:

1. **Start with plugin** - Faster iteration, no GCC rebuild for each change
2. **Simple attribute design** - Not a pragma, easier to parse and use
3. **Test with real specifiers** - Could test against GCC's own %E, %T, %q formats
4. **Verify -Wmissing-format-attribute** - Ensure custom specifiers work with this warning
5. **Submit for feedback** - Get community input before proposing for mainline
6. **Later: consider mainline** - If successful, the attribute could become a GCC built-in

---

## Prior Art

**Plan 9** (comment #31 in bug discussion):
```c
#pragma varargck type "C" TYPE   /* specifier 'C' consumes 1 arg of TYPE */
#pragma varargck flag 'C'        /* modifier 'C' consumes 0 args */
```
Similar to our attribute design, validates the approach.

---

## Out of Scope (Future Work)

- **Sub-specifiers** like `%pS` (kernel-style extensions)
- **Creating new format types** (rare use case)
- **Multi-argument specifiers** like `%.*s`
- **sprintf optimization pass integration** (mentioned by Martin Sebor)
