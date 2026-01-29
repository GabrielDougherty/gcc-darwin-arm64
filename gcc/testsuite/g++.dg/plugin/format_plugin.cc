/* Demonstrates how to register custom format specifiers via plugin API.

   This plugin provides two ways to register custom format specifiers:

   1. Programmatic API (register_format_specifier_simple):
      Called during PLUGIN_ATTRIBUTES to register %T, %Q, %V specifiers.

   2. Attribute-based API (__attribute__((format_specifier(...)))):
      Users can declare custom specifiers in their source code.
      The expected argument types are extracted from the function parameters:

        __attribute__((format_specifier(printf, "M")))
        int my_arginfo(MyStruct *);  // %M expects MyStruct*

        __attribute__((format_specifier(printf, "W")))
        int width_arginfo(int, long, long long);  // %W, %lW, %llW

   This mirrors glibc's register_printf_specifier pattern where an arginfo
   function describes the expected argument types.

   Usage:
     gcc -fplugin=./format_plugin.so -Wformat test.c

   This addresses PR c/47781 where glibc's register_printf_specifier
   could define custom formats at runtime, but GCC's -Wformat didn't know
   about them and would emit spurious warnings.
*/

#include "gcc-plugin.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "stringpool.h"
#include "attribs.h"
#include "c-family/c-format.h"
#include "diagnostic.h"
#include "plugin.h"

int plugin_is_GPL_compatible;

/* Handle the format_specifier attribute.

   Syntax: __attribute__((format_specifier(format_type, "specifier")))

   The expected argument types are extracted from the function's parameter list.
   This mirrors glibc's printf_arginfo_function pattern.

   Examples:
     // Simple specifier: %B expects int
     __attribute__((format_specifier(printf, "B")))
     int B_arginfo(int);

     // Pointer type: %M expects MyStruct*
     __attribute__((format_specifier(printf, "M")))
     int M_arginfo(MyStruct *);

     // With length modifiers: %W (int), %lW (long), %llW (long long)
     __attribute__((format_specifier(printf, "W")))
     int W_arginfo(int, long, long long);
*/

static tree
handle_format_specifier_attribute (tree *node, tree name, tree args,
				   int flags, bool *no_add_attrs)
{
  /* We don't actually attach this attribute to the decl.  */
  *no_add_attrs = true;

  /* The node should be a function declaration.  */
  tree decl = *node;
  if (TREE_CODE (decl) != FUNCTION_DECL)
    {
      error ("format_specifier attribute requires a function declaration");
      return NULL_TREE;
    }

  /* Get the function type to extract parameter types.  */
  tree fntype = TREE_TYPE (decl);
  if (TREE_CODE (fntype) != FUNCTION_TYPE && TREE_CODE (fntype) != METHOD_TYPE)
    {
      error ("format_specifier attribute requires a function type");
      return NULL_TREE;
    }

  /* Parse arguments: (format_type, "specifier_char") */
  if (!args)
    {
      error ("format_specifier attribute requires arguments");
      return NULL_TREE;
    }

  /* First arg: format type identifier (printf, scanf, etc.)
     In C++, 'printf' might be looked up and resolved to the function decl,
     so we need to handle both IDENTIFIER_NODE and ADDR_EXPR of FUNCTION_DECL.  */
  tree format_type_id = TREE_VALUE (args);
  const char *format_name = NULL;

  if (TREE_CODE (format_type_id) == IDENTIFIER_NODE)
    {
      format_name = IDENTIFIER_POINTER (format_type_id);
    }
  else if (TREE_CODE (format_type_id) == ADDR_EXPR
	   && TREE_CODE (TREE_OPERAND (format_type_id, 0)) == FUNCTION_DECL)
    {
      /* C++ looked up 'printf' and found the function - extract the name.  */
      tree fn_decl = TREE_OPERAND (format_type_id, 0);
      format_name = IDENTIFIER_POINTER (DECL_NAME (fn_decl));
    }
  else if (TREE_CODE (format_type_id) == FUNCTION_DECL)
    {
      /* C++ looked up 'printf' and resolved it directly to a function decl.  */
      format_name = IDENTIFIER_POINTER (DECL_NAME (format_type_id));
    }
  else
    {
      error ("format_specifier: first argument must be a format type "
	     "(e.g., printf, scanf), got tree code %s",
	     get_tree_code_name (TREE_CODE (format_type_id)));
      return NULL_TREE;
    }

  /* Convert "printf" to "gnu_printf" for lookup.  */
  char full_format_name[64];
  if (strcmp (format_name, "printf") == 0
      || strcmp (format_name, "scanf") == 0
      || strcmp (format_name, "strftime") == 0
      || strcmp (format_name, "strfmon") == 0)
    {
      snprintf (full_format_name, sizeof (full_format_name),
		"gnu_%s", format_name);
      format_name = full_format_name;
    }

  args = TREE_CHAIN (args);
  if (!args)
    {
      error ("format_specifier: missing specifier character");
      return NULL_TREE;
    }

  /* Second arg: specifier character as a string.  */
  tree spec_char_tree = TREE_VALUE (args);
  if (TREE_CODE (spec_char_tree) != STRING_CST)
    {
      error ("format_specifier: second argument must be a string "
	     "(e.g., \"B\")");
      return NULL_TREE;
    }
  const char *spec_chars = TREE_STRING_POINTER (spec_char_tree);
  if (!spec_chars || !spec_chars[0])
    {
      error ("format_specifier: specifier string cannot be empty");
      return NULL_TREE;
    }

  /* Build the format_specifier_def by extracting types from function params.  */
  struct format_specifier_def spec;
  memset (&spec, 0, sizeof (spec));
  spec.chars = spec_chars;
  spec.pointer_count = 0;
  spec.flags = NULL;  /* Use default flags.  */

  /* Iterate over function parameter types.
     Parameter 1 -> FMT_LEN_none (no length modifier)
     Parameter 2 -> FMT_LEN_l ('l' length modifier)
     Parameter 3 -> FMT_LEN_ll ('ll' length modifier)  */
  tree arg_types = TYPE_ARG_TYPES (fntype);
  int param_index = 0;

  /* Map parameter index to format length modifier.  */
  static const enum format_lengths len_map[] = {
    FMT_LEN_none,  /* param 0: no modifier */
    FMT_LEN_l,     /* param 1: 'l' modifier */
    FMT_LEN_ll     /* param 2: 'll' modifier */
  };

  for (; arg_types && TREE_VALUE (arg_types) != void_type_node;
       arg_types = TREE_CHAIN (arg_types), param_index++)
    {
      if (param_index >= 3)
	{
	  warning (0, "format_specifier: ignoring parameters beyond the third");
	  break;
	}

      tree param_type = TREE_VALUE (arg_types);

      /* Handle pointer types specially.  */
      if (POINTER_TYPE_P (param_type))
	{
	  /* For the first parameter, set pointer_count.  */
	  if (param_index == 0)
	    spec.pointer_count = 1;
	  /* Store the pointed-to type.  */
	  spec.types[len_map[param_index]] = TREE_TYPE (param_type);
	}
      else
	{
	  spec.types[len_map[param_index]] = param_type;
	}
    }

  if (param_index == 0)
    {
      error ("format_specifier: function must have at least one parameter "
	     "to specify the expected argument type");
      return NULL_TREE;
    }

  /* Register the specifier.  */
  if (!register_format_specifier_simple (format_name, &spec))
    {
      /* Error already reported by register_format_specifier_simple.  */
      return NULL_TREE;
    }

  inform (UNKNOWN_LOCATION,
	  "format_plugin: registered %%%s via attribute for %qs",
	  spec_chars, format_name);

  return NULL_TREE;
}

/* Attribute specification for format_specifier.  */
static struct attribute_spec format_specifier_attr = {
  "format_specifier",		/* name */
  2,				/* min_length (format_type, specifier_char) */
  2,				/* max_length (types come from function params) */
  true,				/* decl_required (must be on a function decl) */
  false,			/* type_required */
  false,			/* function_type_required */
  false,			/* affects_type_identity */
  handle_format_specifier_attribute,  /* handler */
  NULL				/* exclude */
};

/* Called during attribute registration.  */

static void
register_attributes_and_formats (void *event_data, void *data)
{
  /* Register the format_specifier attribute.  */
  register_attribute (&format_specifier_attr);

  inform (UNKNOWN_LOCATION,
	  "format_plugin: registered format_specifier attribute");

  /* Also register our built-in specifiers programmatically.  */

  /* %T: accepts an int (for bool - prints TRUE/FALSE).
     Inspired by the original PR c/47781 use case.  */
  struct format_specifier_def spec_T = {
    .chars = "T",
    .pointer_count = 0,
    .types = { [FMT_LEN_none] = integer_type_node },
    .flags = "-w",
  };

  if (register_format_specifier_simple ("gnu_printf", &spec_T))
    inform (UNKNOWN_LOCATION,
	    "format_plugin: added %%T specifier to gnu_printf");

  /* %Q: accepts a void* pointer, like %p.  */
  struct format_specifier_def spec_Q = {
    .chars = "Q",
    .pointer_count = 1,
    .types = { [FMT_LEN_none] = void_type_node },
    .flags = "-w",
  };

  if (register_format_specifier_simple ("gnu_printf", &spec_Q))
    inform (UNKNOWN_LOCATION,
	    "format_plugin: added %%Q specifier to gnu_printf");

  /* %V: prints a "value" - accepts int, long, or long long depending
     on length modifier.  Demonstrates length modifier support.  */
  struct format_specifier_def spec_V = {
    .chars = "V",
    .pointer_count = 0,
    .types = {
      [FMT_LEN_none] = integer_type_node,
      [FMT_LEN_l] = long_integer_type_node,
      [FMT_LEN_ll] = long_long_integer_type_node,
    },
    .flags = "-+0 wp",
  };

  if (register_format_specifier_simple ("gnu_printf", &spec_V))
    inform (UNKNOWN_LOCATION,
	    "format_plugin: added %%V specifier to gnu_printf");
}

int
plugin_init (struct plugin_name_args *plugin_info,
	     struct plugin_gcc_version *version)
{
  const char *plugin_name = plugin_info->base_name;

  /* Register our callback to be invoked during attribute registration.
     This is when format types are being set up.  */
  register_callback (plugin_name, PLUGIN_ATTRIBUTES,
		     register_attributes_and_formats, NULL);

  inform (UNKNOWN_LOCATION, "format_plugin: loaded successfully");
  return 0;
}
