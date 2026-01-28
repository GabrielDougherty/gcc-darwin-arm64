/* Test the custom format plugin.
   { dg-do compile }
   { dg-options "-Wformat" }
*/

extern "C" int printf (const char *, ...)
    __attribute__ ((format (printf, 1, 2)));

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
test_printf_V (int ival, long lval, long long llval, const char *s)
{
  /* Test %V with length modifiers added to printf.  */
  printf ("%V\n", ival);    /* No warning - %V accepts int.  */
  printf ("%lV\n", lval);   /* No warning - %lV accepts long.  */
  printf ("%llV\n", llval); /* No warning - %llV accepts long long.  */

  /* These should warn about type mismatches.  */
  printf ("%V\n",
          lval); /* { dg-warning "format '%V' expects argument of type 'int', but argument 2 has type 'long int'" } */
  printf ("%lV\n",
          ival); /* { dg-warning "format '%lV' expects argument of type 'long int', but argument 2 has type 'int'" } */
  printf ("%llV\n",
          lval); /* { dg-warning "format '%llV' expects argument of type 'long long int', but argument 2 has type 'long int'" } */

  /* Test invalid length modifier for %V.  */
  printf ("%hV\n", ival); /* { dg-warning "length" } */

  /* Test flags and width/precision.  */
  printf ("%+10V\n", ival);   /* No warning - + and width allowed.  */
  printf ("%010V\n", ival);   /* No warning - 0 and width allowed.  */
  printf ("%-10V\n", ival);   /* No warning - - and width allowed.  */
  printf ("%10.5V\n", ival);  /* No warning - width and precision allowed.  */
}
