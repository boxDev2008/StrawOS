#pragma once

#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308
#define M_SQRT1_2   0.70710678118654752440
#define M_E         2.71828182845904523536
#define M_SQRT2     1.41421356237309504880
#define M_LN2       0.69314718055994530941
#define M_LN10      2.30258509299404568402
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define HUGE_VAL    __builtin_huge_val()
#ifndef NAN
#define NAN         __builtin_nan("")
#endif
#ifndef INFINITY
#define INFINITY    __builtin_inff()
#endif

#define isinf(x)   ((x) == INFINITY || (x) == -INFINITY)
#define isnan(x)   ((x) != (x))
#define signbit(x) __builtin_signbit(x)
#define isfinite(x) (!isinf(x) && !isnan(x))

double fabs(double x);
float ceilf(float x);
float floorf(float x);
float roundf(float x);
float truncf(float x);
float fmodf(float x, float y);
float powf(float x, float y);
float expf(float x);
float logf(float x);
float tanf(float x);
float atanf(float x);
float atan2f(float y, float x);
float hypotf(float x, float y);
long lround(double x);
long lroundf(float x);
long lrint(double x);
double rint(double x);
double nearbyint(double x);
double scalbn(double x, int n);
double log1p(double x);
double expm1(double x);
float fabsf(float x);

double floor(double x);

double ceil(double x);
double trunc(double x);
double round(double x);
double fmod(double x, double y);
double modf(double x, double *ip);
double sqrt(double x);
double exp(double x);
double ldexp(double x, int exp);
long double ldexpl(long double x, int exp);
double frexp(double x, int *exp);
double log(double x);

double log2(double x);
double log10(double x);

double pow(double base, double n);
double cbrt(double x);
double hypot(double x, double y);
double sin(double x);

double cos(double x);

float sinf(float x);
float cosf(float x);
float sqrtf(float x);
float fminf(float a, float b);
float fmaxf(float a, float b);

double tan(double x);
double atan(double x);

double atan2(double y, double x);
double asin(double x);
double acos(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);

double asinh(double x);
double acosh(double x);
double atanh(double x);
double copysign(double x, double y);
double fmax(double a, double b);
double fmin(double a, double b);
double fdim(double a, double b);
double fma(double x, double y, double z);