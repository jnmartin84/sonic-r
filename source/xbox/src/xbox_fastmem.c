/*
 * Fast memcpy / memset for the Xbox build.
 *
 * pdclib implements both as plain byte loops. They are on hot paths everywhere
 * (game buffers, SDL audio, texture uploads), so this object provides the two
 * symbols with dword string instructions; being linked as an object it takes
 * precedence over the pdclib archive members of the same name.
 *
 * Inline asm only: this file must never contain a C loop the compiler could
 * turn back into a call to memcpy/memset (built with -fno-builtin as well).
 */
#include <stddef.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    void *ret = dst;
    size_t dwords = n >> 2;
    size_t tail = n & 3;
    __asm__ __volatile__(
        "cld\n\t"
        "rep movsl\n\t"
        "movl %[tail], %%ecx\n\t"
        "rep movsb\n\t"
        : "+D"(dst), "+S"(src), "+c"(dwords)
        : [tail] "r"(tail)
        : "memory", "cc");
    return ret;
}

void *memset(void *dst, int c, size_t n)
{
    void *ret = dst;
    unsigned int v = (unsigned char)c;
    v |= v << 8;
    v |= v << 16;
    size_t dwords = n >> 2;
    size_t tail = n & 3;
    __asm__ __volatile__(
        "cld\n\t"
        "rep stosl\n\t"
        "movl %[tail], %%ecx\n\t"
        "rep stosb\n\t"
        : "+D"(dst), "+c"(dwords)
        : "a"(v), [tail] "r"(tail)
        : "memory", "cc");
    return ret;
}
