#include <math.h>

double fabs(double x)
{
    return x < 0.0 ? -x : x;
}

float fabsf(float x)
{
    return x < 0.0f ? -x : x;
}

double floor(double x)
{
    if (isnan(x) || isinf(x)) return x;
    long long i = (long long)x;
    return (double)(i - (x < (double)i ? 1 : 0));
}

double ceil(double x)
{
    if (isnan(x) || isinf(x)) return x;
    long long i = (long long)x;
    return (double)(i + (x > (double)i ? 1 : 0));
}

double trunc(double x)
{
    return (double)(long long)x;
}

double round(double x)
{
    return floor(x + 0.5);
}

double fmod(double x, double y)
{
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x)) return NAN;
    double q = trunc(x / y);
    return x - q * y;
}

double sqrt(double x)
{
    if (x < 0.0)  return NAN;
    if (x == 0.0 || isinf(x)) return x;
    double r = x > 1.0 ? x / 2.0 : 1.0;
    for (int i = 0; i < 64; i++)
    {
        double r2 = (r + x / r) * 0.5;
        if (fabs(r2 - r) < 1e-15 * r) { r = r2; break; }
        r = r2;
    }
    return r;
}

double exp(double x)
{
    if (isnan(x)) return NAN;
    if (isinf(x)) return x > 0 ? INFINITY : 0.0;
    if (x > 709.78) return INFINITY;
    if (x < -745.13) return 0.0;

    int k = (int)round(x / M_LN2);
    double r = x - k * M_LN2;

    double term = 1.0, sum = 1.0;
    for (int n = 1; n <= 24; n++)
    {
        term *= r / n;
        sum  += term;
    }

    double scale = 1.0;
    int ak = k < 0 ? -k : k;
    for (int i = 0; i < ak; i++) scale *= 2.0;
    return k >= 0 ? sum * scale : sum / scale;
}

double ldexp(double x, int exp)
{
    if (isnan(x) || isinf(x) || x == 0.0) return x;
    double scale = 1.0;
    int ae = exp < 0 ? -exp : exp;
    for (int i = 0; i < ae; i++) scale *= 2.0;
    return exp >= 0 ? x * scale : x / scale;
}

double frexp(double x, int *exp)
{
    if (isnan(x) || isinf(x)) { *exp = 0; return x; }
    if (x == 0.0)             { *exp = 0; return 0.0; }

    *exp = 0;
    double m = x < 0.0 ? -x : x;

    while (m >= 1.0) { m *= 0.5; (*exp)++; }
    while (m <  0.5) { m *= 2.0; (*exp)--; }

    return x < 0.0 ? -m : m;
}

double log(double x)
{
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return INFINITY;

    int exp2 = 0;
    double m = x;
    while (m >= 2.0) { m /= 2.0; exp2++; }
    while (m <  1.0) { m *= 2.0; exp2--; }

    double y  = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double s  = y;
    double t  = y;
    for (int n = 3; n <= 53; n += 2) {
        t *= y2;
        s += t / n;
    }

    return 2.0 * s + exp2 * M_LN2;
}

double log2(double x)  { return log(x) / M_LN2;  }
double log10(double x) { return log(x) / M_LN10; }

double pow(double base, double n)
{
    if (n == 0.0) return 1.0;
    if (base == 1.0) return 1.0;
    if (isnan(base) || isnan(n)) return NAN;
    if (base < 0.0) {
        long long ie = (long long)n;
        if ((double)ie != n) return NAN;
        double r = exp(n * log(-base));
        return (ie % 2 != 0) ? -r : r;
    }
    return exp(n * log(base));
}

double cbrt(double x) {
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    int neg = x < 0;
    if (neg) x = -x;
    double r = exp(log(x) / 3.0);
    r = (2.0*r + x/(r*r)) / 3.0;
    return neg ? -r : r;
}

double hypot(double x, double y) {
    return sqrt(x*x + y*y);
}

double _sin_kernel(double x) {
    double x2 = x*x, t = x, s = x;
    for (int n = 3; n <= 35; n += 2) {
        t *= -x2 / ((n-1)*n);
        s += t;
    }
    return s;
}

double _cos_kernel(double x) {
    double x2 = x*x, t = 1.0, s = 1.0;
    for (int n = 2; n <= 34; n += 2) {
        t *= -x2 / ((n-1)*n);
        s += t;
    }
    return s;
}

double sin(double x) {
    if (isnan(x) || isinf(x)) return NAN;
    x = fmod(x, 2.0*M_PI);
    if (x < 0) x += 2.0*M_PI;
    int neg = 0;
    if (x > M_PI) { x -= M_PI; neg = 1; }
    if (x > M_PI/2.0) x = M_PI - x;
    double r = _sin_kernel(x);
    return neg ? -r : r;
}

double cos(double x) {
    return sin(x + M_PI/2.0);
}

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) return HUGE_VAL;
    return sin(x) / c;
}

double atan(double x) {
    if (isnan(x)) return NAN;
    if (isinf(x)) return x > 0 ? M_PI/2.0 : -M_PI/2.0;
    int neg = x < 0, recip = 0;
    if (neg) x = -x;
    if (x > 1.0) { x = 1.0/x; recip = 1; }
    double t = 0.57735026918962576451;
    double shifted = 0;
    int did_shift = 0;
    if (x > 0.26795) {
        shifted = (x - t) / (1.0 + t*x);
        did_shift = 1;
    } else {
        shifted = x;
    }
    double x2 = shifted*shifted, s = shifted, term = shifted;
    for (int n = 3; n <= 55; n += 2) {
        term *= -x2;
        s += term / n;
    }
    if (did_shift) s += M_PI/6.0;
    if (recip)     s = M_PI/2.0 - s;
    return neg ? -s : s;
}

double atan2(double y, double x) {
    if (x == 0.0) {
        if (y == 0.0) return 0.0;
        return y > 0 ? M_PI/2.0 : -M_PI/2.0;
    }
    double a = atan(y/x);
    if (x < 0.0) return y >= 0.0 ? a + M_PI : a - M_PI;
    return a;
}

double asin(double x) {
    if (fabs(x) > 1.0) return NAN;
    if (fabs(x) == 1.0) return x * M_PI/2.0;
    return atan(x / sqrt(1.0 - x*x));
}

double acos(double x) {
    if (fabs(x) > 1.0) return NAN;
    return M_PI/2.0 - asin(x);
}

double sinh(double x) {
    return (exp(x) - exp(-x)) * 0.5;
}
double cosh(double x) {
    return (exp(x) + exp(-x)) * 0.5;
}
double tanh(double x) {
    double e2 = exp(2.0*x);
    return (e2 - 1.0) / (e2 + 1.0);
}

double asinh(double x) { return log(x + sqrt(x*x + 1.0)); }
double acosh(double x) {
    if (x < 1.0) return NAN;
    return log(x + sqrt(x*x - 1.0));
}
double atanh(double x) {
    if (fabs(x) >= 1.0) return x > 0 ? INFINITY : -INFINITY;
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

double copysign(double x, double y) {
    return (y < 0.0 || (y == 0.0 && (1.0/y) < 0.0)) ? -fabs(x) : fabs(x);
}

double fmax(double a, double b) { return a > b ? a : b; }
double fmin(double a, double b) { return a < b ? a : b; }
double fdim(double a, double b) { return a > b ? a-b : 0.0; }

double fma(double x, double y, double z) { return x*y + z; }