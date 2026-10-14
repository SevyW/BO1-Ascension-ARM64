// native_p32.h - force-included into every translation unit of a 64-bit native build (cmake/native.cmake), which
// compiles the 32-bit-pointer copy of the tree (tools/p32/p32tree.py). Defines what the rewriter inserts.
//
// This file is not rewritten: pointers written here are native 64-bit pointers.
#pragma once

// after a '*': that pointer object is 4 bytes, zero-extended when loaded (all engine memory is below 4 GB)
#define BO1_P32 __ptr32 __uptr

// BO1_NAT(f): the function pointer f as a native pointer. Around the callee of every indirect call: LLVM cannot
// select a call through a 32-bit function pointer ("Cannot select: X86ISD::CALL" / "AArch64ISD::CALL").
//   - The native type comes from binding *f to a function reference, which drops the 32-bit address space
//     (`__typeof__(*(f)) *` is the 32-bit pointer type again).
//   - The value goes through an integer and an optimization barrier: any cast the optimizer can see through is
//     folded back into a call on the 32-bit pointer.
// BO1_NATD(p): the same for a data pointer, for `delete`.
static inline __attribute__((always_inline)) unsigned long bo1_nat_bits(unsigned long bits)
{
    __asm__("" : "+r"(bits));
    return bits;
}
#ifdef __cplusplus
template <class R, class... A> auto bo1_nat_fn(R (&)(A...)) -> R (*)(A...);
template <class R, class... A> auto bo1_nat_fn(R (&)(A..., ...)) -> R (*)(A..., ...);
template <class T> T *bo1_nat_data(T *__ptr32 __uptr);
// (variadic: the wrapped text may hold template argument commas)
#define BO1_NAT(...) ((decltype(bo1_nat_fn(*(__VA_ARGS__))))bo1_nat_bits((unsigned long)(__VA_ARGS__)))
#define BO1_NATD(...) ((decltype(bo1_nat_data(__VA_ARGS__)))(unsigned long)(__VA_ARGS__))
#else
#define BO1_NAT(f) (f)
#define BO1_NATD(p) (p)
#endif

// BO1_NATP(p): a data pointer as a native pointer, whether p is a 32-bit pointer or already native. Around the
// operand of a base-to-derived cast.
#ifdef __cplusplus
template <class T> T *bo1_natp(T *__ptr32 __uptr);
template <class T> T *bo1_natp(T *);
#define BO1_NATP(...) ((decltype(bo1_natp(__VA_ARGS__)))(unsigned long)(__VA_ARGS__))
#else
#define BO1_NATP(p) (p)
#endif

// Around a signed 32-bit integer that is being cast to a pointer: zero-extend it (an address from 2 GB up kept in
// an `int` would sign-extend into the kernel's half of the address space).
#define BO1_U32(...) ((unsigned int)(__VA_ARGS__))

// Around an unsigned 32-bit array subscript or pointer offset: its value as a signed int, which is what 32-bit
// address arithmetic made of it (`array[index - 13]` with an unsigned index below 13).
#define BO1_IDX(...) ((int)(__VA_ARGS__))

// Around a pointer argument passed through `...`: all 8 bytes of it. The callee reads a native pointer (printf's
// %s, va_arg), and a 32-bit pointer value would be passed as 4 bytes with the rest of the slot undefined. The type
// does not matter to a variadic callee.
#define BO1_VARG(...) ((void *)(unsigned long)(__VA_ARGS__))

// Around every address in the initializer of a static-storage variable. LLVM cannot emit a constant that narrows an
// address ("unsupported expression in static initializer: addrspacecast"), so `const char *names[] = {"a", "b"}`
// with 32-bit elements has to be filled in at startup. bo1_addr_opaque lives in another translation unit
// (native_p32.cpp) so that neither the front end nor the optimizer folds the call back into a constant.
#ifdef __cplusplus
extern "C" void *bo1_addr_opaque(const void *address);
template <class T> inline T *bo1_addr(T *address)
{
    return (T *)bo1_addr_opaque((const void *)address);
}
#define BO1_ADDR(...) bo1_addr(__VA_ARGS__)
#else
#define BO1_ADDR(p) (p)
#endif

// ----- native_lowmem.c: everything the engine can point at stays below 4 GB -----
#ifdef __cplusplus
extern "C" {
#endif
void *bo1_lowmem_thread_stack(unsigned long size);
int bo1_lowmem_run(int (*function)(int, char **), int argc, char **argv, unsigned long stackSize);
int bo1_lowmem_is_low(const void *address);
#ifdef __cplusplus
}
#endif
