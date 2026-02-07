Hi, I'm not sure if this is my league, but I am at the least interested in working on this.

My basic idea is to have an attribute like the following:

```c
struct My {};
struct MyL {};
struct MyLL {};
__attribute__((format_spec(
  printf, "M",
  /* pointer_count */ 1,
  /* flag_chars    */ "-+0 #wp",
  /* flags2        */ "iWR[]<>",
  /* length token pairs */.
  "none", __typeof__(struct My *),
  "l",    __typeof__(struct MyL *),
  "ll",   __typeof__(struct MyLL *)
)))
static int
custom_printf_M (FILE *fp, const struct printf_info *info,
		     const void *const *args)
{
  /* ... */
#include <printf.h>
register_printf_specifier(..,custom_printf_M)
```

This is a direct conversion from GCC internals, with the intention to support all mainstream printf specifier cases, except for chaining (multi-arg specs) which is intentionally left out of this proposal because it seems to be an edge case for Solaris from my reading of git. I also do not allow per-spec STD_* setting, they stay at STD_EXT with no custom diagnostic names. So this proposal does not get full parity with all that format_char_info offers; it would just cover what I imagine to be the majority of cases based on reading uses in the wild.

This syntax lists printf as the first arg, this gets normalized to gnu_printf or ms_printf depending on the target platform using convert_format_name_to_system_name, then get_format_type_by_name. If not found, we error. Even though this proposal targets printf, since it's parametrized I'll refer to it by "format kind" in this proposal.

pointer_count (meaning the level of pointer indirection, with 0 meaning no indirection, 1 meaning one level, *p, and so on) applies uniformly to all length entries, there is not a per-length override.

flag_chars and flags2 are both validated against the chosen format kind’s flag_specs; any character not defined there is rejected.

For the length token pairs, "none" is a special value meaning format_lengths FMT_LEN_none signifying no length modifier. If "none" is not provided then bare %M in this case would be disallowed. The other options are all matched against format_kind_info.length_char_specs and translated by printf_length_specs. If we get a truly unknown token, that becomes a diagnostic.

The gcc format_spec attribute can be set on the custom format kind's function (custom_printf_M in this example) or on a dummy function as needed, it just needs to be available in the Translation Unit where the format checking is done by GCC.

Happy to contribute an implementation (in the form of a plugin to start) and tests, just wanted to get alignment on the interface first.

Thanks
Gabriel
