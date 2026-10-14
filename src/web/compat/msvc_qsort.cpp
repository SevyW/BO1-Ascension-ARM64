// msvc_qsort.cpp - web: qsort with the result Microsoft's C runtime gives.
//
// qsort is not a stable sort and the standard does not say where equal elements end up; every C library has its own
// algorithm (MSVC: quicksort with a median of three and a selection sort below nine elements; glibc: merge sort or
// introsort; bionic and musl: others). The engine builds its network Huffman tree by sorting 256 symbol nodes by
// frequency (qcommon/huffman.cpp), and many frequencies are equal: with the host's qsort a Linux build, an Android
// build and the Windows build each got another tree and could not read each other's packets (first seen as
// "ASSERT msg_mp.cpp(2440)" on an Android client of a Linux server).
//
// This is the algorithm of the Visual C++ runtime's qsort (unchanged since Visual C++ 2005), so the order of equal
// elements is the Windows build's. Only what must match across machines uses it.
#include <stddef.h>

extern "C" void bo1_msvc_qsort(void *base, size_t count, size_t width, int (*compare)(const void *, const void *));

namespace
{
const size_t kCutoff = 8;

inline void Swap(char *a, char *b, size_t width)
{
    if (a != b)
    {
        while (width--)
        {
            const char t = *a;
            *a++ = *b;
            *b++ = t;
        }
    }
}

// selection sort: the largest of what is left goes to the end
void ShortSort(char *lo, char *hi, size_t width, int (*compare)(const void *, const void *))
{
    while (hi > lo)
    {
        char *max = lo;
        for (char *p = lo + width; p <= hi; p += width)
            if (compare(p, max) > 0)
                max = p;
        Swap(max, hi, width);
        hi -= width;
    }
}
}

extern "C" void bo1_msvc_qsort(void *base, size_t count, size_t width, int (*compare)(const void *, const void *))
{
    if (count < 2 || !width)
        return;
    char *loStack[8 * sizeof(void *) - 2], *hiStack[8 * sizeof(void *) - 2];
    int depth = 0;
    char *lo = (char *)base;
    char *hi = (char *)base + width * (count - 1);

recurse:
    const size_t size = (size_t)(hi - lo) / width + 1;
    if (size <= kCutoff)
    {
        ShortSort(lo, hi, width, compare);
    }
    else
    {
        char *mid = lo + (size / 2) * width;
        if (compare(lo, mid) > 0)
            Swap(lo, mid, width);
        if (compare(lo, hi) > 0)
            Swap(lo, hi, width);
        if (compare(mid, hi) > 0)
            Swap(mid, hi, width);
        char *loguy = lo;
        char *higuy = hi;
        for (;;)
        {
            if (mid > loguy)
            {
                do
                {
                    loguy += width;
                } while (loguy < mid && compare(loguy, mid) <= 0);
            }
            if (mid <= loguy)
            {
                do
                {
                    loguy += width;
                } while (loguy <= hi && compare(loguy, mid) <= 0);
            }
            do
            {
                higuy -= width;
            } while (higuy > mid && compare(higuy, mid) > 0);
            if (higuy < loguy)
                break;
            Swap(loguy, higuy, width);
            if (mid == higuy)
                mid = loguy;
        }
        higuy += width;
        if (mid < higuy)
        {
            do
            {
                higuy -= width;
            } while (higuy > mid && compare(higuy, mid) == 0);
        }
        if (mid >= higuy)
        {
            do
            {
                higuy -= width;
            } while (higuy > lo && compare(higuy, mid) == 0);
        }
        // the smaller part first, the larger one on the stack
        if (higuy - lo >= hi - loguy)
        {
            if (lo < higuy)
            {
                loStack[depth] = lo;
                hiStack[depth] = higuy;
                ++depth;
            }
            if (loguy < hi)
            {
                lo = loguy;
                goto recurse;
            }
        }
        else
        {
            if (loguy < hi)
            {
                loStack[depth] = loguy;
                hiStack[depth] = hi;
                ++depth;
            }
            if (lo < higuy)
            {
                hi = higuy;
                goto recurse;
            }
        }
    }
    if (--depth >= 0)
    {
        lo = loStack[depth];
        hi = hiStack[depth];
        goto recurse;
    }
}
