# Simplified Plugin API for Custom Format Specifiers

## Goal

Replace the current low-level API that exposes GCC internals with a clean,
user-friendly API that covers 100% of use cases while hiding implementation
details like BADLEN, 20-element type arrays, and format_char_info structures.

---

## Proposed API (c-format.h)

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
/* Add a format specifier to an existing format type (e.g., "gnu_printf").
   Returns true on success, false on failure.
   The format_specifier_def is copied; caller retains ownership.  */
extern bool register_format_specifier (const char *format_name,
                                       const struct format_specifier_def *spec);

/* Create a new printf-like format type.
   Returns the format type index on success, -1 on failure.
   The new type starts with basic specifiers: %% and %n.  */
extern int register_printf_like_format (const char *name);

/* Create a new scanf-like format type.
   Returns the format type index on success, -1 on failure.  */
extern int register_scanf_like_format (const char *name);

/* Look up a format type by name.
   Returns the format type index, or -1 if not found.  */
extern int get_format_type_by_name (const char *name);
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

  /* Create entirely new format type */
  register_printf_like_format ("my_custom");
  register_format_specifier ("my_custom", &(struct format_specifier_def){
    .chars = "X",
    .types = { [FMT_LEN_none] = integer_type_node },
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

3. **Register using internal machinery**

### register_printf_like_format Implementation

Creates a new format_kind_info with:
- Standard printf length modifiers (h, hh, l, ll, z, t, j, L)
- Standard printf flags (-, +, 0, space, #, ', w, p)
- Basic specifiers: %% (literal percent)
- Printf-style flag validation rules

### register_scanf_like_format Implementation

Similar but with:
- Scanf semantics (pointer arguments, suppression with *)
- Scanf-specific flags

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
| `format_kind_info` structure | Never exposed (for printf/scanf-like formats) |

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

---

## Files to Modify

- `gcc/c-family/c-format.h` - Add new API declarations
- `gcc/c-family/c-format.cc` - Add implementation
- `gcc/testsuite/g++.dg/plugin/format_plugin.cc` - Simplify using new API
- `gcc/doc/plugins.texi` - Document the API
