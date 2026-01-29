/* Test the format_specifier attribute syntax with types from parameters.
   { dg-do compile }
   { dg-options "-Wformat" }
*/

extern "C" int printf (const char *, ...)
    __attribute__ ((format (printf, 1, 2)));

/* Custom struct type for testing.  */
struct MyData {
  int x;
  int y;
};

/* Declare custom format specifiers using the attribute syntax.
   Types are extracted from the function parameter list, similar to
   glibc's printf_arginfo_function pattern.  */

/* %K: accepts an int (simple case).
   Note: We use K instead of B because B is already used for binary format.  */
__attribute__((format_specifier(printf, "K")))
int K_arginfo(int);

/* %P: accepts a void* pointer.  */
__attribute__((format_specifier(printf, "P")))
int P_arginfo(void *);

/* %W: accepts int, long, or long long depending on length modifier.  */
__attribute__((format_specifier(printf, "W")))
int W_arginfo(int, long, long long);

/* %M: accepts a MyData* pointer (custom struct type).  */
__attribute__((format_specifier(printf, "M")))
int M_arginfo(struct MyData *);

/* %U: accepts a const char16_t* string (UTF-16 string type).
   Note: We use U instead of S because S is already used for wide strings.  */
__attribute__((format_specifier(printf, "U")))
int U_arginfo(const char16_t *);

void
test_specifier_K (int i, const char *s)
{
  /* %K should accept int.  */
  printf ("%K\n", i);    /* No warning.  */
  printf ("%K\n", 42);   /* No warning.  */

  /* %K should warn on wrong type.  */
  printf ("%K\n", s);    /* { dg-warning "format '%K' expects argument of type 'int'" } */
}

void
test_specifier_P (int *p, int i)
{
  /* %P should accept any pointer.  */
  printf ("%P\n", p);    /* No warning.  */
  printf ("%P\n", (void*)0);  /* No warning.  */

  /* %P should warn on non-pointer.  */
  printf ("%P\n", i);    /* { dg-warning "format '%P' expects argument of type 'void\\*'" } */
}

void
test_specifier_W (int i, long l, long long ll)
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

void
test_specifier_M (struct MyData *data, int i)
{
  /* %M should accept MyData*.  */
  printf ("%M\n", data);  /* No warning.  */

  /* %M should warn on wrong type.  */
  printf ("%M\n", i);     /* { dg-warning "format '%M' expects argument of type 'MyData\\*'" } */
  printf ("%M\n", (void*)0);  /* { dg-warning "format '%M' expects argument of type 'MyData\\*'" } */
}

void
test_specifier_U (const char16_t *str, const char *cstr)
{
  /* %U should accept const char16_t*.  */
  printf ("%U\n", str);   /* No warning.  */
  printf ("%U\n", u"hello");  /* No warning.  */

  /* %U should warn on wrong string type.  */
  printf ("%U\n", cstr);  /* { dg-warning "format '%U' expects argument of type 'char16_t\\*'" } */
  printf ("%U\n", "hello");  /* { dg-warning "format '%U' expects argument of type 'char16_t\\*'" } */
}
