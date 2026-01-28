/* Demonstrates how to register custom format specifiers via plugin API.

   This plugin provides two ways to register custom format specifiers:

   1. Programmatic API (register_format_specifier_simple):
      Called during PLUGIN_ATTRIBUTES to register %T, %Q, %V specifiers.

   2. Attribute-based API (__attribute__((format_specifier(...)))):
      Users can declare custom specifiers in their source code:

        __attribute__((format_specifier(printf, "B", "int")))
        void dummy_for_B(void);

      This registers %B to accept an int argument.

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

/* Map a type name string to a tree type node.
   Returns NULL_TREE if the type name is not recognized.  */

static tree
lookup_type_by_name (const char *name)
{
  if (!name)
    return NULL_TREE;

  /* Basic integer types.  */
  if (strcmp (name, "int") == 0)
    return integer_type_node;
  if (strcmp (name, "unsigned int") == 0 || strcmp (name, "unsigned") == 0)
    return unsigned_type_node;
  if (strcmp (name, "long") == 0 || strcmp (name, "long int") == 0)
    return long_integer_type_node;
  if (strcmp (name, "unsigned long") == 0
      || strcmp (name, "unsigned long int") == 0)
    return long_unsigned_type_node;
  if (strcmp (name, "long long") == 0 || strcmp (name, "long long int") == 0)
    return long_long_integer_type_node;
  if (strcmp (name, "unsigned long long") == 0
      || strcmp (name, "unsigned long long int") == 0)
    return long_long_unsigned_type_node;
  if (strcmp (name, "short") == 0 || strcmp (name, "short int") == 0)
    return short_integer_type_node;
  if (strcmp (name, "unsigned short") == 0
      || strcmp (name, "unsigned short int") == 0)
    return short_unsigned_type_node;

  /* Character types.  */
  if (strcmp (name, "char") == 0)
    return char_type_node;
  if (strcmp (name, "signed char") == 0)
    return signed_char_type_node;
  if (strcmp (name, "unsigned char") == 0)
    return unsigned_char_type_node;

  /* Floating point types.  */
  if (strcmp (name, "float") == 0)
    return float_type_node;
  if (strcmp (name, "double") == 0)
    return double_type_node;
  if (strcmp (name, "long double") == 0)
    return long_double_type_node;

  /* Void type.  */
  if (strcmp (name, "void") == 0)
    return void_type_node;

  /* Pointer types.  */
  if (strcmp (name, "void*") == 0 || strcmp (name, "void *") == 0)
    return ptr_type_node;  /* void* */
  if (strcmp (name, "char*") == 0 || strcmp (name, "char *") == 0)
    return build_pointer_type (char_type_node);
  if (strcmp (name, "const char*") == 0 || strcmp (name, "const char *") == 0)
    return build_pointer_type (build_qualified_type (char_type_node,
						     TYPE_QUAL_CONST));

  /* Size types.  */
  if (strcmp (name, "size_t") == 0)
    return size_type_node;
  if (strcmp (name, "ptrdiff_t") == 0)
    return ptrdiff_type_node;

  return NULL_TREE;
}

/* Handle the format_specifier attribute.

   Syntax: __attribute__((format_specifier(format_type, "char", "type"
                                           [, "long_type" [, "long_long_type"]])))

   Examples:
     __attribute__((format_specifier(printf, "B", "int")))
     __attribute__((format_specifier(printf, "W", "int", "long", "long long")))
*/

static tree
handle_format_specifier_attribute (tree *node, tree name, tree args,
				   int flags, bool *no_add_attrs)
{
  /* We don't actually attach this attribute to the decl.  */
  *no_add_attrs = true;

  /* Parse arguments: (format_type, "specifier_char", "type" [, ...]) */
  if (!args)
    {
      error ("format_specifier attribute requires arguments");
      return NULL_TREE;
    }

  /* First arg: format type identifier (printf, scanf, etc.) */
  tree format_type_id = TREE_VALUE (args);
  if (TREE_CODE (format_type_id) != IDENTIFIER_NODE)
    {
      error ("format_specifier: first argument must be a format type "
	     "(e.g., printf, scanf)");
      return NULL_TREE;
    }
  const char *format_name = IDENTIFIER_POINTER (format_type_id);

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

  args = TREE_CHAIN (args);
  if (!args)
    {
      error ("format_specifier: missing type argument");
      return NULL_TREE;
    }

  /* Build the format_specifier_def.  */
  struct format_specifier_def spec;
  memset (&spec, 0, sizeof (spec));
  spec.chars = spec_chars;
  spec.pointer_count = 0;
  spec.flags = NULL;  /* Use default flags.  */

  /* Third arg: base type name (for no length modifier).  */
  tree type_name_tree = TREE_VALUE (args);
  if (TREE_CODE (type_name_tree) != STRING_CST)
    {
      error ("format_specifier: type argument must be a string "
	     "(e.g., \"int\")");
      return NULL_TREE;
    }
  const char *type_name = TREE_STRING_POINTER (type_name_tree);
  tree type_node = lookup_type_by_name (type_name);
  if (!type_node)
    {
      error ("format_specifier: unknown type %qs", type_name);
      return NULL_TREE;
    }

  /* Check if it's a pointer type.  */
  if (POINTER_TYPE_P (type_node))
    {
      spec.pointer_count = 1;
      spec.types[FMT_LEN_none] = TREE_TYPE (type_node);
    }
  else
    {
      spec.types[FMT_LEN_none] = type_node;
    }

  /* Optional fourth arg: type for 'l' length modifier.  */
  args = TREE_CHAIN (args);
  if (args)
    {
      type_name_tree = TREE_VALUE (args);
      if (TREE_CODE (type_name_tree) != STRING_CST)
	{
	  error ("format_specifier: type argument must be a string");
	  return NULL_TREE;
	}
      type_name = TREE_STRING_POINTER (type_name_tree);
      type_node = lookup_type_by_name (type_name);
      if (!type_node)
	{
	  error ("format_specifier: unknown type %qs", type_name);
	  return NULL_TREE;
	}
      spec.types[FMT_LEN_l] = type_node;

      /* Optional fifth arg: type for 'll' length modifier.  */
      args = TREE_CHAIN (args);
      if (args)
	{
	  type_name_tree = TREE_VALUE (args);
	  if (TREE_CODE (type_name_tree) != STRING_CST)
	    {
	      error ("format_specifier: type argument must be a string");
	      return NULL_TREE;
	    }
	  type_name = TREE_STRING_POINTER (type_name_tree);
	  type_node = lookup_type_by_name (type_name);
	  if (!type_node)
	    {
	      error ("format_specifier: unknown type %qs", type_name);
	      return NULL_TREE;
	    }
	  spec.types[FMT_LEN_ll] = type_node;
	}
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
  3,				/* min_length (format_type, char, type) */
  5,				/* max_length (+ optional long_type, long_long_type) */
  false,			/* decl_required */
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
