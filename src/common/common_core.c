#if COMPILER_GCC || COMPILER_CLANG
    internal u64 count_ones_u64(u64 x) {
        return (u64)__builtin_popcountll(x);
    }
    internal u64 count_trailing_zeros_u64(u64 x) {
        Assert(x != 0);
        return (u64)__builtin_ctzll(x);
    }
    internal u64 count_leading_zeros_u64(u64 x) {
        Assert(x != 0);
        return (u64)__builtin_clzll(x);
    }
#elif COMPILER_MSVC

    #include <intrin.h>
    
    internal u64 count_ones_u64(u64 x) {
        #if ARCH_X86
        return __popcnt((u32)(x)) + __popcnt((u32)(x >> 32));
        #else
        return __popcnt64(x);
        #endif
    }
    internal u64 count_trailing_zeros_u64(u64 x) {
        Assert(x != 0);
        unsigned long index;
        _BitScanForward64(&index, x);
        return (u64)index;
    }
    internal u64 count_leading_zeros_u64(u64 x) {
        Assert(x != 0);
        unsigned long index;
        _BitScanReverse64(&index, x);
        return 63 - (u64)index;
    }
#else
    #error Compiler not supported.
#endif