/* Test the custom format plugin.
   { dg-do compile }
   { dg-options "-Wformat" }
*/

extern "C" int printf (const char *, ...)
    __attribute__ ((format (printf, 1, 2)));

/* Declare a function using our custom_printf format.  */
extern "C" int custom_print (const char *, ...)
    __attribute__ ((format (custom_printf, 1, 2)));

struct quad
{
  int x, y, z, w;
};

void
test_printf_T (int foo, int bar, const char *str)
{
  /* Test %T added to printf for bool/int - inspired by PR c/47781.
     (Note: the original bug used %b, but %b/%B are now C23 for binary)  */
  printf ("bool: %T\n", foo);          /* No warning - %T accepts int.  */
  printf ("bools: %T %T\n", foo, bar); /* No warning.  */
  printf (
      "%T\n",
      str); /* { dg-warning "format '%T' expects argument of type 'int'" } */
}

void
test_printf_Q (struct quad *q, int i)
{
  /* Test %Q added to printf - should accept a pointer.  */
  printf ("%Q\n", q); /* No warning expected.  */
  printf ("%Q\n", i); /* { dg-warning "format '%Q' expects argument of type
                         'void \\*'" } */
  printf ("%Q\n");    /* { dg-warning "too few arguments" } */
}

void
test_custom_printf (int ival, long val, long long llval, const char *s)
{
  /* Test %V in custom_printf format.  */
  custom_print ("%V\n", ival);    /* No warning - %V accepts int.  */
  custom_print ("%lV\n", val);    /* No warning - %lV accepts long.  */
  custom_print ("%llV\n", llval); /* No warning - %llV accepts long long.  */
  custom_print ("%s\n", s);       /* No warning expected.  */
  custom_print ("%%\n");          /* No warning expected.  */

  /* These should warn.  */
  custom_print ("%V\n",
                val); /* { dg-warning "format '%V' expects argument of type
                         'int', but argument 2 has type 'long int'" } */
  custom_print ("%d\n",
                42); /* { dg-warning "unknown conversion type character" } */
  custom_print ("%lV\n"); /* { dg-warning "too few arguments" } */
}
