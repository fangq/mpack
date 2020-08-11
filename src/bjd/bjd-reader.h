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
 * Declares the core BJData Tag Reader.
 */

#ifndef BJDATA_READER_H
#define BJDATA_READER_H 1

#include "bjd-common.h"

BJDATA_HEADER_START
BJDATA_EXTERN_C_START

#if BJDATA_READER

#if BJDATA_READ_TRACKING
struct bjd_track_t;
#endif

// The denominator to determine whether a read is a small
// fraction of the buffer size.
#define BJDATA_READER_SMALL_FRACTION_DENOMINATOR 32

/**
 * @defgroup reader Reader API
 *
 * The BJData Reader API contains functions for imperatively reading dynamically
 * typed data from a Binary JData stream.
 *
 * See @ref docs/reader.md for examples.
 *
 * @note If you are not writing code for an embedded device (or otherwise do
 * not need maximum performance with minimal memory usage), you should not use
 * this. You probably want to use the @link node Node API@endlink instead.
 *
 * This forms the basis of the @link expect Expect API@endlink, which can be
 * used to interpret the stream of elements in expected types and value ranges.
 *
 * @{
 */

/**
 * @def BJDATA_READER_MINIMUM_BUFFER_SIZE
 *
 * The minimum buffer size for a reader with a fill function.
 */
#define BJDATA_READER_MINIMUM_BUFFER_SIZE 32

/**
 * A buffered Binary JData decoder.
 *
 * The decoder wraps an existing buffer and, optionally, a fill function.
 * This allows efficiently decoding data from existing memory buffers, files,
 * streams, etc.
 *
 * All read operations are synchronous; they will block until the
 * requested data is fully read, or an error occurs.
 *
 * This structure is opaque; its fields should not be accessed outside
 * of BJData.
 */
typedef struct bjd_reader_t bjd_reader_t;

/**
 * The BJData reader's fill function. It should fill the buffer with at
 * least one byte and at most the given @c count, returning the number
 * of bytes written to the buffer.
 *
 * In case of error, it should flag an appropriate error on the reader
 * (usually @ref bjd_error_io), or simply return zero. If zero is
 * returned, bjd_error_io is raised.
 *
 * @note When reading from a stream, you should only copy and return
 * the bytes that are immediately available. It is always safe to return
 * less than the requested count as long as some non-zero number of bytes
 * are read; if more bytes are needed, the read function will simply be
 * called again.
 *
 * @see bjd_reader_context()
 */
typedef size_t (*bjd_reader_fill_t)(bjd_reader_t* reader, char* buffer, size_t count);

/**
 * The BJData reader's skip function. It should discard the given number
 * of bytes from the source (for example by seeking forward.)
 *
 * In case of error, it should flag an appropriate error on the reader.
 *
 * @see bjd_reader_context()
 */
typedef void (*bjd_reader_skip_t)(bjd_reader_t* reader, size_t count);

/**
 * An error handler function to be called when an error is flagged on
 * the reader.
 *
 * The error handler will only be called once on the first error flagged;
 * any subsequent reads and errors are ignored, and the reader is
 * permanently in that error state.
 *
 * BJData is safe against non-local jumps out of error handler callbacks.
 * This means you are allowed to longjmp or throw an exception (in C++,
 * Objective-C, or with SEH) out of this callback.
 *
 * Bear in mind when using longjmp that local non-volatile variables that
 * have changed are undefined when setjmp() returns, so you can't put the
 * reader on the stack in the same activation frame as the setjmp without
 * declaring it volatile.
 *
 * You must still eventually destroy the reader. It is not destroyed
 * automatically when an error is flagged. It is safe to destroy the
 * reader within this error callback, but you will either need to perform
 * a non-local jump, or store something in your context to identify
 * that the reader is destroyed since any future accesses to it cause
 * undefined behavior.
 */
typedef void (*bjd_reader_error_t)(bjd_reader_t* reader, bjd_error_t error);

/**
 * A teardown function to be called when the reader is destroyed.
 */
typedef void (*bjd_reader_teardown_t)(bjd_reader_t* reader);

/* Hide internals from documentation */
/** @cond */

struct bjd_reader_t {
    void* context;                    /* Context for reader callbacks */
    bjd_reader_fill_t fill;         /* Function to read bytes into the buffer */
    bjd_reader_error_t error_fn;    /* Function to call on error */
    bjd_reader_teardown_t teardown; /* Function to teardown the context on destroy */
    bjd_reader_skip_t skip;         /* Function to skip bytes from the source */

    char* buffer;       /* Writeable byte buffer */
    size_t size;        /* Size of the buffer */

    const char* data;   /* Current data pointer (in the buffer, if it is used) */
    const char* end;    /* The end of available data (in the buffer, if it is used) */

    bjd_error_t error;  /* Error state */

    #if BJDATA_READ_TRACKING
    bjd_track_t track; /* Stack of map/array/str/bin/ext reads */
    #endif
};

/** @endcond */

/**
 * @name Lifecycle Functions
 * @{
 */

/**
 * Initializes an BJData reader with the given buffer. The reader does
 * not assume ownership of the buffer, but the buffer must be writeable
 * if a fill function will be used to refill it.
 *
 * @param reader The BJData reader.
 * @param buffer The buffer with which to read Binary JData data.
 * @param size The size of the buffer.
 * @param count The number of bytes already in the buffer.
 */
void bjd_reader_init(bjd_reader_t* reader, char* buffer, size_t size, size_t count);

/**
 * Initializes an BJData reader directly into an error state. Use this if you
 * are writing a wrapper to bjd_reader_init() which can fail its setup.
 */
void bjd_reader_init_error(bjd_reader_t* reader, bjd_error_t error);

/**
 * Initializes an BJData reader to parse a pre-loaded contiguous chunk of data. The
 * reader does not assume ownership of the data.
 *
 * @param reader The BJData reader.
 * @param data The data to parse.
 * @param count The number of bytes pointed to by data.
 */
void bjd_reader_init_data(bjd_reader_t* reader, const char* data, size_t count);

#if BJDATA_STDIO
/**
 * Initializes an BJData reader that reads from a file.
 *
 * The file will be automatically opened and closed by the reader.
 */
void bjd_reader_init_filename(bjd_reader_t* reader, const char* filename);

/**
 * Deprecated.
 *
 * \deprecated Renamed to bjd_reader_init_filename().
 */
BJDATA_INLINE void bjd_reader_init_file(bjd_reader_t* reader, const char* filename) {
    bjd_reader_init_filename(reader, filename);
}

/**
 * Initializes an BJData reader that reads from a libc FILE. This can be used to
 * read from stdin, or from a file opened separately.
 *
 * @param reader The BJData reader.
 * @param stdfile The FILE.
 * @param close_when_done If true, fclose() will be called on the FILE when it
 *         is no longer needed. If false, the file will not be closed when
 *         reading is done.
 *
 * @warning The reader is buffered. It will read data in advance of parsing it,
 * and it may read more data than it parsed. See bjd_reader_remaining() to
 * access the extra data.
 */
void bjd_reader_init_stdfile(bjd_reader_t* reader, FILE* stdfile, bool close_when_done);
#endif

/**
 * @def bjd_reader_init_stack(reader)
 * @hideinitializer
 *
 * Initializes an BJData reader using stack space as a buffer. A fill function
 * should be added to the reader to fill the buffer.
 *
 * @see bjd_reader_set_fill
 */

/** @cond */
#define bjd_reader_init_stack_line_ex(line, reader) \
    char bjd_buf_##line[BJDATA_STACK_SIZE]; \
    bjd_reader_init((reader), bjd_buf_##line, sizeof(bjd_buf_##line), 0)

#define bjd_reader_init_stack_line(line, reader) \
    bjd_reader_init_stack_line_ex(line, reader)
/** @endcond */

#define bjd_reader_init_stack(reader) \
    bjd_reader_init_stack_line(__LINE__, (reader))

/**
 * Cleans up the BJData reader, ensuring that all compound elements
 * have been completely read. Returns the final error state of the
 * reader.
 *
 * This will assert in tracking mode if the reader is not in an error
 * state and has any incomplete reads. If you want to cancel reading
 * in the middle of a document, you need to flag an error on the reader
 * before destroying it (such as bjd_error_data).
 *
 * @see bjd_read_tag()
 * @see bjd_reader_flag_error()
 * @see bjd_error_data
 */
bjd_error_t bjd_reader_destroy(bjd_reader_t* reader);

/**
 * @}
 */

/**
 * @name Callbacks
 * @{
 */

/**
 * Sets the custom pointer to pass to the reader callbacks, such as fill
 * or teardown.
 *
 * @param reader The BJData reader.
 * @param context User data to pass to the reader callbacks.
 *
 * @see bjd_reader_context()
 */
BJDATA_INLINE void bjd_reader_set_context(bjd_reader_t* reader, void* context) {
    reader->context = context;
}

/**
 * Returns the custom context for reader callbacks.
 *
 * @see bjd_reader_set_context
 * @see bjd_reader_set_fill
 * @see bjd_reader_set_skip
 */
BJDATA_INLINE void* bjd_reader_context(bjd_reader_t* reader) {
    return reader->context;
}

/**
 * Sets the fill function to refill the data buffer when it runs out of data.
 *
 * If no fill function is used, truncated Binary JData data results in
 * bjd_error_invalid (since the buffer is assumed to contain a
 * complete Binary JData object.)
 *
 * If a fill function is used, truncated Binary JData data usually
 * results in bjd_error_io (since the fill function fails to get
 * the missing data.)
 *
 * This should normally be used with bjd_reader_set_context() to register
 * a custom pointer to pass to the fill function.
 *
 * @param reader The BJData reader.
 * @param fill The function to fetch additional data into the buffer.
 */
void bjd_reader_set_fill(bjd_reader_t* reader, bjd_reader_fill_t fill);

/**
 * Sets the skip function to discard bytes from the source stream.
 *
 * It's not necessary to implement this function. If the stream is not
 * seekable, don't set a skip callback. The reader will fall back to
 * using the fill function instead.
 *
 * This should normally be used with bjd_reader_set_context() to register
 * a custom pointer to pass to the skip function.
 *
 * The skip function is ignored in size-optimized builds to reduce code
 * size. Data will be skipped with the fill function when necessary.
 *
 * @param reader The BJData reader.
 * @param skip The function to discard bytes from the source stream.
 */
void bjd_reader_set_skip(bjd_reader_t* reader, bjd_reader_skip_t skip);

/**
 * Sets the error function to call when an error is flagged on the reader.
 *
 * This should normally be used with bjd_reader_set_context() to register
 * a custom pointer to pass to the error function.
 *
 * See the definition of bjd_reader_error_t for more information about
 * what you can do from an error callback.
 *
 * @see bjd_reader_error_t
 * @param reader The BJData reader.
 * @param error_fn The function to call when an error is flagged on the reader.
 */
BJDATA_INLINE void bjd_reader_set_error_handler(bjd_reader_t* reader, bjd_reader_error_t error_fn) {
    reader->error_fn = error_fn;
}

/**
 * Sets the teardown function to call when the reader is destroyed.
 *
 * This should normally be used with bjd_reader_set_context() to register
 * a custom pointer to pass to the teardown function.
 *
 * @param reader The BJData reader.
 * @param teardown The function to call when the reader is destroyed.
 */
BJDATA_INLINE void bjd_reader_set_teardown(bjd_reader_t* reader, bjd_reader_teardown_t teardown) {
    reader->teardown = teardown;
}

/**
 * @}
 */

/**
 * @name Core Reader Functions
 * @{
 */

/**
 * Queries the error state of the BJData reader.
 *
 * If a reader is in an error state, you should discard all data since the
 * last time the error flag was checked. The error flag cannot be cleared.
 */
BJDATA_INLINE bjd_error_t bjd_reader_error(bjd_reader_t* reader) {
    return reader->error;
}

/**
 * Places the reader in the given error state, calling the error callback if one
 * is set.
 *
 * This allows you to externally flag errors, for example if you are validating
 * data as you read it.
 *
 * If the reader is already in an error state, this call is ignored and no
 * error callback is called.
 */
void bjd_reader_flag_error(bjd_reader_t* reader, bjd_error_t error);

/**
 * Places the reader in the given error state if the given error is not bjd_ok,
 * returning the resulting error state of the reader.
 *
 * This allows you to externally flag errors, for example if you are validating
 * data as you read it.
 *
 * If the given error is bjd_ok or if the reader is already in an error state,
 * this call is ignored and the actual error state of the reader is returned.
 */
BJDATA_INLINE bjd_error_t bjd_reader_flag_if_error(bjd_reader_t* reader, bjd_error_t error) {
    if (error != bjd_ok)
        bjd_reader_flag_error(reader, error);
    return bjd_reader_error(reader);
}

/**
 * Returns bytes left in the reader's buffer.
 *
 * If you are done reading Binary JData data but there is other interesting data
 * following it, the reader may have buffered too much data. The number of bytes
 * remaining in the buffer and a pointer to the position of those bytes can be
 * queried here.
 *
 * If you know the length of the BJData chunk beforehand, it's better to instead
 * have your fill function limit the data it reads so that the reader does not
 * have extra data. In this case you can simply check that this returns zero.
 *
 * Returns 0 if the reader is in an error state.
 *
 * @param reader The BJData reader from which to query remaining data.
 * @param data [out] A pointer to the remaining data, or NULL.
 * @return The number of bytes remaining in the buffer.
 */
size_t bjd_reader_remaining(bjd_reader_t* reader, const char** data);

/**
 * Reads a Binary JData object header (an BJData tag.)
 *
 * If an error occurs, the reader is placed in an error state and a
 * nil tag is returned. If the reader is already in an error state,
 * a nil tag is returned.
 *
 * If the type is compound (i.e. is a map, array, string, binary or
 * extension type), additional reads are required to get the contained
 * data, and the corresponding done function must be called when done.
 *
 * @note Maps in JSON are unordered, so it is recommended not to expect
 * a specific ordering for your map values in case your data is converted
 * to/from JSON.
 * 
 * @see bjd_read_bytes()
 * @see bjd_done_array()
 * @see bjd_done_map()
 * @see bjd_done_str()
 * @see bjd_done_bin()
 * @see bjd_done_ext()
 */
bjd_tag_t bjd_read_tag(bjd_reader_t* reader);

/**
 * Parses the next Binary JData object header (an BJData tag) without
 * advancing the reader.
 *
 * If an error occurs, the reader is placed in an error state and a
 * nil tag is returned. If the reader is already in an error state,
 * a nil tag is returned.
 *
 * @note Maps in JSON are unordered, so it is recommended not to expect
 * a specific ordering for your map values in case your data is converted
 * to/from JSON.
 *
 * @see bjd_read_tag()
 * @see bjd_discard()
 */
bjd_tag_t bjd_peek_tag(bjd_reader_t* reader);

/**
 * @}
 */

/**
 * @name String and Data Functions
 * @{
 */

/**
 * Skips bytes from the underlying stream. This is used only to
 * skip the contents of a string, binary blob or extension object.
 */
void bjd_skip_bytes(bjd_reader_t* reader, size_t count);

/**
 * Reads bytes from a string, binary blob or extension object, copying
 * them into the given buffer.
 *
 * A str, bin or ext must have been opened by a call to bjd_read_tag()
 * which yielded one of these types, or by a call to an expect function
 * such as bjd_expect_str() or bjd_expect_bin().
 *
 * If an error occurs, the buffer contents are undefined.
 *
 * This can be called multiple times for a single str, bin or ext
 * to read the data in chunks. The total data read must add up
 * to the size of the object.
 *
 * @param reader The BJData reader
 * @param p The buffer in which to copy the bytes
 * @param count The number of bytes to read
 */
void bjd_read_bytes(bjd_reader_t* reader, char* p, size_t count);

/**
 * Reads bytes from a string, ensures that the string is valid UTF-8,
 * and copies the bytes into the given buffer.
 *
 * A string must have been opened by a call to bjd_read_tag() which
 * yielded a string, or by a call to an expect function such as
 * bjd_expect_str().
 *
 * The given byte count must match the complete size of the string as
 * returned by the tag or expect function. You must ensure that the
 * buffer fits the data.
 *
 * This does not accept any UTF-8 variant such as Modified UTF-8, CESU-8 or
 * WTF-8. Only pure UTF-8 is allowed.
 *
 * If an error occurs, the buffer contents are undefined.
 *
 * Unlike bjd_read_bytes(), this cannot be used to read the data in
 * chunks (since this might split a character's UTF-8 bytes, and the
 * reader does not keep track of the UTF-8 decoding state between reads.)
 *
 * @throws bjd_error_type if the string contains invalid UTF-8.
 */
void bjd_read_utf8(bjd_reader_t* reader, char* p, size_t byte_count);

/**
 * Reads bytes from a string, ensures that the string contains no NUL
 * bytes, copies the bytes into the given buffer and adds a null-terminator.
 *
 * A string must have been opened by a call to bjd_read_tag() which
 * yielded a string, or by a call to an expect function such as
 * bjd_expect_str().
 *
 * The given byte count must match the size of the string as returned
 * by the tag or expect function. The string will only be copied if
 * the buffer is large enough to store it.
 *
 * If an error occurs, the buffer will contain an empty string.
 *
 * @note If you know the object will be a string before reading it,
 * it is highly recommended to use bjd_expect_cstr() instead.
 * Alternatively you could use bjd_peek_tag() and call
 * bjd_expect_cstr() if it's a string.
 *
 * @throws bjd_error_too_big if the string plus null-terminator is larger than the given buffer size
 * @throws bjd_error_type if the string contains a null byte.
 *
 * @see bjd_peek_tag()
 * @see bjd_expect_cstr()
 * @see bjd_expect_utf8_cstr()
 */
void bjd_read_cstr(bjd_reader_t* reader, char* buf, size_t buffer_size, size_t byte_count);

/**
 * Reads bytes from a string, ensures that the string is valid UTF-8
 * with no NUL bytes, copies the bytes into the given buffer and adds a
 * null-terminator.
 *
 * A string must have been opened by a call to bjd_read_tag() which
 * yielded a string, or by a call to an expect function such as
 * bjd_expect_str().
 *
 * The given byte count must match the size of the string as returned
 * by the tag or expect function. The string will only be copied if
 * the buffer is large enough to store it.
 *
 * This does not accept any UTF-8 variant such as Modified UTF-8, CESU-8 or
 * WTF-8. Only pure UTF-8 is allowed, but without the NUL character, since
 * it cannot be represented in a null-terminated string.
 *
 * If an error occurs, the buffer will contain an empty string.
 *
 * @note If you know the object will be a string before reading it,
 * it is highly recommended to use bjd_expect_utf8_cstr() instead.
 * Alternatively you could use bjd_peek_tag() and call
 * bjd_expect_utf8_cstr() if it's a string.
 *
 * @throws bjd_error_too_big if the string plus null-terminator is larger than the given buffer size
 * @throws bjd_error_type if the string contains invalid UTF-8 or a null byte.
 *
 * @see bjd_peek_tag()
 * @see bjd_expect_utf8_cstr()
 */
void bjd_read_utf8_cstr(bjd_reader_t* reader, char* buf, size_t buffer_size, size_t byte_count);

#ifdef BJDATA_MALLOC
/** @cond */
// This can optionally add a null-terminator, but it does not check
// whether the data contains null bytes. This must be done separately
// in a cstring read function (possibly as part of a UTF-8 check.)
char* bjd_read_bytes_alloc_impl(bjd_reader_t* reader, size_t count, bool null_terminated);
/** @endcond */

/**
 * Reads bytes from a string, binary blob or extension object, allocating
 * storage for them and returning the allocated pointer.
 *
 * The allocated string must be freed with BJDATA_FREE() (or simply free()
 * if BJData's allocator hasn't been customized.)
 *
 * Returns NULL if any error occurs, or if count is zero.
 */
BJDATA_INLINE char* bjd_read_bytes_alloc(bjd_reader_t* reader, size_t count) {
    return bjd_read_bytes_alloc_impl(reader, count, false);
}
#endif

/**
 * Reads bytes from a string, binary blob or extension object in-place in
 * the buffer. This can be used to avoid copying the data.
 *
 * A str, bin or ext must have been opened by a call to bjd_read_tag()
 * which yielded one of these types, or by a call to an expect function
 * such as bjd_expect_str() or bjd_expect_bin().
 *
 * If the bytes are from a string, the string is not null-terminated! Use
 * bjd_read_cstr() to copy the string into a buffer and add a null-terminator.
 *
 * The returned pointer is invalidated on the next read, or when the buffer
 * is destroyed.
 *
 * The reader will move data around in the buffer if needed to ensure that
 * the pointer can always be returned, so this should only be used if
 * count is very small compared to the buffer size. If you need to check
 * whether a small size is reasonable (for example you intend to handle small and
 * large sizes differently), you can call bjd_should_read_bytes_inplace().
 *
 * This can be called multiple times for a single str, bin or ext
 * to read the data in chunks. The total data read must add up
 * to the size of the object.
 *
 * NULL is returned if the reader is in an error state.
 *
 * @throws bjd_error_too_big if the requested size is larger than the buffer size
 *
 * @see bjd_should_read_bytes_inplace()
 */
const char* bjd_read_bytes_inplace(bjd_reader_t* reader, size_t count);

/**
 * Reads bytes from a string in-place in the buffer and ensures they are
 * valid UTF-8. This can be used to avoid copying the data.
 *
 * A string must have been opened by a call to bjd_read_tag() which
 * yielded a string, or by a call to an expect function such as
 * bjd_expect_str().
 *
 * The string is not null-terminated! Use bjd_read_utf8_cstr() to
 * copy the string into a buffer and add a null-terminator.
 *
 * The returned pointer is invalidated on the next read, or when the buffer
 * is destroyed.
 *
 * The reader will move data around in the buffer if needed to ensure that
 * the pointer can always be returned, so this should only be used if
 * count is very small compared to the buffer size. If you need to check
 * whether a small size is reasonable (for example you intend to handle small and
 * large sizes differently), you can call bjd_should_read_bytes_inplace().
 *
 * This does not accept any UTF-8 variant such as Modified UTF-8, CESU-8 or
 * WTF-8. Only pure UTF-8 is allowed.
 *
 * Unlike bjd_read_bytes_inplace(), this cannot be used to read the data in
 * chunks (since this might split a character's UTF-8 bytes, and the
 * reader does not keep track of the UTF-8 decoding state between reads.)
 *
 * NULL is returned if the reader is in an error state.
 *
 * @throws bjd_error_type if the string contains invalid UTF-8
 * @throws bjd_error_too_big if the requested size is larger than the buffer size
 *
 * @see bjd_should_read_bytes_inplace()
 */
const char* bjd_read_utf8_inplace(bjd_reader_t* reader, size_t count);

/**
 * Returns true if it's a good idea to read the given number of bytes
 * in-place.
 *
 * If the read will be larger than some small fraction of the buffer size,
 * this will return false to avoid shuffling too much data back and forth
 * in the buffer.
 *
 * Use this if you're expecting arbitrary size data, and you want to read
 * in-place for the best performance when possible but will fall back to
 * a normal read if the data is too large.
 *
 * @see bjd_read_bytes_inplace()
 */
BJDATA_INLINE bool bjd_should_read_bytes_inplace(bjd_reader_t* reader, size_t count) {
    return (reader->size == 0 || count <= reader->size / BJDATA_READER_SMALL_FRACTION_DENOMINATOR);
}

#if BJDATA_EXTENSIONS
/**
 * Reads a timestamp contained in an ext object of the given size, closing the
 * ext type.
 *
 * An ext object of exttype @ref BJDATA_EXTTYPE_TIMESTAMP must have been opened
 * by a call to e.g. bjd_read_tag() or bjd_expect_ext().
 *
 * You must NOT call bjd_done_ext() after calling this. A timestamp ext
 * object can only contain a single timestamp value, so this calls
 * bjd_done_ext() automatically.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 *
 * @throws bjd_error_invalid if the size is not one of the supported
 * timestamp sizes, or if the nanoseconds are out of range.
 */
bjd_timestamp_t bjd_read_timestamp(bjd_reader_t* reader, size_t size);
#endif

/**
 * @}
 */

/**
 * @name Core Reader Functions
 * @{
 */

#if BJDATA_READ_TRACKING
/**
 * Finishes reading the given type.
 *
 * This will track reads to ensure that the correct number of elements
 * or bytes are read.
 */
void bjd_done_type(bjd_reader_t* reader, bjd_type_t type);
#else
BJDATA_INLINE void bjd_done_type(bjd_reader_t* reader, bjd_type_t type) {
    BJDATA_UNUSED(reader);
    BJDATA_UNUSED(type);
}
#endif

/**
 * Finishes reading an array.
 *
 * This will track reads to ensure that the correct number of elements are read.
 */
BJDATA_INLINE void bjd_done_array(bjd_reader_t* reader) {
    bjd_done_type(reader, bjd_type_array);
}

/**
 * @fn bjd_done_map(bjd_reader_t* reader)
 *
 * Finishes reading a map.
 *
 * This will track reads to ensure that the correct number of elements are read.
 */
BJDATA_INLINE void bjd_done_map(bjd_reader_t* reader) {
    bjd_done_type(reader, bjd_type_map);
}

/**
 * @fn bjd_done_str(bjd_reader_t* reader)
 *
 * Finishes reading a string.
 *
 * This will track reads to ensure that the correct number of bytes are read.
 */
BJDATA_INLINE void bjd_done_str(bjd_reader_t* reader) {
    bjd_done_type(reader, bjd_type_str);
}

/**
 * @fn bjd_done_bin(bjd_reader_t* reader)
 *
 * Finishes reading a binary data blob.
 *
 * This will track reads to ensure that the correct number of bytes are read.
 */
BJDATA_INLINE void bjd_done_bin(bjd_reader_t* reader) {
    bjd_done_type(reader, bjd_type_huge);
}

#if BJDATA_EXTENSIONS
/**
 * @fn bjd_done_ext(bjd_reader_t* reader)
 *
 * Finishes reading an extended type binary data blob.
 *
 * This will track reads to ensure that the correct number of bytes are read.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 */
BJDATA_INLINE void bjd_done_ext(bjd_reader_t* reader) {
    bjd_done_type(reader, bjd_type_ext);
}
#endif

/**
 * Reads and discards the next object. This will read and discard all
 * contained data as well if it is a compound type.
 */
void bjd_discard(bjd_reader_t* reader);

/**
 * @}
 */

/** @cond */

#if BJDATA_DEBUG && BJDATA_STDIO
/**
 * @name Debugging Functions
 * @{
 */
/*
 * Converts a blob of Binary JData to a pseudo-JSON string for debugging
 * purposes, placing the result in the given buffer with a null-terminator.
 *
 * If the buffer does not have enough space, the result will be truncated (but
 * it is guaranteed to be null-terminated.)
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
void bjd_print_data_to_buffer(const char* data, size_t data_size, char* buffer, size_t buffer_size);

/*
 * Converts a node to pseudo-JSON for debugging purposes, calling the given
 * callback as many times as is necessary to output the character data.
 *
 * No null-terminator or trailing newline will be written.
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
void bjd_print_data_to_callback(const char* data, size_t size, bjd_print_callback_t callback, void* context);

/*
 * Converts a blob of Binary JData to pseudo-JSON for debugging purposes
 * and pretty-prints it to the given file.
 */
void bjd_print_data_to_file(const char* data, size_t len, FILE* file);

/*
 * Converts a blob of Binary JData to pseudo-JSON for debugging purposes
 * and pretty-prints it to stdout.
 */
BJDATA_INLINE void bjd_print_data_to_stdout(const char* data, size_t len) {
    bjd_print_data_to_file(data, len, stdout);
}

/*
 * Converts the Binary JData contained in the given `FILE*` to pseudo-JSON for
 * debugging purposes, calling the given callback as many times as is necessary
 * to output the character data.
 */
void bjd_print_stdfile_to_callback(FILE* file, bjd_print_callback_t callback, void* context);

/*
 * Deprecated.
 *
 * \deprecated Renamed to bjd_print_data_to_stdout().
 */
BJDATA_INLINE void bjd_print(const char* data, size_t len) {
    bjd_print_data_to_stdout(data, len);
}

/**
 * @}
 */
#endif

/** @endcond */

/**
 * @}
 */



#if BJDATA_INTERNAL

bool bjd_reader_ensure_straddle(bjd_reader_t* reader, size_t count);

/*
 * Ensures there are at least @c count bytes left in the
 * data, raising an error and returning false if more
 * data cannot be made available.
 */
BJDATA_INLINE bool bjd_reader_ensure(bjd_reader_t* reader, size_t count) {
    bjd_assert(count != 0, "cannot ensure zero bytes!");
    bjd_assert(reader->error == bjd_ok, "reader cannot be in an error state!");

    if (count <= (size_t)(reader->end - reader->data))
        return true;
    return bjd_reader_ensure_straddle(reader, count);
}

void bjd_read_native_straddle(bjd_reader_t* reader, char* p, size_t count);

// Reads count bytes into p, deferring to bjd_read_native_straddle() if more
// bytes are needed than are available in the buffer.
BJDATA_INLINE void bjd_read_native(bjd_reader_t* reader, char* p, size_t count) {
    bjd_assert(count == 0 || p != NULL, "data pointer for %i bytes is NULL", (int)count);

    if (count > (size_t)(reader->end - reader->data)) {
        bjd_read_native_straddle(reader, p, count);
    } else {
        bjd_memcpy(p, reader->data, count);
        reader->data += count;
    }
}

#if BJDATA_READ_TRACKING
#define BJDATA_READER_TRACK(reader, error_expr) \
    (((reader)->error == bjd_ok) ? bjd_reader_flag_if_error((reader), (error_expr)) : (reader)->error)
#else
#define BJDATA_READER_TRACK(reader, error_expr) (BJDATA_UNUSED(reader), bjd_ok)
#endif

BJDATA_INLINE bjd_error_t bjd_reader_track_element(bjd_reader_t* reader) {
    return BJDATA_READER_TRACK(reader, bjd_track_element(&reader->track, true));
}

BJDATA_INLINE bjd_error_t bjd_reader_track_peek_element(bjd_reader_t* reader) {
    return BJDATA_READER_TRACK(reader, bjd_track_peek_element(&reader->track, true));
}

BJDATA_INLINE bjd_error_t bjd_reader_track_bytes(bjd_reader_t* reader, size_t count) {
    BJDATA_UNUSED(count);
    return BJDATA_READER_TRACK(reader, bjd_track_bytes(&reader->track, true, count));
}

BJDATA_INLINE bjd_error_t bjd_reader_track_str_bytes_all(bjd_reader_t* reader, size_t count) {
    BJDATA_UNUSED(count);
    return BJDATA_READER_TRACK(reader, bjd_track_str_bytes_all(&reader->track, true, count));
}

#endif



#endif

BJDATA_EXTERN_C_END
BJDATA_HEADER_END

#endif

