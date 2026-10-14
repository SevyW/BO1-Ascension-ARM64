// native_p32.cpp - run-time support for the 32-bit-pointer build (native_p32.h).

// Returns its argument. Deliberately out of line and in its own translation unit: static initializers that store
// addresses into 32-bit pointers call it, which keeps them dynamic (native_p32.h, BO1_ADDR).
extern "C" __attribute__((noinline)) void *bo1_addr_opaque(const void *address)
{
    return (void *)address;
}
