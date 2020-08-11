/*
 * Copyright (c) 2015-2018 Nicholas Fraser
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 *
 * Abstracts all platform-specific code from BJData. This contains
 * implementations of standard C functions when libc is not available,
 * as well as wrappers to library functions.
 */

#ifndef BJDATA_PLATFORM_H
#define BJDATA_PLATFORM_H 1



/* Pre-include checks */

#if defined(_MSC_VER) && _MSC_VER < 1800 && !defined(__cplusplus)
#error "In Visual Studio 2012 and earlier, BJData must be compiled as C++. Enable the /Tp compiler flag."
#endif

#if defined(WIN32) && defined(BJDATA_INTERNAL) && BJDATA_INTERNAL
#define _CRT_SECURE_NO_WARNINGS 1
#endif



/* Doxygen preprocs */
#if defined(BJDATA_DOXYGEN) && BJDATA_DOXYGEN
#define BJDATA_HAS_CONFIG 0
// We give these their default values of 0 here even though they are defined to
// 1 in the doxyfile. Doxygen will show this as the value in the docs, even
// though it ignores it when parsing the rest of the source. This is what we
// want, since we want the documentation to show these defaults but still
// generate documentation for the functions they add when they're on.
#define BJDATA_COMPATIBILITY 0
#define BJDATA_EXTENSIONS 0
#endif



/* Include the custom config file if enabled */

#if defined(BJDATA_HAS_CONFIG) && BJDATA_HAS_CONFIG
#include "bjd-config.h"
#endif

/*
 * Now that the optional config is included, we define the defaults
 * for any of the configuration options and other switches that aren't
 * yet defined.
 */
#if defined(BJDATA_DOXYGEN) && BJDATA_DOXYGEN
#include "bjd-defaults-doxygen.h"
#else
#include "bjd-defaults.h"
#endif

/*
 * All remaining configuration options that have not yet been set must
 * be defined here in order to support -Wundef.
 */
#ifndef BJDATA_DEBUG
#define BJDATA_DEBUG 0
#endif
#ifndef BJDATA_CUSTOM_BREAK
#define BJDATA_CUSTOM_BREAK 0
#endif
#ifndef BJDATA_READ_TRACKING
#define BJDATA_READ_TRACKING 0
#endif
#ifndef BJDATA_WRITE_TRACKING
#define BJDATA_WRITE_TRACKING 0
#endif
#ifndef BJDATA_EMIT_INLINE_DEFS
#define BJDATA_EMIT_INLINE_DEFS 0
#endif
#ifndef BJDATA_AMALGAMATED
#define BJDATA_AMALGAMATED 0
#endif
#ifndef BJDATA_RELEASE_VERSION
#define BJDATA_RELEASE_VERSION 0
#endif
#ifndef BJDATA_INTERNAL
#define BJDATA_INTERNAL 0
#endif
#ifndef BJDATA_NO_BUILTINS
#define BJDATA_NO_BUILTINS 0
#endif



/* System headers (based on configuration) */

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS 1
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

#if BJDATA_STDLIB
#include <string.h>
#include <stdlib.h>
#endif

#if BJDATA_STDIO
#include <stdio.h>
#include <errno.h>
#endif



/*
 * Header configuration
 */

#ifdef __cplusplus
    #define BJDATA_EXTERN_C_START extern "C" {
    #define BJDATA_EXTERN_C_END   }
#else
    #define BJDATA_EXTERN_C_START /* nothing */
    #define BJDATA_EXTERN_C_END   /* nothing */
#endif

/* We can't push/pop diagnostics before GCC 4.6, so if you're on a really old
 * compiler, BJData does not support the below warning flags. You will have to
 * manually disable them to use BJData. */

/* GCC versions before 5.1 warn about defining a C99 non-static inline function
 * before declaring it (see issue #20). Diagnostic push is not supported before
 * GCC 4.6. */
#if defined(__GNUC__) && !defined(__clang__)
    #if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ == 5 && __GNUC_MINOR__ < 1)
        #ifdef __cplusplus
            #define BJDATA_DECLARED_INLINE_WARNING_START \
                _Pragma ("GCC diagnostic push") \
                _Pragma ("GCC diagnostic ignored \"-Wmissing-declarations\"")
        #else
            #define BJDATA_DECLARED_INLINE_WARNING_START \
                _Pragma ("GCC diagnostic push") \
                _Pragma ("GCC diagnostic ignored \"-Wmissing-prototypes\"")
        #endif
        #define BJDATA_DECLARED_INLINE_WARNING_END \
            _Pragma ("GCC diagnostic pop")
    #endif
#endif
#ifndef BJDATA_DECLARED_INLINE_WARNING_START
    #define BJDATA_DECLARED_INLINE_WARNING_START /* nothing */
    #define BJDATA_DECLARED_INLINE_WARNING_END /* nothing */
#endif

/* GCC versions before 4.8 warn about shadowing a function with a variable that
 * isn't a function or function pointer (like "index"). Diagnostic push is not
 * supported before GCC 4.6. */
#if defined(__GNUC__) && !defined(__clang__)
    #if __GNUC__ == 4 && __GNUC_MINOR__ >= 6 && __GNUC_MINOR__ < 8
        #define BJDATA_WSHADOW_WARNING_START \
            _Pragma ("GCC diagnostic push") \
            _Pragma ("GCC diagnostic ignored \"-Wshadow\"")
        #define BJDATA_WSHADOW_WARNING_END \
            _Pragma ("GCC diagnostic pop")
    #endif
#endif
#ifndef BJDATA_WSHADOW_WARNING_START
    #define BJDATA_WSHADOW_WARNING_START /* nothing */
    #define BJDATA_WSHADOW_WARNING_END /* nothing */
#endif

#define BJDATA_HEADER_START \
    BJDATA_WSHADOW_WARNING_START \
    BJDATA_DECLARED_INLINE_WARNING_START

#define BJDATA_HEADER_END \
    BJDATA_DECLARED_INLINE_WARNING_END \
    BJDATA_WSHADOW_WARNING_END

BJDATA_HEADER_START
BJDATA_EXTERN_C_START



/* Miscellaneous helper macros */

#define BJDATA_UNUSED(var) ((void)(var))

#define BJDATA_STRINGIFY_IMPL(arg) #arg
#define BJDATA_STRINGIFY(arg) BJDATA_STRINGIFY_IMPL(arg)

// This is a workaround for MSVC's incorrect expansion of __VA_ARGS__. It
// treats __VA_ARGS__ as a single preprocessor token when passed in the
// argument list of another macro unless we use an outer wrapper to expand it
// lexically first. (For safety/consistency we use this in all variadic macros
// that don't ignore the variadic arguments regardless of whether __VA_ARGS__
// is passed to another macro.)
//     https://stackoverflow.com/a/32400131
#define BJDATA_EXPAND(x) x

// Extracts the first argument of a variadic macro list, where there might only
// be one argument.
#define BJDATA_EXTRACT_ARG0_IMPL(first, ...) first
#define BJDATA_EXTRACT_ARG0(...) BJDATA_EXPAND(BJDATA_EXTRACT_ARG0_IMPL( __VA_ARGS__ , ignored))

// Stringifies the first argument of a variadic macro list, where there might
// only be one argument.
#define BJDATA_STRINGIFY_ARG0_impl(first, ...) #first
#define BJDATA_STRINGIFY_ARG0(...) BJDATA_EXPAND(BJDATA_STRINGIFY_ARG0_impl( __VA_ARGS__ , ignored))



/*
 * Definition of inline macros.
 *
 * BJData does not use static inline in header files; only one non-inline definition
 * of each function should exist in the final build. This can reduce the binary size
 * in cases where the compiler cannot or chooses not to inline a function.
 * The addresses of functions should also compare equal across translation units
 * regardless of whether they are declared inline.
 *
 * The above requirements mean that the declaration and definition of non-trivial
 * inline functions must be separated so that the definitions will only
 * appear when necessary. In addition, three different linkage models need
 * to be supported:
 *
 *  - The C99 model, where a standalone function is emitted only if there is any
 *    `extern inline` or non-`inline` declaration (including the definition itself)
 *
 *  - The GNU model, where an `inline` definition emits a standalone function and an
 *    `extern inline` definition does not, regardless of other declarations
 *
 *  - The C++ model, where `inline` emits a standalone function with special
 *    (COMDAT) linkage
 *
 * The macros below wrap up everything above. All inline functions defined in header
 * files have a single non-inline definition emitted in the compilation of
 * bjd-platform.c. All inline declarations and definitions use the same BJDATA_INLINE
 * specification to simplify the rules on when standalone functions are emitted.
 * Inline functions in source files are defined BJDATA_STATIC_INLINE.
 *
 * Additional reading:
 *     http://www.greenend.org.uk/rjk/tech/inline.html
 */

#if defined(__cplusplus)
    // C++ rules
    // The linker will need COMDAT support to link C++ object files,
    // so we don't need to worry about emitting definitions from C++
    // translation units. If bjd-platform.c (or the amalgamation)
    // is compiled as C, its definition will be used, otherwise a
    // C++ definition will be used, and no other C files will emit
    // a definition.
    #define BJDATA_INLINE inline

#elif defined(_MSC_VER)
    // MSVC 2013 always uses COMDAT linkage, but it doesn't treat 'inline' as a
    // keyword in C99 mode. (This appears to be fixed in a later version of
    // MSVC but we don't bother detecting it.)
    #define BJDATA_INLINE __inline
    #define BJDATA_STATIC_INLINE static __inline

#elif defined(__GNUC__) && (defined(__GNUC_GNU_INLINE__) || \
        (!defined(__GNUC_STDC_INLINE__) && !defined(__GNUC_GNU_INLINE__)))
    // GNU rules
    #if BJDATA_EMIT_INLINE_DEFS
        #define BJDATA_INLINE inline
    #else
        #define BJDATA_INLINE extern inline
    #endif

#elif defined(__TINYC__)
    // tcc ignores the inline keyword, so we have to use static inline. We
    // issue a warning to make sure you are aware. You can define the below
    // macro to disable the warning. Hopefully this will be fixed soon:
    //     https://lists.nongnu.org/archive/html/tinycc-devel/2019-06/msg00000.html
    #ifndef BJDATA_DISABLE_TINYC_INLINE_WARNING
        #warning "Single-definition inline is not supported by tcc. All inlines will be static. Define BJDATA_DISABLE_TINYC_INLINE_WARNING to disable this warning."
    #endif
    #define BJDATA_INLINE static inline

#else
    // C99 rules
    #if BJDATA_EMIT_INLINE_DEFS
        #define BJDATA_INLINE extern inline
    #else
        #define BJDATA_INLINE inline
    #endif
#endif

#ifndef BJDATA_STATIC_INLINE
#define BJDATA_STATIC_INLINE static inline
#endif

#ifdef BJDATA_OPTIMIZE_FOR_SPEED
    #error "You should define BJDATA_OPTIMIZE_FOR_SIZE, not BJDATA_OPTIMIZE_FOR_SPEED."
#endif



/*
 * Prevent inlining
 *
 * When a function is only used once, it is almost always inlined
 * automatically. This can cause poor instruction cache usage because a
 * function that should rarely be called (such as buffer exhaustion handling)
 * will get inlined into the middle of a hot code path.
 */

#if !BJDATA_NO_BUILTINS
    #if defined(_MSC_VER)
        #define BJDATA_NOINLINE __declspec(noinline)
    #elif defined(__GNUC__) || defined(__clang__)
        #define BJDATA_NOINLINE __attribute__((noinline))
    #endif
#endif
#ifndef BJDATA_NOINLINE
    #define BJDATA_NOINLINE /* nothing */
#endif



/* Some compiler-specific keywords and builtins */

#if !BJDATA_NO_BUILTINS
    #if defined(__GNUC__) || defined(__clang__)
        #define BJDATA_UNREACHABLE __builtin_unreachable()
        #define BJDATA_NORETURN(fn) fn __attribute__((noreturn))
        #define BJDATA_RESTRICT __restrict__
    #elif defined(_MSC_VER)
        #define BJDATA_UNREACHABLE __assume(0)
        #define BJDATA_NORETURN(fn) __declspec(noreturn) fn
        #define BJDATA_RESTRICT __restrict
    #endif
#endif

#ifndef BJDATA_RESTRICT
#ifdef __cplusplus
#define BJDATA_RESTRICT /* nothing, unavailable in C++ */
#else
#define BJDATA_RESTRICT restrict /* required in C99 */
#endif
#endif

#ifndef BJDATA_UNREACHABLE
#define BJDATA_UNREACHABLE ((void)0)
#endif
#ifndef BJDATA_NORETURN
#define BJDATA_NORETURN(fn) fn
#endif



/*
 * Likely/unlikely
 *
 * These should only really be used when a branch is taken (or not taken) less
 * than 1/1000th of the time. Buffer flush checks when writing very small
 * elements are a good example.
 */

#if !BJDATA_NO_BUILTINS
    #if defined(__GNUC__) || defined(__clang__)
        #ifndef BJDATA_LIKELY
            #define BJDATA_LIKELY(x) __builtin_expect((x),1)
        #endif
        #ifndef BJDATA_UNLIKELY
            #define BJDATA_UNLIKELY(x) __builtin_expect((x),0)
        #endif
    #endif
#endif

#ifndef BJDATA_LIKELY
    #define BJDATA_LIKELY(x) (x)
#endif
#ifndef BJDATA_UNLIKELY
    #define BJDATA_UNLIKELY(x) (x)
#endif



/* Static assert */

#ifndef BJDATA_STATIC_ASSERT
    #if defined(__cplusplus)
        #if __cplusplus >= 201103L
            #define BJDATA_STATIC_ASSERT static_assert
        #endif
    #elif defined(__STDC_VERSION__)
        #if __STDC_VERSION__ >= 201112L
            #define BJDATA_STATIC_ASSERT _Static_assert
        #endif
    #endif
#endif

#if !BJDATA_NO_BUILTINS
    #ifndef BJDATA_STATIC_ASSERT
        #if defined(__has_feature)
            #if __has_feature(cxx_static_assert)
                #define BJDATA_STATIC_ASSERT static_assert
            #elif __has_feature(c_static_assert)
                #define BJDATA_STATIC_ASSERT _Static_assert
            #endif
        #endif
    #endif

    #ifndef BJDATA_STATIC_ASSERT
        #if defined(__GNUC__)
            /* Diagnostic push is not supported before GCC 4.6. */
            #if defined(__clang__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
                #ifndef __cplusplus
                    #if defined(__clang__) || __GNUC__ >= 5
                    #define BJDATA_IGNORE_PEDANTIC "GCC diagnostic ignored \"-Wpedantic\""
                    #else
                    #define BJDATA_IGNORE_PEDANTIC "GCC diagnostic ignored \"-pedantic\""
                    #endif
                    #define BJDATA_STATIC_ASSERT(expr, str) do { \
                        _Pragma ("GCC diagnostic push") \
                        _Pragma (BJDATA_IGNORE_PEDANTIC) \
                        _Pragma ("GCC diagnostic ignored \"-Wc++-compat\"") \
                        _Static_assert(expr, str); \
                        _Pragma ("GCC diagnostic pop") \
                    } while (0)
                #endif
            #endif
        #endif
    #endif

    #ifndef BJDATA_STATIC_ASSERT
        #ifdef _MSC_VER
            #if _MSC_VER >= 1600
                #define BJDATA_STATIC_ASSERT static_assert
            #endif
        #endif
    #endif
#endif

#ifndef BJDATA_STATIC_ASSERT
    #define BJDATA_STATIC_ASSERT(expr, str) (BJDATA_UNUSED(sizeof(char[1 - 2*!(expr)])))
#endif



/* _Generic */

#ifndef BJDATA_HAS_GENERIC
    #if defined(__clang__) && defined(__has_feature)
        // With Clang we can test for _Generic support directly
        // and ignore C/C++ version
        #if __has_feature(c_generic_selections)
            #define BJDATA_HAS_GENERIC 1
        #else
            #define BJDATA_HAS_GENERIC 0
        #endif
    #endif
#endif

#ifndef BJDATA_HAS_GENERIC
    #if defined(__STDC_VERSION__)
        #if __STDC_VERSION__ >= 201112L
            #if defined(__GNUC__) && !defined(__clang__)
                // GCC does not have full C11 support in GCC 4.7 and 4.8
                #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9)
                    #define BJDATA_HAS_GENERIC 1
                #endif
            #else
                // We hope other compilers aren't lying about C11/_Generic support
                #define BJDATA_HAS_GENERIC 1
            #endif
        #endif
    #endif
#endif

#ifndef BJDATA_HAS_GENERIC
    #define BJDATA_HAS_GENERIC 0
#endif



/*
 * Finite Math
 *
 * -ffinite-math-only, included in -ffast-math, breaks functions that
 * that check for non-finite real values such as isnan() and isinf().
 *
 * We should use this to trap errors when reading data that contains
 * non-finite reals. This isn't currently implemented.
 */

#ifndef BJDATA_FINITE_MATH
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#define BJDATA_FINITE_MATH 1
#endif
#endif

#ifndef BJDATA_FINITE_MATH
#define BJDATA_FINITE_MATH 0
#endif



/*
 * Endianness checks
 *
 * These define BJDATA_NHSWAP*() which swap network<->host byte
 * order when needed.
 *
 * We leave them undefined if we can't determine the endianness
 * at compile-time, in which case we fall back to bit-shifts.
 *
 * See the notes in bjd-common.h.
 */

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
    #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define BJDATA_NHSWAP16(x) (x)
        #define BJDATA_NHSWAP32(x) (x)
        #define BJDATA_NHSWAP64(x) (x)
    #elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

        #if !BJDATA_NO_BUILTINS
            #if defined(__clang__)
                #ifdef __has_builtin
                    // Unlike the GCC builtins, the bswap builtins in Clang
                    // significantly improve ARM performance.
                    #if __has_builtin(__builtin_bswap16)
                        #define BJDATA_NHSWAP16(x) __builtin_bswap16(x)
                    #endif
                    #if __has_builtin(__builtin_bswap32)
                        #define BJDATA_NHSWAP32(x) __builtin_bswap32(x)
                    #endif
                    #if __has_builtin(__builtin_bswap64)
                        #define BJDATA_NHSWAP64(x) __builtin_bswap64(x)
                    #endif
                #endif

            #elif defined(__GNUC__)

                // The GCC bswap builtins are apparently poorly optimized on older
                // versions of GCC, so we set a minimum version here just in case.
                //     http://hardwarebug.org/2010/01/14/beware-the-builtins/

                #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
                    #define BJDATA_NHSWAP64(x) __builtin_bswap64(x)
                #endif

                // __builtin_bswap16() was not implemented on all platforms
                // until GCC 4.8.0:
                //     https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52624
                //
                // The 16- and 32-bit versions in GCC significantly reduce performance
                // on ARM with little effect on code size so we don't use them.

            #endif
        #endif
    #endif

#elif defined(_MSC_VER) && defined(_WIN32) && !BJDATA_NO_BUILTINS

    // On Windows, we assume x86 and x86_64 are always little-endian.
    // We make no assumptions about ARM even though all current
    // Windows Phone devices are little-endian in case Microsoft's
    // compiler is ever used with a big-endian ARM device.

    #if defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64)
        #define BJDATA_NHSWAP16(x) _byteswap_ushort(x)
        #define BJDATA_NHSWAP32(x) _byteswap_ulong(x)
        #define BJDATA_NHSWAP64(x) _byteswap_uint64(x)
    #endif

#endif

#if defined(__FLOAT_WORD_ORDER__) && defined(__BYTE_ORDER__)

    // We check where possible that the float byte order matches the
    // integer byte order. This is extremely unlikely to fail, but
    // we check anyway just in case.
    //
    // (The static assert is placed in float/double encoders instead
    // of here because our static assert fallback doesn't work at
    // file scope)

    #define BJDATA_CHECK_FLOAT_ORDER() \
        BJDATA_STATIC_ASSERT(__FLOAT_WORD_ORDER__ == __BYTE_ORDER__, \
            "float byte order does not match int byte order! float/double " \
            "encoding is not properly implemented on this platform.")

#endif

#ifndef BJDATA_CHECK_FLOAT_ORDER
    #define BJDATA_CHECK_FLOAT_ORDER() /* nothing */
#endif


/*
 * Here we define bjd_assert() and bjd_break(). They both work like a normal
 * assertion function in debug mode, causing a trap or abort. However, on some platforms
 * you can safely resume execution from bjd_break(), whereas bjd_assert() is
 * always fatal.
 *
 * In release mode, bjd_assert() is converted to an assurance to the compiler
 * that the expression cannot be false (via e.g. __assume() or __builtin_unreachable())
 * to improve optimization where supported. There is thus no point in "safely" handling
 * the case of this being false. Writing bjd_assert(0) rarely makes sense (except
 * possibly as a default handler in a switch) since the compiler will throw away any
 * code after it. If at any time an bjd_assert() is not true, the behaviour is
 * undefined. This also means the expression is evaluated even in release.
 *
 * bjd_break() on the other hand is compiled to nothing in release. It is
 * used in situations where we want to highlight a programming error as early as
 * possible (in the debugger), but we still handle the situation safely if it
 * happens in release to avoid producing incorrect results (such as in
 * BJDATA_WRITE_TRACKING.) It does not take an expression to test because it
 * belongs in a safe-handling block after its failing condition has been tested.
 *
 * If stdio is available, we can add a format string describing the error, and
 * on some compilers we can declare it noreturn to get correct results from static
 * analysis tools. Note that the format string and arguments are not evaluated unless
 * the assertion is hit.
 *
 * Note that any arguments to bjd_assert() beyond the first are only evaluated
 * if the expression is false (and are never evaluated in release.)
 *
 * bjd_assert_fail() and bjd_break_hit() are defined separately
 * because assert is noreturn and break isn't. This distinction is very
 * important for static analysis tools to give correct results.
 */

#if BJDATA_DEBUG

    /**
     * @addtogroup config
     * @{
     */
    /**
     * @name Debug Functions
     */
    /**
     * Implement this and define @ref BJDATA_CUSTOM_ASSERT to use a custom
     * assertion function.
     *
     * This function should not return. If it does, BJData will @c abort().
     *
     * If you use C++, make sure you include @c bjd.h where you define
     * this to get the correct linkage (or define it <code>extern "C"</code>.)
     *
     * Asserts are only used when @ref BJDATA_DEBUG is enabled, and can be
     * triggered by bugs in BJData or bugs due to incorrect usage of BJData.
     */
    void bjd_assert_fail(const char* message);
    /**
     * @}
     */
    /**
     * @}
     */

    BJDATA_NORETURN(void bjd_assert_fail_wrapper(const char* message));
    #if BJDATA_STDIO
        BJDATA_NORETURN(void bjd_assert_fail_format(const char* format, ...));
        #define bjd_assert_fail_at(line, file, exprstr, format, ...) \
                BJDATA_EXPAND(bjd_assert_fail_format("bjd assertion failed at " file ":" #line "\n%s\n" format, exprstr, __VA_ARGS__))
    #else
        #define bjd_assert_fail_at(line, file, exprstr, format, ...) \
                bjd_assert_fail_wrapper("bjd assertion failed at " file ":" #line "\n" exprstr "\n")
    #endif

    #define bjd_assert_fail_pos(line, file, exprstr, expr, ...) \
            BJDATA_EXPAND(bjd_assert_fail_at(line, file, exprstr, __VA_ARGS__))

    // This contains a workaround to the pedantic C99 requirement of having at
    // least one argument to a variadic macro. The first argument is the
    // boolean expression, the optional second argument (if provided) must be a
    // literal format string, and any additional arguments are the format
    // argument list.
    //
    // Unfortunately this means macros are expanded in the expression before it
    // gets stringified. I haven't found a workaround to this.
    //
    // This adds two unused arguments to the format argument list when a
    // format string is provided, so this would complicate the use of
    // -Wformat and __attribute__((format)) on bjd_assert_fail_format() if we
    // ever bothered to implement it.
    #define bjd_assert(...) \
            BJDATA_EXPAND(((!(BJDATA_EXTRACT_ARG0(__VA_ARGS__))) ? \
                bjd_assert_fail_pos(__LINE__, __FILE__, BJDATA_STRINGIFY_ARG0(__VA_ARGS__) , __VA_ARGS__ , "", NULL) : \
                (void)0))

    void bjd_break_hit(const char* message);
    #if BJDATA_STDIO
        void bjd_break_hit_format(const char* format, ...);
        #define bjd_break_hit_at(line, file, ...) \
                BJDATA_EXPAND(bjd_break_hit_format("bjd breakpoint hit at " file ":" #line "\n" __VA_ARGS__))
    #else
        #define bjd_break_hit_at(line, file, ...) \
                bjd_break_hit("bjd breakpoint hit at " file ":" #line )
    #endif
    #define bjd_break_hit_pos(line, file, ...) BJDATA_EXPAND(bjd_break_hit_at(line, file, __VA_ARGS__))
    #define bjd_break(...) BJDATA_EXPAND(bjd_break_hit_pos(__LINE__, __FILE__, __VA_ARGS__))
#else
    #define bjd_assert(...) \
            (BJDATA_EXPAND((!(BJDATA_EXTRACT_ARG0(__VA_ARGS__))) ? \
                (BJDATA_UNREACHABLE, (void)0) : \
                (void)0))
    #define bjd_break(...) ((void)0)
#endif



/* Wrap some needed libc functions */

#if BJDATA_STDLIB
    #define bjd_memcmp memcmp
    #define bjd_memcpy memcpy
    #define bjd_memmove memmove
    #define bjd_memset memset
    #ifndef bjd_strlen
        #define bjd_strlen strlen
    #endif

    #if defined(BJDATA_UNIT_TESTS) && BJDATA_INTERNAL && defined(__GNUC__)
        // make sure we don't use the stdlib directly during development
        #undef memcmp
        #undef memcpy
        #undef memmove
        #undef memset
        #undef strlen
        #undef malloc
        #undef free
        #pragma GCC poison memcmp
        #pragma GCC poison memcpy
        #pragma GCC poison memmove
        #pragma GCC poison memset
        #pragma GCC poison strlen
        #pragma GCC poison malloc
        #pragma GCC poison free
    #endif

#elif defined(__GNUC__) && !BJDATA_NO_BUILTINS
    // there's not always a builtin memmove for GCC,
    // and we don't have a way to test for it
    #define bjd_memcmp __builtin_memcmp
    #define bjd_memcpy __builtin_memcpy
    #define bjd_memset __builtin_memset
    #define bjd_strlen __builtin_strlen

#elif defined(__clang__) && defined(__has_builtin) && !BJDATA_NO_BUILTINS
    #if __has_builtin(__builtin_memcmp)
    #define bjd_memcmp __builtin_memcmp
    #endif
    #if __has_builtin(__builtin_memcpy)
    #define bjd_memcpy __builtin_memcpy
    #endif
    #if __has_builtin(__builtin_memmove)
    #define bjd_memmove __builtin_memmove
    #endif
    #if __has_builtin(__builtin_memset)
    #define bjd_memset __builtin_memset
    #endif
    #if __has_builtin(__builtin_strlen)
    #define bjd_strlen __builtin_strlen
    #endif
#endif

#ifndef bjd_memcmp
int bjd_memcmp(const void* s1, const void* s2, size_t n);
#endif
#ifndef bjd_memcpy
void* bjd_memcpy(void* BJDATA_RESTRICT s1, const void* BJDATA_RESTRICT s2, size_t n);
#endif
#ifndef bjd_memmove
void* bjd_memmove(void* s1, const void* s2, size_t n);
#endif
#ifndef bjd_memset
void* bjd_memset(void* s, int c, size_t n);
#endif
#ifndef bjd_strlen
size_t bjd_strlen(const char* s);
#endif

#if BJDATA_STDIO
    #if defined(WIN32)
        #define bjd_snprintf _snprintf
    #else
        #define bjd_snprintf snprintf
    #endif
#endif



/* Debug logging */
#if 0
    #include <stdio.h>
    #define bjd_log(...) (BJDATA_EXPAND(printf(__VA_ARGS__)), fflush(stdout))
#else
    #define bjd_log(...) ((void)0)
#endif



/* Make sure our configuration makes sense */
#if defined(BJDATA_MALLOC) && !defined(BJDATA_FREE)
    #error "BJDATA_MALLOC requires BJDATA_FREE."
#endif
#if !defined(BJDATA_MALLOC) && defined(BJDATA_FREE)
    #error "BJDATA_FREE requires BJDATA_MALLOC."
#endif
#if BJDATA_READ_TRACKING && !defined(BJDATA_READER)
    #error "BJDATA_READ_TRACKING requires BJDATA_READER."
#endif
#if BJDATA_WRITE_TRACKING && !defined(BJDATA_WRITER)
    #error "BJDATA_WRITE_TRACKING requires BJDATA_WRITER."
#endif
#ifndef BJDATA_MALLOC
    #if BJDATA_STDIO
        #error "BJDATA_STDIO requires preprocessor definitions for BJDATA_MALLOC and BJDATA_FREE."
    #endif
    #if BJDATA_READ_TRACKING
        #error "BJDATA_READ_TRACKING requires preprocessor definitions for BJDATA_MALLOC and BJDATA_FREE."
    #endif
    #if BJDATA_WRITE_TRACKING
        #error "BJDATA_WRITE_TRACKING requires preprocessor definitions for BJDATA_MALLOC and BJDATA_FREE."
    #endif
#endif



/* Implement realloc if unavailable */
#ifdef BJDATA_MALLOC
    #ifdef BJDATA_REALLOC
        BJDATA_INLINE void* bjd_realloc(void* old_ptr, size_t used_size, size_t new_size) {
            BJDATA_UNUSED(used_size);
            return BJDATA_REALLOC(old_ptr, new_size);
        }
    #else
        void* bjd_realloc(void* old_ptr, size_t used_size, size_t new_size);
    #endif
#endif



/**
 * @}
 */

BJDATA_EXTERN_C_END
BJDATA_HEADER_END

#endif

