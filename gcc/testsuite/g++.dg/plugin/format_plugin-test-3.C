/* Test the format_specifier attribute syntax.
   { dg-do compile }
   { dg-options "-Wformat" }
*/

extern "C" int printf (const char *, ...)
    __attribute__ ((format (printf, 1, 2)));

/* Declare custom format specifiers using the attribute syntax.
   Types are passed as string names: "int", "long", "void*", etc.  */

/* %B: accepts an int (simple case).  */
__attribute__((format_specifier(printf, "B", "int")))
void register_B(void);

/* %P: accepts a void* pointer.  */
__attribute__((format_specifier(printf, "P", "void*")))
void register_P(void);

/* %W: accepts int, long, or long long depending on length modifier.  */
__attribute__((format_specifier(printf, "W", "int", "long", "long long")))
void register_W(void);

void
test_attribute_specifier_B (int i, const char *s)
{
  /* %B should accept int.  */
  printf ("%B\n", i);    /* No warning.  */
  printf ("%B\n", 42);   /* No warning.  */

  /* %B should warn on wrong type.  */
  printf ("%B\n", s);    /* { dg-warning "format '%B' expects argument of type 'int'" } */
}

void
test_attribute_specifier_P (int *p, int i)
{
  /* %P should accept any pointer.  */
  printf ("%P\n", p);    /* No warning.  */
  printf ("%P\n", (void*)0);  /* No warning.  */

  /* %P should warn on non-pointer.  */
  printf ("%P\n", i);    /* { dg-warning "format '%P' expects argument of type 'void \\*'" } */
}

void
test_attribute_specifier_W (int i, long l, long long ll)
{
  /* %W should accept int.  */
  printf ("%W\n", i);    /* No warning.  */

  /* %lW should accept long.  */
  printf ("%lW\n", l);   /* No warning.  */

  /* %llW should accept long long.  */
  printf ("%llW\n", ll); /* No warning.  */

  /* Type mismatches should warn.  */
  printf ("%W\n", l);    /* { dg-warning "format '%W' expects argument of type 'int', but argument 2 has type 'long int'" } */
  printf ("%lW\n", i);   /* { dg-warning "format '%lW' expects argument of type 'long int', but argument 2 has type 'int'" } */
  printf ("%llW\n", l);  /* { dg-warning "format '%llW' expects argument of type 'long long int', but argument 2 has type 'long int'" } */
}
