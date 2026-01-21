/* Test for PR c/47781 - custom printf format specifiers via plugin.

   This test demonstrates that the format_plugin correctly suppresses
   spurious warnings for custom format specifiers that would otherwise
   cause:
     warning: unknown conversion type character 'T' in format
     warning: too many arguments for format

   The original bug report showed that glibc's register_printf_specifier()
   could define %b for bool at runtime, but GCC's -Wformat didn't know
   about it and emitted false positive warnings.
   Note: %b/%B are now standard C23 for binary, so we use %T for this demo.

   With the plugin loaded, %T is recognized and type-checked properly.  */

/* { dg-do compile } */
/* { dg-options "-Wformat" } */

extern "C" int printf (const char *, ...)
    __attribute__ ((format (printf, 1, 2)));

typedef int bool_t;

void
test_pr47781 (void)
{
  bool_t foo = 1;
  bool_t bar = 0;

  /* These would warn WITHOUT the plugin:
       warning: unknown conversion type character 'T' in format
       warning: too many arguments for format
     With the plugin, %T is recognized and accepts int.  */
  printf ("true bool: %T  false bool: %T\n", foo, bar); /* No warning.  */

  /* Type checking still works - passing wrong type should warn.  */
  printf ("%T\n", "string"); /* { dg-warning "format '%T' expects argument of
                                type 'int'" } */

  /* The %Q specifier accepts void*.  */
  int x = 42;
  printf ("%Q\n", &x); /* No warning - &x is a pointer.  */
  printf ("%Q\n", x);  /* { dg-warning "format '%Q' expects argument of type
                          'void \\*'" } */
}

/* Also test that standard specifiers still work.  */
void
test_standard_specifiers (void)
{
  printf ("%d %s %p\n", 42, "hello", (void *)0); /* No warning.  */
  printf ("%d\n", "wrong"); /* { dg-warning "format '%d' expects argument of
                               type 'int'" } */
}
