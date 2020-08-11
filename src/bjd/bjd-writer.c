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

#include "bjd-writer.h"

#if BJDATA_WRITER

#if BJDATA_WRITE_TRACKING
static void bjd_writer_flag_if_error(bjd_writer_t* writer, bjd_error_t error) {
    if (error != bjd_ok)
        bjd_writer_flag_error(writer, error);
}

void bjd_writer_track_push(bjd_writer_t* writer, bjd_type_t type, uint32_t count) {
    if (writer->error == bjd_ok)
        bjd_writer_flag_if_error(writer, bjd_track_push(&writer->track, type, count));
}

void bjd_writer_track_pop(bjd_writer_t* writer, bjd_type_t type) {
    if (writer->error == bjd_ok)
        bjd_writer_flag_if_error(writer, bjd_track_pop(&writer->track, type));
}

void bjd_writer_track_element(bjd_writer_t* writer) {
    if (writer->error == bjd_ok)
        bjd_writer_flag_if_error(writer, bjd_track_element(&writer->track, false));
}

void bjd_writer_track_bytes(bjd_writer_t* writer, size_t count) {
    if (writer->error == bjd_ok)
        bjd_writer_flag_if_error(writer, bjd_track_bytes(&writer->track, false, count));
}
#endif

static void bjd_writer_clear(bjd_writer_t* writer) {
    #if BJDATA_COMPATIBILITY
    writer->version = bjd_version_current;
    #endif
    writer->flush = NULL;
    writer->error_fn = NULL;
    writer->teardown = NULL;
    writer->context = NULL;

    writer->buffer = NULL;
    writer->current = NULL;
    writer->end = NULL;
    writer->error = bjd_ok;

    #if BJDATA_WRITE_TRACKING
    bjd_memset(&writer->track, 0, sizeof(writer->track));
    #endif
}

void bjd_writer_init(bjd_writer_t* writer, char* buffer, size_t size) {
    bjd_assert(buffer != NULL, "cannot initialize writer with empty buffer");
    bjd_writer_clear(writer);
    writer->buffer = buffer;
    writer->current = buffer;
    writer->end = writer->buffer + size;

    #if BJDATA_WRITE_TRACKING
    bjd_writer_flag_if_error(writer, bjd_track_init(&writer->track));
    #endif

    bjd_log("===========================\n");
    bjd_log("initializing writer with buffer size %i\n", (int)size);
}

void bjd_writer_init_error(bjd_writer_t* writer, bjd_error_t error) {
    bjd_writer_clear(writer);
    writer->error = error;

    bjd_log("===========================\n");
    bjd_log("initializing writer in error state %i\n", (int)error);
}

void bjd_writer_set_flush(bjd_writer_t* writer, bjd_writer_flush_t flush) {
    BJDATA_STATIC_ASSERT(BJDATA_WRITER_MINIMUM_BUFFER_SIZE >= BJDATA_MAXIMUM_TAG_SIZE,
            "minimum buffer size must fit any tag!");
    BJDATA_STATIC_ASSERT(31 + BJDATA_TAG_SIZE_FIXSTR >= BJDATA_WRITER_MINIMUM_BUFFER_SIZE,
            "minimum buffer size must fit the largest possible fixstr!");

    if (bjd_writer_buffer_size(writer) < BJDATA_WRITER_MINIMUM_BUFFER_SIZE) {
        bjd_break("buffer size is %i, but minimum buffer size for flush is %i",
                (int)bjd_writer_buffer_size(writer), BJDATA_WRITER_MINIMUM_BUFFER_SIZE);
        bjd_writer_flag_error(writer, bjd_error_bug);
        return;
    }

    writer->flush = flush;
}

#ifdef BJDATA_MALLOC
typedef struct bjd_growable_writer_t {
    char** target_data;
    size_t* target_size;
} bjd_growable_writer_t;

static char* bjd_writer_get_reserved(bjd_writer_t* writer) {
    // This is in a separate function in order to avoid false strict aliasing
    // warnings. We aren't actually violating strict aliasing (the reserved
    // space is only ever dereferenced as an bjd_growable_writer_t.)
    return (char*)writer->reserved;
}

static void bjd_growable_writer_flush(bjd_writer_t* writer, const char* data, size_t count) {

    // This is an intrusive flush function which modifies the writer's buffer
    // in response to a flush instead of emptying it in order to add more
    // capacity for data. This removes the need to copy data from a fixed buffer
    // into a growable one, improving performance.
    //
    // There are three ways flush can be called:
    //   - flushing the buffer during writing (used is zero, count is all data, data is buffer)
    //   - flushing extra data during writing (used is all flushed data, count is extra data, data is not buffer)
    //   - flushing during teardown (used and count are both all flushed data, data is buffer)
    //
    // In the first two cases, we grow the buffer by at least double, enough
    // to ensure that new data will fit. We ignore the teardown flush.

    if (data == writer->buffer) {

        // teardown, do nothing
        if (bjd_writer_buffer_used(writer) == count)
            return;

        // otherwise leave the data in the buffer and just grow
        writer->current = writer->buffer + count;
        count = 0;
    }

    size_t used = bjd_writer_buffer_used(writer);
    size_t size = bjd_writer_buffer_size(writer);

    bjd_log("flush size %i used %i data %p buffer %p\n",
            (int)count, (int)used, data, writer->buffer);

    bjd_assert(data == writer->buffer || used + count > size,
            "extra flush for %i but there is %i space left in the buffer! (%i/%i)",
            (int)count, (int)bjd_writer_buffer_left(writer), (int)used, (int)size);

    // grow to fit the data
    // TODO: this really needs to correctly test for overflow
    size_t new_size = size * 2;
    while (new_size < used + count)
        new_size *= 2;

    bjd_log("flush growing buffer size from %i to %i\n", (int)size, (int)new_size);

    // grow the buffer
    char* new_buffer = (char*)bjd_realloc(writer->buffer, used, new_size);
    if (new_buffer == NULL) {
        bjd_writer_flag_error(writer, bjd_error_memory);
        return;
    }
    writer->current = new_buffer + used;
    writer->buffer = new_buffer;
    writer->end = writer->buffer + new_size;

    // append the extra data
    if (count > 0) {
        bjd_memcpy(writer->current, data, count);
        writer->current += count;
    }

    bjd_log("new buffer %p, used %i\n", new_buffer, (int)bjd_writer_buffer_used(writer));
}

static void bjd_growable_writer_teardown(bjd_writer_t* writer) {
    bjd_growable_writer_t* growable_writer = (bjd_growable_writer_t*)bjd_writer_get_reserved(writer);

    if (bjd_writer_error(writer) == bjd_ok) {

        // shrink the buffer to an appropriate size if the data is
        // much smaller than the buffer
        if (bjd_writer_buffer_used(writer) < bjd_writer_buffer_size(writer) / 2) {
            size_t used = bjd_writer_buffer_used(writer);

            // We always return a non-null pointer that must be freed, even if
            // nothing was written. malloc() and realloc() do not necessarily
            // do this so we enforce it ourselves.
            size_t size = (used != 0) ? used : 1;

            char* buffer = (char*)bjd_realloc(writer->buffer, used, size);
            if (!buffer) {
                BJDATA_FREE(writer->buffer);
                bjd_writer_flag_error(writer, bjd_error_memory);
                return;
            }
            writer->buffer = buffer;
            writer->end = (writer->current = writer->buffer + used);
        }

        *growable_writer->target_data = writer->buffer;
        *growable_writer->target_size = bjd_writer_buffer_used(writer);
        writer->buffer = NULL;

    } else if (writer->buffer) {
        BJDATA_FREE(writer->buffer);
        writer->buffer = NULL;
    }

    writer->context = NULL;
}

void bjd_writer_init_growable(bjd_writer_t* writer, char** target_data, size_t* target_size) {
    bjd_assert(target_data != NULL, "cannot initialize writer without a destination for the data");
    bjd_assert(target_size != NULL, "cannot initialize writer without a destination for the size");

    *target_data = NULL;
    *target_size = 0;

    BJDATA_STATIC_ASSERT(sizeof(bjd_growable_writer_t) <= sizeof(writer->reserved),
            "not enough reserved space for growable writer!");
    bjd_growable_writer_t* growable_writer = (bjd_growable_writer_t*)bjd_writer_get_reserved(writer);

    growable_writer->target_data = target_data;
    growable_writer->target_size = target_size;

    size_t capacity = BJDATA_BUFFER_SIZE;
    char* buffer = (char*)BJDATA_MALLOC(capacity);
    if (buffer == NULL) {
        bjd_writer_init_error(writer, bjd_error_memory);
        return;
    }

    bjd_writer_init(writer, buffer, capacity);
    bjd_writer_set_flush(writer, bjd_growable_writer_flush);
    bjd_writer_set_teardown(writer, bjd_growable_writer_teardown);
}
#endif

#if BJDATA_STDIO
static void bjd_file_writer_flush(bjd_writer_t* writer, const char* buffer, size_t count) {
    FILE* file = (FILE*)writer->context;
    size_t written = fwrite((const void*)buffer, 1, count, file);
    if (written != count)
        bjd_writer_flag_error(writer, bjd_error_io);
}

static void bjd_file_writer_teardown(bjd_writer_t* writer) {
    BJDATA_FREE(writer->buffer);
    writer->buffer = NULL;
    writer->context = NULL;
}

static void bjd_file_writer_teardown_close(bjd_writer_t* writer) {
    FILE* file = (FILE*)writer->context;

    if (file) {
        int ret = fclose(file);
        if (ret != 0)
            bjd_writer_flag_error(writer, bjd_error_io);
    }

    bjd_file_writer_teardown(writer);
}

void bjd_writer_init_stdfile(bjd_writer_t* writer, FILE* file, bool close_when_done) {
    bjd_assert(file != NULL, "file is NULL");

    size_t capacity = BJDATA_BUFFER_SIZE;
    char* buffer = (char*)BJDATA_MALLOC(capacity);
    if (buffer == NULL) {
        bjd_writer_init_error(writer, bjd_error_memory);
        if (close_when_done) {
            fclose(file);
        }
        return;
    }

    bjd_writer_init(writer, buffer, capacity);
    bjd_writer_set_context(writer, file);
    bjd_writer_set_flush(writer, bjd_file_writer_flush);
    bjd_writer_set_teardown(writer, close_when_done ?
            bjd_file_writer_teardown_close :
            bjd_file_writer_teardown);
}

void bjd_writer_init_filename(bjd_writer_t* writer, const char* filename) {
    bjd_assert(filename != NULL, "filename is NULL");

    FILE* file = fopen(filename, "wb");
    if (file == NULL) {
        bjd_writer_init_error(writer, bjd_error_io);
        return;
    }

    bjd_writer_init_stdfile(writer, file, true);
}
#endif

void bjd_writer_flag_error(bjd_writer_t* writer, bjd_error_t error) {
    bjd_log("writer %p setting error %i: %s\n", (void*)writer, (int)error, bjd_error_to_string(error));

    if (writer->error == bjd_ok) {
        writer->error = error;
        if (writer->error_fn)
            writer->error_fn(writer, writer->error);
    }
}

BJDATA_STATIC_INLINE void bjd_writer_flush_unchecked(bjd_writer_t* writer) {
    // This is a bit ugly; we reset used before calling flush so that
    // a flush function can distinguish between flushing the buffer
    // versus flushing external data. see bjd_growable_writer_flush()
    size_t used = bjd_writer_buffer_used(writer);
    writer->current = writer->buffer;
    writer->flush(writer, writer->buffer, used);
}

void bjd_writer_flush_message(bjd_writer_t* writer) {
    if (writer->error != bjd_ok)
        return;

    #if BJDATA_WRITE_TRACKING
    bjd_writer_flag_if_error(writer, bjd_track_check_empty(&writer->track));
    if (writer->error != bjd_ok)
        return;
    #endif

    if (writer->flush == NULL) {
        bjd_break("cannot call bjd_writer_flush_message() without a flush function!");
        bjd_writer_flag_error(writer, bjd_error_bug);
        return;
    }

    if (bjd_writer_buffer_used(writer) > 0)
        bjd_writer_flush_unchecked(writer);
}

// Ensures there are at least count bytes free in the buffer. This
// will flag an error if the flush function fails to make enough
// room in the buffer.
BJDATA_NOINLINE static bool bjd_writer_ensure(bjd_writer_t* writer, size_t count) {
    bjd_assert(count != 0, "cannot ensure zero bytes!");
    bjd_assert(count <= BJDATA_WRITER_MINIMUM_BUFFER_SIZE,
            "cannot ensure %i bytes, this is more than the minimum buffer size %i!",
            (int)count, (int)BJDATA_WRITER_MINIMUM_BUFFER_SIZE);
    bjd_assert(count > bjd_writer_buffer_left(writer),
            "request to ensure %i bytes but there are already %i left in the buffer!",
            (int)count, (int)bjd_writer_buffer_left(writer));

    bjd_log("ensuring %i bytes, %i left\n", (int)count, (int)bjd_writer_buffer_left(writer));

    if (bjd_writer_error(writer) != bjd_ok)
        return false;

    if (writer->flush == NULL) {
        bjd_writer_flag_error(writer, bjd_error_too_big);
        return false;
    }

    bjd_writer_flush_unchecked(writer);
    if (bjd_writer_error(writer) != bjd_ok)
        return false;

    if (bjd_writer_buffer_left(writer) >= count)
        return true;

    bjd_writer_flag_error(writer, bjd_error_io);
    return false;
}

// Writes encoded bytes to the buffer when we already know the data
// does not fit in the buffer (i.e. it straddles the edge of the
// buffer.) If there is a flush function, it is guaranteed to be
// called; otherwise bjd_error_too_big is raised.
BJDATA_NOINLINE static void bjd_write_native_straddle(bjd_writer_t* writer, const char* p, size_t count) {
    bjd_assert(count == 0 || p != NULL, "data pointer for %i bytes is NULL", (int)count);

    if (bjd_writer_error(writer) != bjd_ok)
        return;
    bjd_log("big write for %i bytes from %p, %i space left in buffer\n",
            (int)count, p, (int)bjd_writer_buffer_left(writer));
    bjd_assert(count > bjd_writer_buffer_left(writer),
            "big write requested for %i bytes, but there is %i available "
            "space in buffer. should have called bjd_write_native() instead",
            (int)count, (int)(bjd_writer_buffer_left(writer)));

    // we'll need a flush function
    if (!writer->flush) {
        bjd_writer_flag_error(writer, bjd_error_too_big);
        return;
    }

    // flush the buffer
    bjd_writer_flush_unchecked(writer);
    if (bjd_writer_error(writer) != bjd_ok)
        return;

    // note that an intrusive flush function (such as bjd_growable_writer_flush())
    // may have changed size and/or reset used to a non-zero value. we treat both as
    // though they may have changed, and there may still be data in the buffer.

    // flush the extra data directly if it doesn't fit in the buffer
    if (count > bjd_writer_buffer_left(writer)) {
        writer->flush(writer, p, count);
        if (bjd_writer_error(writer) != bjd_ok)
            return;
    } else {
        bjd_memcpy(writer->current, p, count);
        writer->current += count;
    }
}

// Writes encoded bytes to the buffer, flushing if necessary.
BJDATA_STATIC_INLINE void bjd_write_native(bjd_writer_t* writer, const char* p, size_t count) {
    bjd_assert(count == 0 || p != NULL, "data pointer for %i bytes is NULL", (int)count);

    if (bjd_writer_buffer_left(writer) < count) {
        bjd_write_native_straddle(writer, p, count);
    } else {
        bjd_memcpy(writer->current, p, count);
        writer->current += count;
    }
}

bjd_error_t bjd_writer_destroy(bjd_writer_t* writer) {

    // clean up tracking, asserting if we're not already in an error state
    #if BJDATA_WRITE_TRACKING
    bjd_track_destroy(&writer->track, writer->error != bjd_ok);
    #endif

    // flush any outstanding data
    if (bjd_writer_error(writer) == bjd_ok && bjd_writer_buffer_used(writer) != 0 && writer->flush != NULL) {
        writer->flush(writer, writer->buffer, bjd_writer_buffer_used(writer));
        writer->flush = NULL;
    }

    if (writer->teardown) {
        writer->teardown(writer);
        writer->teardown = NULL;
    }

    return writer->error;
}

void bjd_write_tag(bjd_writer_t* writer, bjd_tag_t value) {
    switch (value.type) {
        case bjd_type_missing:
            bjd_break("cannot write a missing value!");
            bjd_writer_flag_error(writer, bjd_error_bug);
            return;

        case bjd_type_nil:    bjd_write_nil   (writer);            return;
        case bjd_type_bool:   bjd_write_bool  (writer, value.v.b); return;
        case bjd_type_float:  bjd_write_float (writer, value.v.f); return;
        case bjd_type_double: bjd_write_double(writer, value.v.d); return;
        case bjd_type_int:    bjd_write_int   (writer, value.v.i); return;
        case bjd_type_uint:   bjd_write_uint  (writer, value.v.u); return;

        case bjd_type_str: bjd_start_str(writer, value.v.l); return;
        case bjd_type_huge: bjd_start_bin(writer, value.v.l); return;

        #if BJDATA_EXTENSIONS
        case bjd_type_ext:
            bjd_start_ext(writer, bjd_tag_ext_exttype(&value), bjd_tag_ext_length(&value));
            return;
        #endif

        case bjd_type_array: bjd_start_array(writer, value.v.n); return;
        case bjd_type_map:   bjd_start_map(writer, value.v.n);   return;
    }

    bjd_break("unrecognized type %i", (int)value.type);
    bjd_writer_flag_error(writer, bjd_error_bug);
}

BJDATA_STATIC_INLINE void bjd_write_byte_element(bjd_writer_t* writer, char value) {
    bjd_writer_track_element(writer);
    if (BJDATA_LIKELY(bjd_writer_buffer_left(writer) >= 1) || bjd_writer_ensure(writer, 1))
        *(writer->current++) = value;
}

void bjd_write_nil(bjd_writer_t* writer) {
    bjd_write_byte_element(writer, (char)0xc0);
}

void bjd_write_bool(bjd_writer_t* writer, bool value) {
    bjd_write_byte_element(writer, (char)(0xc2 | (value ? 1 : 0)));
}

void bjd_write_true(bjd_writer_t* writer) {
    bjd_write_byte_element(writer, (char)0xc3);
}

void bjd_write_false(bjd_writer_t* writer) {
    bjd_write_byte_element(writer, (char)0xc2);
}

void bjd_write_object_bytes(bjd_writer_t* writer, const char* data, size_t bytes) {
    bjd_writer_track_element(writer);
    bjd_write_native(writer, data, bytes);
}

/*
 * Encode functions
 */

BJDATA_STATIC_INLINE void bjd_encode_fixuint(char* p, uint8_t value) {
    bjd_assert(value <= 127);
    bjd_store_u8(p, value);
}

BJDATA_STATIC_INLINE void bjd_encode_u8(char* p, uint8_t value) {
    bjd_assert(value > 127);
    bjd_store_u8(p, 0xcc);
    bjd_store_u8(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_u16(char* p, uint16_t value) {
    bjd_assert(value > UINT8_MAX);
    bjd_store_u8(p, 0xcd);
    bjd_store_u16(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_u32(char* p, uint32_t value) {
    bjd_assert(value > UINT16_MAX);
    bjd_store_u8(p, 0xce);
    bjd_store_u32(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_u64(char* p, uint64_t value) {
    bjd_assert(value > UINT32_MAX);
    bjd_store_u8(p, 0xcf);
    bjd_store_u64(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_fixint(char* p, int8_t value) {
    // this can encode positive or negative fixints
    bjd_assert(value >= -32);
    bjd_store_i8(p, value);
}

BJDATA_STATIC_INLINE void bjd_encode_i8(char* p, int8_t value) {
    bjd_assert(value < -32);
    bjd_store_u8(p, 0xd0);
    bjd_store_i8(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_i16(char* p, int16_t value) {
    bjd_assert(value < INT8_MIN);
    bjd_store_u8(p, 0xd1);
    bjd_store_i16(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_i32(char* p, int32_t value) {
    bjd_assert(value < INT16_MIN);
    bjd_store_u8(p, 0xd2);
    bjd_store_i32(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_i64(char* p, int64_t value) {
    bjd_assert(value < INT32_MIN);
    bjd_store_u8(p, 0xd3);
    bjd_store_i64(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_float(char* p, float value) {
    bjd_store_u8(p, 0xca);
    bjd_store_float(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_double(char* p, double value) {
    bjd_store_u8(p, 0xcb);
    bjd_store_double(p + 1, value);
}

BJDATA_STATIC_INLINE void bjd_encode_fixarray(char* p, uint8_t count) {
    bjd_assert(count <= 15);
    bjd_store_u8(p, (uint8_t)(0x90 | count));
}

BJDATA_STATIC_INLINE void bjd_encode_array16(char* p, uint16_t count) {
    bjd_assert(count > 15);
    bjd_store_u8(p, 0xdc);
    bjd_store_u16(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_array32(char* p, uint32_t count) {
    bjd_assert(count > UINT16_MAX);
    bjd_store_u8(p, 0xdd);
    bjd_store_u32(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_fixmap(char* p, uint8_t count) {
    bjd_assert(count <= 15);
    bjd_store_u8(p, (uint8_t)(0x80 | count));
}

BJDATA_STATIC_INLINE void bjd_encode_map16(char* p, uint16_t count) {
    bjd_assert(count > 15);
    bjd_store_u8(p, 0xde);
    bjd_store_u16(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_map32(char* p, uint32_t count) {
    bjd_assert(count > UINT16_MAX);
    bjd_store_u8(p, 0xdf);
    bjd_store_u32(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_fixstr(char* p, uint8_t count) {
    bjd_assert(count <= 31);
    bjd_store_u8(p, (uint8_t)(0xa0 | count));
}

BJDATA_STATIC_INLINE void bjd_encode_str8(char* p, uint8_t count) {
    bjd_assert(count > 31);
    bjd_store_u8(p, 0xd9);
    bjd_store_u8(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_str16(char* p, uint16_t count) {
    // we might be encoding a raw in compatibility mode, so we
    // allow count to be in the range [32, UINT8_MAX].
    bjd_assert(count > 31);
    bjd_store_u8(p, 0xda);
    bjd_store_u16(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_str32(char* p, uint32_t count) {
    bjd_assert(count > UINT16_MAX);
    bjd_store_u8(p, 0xdb);
    bjd_store_u32(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_bin8(char* p, uint8_t count) {
    bjd_store_u8(p, 0xc4);
    bjd_store_u8(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_bin16(char* p, uint16_t count) {
    bjd_assert(count > UINT8_MAX);
    bjd_store_u8(p, 0xc5);
    bjd_store_u16(p + 1, count);
}

BJDATA_STATIC_INLINE void bjd_encode_bin32(char* p, uint32_t count) {
    bjd_assert(count > UINT16_MAX);
    bjd_store_u8(p, 0xc6);
    bjd_store_u32(p + 1, count);
}

#if BJDATA_EXTENSIONS
BJDATA_STATIC_INLINE void bjd_encode_fixext1(char* p, int8_t exttype) {
    bjd_store_u8(p, 0xd4);
    bjd_store_i8(p + 1, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_fixext2(char* p, int8_t exttype) {
    bjd_store_u8(p, 0xd5);
    bjd_store_i8(p + 1, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_fixext4(char* p, int8_t exttype) {
    bjd_store_u8(p, 0xd6);
    bjd_store_i8(p + 1, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_fixext8(char* p, int8_t exttype) {
    bjd_store_u8(p, 0xd7);
    bjd_store_i8(p + 1, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_fixext16(char* p, int8_t exttype) {
    bjd_store_u8(p, 0xd8);
    bjd_store_i8(p + 1, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_ext8(char* p, int8_t exttype, uint8_t count) {
    bjd_assert(count != 1 && count != 2 && count != 4 && count != 8 && count != 16);
    bjd_store_u8(p, 0xc7);
    bjd_store_u8(p + 1, count);
    bjd_store_i8(p + 2, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_ext16(char* p, int8_t exttype, uint16_t count) {
    bjd_assert(count > UINT8_MAX);
    bjd_store_u8(p, 0xc8);
    bjd_store_u16(p + 1, count);
    bjd_store_i8(p + 3, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_ext32(char* p, int8_t exttype, uint32_t count) {
    bjd_assert(count > UINT16_MAX);
    bjd_store_u8(p, 0xc9);
    bjd_store_u32(p + 1, count);
    bjd_store_i8(p + 5, exttype);
}

BJDATA_STATIC_INLINE void bjd_encode_timestamp_4(char* p, uint32_t seconds) {
    bjd_encode_fixext4(p, BJDATA_EXTTYPE_TIMESTAMP);
    bjd_store_u32(p + BJDATA_TAG_SIZE_FIXEXT4, seconds);
}

BJDATA_STATIC_INLINE void bjd_encode_timestamp_8(char* p, int64_t seconds, uint32_t nanoseconds) {
    bjd_assert(nanoseconds <= BJDATA_TIMESTAMP_NANOSECONDS_MAX);
    bjd_encode_fixext8(p, BJDATA_EXTTYPE_TIMESTAMP);
    uint64_t encoded = ((uint64_t)nanoseconds << 34) | (uint64_t)seconds;
    bjd_store_u64(p + BJDATA_TAG_SIZE_FIXEXT8, encoded);
}

BJDATA_STATIC_INLINE void bjd_encode_timestamp_12(char* p, int64_t seconds, uint32_t nanoseconds) {
    bjd_assert(nanoseconds <= BJDATA_TIMESTAMP_NANOSECONDS_MAX);
    bjd_encode_ext8(p, BJDATA_EXTTYPE_TIMESTAMP, 12);
    bjd_store_u32(p + BJDATA_TAG_SIZE_EXT8, nanoseconds);
    bjd_store_i64(p + BJDATA_TAG_SIZE_EXT8 + 4, seconds);
}
#endif



/*
 * Write functions
 */

// This is a macro wrapper to the encode functions to encode
// directly into the buffer. If bjd_writer_ensure() fails
// it will flag an error so we don't have to do anything.
#define BJDATA_WRITE_ENCODED(encode_fn, size, ...) do {                                                 \
    if (BJDATA_LIKELY(bjd_writer_buffer_left(writer) >= size) || bjd_writer_ensure(writer, size)) { \
        BJDATA_EXPAND(encode_fn(writer->current, __VA_ARGS__));                                         \
        writer->current += size;                                                                       \
    }                                                                                                  \
} while (0)

void bjd_write_u8(bjd_writer_t* writer, uint8_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_write_u64(writer, value);
    #else
    bjd_writer_track_element(writer);
    if (value <= 127) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixuint, BJDATA_TAG_SIZE_FIXUINT, value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, value);
    }
    #endif
}

void bjd_write_u16(bjd_writer_t* writer, uint16_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_write_u64(writer, value);
    #else
    bjd_writer_track_element(writer);
    if (value <= 127) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixuint, BJDATA_TAG_SIZE_FIXUINT, (uint8_t)value);
    } else if (value <= UINT8_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, (uint8_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_u16, BJDATA_TAG_SIZE_U16, value);
    }
    #endif
}

void bjd_write_u32(bjd_writer_t* writer, uint32_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_write_u64(writer, value);
    #else
    bjd_writer_track_element(writer);
    if (value <= 127) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixuint, BJDATA_TAG_SIZE_FIXUINT, (uint8_t)value);
    } else if (value <= UINT8_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, (uint8_t)value);
    } else if (value <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_u16, BJDATA_TAG_SIZE_U16, (uint16_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_u32, BJDATA_TAG_SIZE_U32, value);
    }
    #endif
}

void bjd_write_u64(bjd_writer_t* writer, uint64_t value) {
    bjd_writer_track_element(writer);

    if (value <= 127) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixuint, BJDATA_TAG_SIZE_FIXUINT, (uint8_t)value);
    } else if (value <= UINT8_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, (uint8_t)value);
    } else if (value <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_u16, BJDATA_TAG_SIZE_U16, (uint16_t)value);
    } else if (value <= UINT32_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_u32, BJDATA_TAG_SIZE_U32, (uint32_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_u64, BJDATA_TAG_SIZE_U64, value);
    }
}

void bjd_write_i8(bjd_writer_t* writer, int8_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_write_i64(writer, value);
    #else
    bjd_writer_track_element(writer);
    if (value >= -32) {
        // we encode positive and negative fixints together
        BJDATA_WRITE_ENCODED(bjd_encode_fixint, BJDATA_TAG_SIZE_FIXINT, (int8_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_i8, BJDATA_TAG_SIZE_I8, (int8_t)value);
    }
    #endif
}

void bjd_write_i16(bjd_writer_t* writer, int16_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_write_i64(writer, value);
    #else
    bjd_writer_track_element(writer);
    if (value >= -32) {
        if (value <= 127) {
            // we encode positive and negative fixints together
            BJDATA_WRITE_ENCODED(bjd_encode_fixint, BJDATA_TAG_SIZE_FIXINT, (int8_t)value);
        } else if (value <= UINT8_MAX) {
            BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, (uint8_t)value);
        } else {
            BJDATA_WRITE_ENCODED(bjd_encode_u16, BJDATA_TAG_SIZE_U16, (uint16_t)value);
        }
    } else if (value >= INT8_MIN) {
        BJDATA_WRITE_ENCODED(bjd_encode_i8, BJDATA_TAG_SIZE_I8, (int8_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_i16, BJDATA_TAG_SIZE_I16, (int16_t)value);
    }
    #endif
}

void bjd_write_i32(bjd_writer_t* writer, int32_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_write_i64(writer, value);
    #else
    bjd_writer_track_element(writer);
    if (value >= -32) {
        if (value <= 127) {
            // we encode positive and negative fixints together
            BJDATA_WRITE_ENCODED(bjd_encode_fixint, BJDATA_TAG_SIZE_FIXINT, (int8_t)value);
        } else if (value <= UINT8_MAX) {
            BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, (uint8_t)value);
        } else if (value <= UINT16_MAX) {
            BJDATA_WRITE_ENCODED(bjd_encode_u16, BJDATA_TAG_SIZE_U16, (uint16_t)value);
        } else {
            BJDATA_WRITE_ENCODED(bjd_encode_u32, BJDATA_TAG_SIZE_U32, (uint32_t)value);
        }
    } else if (value >= INT8_MIN) {
        BJDATA_WRITE_ENCODED(bjd_encode_i8, BJDATA_TAG_SIZE_I8, (int8_t)value);
    } else if (value >= INT16_MIN) {
        BJDATA_WRITE_ENCODED(bjd_encode_i16, BJDATA_TAG_SIZE_I16, (int16_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_i32, BJDATA_TAG_SIZE_I32, value);
    }
    #endif
}

void bjd_write_i64(bjd_writer_t* writer, int64_t value) {
    #if BJDATA_OPTIMIZE_FOR_SIZE
    if (value > 127) {
        // for non-fix positive ints we call the u64 writer to save space
        bjd_write_u64(writer, (uint64_t)value);
        return;
    }
    #endif

    bjd_writer_track_element(writer);
    if (value >= -32) {
        #if BJDATA_OPTIMIZE_FOR_SIZE
        BJDATA_WRITE_ENCODED(bjd_encode_fixint, BJDATA_TAG_SIZE_FIXINT, (int8_t)value);
        #else
        if (value <= 127) {
            BJDATA_WRITE_ENCODED(bjd_encode_fixint, BJDATA_TAG_SIZE_FIXINT, (int8_t)value);
        } else if (value <= UINT8_MAX) {
            BJDATA_WRITE_ENCODED(bjd_encode_u8, BJDATA_TAG_SIZE_U8, (uint8_t)value);
        } else if (value <= UINT16_MAX) {
            BJDATA_WRITE_ENCODED(bjd_encode_u16, BJDATA_TAG_SIZE_U16, (uint16_t)value);
        } else if (value <= UINT32_MAX) {
            BJDATA_WRITE_ENCODED(bjd_encode_u32, BJDATA_TAG_SIZE_U32, (uint32_t)value);
        } else {
            BJDATA_WRITE_ENCODED(bjd_encode_u64, BJDATA_TAG_SIZE_U64, (uint64_t)value);
        }
        #endif
    } else if (value >= INT8_MIN) {
        BJDATA_WRITE_ENCODED(bjd_encode_i8, BJDATA_TAG_SIZE_I8, (int8_t)value);
    } else if (value >= INT16_MIN) {
        BJDATA_WRITE_ENCODED(bjd_encode_i16, BJDATA_TAG_SIZE_I16, (int16_t)value);
    } else if (value >= INT32_MIN) {
        BJDATA_WRITE_ENCODED(bjd_encode_i32, BJDATA_TAG_SIZE_I32, (int32_t)value);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_i64, BJDATA_TAG_SIZE_I64, value);
    }
}

void bjd_write_float(bjd_writer_t* writer, float value) {
    bjd_writer_track_element(writer);
    BJDATA_WRITE_ENCODED(bjd_encode_float, BJDATA_TAG_SIZE_FLOAT, value);
}

void bjd_write_double(bjd_writer_t* writer, double value) {
    bjd_writer_track_element(writer);
    BJDATA_WRITE_ENCODED(bjd_encode_double, BJDATA_TAG_SIZE_DOUBLE, value);
}

#if BJDATA_EXTENSIONS
void bjd_write_timestamp(bjd_writer_t* writer, int64_t seconds, uint32_t nanoseconds) {
    #if BJDATA_COMPATIBILITY
    if (writer->version <= bjd_version_v4) {
        bjd_break("Timestamps require spec version v5 or later. This writer is in v%i mode.", (int)writer->version);
        bjd_writer_flag_error(writer, bjd_error_bug);
        return;
    }
    #endif

    if (nanoseconds > BJDATA_TIMESTAMP_NANOSECONDS_MAX) {
        bjd_break("timestamp nanoseconds out of bounds: %u", nanoseconds);
        bjd_writer_flag_error(writer, bjd_error_bug);
        return;
    }

    bjd_writer_track_element(writer);

    if (seconds < 0 || seconds >= (INT64_C(1) << 34)) {
        BJDATA_WRITE_ENCODED(bjd_encode_timestamp_12, BJDATA_EXT_SIZE_TIMESTAMP12, seconds, nanoseconds);
    } else if (seconds > UINT32_MAX || nanoseconds > 0) {
        BJDATA_WRITE_ENCODED(bjd_encode_timestamp_8, BJDATA_EXT_SIZE_TIMESTAMP8, seconds, nanoseconds);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_timestamp_4, BJDATA_EXT_SIZE_TIMESTAMP4, (uint32_t)seconds);
    }
}
#endif

void bjd_start_array(bjd_writer_t* writer, uint32_t count) {
    bjd_writer_track_element(writer);

    if (count <= 15) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixarray, BJDATA_TAG_SIZE_FIXARRAY, (uint8_t)count);
    } else if (count <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_array16, BJDATA_TAG_SIZE_ARRAY16, (uint16_t)count);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_array32, BJDATA_TAG_SIZE_ARRAY32, (uint32_t)count);
    }

    bjd_writer_track_push(writer, bjd_type_array, count);
}

void bjd_start_map(bjd_writer_t* writer, uint32_t count) {
    bjd_writer_track_element(writer);

    if (count <= 15) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixmap, BJDATA_TAG_SIZE_FIXMAP, (uint8_t)count);
    } else if (count <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_map16, BJDATA_TAG_SIZE_MAP16, (uint16_t)count);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_map32, BJDATA_TAG_SIZE_MAP32, (uint32_t)count);
    }

    bjd_writer_track_push(writer, bjd_type_map, count);
}

static void bjd_start_str_notrack(bjd_writer_t* writer, uint32_t count) {
    if (count <= 31) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixstr, BJDATA_TAG_SIZE_FIXSTR, (uint8_t)count);

    // str8 is only supported in v5 or later.
    } else if (count <= UINT8_MAX
            #if BJDATA_COMPATIBILITY
            && writer->version >= bjd_version_v5
            #endif
            ) {
        BJDATA_WRITE_ENCODED(bjd_encode_str8, BJDATA_TAG_SIZE_STR8, (uint8_t)count);

    } else if (count <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_str16, BJDATA_TAG_SIZE_STR16, (uint16_t)count);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_str32, BJDATA_TAG_SIZE_STR32, (uint32_t)count);
    }
}

static void bjd_start_bin_notrack(bjd_writer_t* writer, uint32_t count) {
    #if BJDATA_COMPATIBILITY
    // In the v4 spec, there was only the raw type for any kind of
    // variable-length data. In v4 mode, we support the bin functions,
    // but we produce an old-style raw.
    if (writer->version <= bjd_version_v4) {
        bjd_start_str_notrack(writer, count);
        return;
    }
    #endif

    if (count <= UINT8_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_bin8, BJDATA_TAG_SIZE_BIN8, (uint8_t)count);
    } else if (count <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_bin16, BJDATA_TAG_SIZE_BIN16, (uint16_t)count);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_bin32, BJDATA_TAG_SIZE_BIN32, (uint32_t)count);
    }
}

void bjd_start_str(bjd_writer_t* writer, uint32_t count) {
    bjd_writer_track_element(writer);
    bjd_start_str_notrack(writer, count);
    bjd_writer_track_push(writer, bjd_type_str, count);
}

void bjd_start_bin(bjd_writer_t* writer, uint32_t count) {
    bjd_writer_track_element(writer);
    bjd_start_bin_notrack(writer, count);
    bjd_writer_track_push(writer, bjd_type_huge, count);
}

#if BJDATA_EXTENSIONS
void bjd_start_ext(bjd_writer_t* writer, int8_t exttype, uint32_t count) {
    #if BJDATA_COMPATIBILITY
    if (writer->version <= bjd_version_v4) {
        bjd_break("Ext types require spec version v5 or later. This writer is in v%i mode.", (int)writer->version);
        bjd_writer_flag_error(writer, bjd_error_bug);
        return;
    }
    #endif

    bjd_writer_track_element(writer);

    if (count == 1) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixext1, BJDATA_TAG_SIZE_FIXEXT1, exttype);
    } else if (count == 2) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixext2, BJDATA_TAG_SIZE_FIXEXT2, exttype);
    } else if (count == 4) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixext4, BJDATA_TAG_SIZE_FIXEXT4, exttype);
    } else if (count == 8) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixext8, BJDATA_TAG_SIZE_FIXEXT8, exttype);
    } else if (count == 16) {
        BJDATA_WRITE_ENCODED(bjd_encode_fixext16, BJDATA_TAG_SIZE_FIXEXT16, exttype);
    } else if (count <= UINT8_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_ext8, BJDATA_TAG_SIZE_EXT8, exttype, (uint8_t)count);
    } else if (count <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_ext16, BJDATA_TAG_SIZE_EXT16, exttype, (uint16_t)count);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_ext32, BJDATA_TAG_SIZE_EXT32, exttype, (uint32_t)count);
    }

    bjd_writer_track_push(writer, bjd_type_ext, count);
}
#endif



/*
 * Compound helpers and other functions
 */

void bjd_write_str(bjd_writer_t* writer, const char* data, uint32_t count) {
    bjd_assert(data != NULL, "data for string of length %i is NULL", (int)count);

    #if BJDATA_OPTIMIZE_FOR_SIZE
    bjd_writer_track_element(writer);
    bjd_start_str_notrack(writer, count);
    bjd_write_native(writer, data, count);
    #else

    bjd_writer_track_element(writer);

    if (count <= 31) {
        // The minimum buffer size when using a flush function is guaranteed to
        // fit the largest possible fixstr.
        size_t size = count + BJDATA_TAG_SIZE_FIXSTR;
        if (BJDATA_LIKELY(bjd_writer_buffer_left(writer) >= size) || bjd_writer_ensure(writer, size)) {
            char* BJDATA_RESTRICT p = writer->current;
            bjd_encode_fixstr(p, (uint8_t)count);
            bjd_memcpy(p + BJDATA_TAG_SIZE_FIXSTR, data, count);
            writer->current += count + BJDATA_TAG_SIZE_FIXSTR;
        }
        return;
    }

    if (count <= UINT8_MAX
            #if BJDATA_COMPATIBILITY
            && writer->version >= bjd_version_v5
            #endif
            ) {
        if (count + BJDATA_TAG_SIZE_STR8 <= bjd_writer_buffer_left(writer)) {
            char* BJDATA_RESTRICT p = writer->current;
            bjd_encode_str8(p, (uint8_t)count);
            bjd_memcpy(p + BJDATA_TAG_SIZE_STR8, data, count);
            writer->current += count + BJDATA_TAG_SIZE_STR8;
        } else {
            BJDATA_WRITE_ENCODED(bjd_encode_str8, BJDATA_TAG_SIZE_STR8, (uint8_t)count);
            bjd_write_native(writer, data, count);
        }
        return;
    }

    // str16 and str32 are likely to be a significant fraction of the buffer
    // size, so we don't bother with a combined space check in order to
    // minimize code size.
    if (count <= UINT16_MAX) {
        BJDATA_WRITE_ENCODED(bjd_encode_str16, BJDATA_TAG_SIZE_STR16, (uint16_t)count);
        bjd_write_native(writer, data, count);
    } else {
        BJDATA_WRITE_ENCODED(bjd_encode_str32, BJDATA_TAG_SIZE_STR32, (uint32_t)count);
        bjd_write_native(writer, data, count);
    }

    #endif
}

void bjd_write_bin(bjd_writer_t* writer, const char* data, uint32_t count) {
    bjd_assert(data != NULL, "data pointer for bin of %i bytes is NULL", (int)count);
    bjd_start_bin(writer, count);
    bjd_write_bytes(writer, data, count);
    bjd_finish_bin(writer);
}

#if BJDATA_EXTENSIONS
void bjd_write_ext(bjd_writer_t* writer, int8_t exttype, const char* data, uint32_t count) {
    bjd_assert(data != NULL, "data pointer for ext of type %i and %i bytes is NULL", exttype, (int)count);
    bjd_start_ext(writer, exttype, count);
    bjd_write_bytes(writer, data, count);
    bjd_finish_ext(writer);
}
#endif

void bjd_write_bytes(bjd_writer_t* writer, const char* data, size_t count) {
    bjd_assert(data != NULL, "data pointer for %i bytes is NULL", (int)count);
    bjd_writer_track_bytes(writer, count);
    bjd_write_native(writer, data, count);
}

void bjd_write_cstr(bjd_writer_t* writer, const char* cstr) {
    bjd_assert(cstr != NULL, "cstr pointer is NULL");
    size_t length = bjd_strlen(cstr);
    if (length > UINT32_MAX)
        bjd_writer_flag_error(writer, bjd_error_invalid);
    bjd_write_str(writer, cstr, (uint32_t)length);
}

void bjd_write_cstr_or_nil(bjd_writer_t* writer, const char* cstr) {
    if (cstr)
        bjd_write_cstr(writer, cstr);
    else
        bjd_write_nil(writer);
}

void bjd_write_utf8(bjd_writer_t* writer, const char* str, uint32_t length) {
    bjd_assert(str != NULL, "data for string of length %i is NULL", (int)length);
    if (!bjd_utf8_check(str, length)) {
        bjd_writer_flag_error(writer, bjd_error_invalid);
        return;
    }
    bjd_write_str(writer, str, length);
}

void bjd_write_utf8_cstr(bjd_writer_t* writer, const char* cstr) {
    bjd_assert(cstr != NULL, "cstr pointer is NULL");
    size_t length = bjd_strlen(cstr);
    if (length > UINT32_MAX) {
        bjd_writer_flag_error(writer, bjd_error_invalid);
        return;
    }
    bjd_write_utf8(writer, cstr, (uint32_t)length);
}

void bjd_write_utf8_cstr_or_nil(bjd_writer_t* writer, const char* cstr) {
    if (cstr)
        bjd_write_utf8_cstr(writer, cstr);
    else
        bjd_write_nil(writer);
}

#endif

