#pragma once

#define M_PI        3.14159265358979323846
#define M_E         2.71828182845904523536
#define M_SQRT2     1.41421356237309504880
#define M_LN2       0.69314718055994530941
#define M_LN10      2.30258509299404568402
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define HUGE_VAL    (1.0/0.0)
#define NAN         (0.0/0.0)
#define INFINITY    (1.0/0.0)

#define isinf(x)   ((x) == INFINITY || (x) == -INFINITY)
#define isnan(x)   ((x) != (x))
#define isfinite(x) (!isinf(x) && !isnan(x))

double fabs(double x);
float fabsf(float x);

double floor(double x);

double ceil(double x);
double trunc(double x);
double round(double x);
double fmod(double x, double y);
double sqrt(double x);
double exp(double x);
double log(double x);

double log2(double x);
double log10(double x);

double pow(double base, double n);
double cbrt(double x);
double hypot(double x, double y);
double sin(double x);

double cos(double x);

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