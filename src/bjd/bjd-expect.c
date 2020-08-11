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

#define BJDATA_INTERNAL 1

#include "bjd-expect.h"

#if BJDATA_EXPECT


// Helpers

BJDATA_STATIC_INLINE uint8_t bjd_expect_native_u8(bjd_reader_t* reader) {
    if (bjd_reader_error(reader) != bjd_ok)
        return 0;
    uint8_t type;
    if (!bjd_reader_ensure(reader, sizeof(type)))
        return 0;
    type = bjd_load_u8(reader->data);
    reader->data += sizeof(type);
    return type;
}

#if !BJDATA_OPTIMIZE_FOR_SIZE
BJDATA_STATIC_INLINE uint16_t bjd_expect_native_u16(bjd_reader_t* reader) {
    if (bjd_reader_error(reader) != bjd_ok)
        return 0;
    uint16_t type;
    if (!bjd_reader_ensure(reader, sizeof(type)))
        return 0;
    type = bjd_load_u16(reader->data);
    reader->data += sizeof(type);
    return type;
}

BJDATA_STATIC_INLINE uint32_t bjd_expect_native_u32(bjd_reader_t* reader) {
    if (bjd_reader_error(reader) != bjd_ok)
        return 0;
    uint32_t type;
    if (!bjd_reader_ensure(reader, sizeof(type)))
        return 0;
    type = bjd_load_u32(reader->data);
    reader->data += sizeof(type);
    return type;
}
#endif

BJDATA_STATIC_INLINE uint8_t bjd_expect_type_byte(bjd_reader_t* reader) {
    bjd_reader_track_element(reader);
    return bjd_expect_native_u8(reader);
}


// Basic Number Functions

uint8_t bjd_expect_u8(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= UINT8_MAX)
            return (uint8_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= 0 && var.v.i <= UINT8_MAX)
            return (uint8_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

uint16_t bjd_expect_u16(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= UINT16_MAX)
            return (uint16_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= 0 && var.v.i <= UINT16_MAX)
            return (uint16_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

uint32_t bjd_expect_u32(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= UINT32_MAX)
            return (uint32_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= 0 && var.v.i <= UINT32_MAX)
            return (uint32_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

uint64_t bjd_expect_u64(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        return var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= 0)
            return (uint64_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

int8_t bjd_expect_i8(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= INT8_MAX)
            return (int8_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= INT8_MIN && var.v.i <= INT8_MAX)
            return (int8_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

int16_t bjd_expect_i16(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= INT16_MAX)
            return (int16_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= INT16_MIN && var.v.i <= INT16_MAX)
            return (int16_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

int32_t bjd_expect_i32(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= INT32_MAX)
            return (int32_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        if (var.v.i >= INT32_MIN && var.v.i <= INT32_MAX)
            return (int32_t)var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

int64_t bjd_expect_i64(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint) {
        if (var.v.u <= INT64_MAX)
            return (int64_t)var.v.u;
    } else if (var.type == bjd_type_int) {
        return var.v.i;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

float bjd_expect_float(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint)
        return (float)var.v.u;
    else if (var.type == bjd_type_int)
        return (float)var.v.i;
    else if (var.type == bjd_type_float)
        return var.v.f;
    else if (var.type == bjd_type_double)
        return (float)var.v.d;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0.0f;
}

double bjd_expect_double(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_uint)
        return (double)var.v.u;
    else if (var.type == bjd_type_int)
        return (double)var.v.i;
    else if (var.type == bjd_type_float)
        return (double)var.v.f;
    else if (var.type == bjd_type_double)
        return var.v.d;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0.0;
}

float bjd_expect_float_strict(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_float)
        return var.v.f;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0.0f;
}

double bjd_expect_double_strict(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_float)
        return (double)var.v.f;
    else if (var.type == bjd_type_double)
        return var.v.d;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0.0;
}


// Ranged Number Functions
//
// All ranged functions are identical other than the type, so we
// define their content with a macro. The prototypes are still written
// out in full to support ctags/IDE tools.

#define BJDATA_EXPECT_RANGE_IMPL(name, type_t)                           \
                                                                        \
    /* make sure the range is sensible */                               \
    bjd_assert(min_value <= max_value,                                \
            "min_value %i must be less than or equal to max_value %i",  \
            min_value, max_value);                                      \
                                                                        \
    /* read the value */                                                \
    type_t val = bjd_expect_##name(reader);                           \
    if (bjd_reader_error(reader) != bjd_ok)                         \
        return min_value;                                               \
                                                                        \
    /* make sure it fits */                                             \
    if (val < min_value || val > max_value) {                           \
        bjd_reader_flag_error(reader, bjd_error_type);              \
        return min_value;                                               \
    }                                                                   \
                                                                        \
    return val;

uint8_t bjd_expect_u8_range(bjd_reader_t* reader, uint8_t min_value, uint8_t max_value) {BJDATA_EXPECT_RANGE_IMPL(u8, uint8_t)}
uint16_t bjd_expect_u16_range(bjd_reader_t* reader, uint16_t min_value, uint16_t max_value) {BJDATA_EXPECT_RANGE_IMPL(u16, uint16_t)}
uint32_t bjd_expect_u32_range(bjd_reader_t* reader, uint32_t min_value, uint32_t max_value) {BJDATA_EXPECT_RANGE_IMPL(u32, uint32_t)}
uint64_t bjd_expect_u64_range(bjd_reader_t* reader, uint64_t min_value, uint64_t max_value) {BJDATA_EXPECT_RANGE_IMPL(u64, uint64_t)}

int8_t bjd_expect_i8_range(bjd_reader_t* reader, int8_t min_value, int8_t max_value) {BJDATA_EXPECT_RANGE_IMPL(i8, int8_t)}
int16_t bjd_expect_i16_range(bjd_reader_t* reader, int16_t min_value, int16_t max_value) {BJDATA_EXPECT_RANGE_IMPL(i16, int16_t)}
int32_t bjd_expect_i32_range(bjd_reader_t* reader, int32_t min_value, int32_t max_value) {BJDATA_EXPECT_RANGE_IMPL(i32, int32_t)}
int64_t bjd_expect_i64_range(bjd_reader_t* reader, int64_t min_value, int64_t max_value) {BJDATA_EXPECT_RANGE_IMPL(i64, int64_t)}

float bjd_expect_float_range(bjd_reader_t* reader, float min_value, float max_value) {BJDATA_EXPECT_RANGE_IMPL(float, float)}
double bjd_expect_double_range(bjd_reader_t* reader, double min_value, double max_value) {BJDATA_EXPECT_RANGE_IMPL(double, double)}

uint32_t bjd_expect_map_range(bjd_reader_t* reader, uint32_t min_value, uint32_t max_value) {BJDATA_EXPECT_RANGE_IMPL(map, uint32_t)}
uint32_t bjd_expect_array_range(bjd_reader_t* reader, uint32_t min_value, uint32_t max_value) {BJDATA_EXPECT_RANGE_IMPL(array, uint32_t)}


// Matching Number Functions

void bjd_expect_uint_match(bjd_reader_t* reader, uint64_t value) {
    if (bjd_expect_u64(reader) != value)
        bjd_reader_flag_error(reader, bjd_error_type);
}

void bjd_expect_int_match(bjd_reader_t* reader, int64_t value) {
    if (bjd_expect_i64(reader) != value)
        bjd_reader_flag_error(reader, bjd_error_type);
}


// Other Basic Types

void bjd_expect_nil(bjd_reader_t* reader) {
    if (bjd_expect_type_byte(reader) != 0xc0)
        bjd_reader_flag_error(reader, bjd_error_type);
}

bool bjd_expect_bool(bjd_reader_t* reader) {
    uint8_t type = bjd_expect_type_byte(reader);
    if ((type & ~1) != 0xc2)
        bjd_reader_flag_error(reader, bjd_error_type);
    return (bool)(type & 1);
}

void bjd_expect_true(bjd_reader_t* reader) {
    if (bjd_expect_bool(reader) != true)
        bjd_reader_flag_error(reader, bjd_error_type);
}

void bjd_expect_false(bjd_reader_t* reader) {
    if (bjd_expect_bool(reader) != false)
        bjd_reader_flag_error(reader, bjd_error_type);
}

#if BJDATA_EXTENSIONS
bjd_timestamp_t bjd_expect_timestamp(bjd_reader_t* reader) {
    bjd_timestamp_t zero = {0, 0};

    bjd_tag_t tag = bjd_read_tag(reader);
    if (tag.type != bjd_type_ext) {
        bjd_reader_flag_error(reader, bjd_error_type);
        return zero;
    }
    if (bjd_tag_ext_exttype(&tag) != BJDATA_EXTTYPE_TIMESTAMP) {
        bjd_reader_flag_error(reader, bjd_error_type);
        return zero;
    }

    return bjd_read_timestamp(reader, bjd_tag_ext_length(&tag));
}

int64_t bjd_expect_timestamp_truncate(bjd_reader_t* reader) {
    return bjd_expect_timestamp(reader).seconds;
}
#endif


// Compound Types

uint32_t bjd_expect_map(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_map)
        return var.v.n;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

void bjd_expect_map_match(bjd_reader_t* reader, uint32_t count) {
    if (bjd_expect_map(reader) != count)
        bjd_reader_flag_error(reader, bjd_error_type);
}

bool bjd_expect_map_or_nil(bjd_reader_t* reader, uint32_t* count) {
    bjd_assert(count != NULL, "count cannot be NULL");

    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_nil) {
        *count = 0;
        return false;
    }
    if (var.type == bjd_type_map) {
        *count = var.v.n;
        return true;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    *count = 0;
    return false;
}

bool bjd_expect_map_max_or_nil(bjd_reader_t* reader, uint32_t max_count, uint32_t* count) {
    bjd_assert(count != NULL, "count cannot be NULL");

    bool has_map = bjd_expect_map_or_nil(reader, count);
    if (has_map && *count > max_count) {
        *count = 0;
        bjd_reader_flag_error(reader, bjd_error_type);
        return false;
    }
    return has_map;
}

uint32_t bjd_expect_array(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_array)
        return var.v.n;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

void bjd_expect_array_match(bjd_reader_t* reader, uint32_t count) {
    if (bjd_expect_array(reader) != count)
        bjd_reader_flag_error(reader, bjd_error_type);
}

bool bjd_expect_array_or_nil(bjd_reader_t* reader, uint32_t* count) {
    bjd_assert(count != NULL, "count cannot be NULL");

    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_nil) {
        *count = 0;
        return false;
    }
    if (var.type == bjd_type_array) {
        *count = var.v.n;
        return true;
    }
    bjd_reader_flag_error(reader, bjd_error_type);
    *count = 0;
    return false;
}

bool bjd_expect_array_max_or_nil(bjd_reader_t* reader, uint32_t max_count, uint32_t* count) {
    bjd_assert(count != NULL, "count cannot be NULL");

    bool has_array = bjd_expect_array_or_nil(reader, count);
    if (has_array && *count > max_count) {
        *count = 0;
        bjd_reader_flag_error(reader, bjd_error_type);
        return false;
    }
    return has_array;
}

#ifdef BJDATA_MALLOC
void* bjd_expect_array_alloc_impl(bjd_reader_t* reader, size_t element_size, uint32_t max_count, uint32_t* out_count, bool allow_nil) {
    bjd_assert(out_count != NULL, "out_count cannot be NULL");
    *out_count = 0;

    uint32_t count;
    bool has_array = true;
    if (allow_nil)
        has_array = bjd_expect_array_max_or_nil(reader, max_count, &count);
    else
        count = bjd_expect_array_max(reader, max_count);
    if (bjd_reader_error(reader))
        return NULL;

    // size 0 is not an error; we return NULL for no elements.
    if (count == 0) {
        // we call bjd_done_array() automatically ONLY if we are using
        // the _or_nil variant. this is the only way to allow nil and empty
        // to work the same way.
        if (allow_nil && has_array)
            bjd_done_array(reader);
        return NULL;
    }

    void* p = BJDATA_MALLOC(element_size * count);
    if (p == NULL) {
        bjd_reader_flag_error(reader, bjd_error_memory);
        return NULL;
    }

    *out_count = count;
    return p;
}
#endif


// Str, Bin and Ext Functions

uint32_t bjd_expect_str(bjd_reader_t* reader) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_str)
        return var.v.l;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
    #else
    uint8_t type = bjd_expect_type_byte(reader);
    uint32_t count;

    if ((type >> 5) == 5) {
        count = type & (uint8_t)~0xe0;
    } else if (type == 0xd9) {
        count = bjd_expect_native_u8(reader);
    } else if (type == 0xda) {
        count = bjd_expect_native_u16(reader);
    } else if (type == 0xdb) {
        count = bjd_expect_native_u32(reader);
    } else {
        bjd_reader_flag_error(reader, bjd_error_type);
        return 0;
    }

    #if BJDATA_READ_TRACKING
    bjd_reader_flag_if_error(reader, bjd_track_push(&reader->track, bjd_type_str, count));
    #endif
    return count;
    #endif
}

size_t bjd_expect_str_buf(bjd_reader_t* reader, char* buf, size_t bufsize) {
    bjd_assert(buf != NULL, "buf cannot be NULL");

    size_t length = bjd_expect_str(reader);
    if (bjd_reader_error(reader))
        return 0;

    if (length > bufsize) {
        bjd_reader_flag_error(reader, bjd_error_too_big);
        return 0;
    }

    bjd_read_bytes(reader, buf, length);
    if (bjd_reader_error(reader))
        return 0;

    bjd_done_str(reader);
    return length;
}

size_t bjd_expect_utf8(bjd_reader_t* reader, char* buf, size_t size) {
    bjd_assert(buf != NULL, "buf cannot be NULL");

    size_t length = bjd_expect_str_buf(reader, buf, size);

    if (!bjd_utf8_check(buf, length)) {
        bjd_reader_flag_error(reader, bjd_error_type);
        return 0;
    }

    return length;
}

uint32_t bjd_expect_bin(bjd_reader_t* reader) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_huge)
        return var.v.l;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

size_t bjd_expect_bin_buf(bjd_reader_t* reader, char* buf, size_t bufsize) {
    bjd_assert(buf != NULL, "buf cannot be NULL");

    size_t binsize = bjd_expect_bin(reader);
    if (bjd_reader_error(reader))
        return 0;
    if (binsize > bufsize) {
        bjd_reader_flag_error(reader, bjd_error_too_big);
        return 0;
    }
    bjd_read_bytes(reader, buf, binsize);
    if (bjd_reader_error(reader))
        return 0;
    bjd_done_bin(reader);
    return binsize;
}

void bjd_expect_bin_size_buf(bjd_reader_t* reader, char* buf, uint32_t size) {
    bjd_assert(buf != NULL, "buf cannot be NULL");
    bjd_expect_bin_size(reader, size);
    bjd_read_bytes(reader, buf, size);
    bjd_done_bin(reader);
}

#if BJDATA_EXTENSIONS
uint32_t bjd_expect_ext(bjd_reader_t* reader, int8_t* type) {
    bjd_tag_t var = bjd_read_tag(reader);
    if (var.type == bjd_type_ext) {
        *type = bjd_tag_ext_exttype(&var);
        return bjd_tag_ext_length(&var);
    }
    *type = 0;
    bjd_reader_flag_error(reader, bjd_error_type);
    return 0;
}

size_t bjd_expect_ext_buf(bjd_reader_t* reader, int8_t* type, char* buf, size_t bufsize) {
    bjd_assert(buf != NULL, "buf cannot be NULL");

    size_t extsize = bjd_expect_ext(reader, type);
    if (bjd_reader_error(reader))
        return 0;
    if (extsize > bufsize) {
        *type = 0;
        bjd_reader_flag_error(reader, bjd_error_too_big);
        return 0;
    }
    bjd_read_bytes(reader, buf, extsize);
    if (bjd_reader_error(reader)) {
        *type = 0;
        return 0;
    }
    bjd_done_ext(reader);
    return extsize;
}
#endif

void bjd_expect_cstr(bjd_reader_t* reader, char* buf, size_t bufsize) {
    uint32_t length = bjd_expect_str(reader);
    bjd_read_cstr(reader, buf, bufsize, length);
    bjd_done_str(reader);
}

void bjd_expect_utf8_cstr(bjd_reader_t* reader, char* buf, size_t bufsize) {
    uint32_t length = bjd_expect_str(reader);
    bjd_read_utf8_cstr(reader, buf, bufsize, length);
    bjd_done_str(reader);
}

#ifdef BJDATA_MALLOC
static char* bjd_expect_cstr_alloc_unchecked(bjd_reader_t* reader, size_t maxsize, size_t* out_length) {
    bjd_assert(out_length != NULL, "out_length cannot be NULL");
    *out_length = 0;

    // make sure argument makes sense
    if (maxsize < 1) {
        bjd_break("maxsize is zero; you must have room for at least a null-terminator");
        bjd_reader_flag_error(reader, bjd_error_bug);
        return NULL;
    }

    if (maxsize > UINT32_MAX)
        maxsize = UINT32_MAX;

    size_t length = bjd_expect_str_max(reader, (uint32_t)maxsize - 1);
    char* str = bjd_read_bytes_alloc_impl(reader, length, true);
    bjd_done_str(reader);

    if (str)
        *out_length = length;
    return str;
}

char* bjd_expect_cstr_alloc(bjd_reader_t* reader, size_t maxsize) {
    size_t length;
    char* str = bjd_expect_cstr_alloc_unchecked(reader, maxsize, &length);

    if (str && !bjd_str_check_no_null(str, length)) {
        BJDATA_FREE(str);
        bjd_reader_flag_error(reader, bjd_error_type);
        return NULL;
    }

    return str;
}

char* bjd_expect_utf8_cstr_alloc(bjd_reader_t* reader, size_t maxsize) {
    size_t length;
    char* str = bjd_expect_cstr_alloc_unchecked(reader, maxsize, &length);

    if (str && !bjd_utf8_check_no_null(str, length)) {
        BJDATA_FREE(str);
        bjd_reader_flag_error(reader, bjd_error_type);
        return NULL;
    }

    return str;
}
#endif

void bjd_expect_str_match(bjd_reader_t* reader, const char* str, size_t len) {
    bjd_assert(str != NULL, "str cannot be NULL");

    // expect a str the correct length
    if (len > UINT32_MAX)
        bjd_reader_flag_error(reader, bjd_error_type);
    bjd_expect_str_length(reader, (uint32_t)len);
    if (bjd_reader_error(reader))
        return;
    bjd_reader_track_bytes(reader, (uint32_t)len);

    // check each byte one by one (matched strings are likely to be very small)
    for (; len > 0; --len) {
        if (bjd_expect_native_u8(reader) != *str++) {
            bjd_reader_flag_error(reader, bjd_error_type);
            return;
        }
    }

    bjd_done_str(reader);
}

void bjd_expect_tag(bjd_reader_t* reader, bjd_tag_t expected) {
    bjd_tag_t actual = bjd_read_tag(reader);
    if (!bjd_tag_equal(actual, expected))
        bjd_reader_flag_error(reader, bjd_error_type);
}

#ifdef BJDATA_MALLOC
char* bjd_expect_bin_alloc(bjd_reader_t* reader, size_t maxsize, size_t* size) {
    bjd_assert(size != NULL, "size cannot be NULL");
    *size = 0;

    if (maxsize > UINT32_MAX)
        maxsize = UINT32_MAX;

    size_t length = bjd_expect_bin_max(reader, (uint32_t)maxsize);
    if (bjd_reader_error(reader))
        return NULL;

    char* data = bjd_read_bytes_alloc(reader, length);
    bjd_done_bin(reader);

    if (data)
        *size = length;
    return data;
}
#endif

#if BJDATA_EXTENSIONS && defined(BJDATA_MALLOC)
char* bjd_expect_ext_alloc(bjd_reader_t* reader, int8_t* type, size_t maxsize, size_t* size) {
    bjd_assert(size != NULL, "size cannot be NULL");
    *size = 0;

    if (maxsize > UINT32_MAX)
        maxsize = UINT32_MAX;

    size_t length = bjd_expect_ext_max(reader, type, (uint32_t)maxsize);
    if (bjd_reader_error(reader))
        return NULL;

    char* data = bjd_read_bytes_alloc(reader, length);
    bjd_done_ext(reader);

    if (data) {
        *size = length;
    } else {
        *type = 0;
    }
    return data;
}
#endif

size_t bjd_expect_enum(bjd_reader_t* reader, const char* strings[], size_t count) {

    // read the string in-place
    size_t keylen = bjd_expect_str(reader);
    const char* key = bjd_read_bytes_inplace(reader, keylen);
    bjd_done_str(reader);
    if (bjd_reader_error(reader) != bjd_ok)
        return count;

    // find what key it matches
    for (size_t i = 0; i < count; ++i) {
        const char* other = strings[i];
        size_t otherlen = bjd_strlen(other);
        if (keylen == otherlen && bjd_memcmp(key, other, keylen) == 0)
            return i;
    }

    // no matches
    bjd_reader_flag_error(reader, bjd_error_type);
    return count;
}

size_t bjd_expect_enum_optional(bjd_reader_t* reader, const char* strings[], size_t count) {
    if (bjd_reader_error(reader) != bjd_ok)
        return count;

    bjd_assert(count != 0, "count cannot be zero; no strings are valid!");
    bjd_assert(strings != NULL, "strings cannot be NULL");

    // the key is only recognized if it is a string
    if (bjd_peek_tag(reader).type != bjd_type_str) {
        bjd_discard(reader);
        return count;
    }

    // read the string in-place
    size_t keylen = bjd_expect_str(reader);
    const char* key = bjd_read_bytes_inplace(reader, keylen);
    bjd_done_str(reader);
    if (bjd_reader_error(reader) != bjd_ok)
        return count;

    // find what key it matches
    for (size_t i = 0; i < count; ++i) {
        const char* other = strings[i];
        size_t otherlen = bjd_strlen(other);
        if (keylen == otherlen && bjd_memcmp(key, other, keylen) == 0)
            return i;
    }

    // no matches
    return count;
}

size_t bjd_expect_key_uint(bjd_reader_t* reader, bool found[], size_t count) {
    if (bjd_reader_error(reader) != bjd_ok)
        return count;

    if (count == 0) {
        bjd_break("count cannot be zero; no keys are valid!");
        bjd_reader_flag_error(reader, bjd_error_bug);
        return count;
    }
    bjd_assert(found != NULL, "found cannot be NULL");

    // the key is only recognized if it is an unsigned int
    if (bjd_peek_tag(reader).type != bjd_type_uint) {
        bjd_discard(reader);
        return count;
    }

    // read the key
    uint64_t value = bjd_expect_u64(reader);
    if (bjd_reader_error(reader) != bjd_ok)
        return count;

    // unrecognized keys are fine, we just return count
    if (value >= count)
        return count;

    // check if this key is a duplicate
    if (found[value]) {
        bjd_reader_flag_error(reader, bjd_error_invalid);
        return count;
    }

    found[value] = true;
    return (size_t)value;
}

size_t bjd_expect_key_cstr(bjd_reader_t* reader, const char* keys[], bool found[], size_t count) {
    size_t i = bjd_expect_enum_optional(reader, keys, count);

    // unrecognized keys are fine, we just return count
    if (i == count)
        return count;

    // check if this key is a duplicate
    bjd_assert(found != NULL, "found cannot be NULL");
    if (found[i]) {
        bjd_reader_flag_error(reader, bjd_error_invalid);
        return count;
    }

    found[i] = true;
    return i;
}

#endif

