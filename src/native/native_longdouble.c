// native_longdouble.c - the C library's long double math functions, for a build where long double is double.
//
// cmake/native.cmake compiles the engine with -mlong-double-64: MSVC's long double is a double, and the decompiled
// code is full of `long double v3; cos(v3)` temporaries. The system libm's cosl / sinl / ... still take the
// platform's long double (80-bit x87 on x86, 128-bit on AArch64), so a call from this build passes the wrong bytes:
// Bullet_RandomDir got NaN from cos() and the first shot fired hit "!IS_NAN(right)". These definitions take the
// calls instead and forward to the double functions.
//
// Hidden visibility: they serve this executable's own objects only. Shared libraries (Mesa, SDL) were built with the
// platform's long double and must keep reaching libm's.
#include <math.h>

#if defined(BO1_NATIVE) && defined(__LDBL_MANT_DIG__) && __LDBL_MANT_DIG__ == 53

#define BO1_LD __attribute__((visibility("hidden"))) long double

BO1_LD sinl(long double x) { return sin((double)x); }
BO1_LD cosl(long double x) { return cos((double)x); }
BO1_LD tanl(long double x) { return tan((double)x); }
BO1_LD asinl(long double x) { return asin((double)x); }
BO1_LD acosl(long double x) { return acos((double)x); }
BO1_LD atanl(long double x) { return atan((double)x); }
BO1_LD sinhl(long double x) { return sinh((double)x); }
BO1_LD coshl(long double x) { return cosh((double)x); }
BO1_LD tanhl(long double x) { return tanh((double)x); }
BO1_LD asinhl(long double x) { return asinh((double)x); }
BO1_LD acoshl(long double x) { return acosh((double)x); }
BO1_LD atanhl(long double x) { return atanh((double)x); }
BO1_LD expl(long double x) { return exp((double)x); }
BO1_LD exp2l(long double x) { return exp2((double)x); }
BO1_LD expm1l(long double x) { return expm1((double)x); }
BO1_LD logl(long double x) { return log((double)x); }
BO1_LD log10l(long double x) { return log10((double)x); }
BO1_LD log2l(long double x) { return log2((double)x); }
BO1_LD log1pl(long double x) { return log1p((double)x); }
BO1_LD sqrtl(long double x) { return sqrt((double)x); }
BO1_LD cbrtl(long double x) { return cbrt((double)x); }
BO1_LD ceill(long double x) { return ceil((double)x); }
BO1_LD floorl(long double x) { return floor((double)x); }
BO1_LD fabsl(long double x) { return fabs((double)x); }
BO1_LD roundl(long double x) { return round((double)x); }
BO1_LD truncl(long double x) { return trunc((double)x); }
BO1_LD rintl(long double x) { return rint((double)x); }
BO1_LD nearbyintl(long double x) { return nearbyint((double)x); }
BO1_LD erfl(long double x) { return erf((double)x); }
BO1_LD erfcl(long double x) { return erfc((double)x); }
BO1_LD tgammal(long double x) { return tgamma((double)x); }
BO1_LD lgammal(long double x) { return lgamma((double)x); }
BO1_LD logbl(long double x) { return logb((double)x); }
BO1_LD atan2l(long double x, long double y) { return atan2((double)x, (double)y); }
BO1_LD powl(long double x, long double y) { return pow((double)x, (double)y); }
BO1_LD fmodl(long double x, long double y) { return fmod((double)x, (double)y); }
BO1_LD hypotl(long double x, long double y) { return hypot((double)x, (double)y); }
BO1_LD copysignl(long double x, long double y) { return copysign((double)x, (double)y); }
BO1_LD fmaxl(long double x, long double y) { return fmax((double)x, (double)y); }
BO1_LD fminl(long double x, long double y) { return fmin((double)x, (double)y); }
BO1_LD fdiml(long double x, long double y) { return fdim((double)x, (double)y); }
BO1_LD remainderl(long double x, long double y) { return remainder((double)x, (double)y); }
BO1_LD nextafterl(long double x, long double y) { return nextafter((double)x, (double)y); }
BO1_LD fmal(long double x, long double y, long double z) { return fma((double)x, (double)y, (double)z); }
BO1_LD ldexpl(long double x, int e) { return ldexp((double)x, e); }
BO1_LD scalbnl(long double x, int e) { return scalbn((double)x, e); }
BO1_LD scalblnl(long double x, long e) { return scalbln((double)x, e); }
BO1_LD frexpl(long double x, int *e) { return frexp((double)x, e); }
BO1_LD modfl(long double x, long double *ip) { double i; const double f = modf((double)x, &i); *ip = i; return f; }
BO1_LD remquol(long double x, long double y, int *q) { return remquo((double)x, (double)y, q); }
BO1_LD nexttowardl(long double x, long double y) { return nextafter((double)x, (double)y); }
__attribute__((visibility("hidden"))) long lroundl(long double x) { return lround((double)x); }
__attribute__((visibility("hidden"))) long long llroundl(long double x) { return llround((double)x); }
__attribute__((visibility("hidden"))) long lrintl(long double x) { return lrint((double)x); }
__attribute__((visibility("hidden"))) long long llrintl(long double x) { return llrint((double)x); }
__attribute__((visibility("hidden"))) int ilogbl(long double x) { return ilogb((double)x); }
// nexttoward's second argument is a long double in every precision
__attribute__((visibility("hidden"))) double nexttoward(double x, long double y) { return nextafter(x, (double)y); }
__attribute__((visibility("hidden"))) float nexttowardf(float x, long double y) { return nextafterf(x, (float)y); }

#endif
