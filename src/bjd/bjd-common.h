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
 * Defines types and functions shared by the BJData reader and writer.
 */

#ifndef BJDATA_COMMON_H
#define BJDATA_COMMON_H 1

#include "bjd-platform.h"

#ifndef BJDATA_PRINT_BYTE_COUNT
#define BJDATA_PRINT_BYTE_COUNT 12
#endif

BJDATA_HEADER_START
BJDATA_EXTERN_C_START



/**
 * @defgroup common Tags and Common Elements
 *
 * Contains types, constants and functions shared by both the encoding
 * and decoding portions of BJData.
 *
 * @{
 */

/* Version information */

#define BJDATA_VERSION_MAJOR 1  /**< The major version number of BJData. */
#define BJDATA_VERSION_MINOR 0  /**< The minor version number of BJData. */
#define BJDATA_VERSION_PATCH 0  /**< The patch version number of BJData. */

/** A number containing the version number of BJData for comparison purposes. */
#define BJDATA_VERSION ((BJDATA_VERSION_MAJOR * 10000) + \
        (BJDATA_VERSION_MINOR * 100) + BJDATA_VERSION_PATCH)

/** A macro to test for a minimum version of BJData. */
#define BJDATA_VERSION_AT_LEAST(major, minor, patch) \
        (BJDATA_VERSION >= (((major) * 10000) + ((minor) * 100) + (patch)))

/** @cond */
#if (BJDATA_VERSION_PATCH > 0)
#define BJDATA_VERSION_STRING_BASE \
        BJDATA_STRINGIFY(BJDATA_VERSION_MAJOR) "." \
        BJDATA_STRINGIFY(BJDATA_VERSION_MINOR) "." \
        BJDATA_STRINGIFY(BJDATA_VERSION_PATCH)
#else
#define BJDATA_VERSION_STRING_BASE \
        BJDATA_STRINGIFY(BJDATA_VERSION_MAJOR) "." \
        BJDATA_STRINGIFY(BJDATA_VERSION_MINOR)
#endif
/** @endcond */

/**
 * @def BJDATA_VERSION_STRING
 * @hideinitializer
 *
 * A string containing the BJData version.
 */
#if BJDATA_RELEASE_VERSION
#define BJDATA_VERSION_STRING BJDATA_VERSION_STRING_BASE
#else
#define BJDATA_VERSION_STRING BJDATA_VERSION_STRING_BASE "dev"
#endif

/**
 * @def BJDATA_LIBRARY_STRING
 * @hideinitializer
 *
 * A string describing BJData, containing the library name, version and debug mode.
 */
#if BJDATA_DEBUG
#define BJDATA_LIBRARY_STRING "BJData " BJDATA_VERSION_STRING "-debug"
#else
#define BJDATA_LIBRARY_STRING "BJData " BJDATA_VERSION_STRING
#endif

/** @cond */
/**
 * @def BJDATA_MAXIMUM_TAG_SIZE
 *
 * The maximum encoded size of a tag in bytes.
 */
#define BJDATA_MAXIMUM_TAG_SIZE 9
/** @endcond */

#if BJDATA_EXTENSIONS
/**
 * @def BJDATA_TIMESTAMP_NANOSECONDS_MAX
 *
 * The maximum value of nanoseconds for a timestamp.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 */
#define BJDATA_TIMESTAMP_NANOSECONDS_MAX 999999999
#endif



#if BJDATA_COMPATIBILITY
/**
 * Versions of the Binary JData format.
 *
 * A reader, writer, or tree can be configured to serialize in an older
 * version of the Binary JData spec. This is necessary to interface with
 * older Binary JData libraries that do not support new Binary JData features.
 *
 * @note This requires @ref BJDATA_COMPATIBILITY.
 */
typedef enum bjd_version_t {

    /**
     * Version 1.0/v4, supporting only the @c raw type without @c str8.
     */
    bjd_version_v4 = 4,

    /**
     * Version 2.0/v5, supporting the @c str8, @c bin and @c ext types.
     */
    bjd_version_v5 = 5,

    /**
     * The most recent supported version of Binary JData. This is the default.
     */
    bjd_version_current = bjd_version_v5,

} bjd_version_t;
#endif

/**
 * Error states for BJData objects.
 *
 * When a reader, writer, or tree is in an error state, all subsequent calls
 * are ignored and their return values are nil/zero. You should check whether
 * the source is in an error state before using such values.
 */
typedef enum bjd_error_t {
    bjd_ok = 0,        /**< No error. */
    bjd_error_io = 2,  /**< The reader or writer failed to fill or flush, or some other file or socket error occurred. */
    bjd_error_invalid, /**< The data read is not valid Binary JData. */
    bjd_error_unsupported, /**< The data read is not supported by this configuration of BJData. (See @ref BJDATA_EXTENSIONS.) */
    bjd_error_type,    /**< The type or value range did not match what was expected by the caller. */
    bjd_error_too_big, /**< A read or write was bigger than the maximum size allowed for that operation. */
    bjd_error_memory,  /**< An allocation failure occurred. */
    bjd_error_bug,     /**< The BJData API was used incorrectly. (This will always assert in debug mode.) */
    bjd_error_data,    /**< The contained data is not valid. */
    bjd_error_eof,     /**< The reader failed to read because of file or socket EOF */
} bjd_error_t;

/**
 * Converts an BJData error to a string. This function returns an empty
 * string when BJDATA_DEBUG is not set.
 */
const char* bjd_error_to_string(bjd_error_t error);

/**
 * Defines the type of a Binary JData tag.
 *
 * Note that extension types, both user defined and built-in, are represented
 * in tags as @ref bjd_type_ext. The value for an extension type is stored
 * separately.
 */
typedef enum bjd_type_t {
    bjd_type_missing = 0, /**< Special type indicating a missing optional value. */
    bjd_type_nil,         /**< A null value. */
    bjd_type_noop,        /**< A no-op value. */
    bjd_type_bool,        /**< A boolean (true or false.) */
    bjd_type_int,         /**< A 64-bit signed integer. */
    bjd_type_uint,        /**< A 64-bit unsigned integer. */
    bjd_type_float,       /**< A 32-bit IEEE 754 floating point number. */
    bjd_type_double,      /**< A 64-bit IEEE 754 floating point number. */
    bjd_type_str,         /**< A string. */
    bjd_type_huge,        /**< A chunk of binary data. */
    bjd_type_array,       /**< An array of Binary JData objects. */
    bjd_type_map,         /**< An ordered map of key/value pairs of Binary JData objects. */

    #if BJDATA_EXTENSIONS
    /**
     * A typed Binary JData extension object containing a chunk of binary data.
     *
     * @note This requires @ref BJDATA_EXTENSIONS.
     */
    bjd_type_ext,
    #endif
} bjd_type_t;

/**
 * Converts an BJData type to a string. This function returns an empty
 * string when BJDATA_DEBUG is not set.
 */
const char* bjd_type_to_string(bjd_type_t type);

#if BJDATA_EXTENSIONS
/**
 * A timestamp.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 */
typedef struct bjd_timestamp_t {
    int64_t seconds; /*< The number of seconds (signed) since 1970-01-01T00:00:00Z. */
    uint32_t nanoseconds; /*< The number of additional nanoseconds, between 0 and 999,999,999. */
} bjd_timestamp_t;
#endif

/**
 * An BJData tag is a Binary JData object header. It is a variant type
 * representing any kind of object, and includes the length of compound types
 * (e.g. map, array, string) or the value of non-compound types (e.g. boolean,
 * integer, float.)
 *
 * If the type is compound (str, bin, ext, array or map), the contained
 * elements or bytes are stored separately.
 *
 * This structure is opaque; its fields should not be accessed outside
 * of BJData.
 */
typedef struct bjd_tag_t bjd_tag_t;

/* Hide internals from documentation */
/** @cond */
struct bjd_tag_t {
    bjd_type_t type; /*< The type of value. */

    #if BJDATA_EXTENSIONS
    int8_t exttype; /*< The extension type if the type is @ref bjd_type_ext. */
    #endif

    /* The value for non-compound types. */
    union {
        uint64_t u; /*< The value if the type is unsigned int. */
        int64_t  i; /*< The value if the type is signed int. */
        double   d; /*< The value if the type is double. */
        float    f; /*< The value if the type is float. */
        bool     b; /*< The value if the type is bool. */

        /* The number of bytes if the type is str, bin or ext. */
        uint32_t l;

        /* The element count if the type is an array, or the number of
            key/value pairs if the type is map. */
        uint32_t n;
    } v;
};
/** @endcond */

/**
 * @name Tag Generators
 * @{
 */

/**
 * @def BJDATA_TAG_ZERO
 *
 * An @ref bjd_tag_t initializer that zeroes the given tag.
 *
 * @warning This does not make the tag nil! The tag's type is invalid when
 * initialized this way. Use @ref bjd_tag_make_nil() to generate a nil tag.
 */
#if BJDATA_EXTENSIONS
#define BJDATA_TAG_ZERO {(bjd_type_t)0, 0, {0}}
#else
#define BJDATA_TAG_ZERO {(bjd_type_t)0, {0}}
#endif

/** Generates a nil tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_nil(void) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_nil;
    return ret;
}


/** Generates a nil tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_noop(void) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_noop;
    return ret;
}

/** Generates a bool tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_bool(bool value) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_bool;
    ret.v.b = value;
    return ret;
}

/** Generates a bool tag with value true. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_true(void) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_bool;
    ret.v.b = true;
    return ret;
}

/** Generates a bool tag with value false. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_false(void) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_bool;
    ret.v.b = false;
    return ret;
}

/** Generates a signed int tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_int(int64_t value) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_int;
    ret.v.i = value;
    return ret;
}

/** Generates an unsigned int tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_uint(uint64_t value) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_uint;
    ret.v.u = value;
    return ret;
}

/** Generates a float tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_float(float value) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_float;
    ret.v.f = value;
    return ret;
}

/** Generates a double tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_double(double value) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_double;
    ret.v.d = value;
    return ret;
}

/** Generates an array tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_array(uint32_t count) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_array;
    ret.v.n = count;
    return ret;
}

/** Generates a map tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_map(uint32_t count) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_map;
    ret.v.n = count;
    return ret;
}

/** Generates a str tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_str(uint32_t length) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_str;
    ret.v.l = length;
    return ret;
}

/** Generates a bin tag. */
BJDATA_INLINE bjd_tag_t bjd_tag_make_huge(uint32_t length) {
    bjd_tag_t ret = BJDATA_TAG_ZERO;
    ret.type = bjd_type_huge;
    ret.v.l = length;
    return ret;
}

/**
 * @}
 */

/**
 * @name Tag Querying Functions
 * @{
 */

/**
 * Gets the type of a tag.
 */
BJDATA_INLINE bjd_type_t bjd_tag_type(bjd_tag_t* tag) {
    return tag->type;
}

/**
 * Gets the boolean value of a bool-type tag. The tag must be of type @ref
 * bjd_type_bool.
 *
 * This asserts that the type in the tag is @ref bjd_type_bool. (No check is
 * performed if BJDATA_DEBUG is not set.)
 */
BJDATA_INLINE bool bjd_tag_bool_value(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_bool, "tag is not a bool!");
    return tag->v.b;
}

/**
 * Gets the signed integer value of an int-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_int. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @warning This does not convert between signed and unsigned tags! A positive
 * integer may be stored in a tag as either @ref bjd_type_int or @ref
 * bjd_type_uint. You must check the type first; this can only be used if the
 * type is @ref bjd_type_int.
 *
 * @see bjd_type_int
 */
BJDATA_INLINE int64_t bjd_tag_int_value(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_int, "tag is not an int!");
    return tag->v.i;
}

/**
 * Gets the unsigned integer value of a uint-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_uint. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @warning This does not convert between signed and unsigned tags! A positive
 * integer may be stored in a tag as either @ref bjd_type_int or @ref
 * bjd_type_uint. You must check the type first; this can only be used if the
 * type is @ref bjd_type_uint.
 *
 * @see bjd_type_uint
 */
BJDATA_INLINE uint64_t bjd_tag_uint_value(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_uint, "tag is not a uint!");
    return tag->v.u;
}

/**
 * Gets the float value of a float-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_float. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @warning This does not convert between float and double tags! This can only
 * be used if the type is @ref bjd_type_float.
 *
 * @see bjd_type_float
 */
BJDATA_INLINE float bjd_tag_float_value(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_float, "tag is not a float!");
    return tag->v.f;
}

/**
 * Gets the double value of a double-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_double. (No check
 * is performed if BJDATA_DEBUG is not set.)
 *
 * @warning This does not convert between float and double tags! This can only
 * be used if the type is @ref bjd_type_double.
 *
 * @see bjd_type_double
 */
BJDATA_INLINE double bjd_tag_double_value(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_double, "tag is not a double!");
    return tag->v.d;
}

/**
 * Gets the number of elements in an array tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_array. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @see bjd_type_array
 */
BJDATA_INLINE uint32_t bjd_tag_array_count(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_array, "tag is not an array!");
    return tag->v.n;
}

/**
 * Gets the number of key-value pairs in a map tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_map. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @see bjd_type_map
 */
BJDATA_INLINE uint32_t bjd_tag_map_count(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_map, "tag is not a map!");
    return tag->v.n;
}

/**
 * Gets the length in bytes of a str-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_str. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @see bjd_type_str
 */
BJDATA_INLINE uint32_t bjd_tag_str_length(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_str, "tag is not a str!");
    return tag->v.l;
}

/**
 * Gets the length in bytes of a bin-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_huge. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @see bjd_type_huge
 */
BJDATA_INLINE uint32_t bjd_tag_bin_length(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_huge, "tag is not a bin!");
    return tag->v.l;
}

#if BJDATA_EXTENSIONS
/**
 * Gets the length in bytes of an ext-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_ext. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 *
 * @see bjd_type_ext
 */
BJDATA_INLINE uint32_t bjd_tag_ext_length(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_ext, "tag is not an ext!");
    return tag->v.l;
}

/**
 * Gets the extension type (exttype) of an ext-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_ext. (No check is
 * performed if BJDATA_DEBUG is not set.)
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 *
 * @see bjd_type_ext
 */
BJDATA_INLINE int8_t bjd_tag_ext_exttype(bjd_tag_t* tag) {
    bjd_assert(tag->type == bjd_type_ext, "tag is not an ext!");
    return tag->exttype;
}
#endif

/**
 * Gets the length in bytes of a str-, bin- or ext-type tag.
 *
 * This asserts that the type in the tag is @ref bjd_type_str, @ref
 * bjd_type_huge or @ref bjd_type_ext. (No check is performed if BJDATA_DEBUG
 * is not set.)
 *
 * @see bjd_type_str
 * @see bjd_type_huge
 * @see bjd_type_ext
 */
BJDATA_INLINE uint32_t bjd_tag_bytes(bjd_tag_t* tag) {
    #if BJDATA_EXTENSIONS
    bjd_assert(tag->type == bjd_type_str || tag->type == bjd_type_huge
            || tag->type == bjd_type_ext, "tag is not a str, bin or ext!");
    #else
    bjd_assert(tag->type == bjd_type_str || tag->type == bjd_type_huge,
            "tag is not a str or bin!");
    #endif
    return tag->v.l;
}

/**
 * @}
 */

/**
 * @name Other tag functions
 * @{
 */

#if BJDATA_EXTENSIONS
/**
 * The extension type for a timestamp.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 */
#define BJDATA_EXTTYPE_TIMESTAMP ((int8_t)(-1))
#endif

/**
 * Compares two tags with an arbitrary fixed ordering. Returns 0 if the tags are
 * equal, a negative integer if left comes before right, or a positive integer
 * otherwise.
 *
 * \warning The ordering is not guaranteed to be preserved across BJData versions; do
 * not rely on it in persistent data.
 *
 * \warning Floating point numbers are compared bit-for-bit, not using the language's
 * operator==. This means that NaNs with matching representation will compare equal.
 * This behaviour is up for debate; see comments in the definition of bjd_tag_cmp().
 *
 * See bjd_tag_equal() for more information on when tags are considered equal.
 */
int bjd_tag_cmp(bjd_tag_t left, bjd_tag_t right);

/**
 * Compares two tags for equality. Tags are considered equal if the types are compatible
 * and the values (for non-compound types) are equal.
 *
 * The field width of variable-width fields is ignored (and in fact is not stored
 * in a tag), and positive numbers in signed integers are considered equal to their
 * unsigned counterparts. So for example the value 1 stored as a positive fixint
 * is equal to the value 1 stored in a 64-bit unsigned integer field.
 *
 * The "extension type" of an extension object is considered part of the value
 * and must match exactly.
 *
 * \warning Floating point numbers are compared bit-for-bit, not using the language's
 * operator==. This means that NaNs with matching representation will compare equal.
 * This behaviour is up for debate; see comments in the definition of bjd_tag_cmp().
 */
BJDATA_INLINE bool bjd_tag_equal(bjd_tag_t left, bjd_tag_t right) {
    return bjd_tag_cmp(left, right) == 0;
}

#if BJDATA_DEBUG && BJDATA_STDIO
/**
 * Generates a json-like debug description of the given tag into the given buffer.
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 *
 * The prefix is used to print the first few hexadecimal bytes of a bin or ext
 * type. Pass NULL if not a bin or ext.
 */
void bjd_tag_debug_pseudo_json(bjd_tag_t tag, char* buffer, size_t buffer_size,
        const char* prefix, size_t prefix_size);

/**
 * Generates a debug string description of the given tag into the given buffer.
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
void bjd_tag_debug_describe(bjd_tag_t tag, char* buffer, size_t buffer_size);

/** @cond */

/*
 * A callback function for printing pseudo-JSON for debugging purposes.
 *
 * @see bjd_node_print_callback
 */
typedef void (*bjd_print_callback_t)(void* context, const char* data, size_t count);

// helpers for printing debug output
// i feel a bit like i'm re-implementing a buffered writer again...
typedef struct bjd_print_t {
    char* buffer;
    size_t size;
    size_t count;
    bjd_print_callback_t callback;
    void* context;
} bjd_print_t;

void bjd_print_append(bjd_print_t* print, const char* data, size_t count);

BJDATA_INLINE void bjd_print_append_cstr(bjd_print_t* print, const char* cstr) {
    bjd_print_append(print, cstr, bjd_strlen(cstr));
}

void bjd_print_flush(bjd_print_t* print);

void bjd_print_file_callback(void* context, const char* data, size_t count);

/** @endcond */

#endif

/**
 * @}
 */

/**
 * @name Deprecated Tag Generators
 * @{
 */

/*
 * "make" has been added to their names to disambiguate them from the
 * value-fetching functions (e.g. bjd_tag_make_bool() vs
 * bjd_tag_bool_value().)
 *
 * The length and count for all compound types was the wrong sign (int32_t
 * instead of uint32_t.) These preserve the old behaviour; the new "make"
 * functions have the correct sign.
 */

/** \deprecated Renamed to bjd_tag_make_nil(). */
BJDATA_INLINE bjd_tag_t bjd_tag_nil(void) {
    return bjd_tag_make_nil();
}

/** \deprecated Renamed to bjd_tag_make_bool(). */
BJDATA_INLINE bjd_tag_t bjd_tag_noop(bool value) {
    return bjd_tag_make_noop(value);
}

/** \deprecated Renamed to bjd_tag_make_bool(). */
BJDATA_INLINE bjd_tag_t bjd_tag_bool(bool value) {
    return bjd_tag_make_bool(value);
}

/** \deprecated Renamed to bjd_tag_make_true(). */
BJDATA_INLINE bjd_tag_t bjd_tag_true(void) {
    return bjd_tag_make_true();
}

/** \deprecated Renamed to bjd_tag_make_false(). */
BJDATA_INLINE bjd_tag_t bjd_tag_false(void) {
    return bjd_tag_make_false();
}

/** \deprecated Renamed to bjd_tag_make_int(). */
BJDATA_INLINE bjd_tag_t bjd_tag_int(int64_t value) {
    return bjd_tag_make_int(value);
}

/** \deprecated Renamed to bjd_tag_make_uint(). */
BJDATA_INLINE bjd_tag_t bjd_tag_uint(uint64_t value) {
    return bjd_tag_make_uint(value);
}

/** \deprecated Renamed to bjd_tag_make_float(). */
BJDATA_INLINE bjd_tag_t bjd_tag_float(float value) {
    return bjd_tag_make_float(value);
}

/** \deprecated Renamed to bjd_tag_make_double(). */
BJDATA_INLINE bjd_tag_t bjd_tag_double(double value) {
    return bjd_tag_make_double(value);
}

/** \deprecated Renamed to bjd_tag_make_array(). */
BJDATA_INLINE bjd_tag_t bjd_tag_array(int32_t count) {
    return bjd_tag_make_array((uint32_t)count);
}

/** \deprecated Renamed to bjd_tag_make_map(). */
BJDATA_INLINE bjd_tag_t bjd_tag_map(int32_t count) {
    return bjd_tag_make_map((uint32_t)count);
}

/** \deprecated Renamed to bjd_tag_make_str(). */
BJDATA_INLINE bjd_tag_t bjd_tag_str(int32_t length) {
    return bjd_tag_make_str((uint32_t)length);
}

/** \deprecated Renamed to bjd_tag_make_bin(). */
BJDATA_INLINE bjd_tag_t bjd_tag_bin(int32_t length) {
    return bjd_tag_make_bin((uint32_t)length);
}

#if BJDATA_EXTENSIONS
/** \deprecated Renamed to bjd_tag_make_ext(). */
BJDATA_INLINE bjd_tag_t bjd_tag_ext(int8_t exttype, int32_t length) {
    return bjd_tag_make_ext(exttype, (uint32_t)length);
}
#endif

/**
 * @}
 */

/** @cond */

/*
 * Helpers to perform unaligned network-endian loads and stores
 * at arbitrary addresses. Byte-swapping builtins are used if they
 * are available and if they improve performance.
 *
 * These will remain available in the public API so feel free to
 * use them for other purposes, but they are undocumented.
 */

BJDATA_INLINE uint64_t bjd_load_uint(const char* p) {
    switch(p[0]){
	case 'i':
	    return bjd_load_i8(p+1);
	    break;
	case 'U':
	    return bjd_load_u8(p+1);
	    break;
	case 'I':
	    return bjd_load_i16(p+1);
	    break;
	case 'u':
	    return bjd_load_u16(p+1);
	    break;
	case 'l':
	    return bjd_load_i32(p+1);
	    break;
	case 'm':
	    return bjd_load_u32(p+1);
	    break;
	case 'L':
	    return bjd_load_i64(p+1);
	    break;
	case 'M':
	    return bjd_load_u64(p+1);
	    break;
    }
}

BJDATA_INLINE uint8_t bjd_load_u8(const char* p) {
    return (uint8_t)p[0];
}

BJDATA_INLINE uint16_t bjd_load_u16(const char* p) {
    #ifdef BJDATA_NHSWAP16
    uint16_t val;
    bjd_memcpy(&val, p, sizeof(val));
    return BJDATA_NHSWAP16(val);
    #else
    return (uint16_t)((((uint16_t)(uint8_t)p[0]) << 8) |
           ((uint16_t)(uint8_t)p[1]));
    #endif
}

BJDATA_INLINE uint32_t bjd_load_u32(const char* p) {
    #ifdef BJDATA_NHSWAP32
    uint32_t val;
    bjd_memcpy(&val, p, sizeof(val));
    return BJDATA_NHSWAP32(val);
    #else
    return (((uint32_t)(uint8_t)p[0]) << 24) |
           (((uint32_t)(uint8_t)p[1]) << 16) |
           (((uint32_t)(uint8_t)p[2]) <<  8) |
            ((uint32_t)(uint8_t)p[3]);
    #endif
}

BJDATA_INLINE uint64_t bjd_load_u64(const char* p) {
    #ifdef BJDATA_NHSWAP64
    uint64_t val;
    bjd_memcpy(&val, p, sizeof(val));
    return BJDATA_NHSWAP64(val);
    #else
    return (((uint64_t)(uint8_t)p[0]) << 56) |
           (((uint64_t)(uint8_t)p[1]) << 48) |
           (((uint64_t)(uint8_t)p[2]) << 40) |
           (((uint64_t)(uint8_t)p[3]) << 32) |
           (((uint64_t)(uint8_t)p[4]) << 24) |
           (((uint64_t)(uint8_t)p[5]) << 16) |
           (((uint64_t)(uint8_t)p[6]) <<  8) |
            ((uint64_t)(uint8_t)p[7]);
    #endif
}

BJDATA_INLINE void bjd_store_u8(char* p, uint8_t val) {
    uint8_t* u = (uint8_t*)p;
    u[0] = val;
}

BJDATA_INLINE void bjd_store_u16(char* p, uint16_t val) {
    #ifdef BJDATA_NHSWAP16
    val = BJDATA_NHSWAP16(val);
    bjd_memcpy(p, &val, sizeof(val));
    #else
    uint8_t* u = (uint8_t*)p;
    u[0] = (uint8_t)((val >> 8) & 0xFF);
    u[1] = (uint8_t)( val       & 0xFF);
    #endif
}

BJDATA_INLINE void bjd_store_u32(char* p, uint32_t val) {
    #ifdef BJDATA_NHSWAP32
    val = BJDATA_NHSWAP32(val);
    bjd_memcpy(p, &val, sizeof(val));
    #else
    uint8_t* u = (uint8_t*)p;
    u[0] = (uint8_t)((val >> 24) & 0xFF);
    u[1] = (uint8_t)((val >> 16) & 0xFF);
    u[2] = (uint8_t)((val >>  8) & 0xFF);
    u[3] = (uint8_t)( val        & 0xFF);
    #endif
}

BJDATA_INLINE void bjd_store_u64(char* p, uint64_t val) {
    #ifdef BJDATA_NHSWAP64
    val = BJDATA_NHSWAP64(val);
    bjd_memcpy(p, &val, sizeof(val));
    #else
    uint8_t* u = (uint8_t*)p;
    u[0] = (uint8_t)((val >> 56) & 0xFF);
    u[1] = (uint8_t)((val >> 48) & 0xFF);
    u[2] = (uint8_t)((val >> 40) & 0xFF);
    u[3] = (uint8_t)((val >> 32) & 0xFF);
    u[4] = (uint8_t)((val >> 24) & 0xFF);
    u[5] = (uint8_t)((val >> 16) & 0xFF);
    u[6] = (uint8_t)((val >>  8) & 0xFF);
    u[7] = (uint8_t)( val        & 0xFF);
    #endif
}

BJDATA_INLINE int64_t bjd_load_int(const char* p) {return (int64_t)bjd_load_uint(p);}
BJDATA_INLINE int8_t  bjd_load_i8 (const char* p) {return (int8_t) bjd_load_u8 (p);}
BJDATA_INLINE int16_t bjd_load_i16(const char* p) {return (int16_t)bjd_load_u16(p);}
BJDATA_INLINE int32_t bjd_load_i32(const char* p) {return (int32_t)bjd_load_u32(p);}
BJDATA_INLINE int64_t bjd_load_i64(const char* p) {return (int64_t)bjd_load_u64(p);}
BJDATA_INLINE void bjd_store_i8 (char* p, int8_t  val) {bjd_store_u8 (p, (uint8_t) val);}
BJDATA_INLINE void bjd_store_i16(char* p, int16_t val) {bjd_store_u16(p, (uint16_t)val);}
BJDATA_INLINE void bjd_store_i32(char* p, int32_t val) {bjd_store_u32(p, (uint32_t)val);}
BJDATA_INLINE void bjd_store_i64(char* p, int64_t val) {bjd_store_u64(p, (uint64_t)val);}

BJDATA_INLINE float bjd_load_float(const char* p) {
    BJDATA_CHECK_FLOAT_ORDER();
    BJDATA_STATIC_ASSERT(sizeof(float) == sizeof(uint32_t), "float is wrong size??");
    union {
        float f;
        uint32_t u;
    } v;
    v.u = bjd_load_u32(p);
    return v.f;
}

BJDATA_INLINE double bjd_load_double(const char* p) {
    BJDATA_CHECK_FLOAT_ORDER();
    BJDATA_STATIC_ASSERT(sizeof(double) == sizeof(uint64_t), "double is wrong size??");
    union {
        double d;
        uint64_t u;
    } v;
    v.u = bjd_load_u64(p);
    return v.d;
}

BJDATA_INLINE void bjd_store_float(char* p, float value) {
    BJDATA_CHECK_FLOAT_ORDER();
    union {
        float f;
        uint32_t u;
    } v;
    v.f = value;
    bjd_store_u32(p, v.u);
}

BJDATA_INLINE void bjd_store_double(char* p, double value) {
    BJDATA_CHECK_FLOAT_ORDER();
    union {
        double d;
        uint64_t u;
    } v;
    v.d = value;
    bjd_store_u64(p, v.u);
}

/** @endcond */



/** @cond */

// Sizes in bytes for the various possible tags
#define BJDATA_TAG_SIZE_U8       2
#define BJDATA_TAG_SIZE_U16      3
#define BJDATA_TAG_SIZE_U32      5
#define BJDATA_TAG_SIZE_U64      9
#define BJDATA_TAG_SIZE_I8       2
#define BJDATA_TAG_SIZE_I16      3
#define BJDATA_TAG_SIZE_I32      5
#define BJDATA_TAG_SIZE_I64      9
#define BJDATA_TAG_SIZE_FLOAT    5
#define BJDATA_TAG_SIZE_DOUBLE   9


/** @endcond */



#if BJDATA_READ_TRACKING || BJDATA_WRITE_TRACKING
/* Tracks the write state of compound elements (maps, arrays, */
/* strings, binary blobs and extension types) */
/** @cond */

typedef struct bjd_track_element_t {
    bjd_type_t type;
    uint32_t left;

    // indicates that a value still needs to be read/written for an already
    // read/written key. left is not decremented until both key and value are
    // read/written.
    bool key_needs_value;
} bjd_track_element_t;

typedef struct bjd_track_t {
    size_t count;
    size_t capacity;
    bjd_track_element_t* elements;
} bjd_track_t;

#if BJDATA_INTERNAL
bjd_error_t bjd_track_init(bjd_track_t* track);
bjd_error_t bjd_track_grow(bjd_track_t* track);
bjd_error_t bjd_track_push(bjd_track_t* track, bjd_type_t type, uint32_t count);
bjd_error_t bjd_track_pop(bjd_track_t* track, bjd_type_t type);
bjd_error_t bjd_track_element(bjd_track_t* track, bool read);
bjd_error_t bjd_track_peek_element(bjd_track_t* track, bool read);
bjd_error_t bjd_track_bytes(bjd_track_t* track, bool read, size_t count);
bjd_error_t bjd_track_str_bytes_all(bjd_track_t* track, bool read, size_t count);
bjd_error_t bjd_track_check_empty(bjd_track_t* track);
bjd_error_t bjd_track_destroy(bjd_track_t* track, bool cancel);
#endif

/** @endcond */
#endif



#if BJDATA_INTERNAL
/** @cond */



/* Miscellaneous string functions */

/**
 * Returns true if the given UTF-8 string is valid.
 */
bool bjd_utf8_check(const char* str, size_t bytes);

/**
 * Returns true if the given UTF-8 string is valid and contains no null characters.
 */
bool bjd_utf8_check_no_null(const char* str, size_t bytes);

/**
 * Returns true if the given string has no null bytes.
 */
bool bjd_str_check_no_null(const char* str, size_t bytes);



/** @endcond */
#endif



/**
 * @}
 */

/**
 * @}
 */

BJDATA_EXTERN_C_END
BJDATA_HEADER_END

#endif

