/* Demonstrates how to register custom format specifiers via plugin API.

   This plugin:
   1. Adds %b specifier to gnu_printf for bool (int) - solves PR c/47781
   2. Adds %Q specifier to gnu_printf that accepts void*
   3. Registers a completely new format type "custom_printf"

   Usage:
     gcc -fplugin=./format_plugin.so -Wformat test.c

   This addresses the original bug report where glibc's register_printf_specifier
   could define custom formats at runtime, but GCC's -Wformat didn't know about
   them and would emit spurious warnings.
*/

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "intl.h"
#include "c-family/c-format.h"
#include "c-family/c-common.h"
#include "diagnostic.h"
#include "plugin.h"

int plugin_is_GPL_compatible;

/* We need to define type specs similar to how c-format.h does it.
   Since we can't use the macros (they reference internal static vars),
   we'll define our own using the global type nodes.  */

#define PLUGIN_T_I   &integer_type_node
#define PLUGIN_T89_I { STD_C89, NULL, PLUGIN_T_I }
#define PLUGIN_T_V   &void_type_node
#define PLUGIN_T89_V { STD_C89, NULL, PLUGIN_T_V }
#define PLUGIN_T_L   &long_integer_type_node
#define PLUGIN_T89_L { STD_C89, NULL, PLUGIN_T_L }
#define PLUGIN_T_LL  &long_long_integer_type_node
#define PLUGIN_T9L_LL { STD_C9L, NULL, PLUGIN_T_LL }
#define PLUGIN_T_C   &char_type_node
#define PLUGIN_T89_C { STD_C89, NULL, PLUGIN_T_C }

/* Define %T for bool (int) - inspired by the original PR c/47781 use case.
   Note: %b/%B are now standard C23 for binary integers, so we use %T.
   Users of glibc's register_printf_specifier('T', ...) can now get
   proper -Wformat checking.  */

static const format_char_info custom_T_specifier[] =
{
  /* %T: accepts an int (for bool - prints TRUE/FALSE).  */
  { "T",  0, STD_EXT, { PLUGIN_T89_I, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN },
    "-w",  "", NULL },
  { NULL, 0, STD_C89, NOLENGTHS, NULL, NULL, NULL }
};

/* Define %Q for pointers (like %p).  */

static const format_char_info custom_Q_specifier[] =
{
  /* %Q: accepts a void* pointer, like %p.  */
  { "Q",  1, STD_EXT, { PLUGIN_T89_V, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN },
    "-w",  "c", NULL },
  { NULL, 0, STD_C89, NOLENGTHS, NULL, NULL, NULL }
};

/* For a completely new format type, we need length specs and char table.
   This is a minimal "custom_printf" that only supports %V for "value".  */

static const format_length_info custom_length_specs[] =
{
  { "l", FMT_LEN_l, STD_C89, "ll", FMT_LEN_ll, STD_C89, 0 },
  { NULL, FMT_LEN_none, STD_C89, NULL, FMT_LEN_none, STD_C89, 0 }
};

static const format_char_info custom_char_table[] =
{
  /* %V: prints a "value" - accepts int, long, or long long depending on length.
     pointer_count = 0 means we expect the value, not a pointer to it.
     PLUGIN_T89_I at index 0 means bare %V accepts int.  */
  { "V",  0, STD_EXT, { PLUGIN_T89_I, BADLEN, BADLEN, PLUGIN_T89_L, PLUGIN_T9L_LL, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN },
    "-+ 0", "", NULL },
  /* %s: standard string support - pointer_count = 1 for char*.  */
  { "s",  1, STD_C89, { PLUGIN_T89_C, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN,
			BADLEN, BADLEN },
    "-wp", "cR", NULL },
  /* %%: literal percent.  */
  { "%",  0, STD_C89, NOLENGTHS, "", "", NULL },
  { NULL, 0, STD_C89, NOLENGTHS, NULL, NULL, NULL }
};

static const format_flag_spec custom_flag_specs[] =
{
  { ' ',  0, 0, 0, N_("' ' flag"),        N_("the ' ' printf flag"),              STD_C89 },
  { '+',  0, 0, 0, N_("'+' flag"),        N_("the '+' printf flag"),              STD_C89 },
  { '0',  0, 0, 0, N_("'0' flag"),        N_("the '0' printf flag"),              STD_C89 },
  { '-',  0, 0, 0, N_("'-' flag"),        N_("the '-' printf flag"),              STD_C89 },
  { 'w',  0, 0, 0, N_("field width"),     N_("field width in printf format"),     STD_C89 },
  { 'p',  0, 0, 0, N_("precision"),       N_("precision in printf format"),       STD_C89 },
  { 'L',  0, 0, 0, N_("length modifier"), N_("length modifier in printf format"), STD_C89 },
  { 0, 0, 0, 0, NULL, NULL, STD_C89 }
};

static const format_flag_pair custom_flag_pairs[] =
{
  { '-', '0', 1, 0 },  /* -0 is okay, 0 is ignored.  */
  { 0, 0, 0, 0 }
};

/* Called during attribute registration to set up our custom formats.  */

static void
register_custom_formats (void *event_data, void *data)
{
  int printf_idx;
  int custom_idx;

  inform (UNKNOWN_LOCATION, "format_plugin: registering custom format specifiers");

  /* Add specifiers to the existing gnu_printf format.  */
  printf_idx = get_format_type_by_name ("gnu_printf");
  if (printf_idx >= 0)
    {
      /* Add %T for bool (inspired by the original PR c/47781 use case).  */
      if (register_format_specifier (printf_idx, &custom_T_specifier[0]))
	inform (UNKNOWN_LOCATION,
		"format_plugin: added %%T specifier to gnu_printf");
      else
	warning (0, "format_plugin: failed to add %%T to gnu_printf");

      /* Add %Q for void*.  */
      if (register_format_specifier (printf_idx, &custom_Q_specifier[0]))
	inform (UNKNOWN_LOCATION,
		"format_plugin: added %%Q specifier to gnu_printf");
      else
	warning (0, "format_plugin: failed to add %%Q to gnu_printf");
    }
  else
    warning (0, "format_plugin: gnu_printf format not found");

  /* Second, register a completely new format type "custom_printf".  */
  format_kind_info custom_format = {
    "custom_printf",           /* name */
    custom_length_specs,       /* length_char_specs */
    custom_char_table,         /* conversion_specs */
    "-+ 0",                    /* flag_chars */
    NULL,                      /* modifier_chars */
    custom_flag_specs,         /* flag_specs */
    custom_flag_pairs,         /* bad_flag_pairs */
    FMT_FLAG_ARG_CONVERT,      /* flags */
    'w',                       /* width_char */
    0,                         /* left_precision_char */
    'p',                       /* precision_char */
    0,                         /* suppression_char */
    'L',                       /* length_code_char */
    0,                         /* alloc_char */
    &integer_type_node,        /* width_type */
    &integer_type_node         /* precision_type */
  };

  custom_idx = register_format_type (&custom_format);
  if (custom_idx >= 0)
    inform (UNKNOWN_LOCATION,
	    "format_plugin: registered custom_printf format (index %d)",
	    custom_idx);
  else
    warning (0, "format_plugin: failed to register custom_printf format");
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  const char *plugin_name = plugin_info->base_name;

  /* Register our callback to be invoked during attribute registration.
     This is when format types are being set up.  */
  register_callback (plugin_name, PLUGIN_ATTRIBUTES,
		     register_custom_formats, NULL);

  inform (UNKNOWN_LOCATION, "format_plugin: loaded successfully");
  return 0;
}
