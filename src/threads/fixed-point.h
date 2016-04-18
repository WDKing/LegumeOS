#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* Conversions */
int convert_to_fp(int n);
int convert_to_int_round_zero(int x);
int convert_to_int_round_nearest(int x);

/* Addition */
int add_fp(int x, int y);
int add_fp_int(int x, int n);

/* Subtraction */
int subtract_fp(int x, int y);
int subtract_fp_int(int x, int n);

/* Multiplication */
int multiply_fp(int x, int y);
int multiply_fp_int(int x, int n);

/* Division */
int divide_fp(int x, int y);
int divide_fp_int(int x, int n);

/* fixed-point conversion value 
   f = 2^q, we are using f.q = 17.14 */
int f = 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 2;

/* Convert n to a fixed point */
int convert_to_fp(int n)
{
  return n * f;
}

/* Convert x to integer (rounding toward zero) */
int convert_to_int_round_zero(int x)
{
  return x / f;
}

/* Convert x to integer (rounding to nearest) */
int convert_to_int_round_nearest(int x)
{
  if( x >= 0 ) 
  {
    return ( x + f / 2 ) / f;
  }
  else 
  {
    return ( x - f / 2 ) / f;
  }
}

/* Add x + y */
int add_fp(int x, int y) 
{
  return x + y;
}

/* Subtract y from x */
int subtract_fp(int x, int y)
{
  return x - y;
}


/* Add x and n */
int add_fp_int(int x, int n)
{
  return x + n * f;
}


/* Subtract n from x */
int subtract_fp_int(int x, int n)
{
  return x - n * f;
}

/* Multiply x by y */
int multiply_fp(int x, int y)
{
  return ((int64_t)x) * y / f;;
}

/* Multiply x by n */
int multiply_fp_int(int x, int n)
{
  return x * n;
}

/* Divide x by y */
int divide_fp(int x, int y)
{
  return ((int64_t)x) * f / y;
} 

/* Divide x by n */
int divide_fp_int(int x, int n)
{
  return x / n;
}

#endif /* threads/fixed-point.h */
