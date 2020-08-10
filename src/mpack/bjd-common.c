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

#include "bjd-common.h"

#if BJDATA_DEBUG && BJDATA_STDIO
#include <stdarg.h>
#endif

const char* bjd_error_to_string(bjd_error_t error) {
    #if BJDATA_STRINGS
    switch (error) {
        #define BJDATA_ERROR_STRING_CASE(e) case e: return #e
        BJDATA_ERROR_STRING_CASE(bjd_ok);
        BJDATA_ERROR_STRING_CASE(bjd_error_io);
        BJDATA_ERROR_STRING_CASE(bjd_error_invalid);
        BJDATA_ERROR_STRING_CASE(bjd_error_unsupported);
        BJDATA_ERROR_STRING_CASE(bjd_error_type);
        BJDATA_ERROR_STRING_CASE(bjd_error_too_big);
        BJDATA_ERROR_STRING_CASE(bjd_error_memory);
        BJDATA_ERROR_STRING_CASE(bjd_error_bug);
        BJDATA_ERROR_STRING_CASE(bjd_error_data);
        BJDATA_ERROR_STRING_CASE(bjd_error_eof);
        #undef BJDATA_ERROR_STRING_CASE
    }
    bjd_assert(0, "unrecognized error %i", (int)error);
    return "(unknown bjd_error_t)";
    #else
    BJDATA_UNUSED(error);
    return "";
    #endif
}

const char* bjd_type_to_string(bjd_type_t type) {
    #if BJDATA_STRINGS
    switch (type) {
        #define BJDATA_TYPE_STRING_CASE(e) case e: return #e
        BJDATA_TYPE_STRING_CASE(bjd_type_missing);
        BJDATA_TYPE_STRING_CASE(bjd_type_nil);
        BJDATA_TYPE_STRING_CASE(bjd_type_bool);
        BJDATA_TYPE_STRING_CASE(bjd_type_float);
        BJDATA_TYPE_STRING_CASE(bjd_type_double);
        BJDATA_TYPE_STRING_CASE(bjd_type_int);
        BJDATA_TYPE_STRING_CASE(bjd_type_uint);
        BJDATA_TYPE_STRING_CASE(bjd_type_str);
        BJDATA_TYPE_STRING_CASE(bjd_type_bin);
        BJDATA_TYPE_STRING_CASE(bjd_type_array);
        BJDATA_TYPE_STRING_CASE(bjd_type_map);
        #if BJDATA_EXTENSIONS
        BJDATA_TYPE_STRING_CASE(bjd_type_ext);
        #endif
        #undef BJDATA_TYPE_STRING_CASE
    }
    bjd_assert(0, "unrecognized type %i", (int)type);
    return "(unknown bjd_type_t)";
    #else
    BJDATA_UNUSED(type);
    return "";
    #endif
}

int bjd_tag_cmp(bjd_tag_t left, bjd_tag_t right) {

    // positive numbers may be stored as int; convert to uint
    if (left.type == bjd_type_int && left.v.i >= 0) {
        left.type = bjd_type_uint;
        left.v.u = (uint64_t)left.v.i;
    }
    if (right.type == bjd_type_int && right.v.i >= 0) {
        right.type = bjd_type_uint;
        right.v.u = (uint64_t)right.v.i;
    }

    if (left.type != right.type)
        return ((int)left.type < (int)right.type) ? -1 : 1;

    switch (left.type) {
        case bjd_type_missing: // fallthrough
        case bjd_type_nil:
            return 0;

        case bjd_type_bool:
            return (int)left.v.b - (int)right.v.b;

        case bjd_type_int:
            if (left.v.i == right.v.i)
                return 0;
            return (left.v.i < right.v.i) ? -1 : 1;

        case bjd_type_uint:
            if (left.v.u == right.v.u)
                return 0;
            return (left.v.u < right.v.u) ? -1 : 1;

        case bjd_type_array:
        case bjd_type_map:
            if (left.v.n == right.v.n)
                return 0;
            return (left.v.n < right.v.n) ? -1 : 1;

        case bjd_type_str:
        case bjd_type_bin:
            if (left.v.l == right.v.l)
                return 0;
            return (left.v.l < right.v.l) ? -1 : 1;

        #if BJDATA_EXTENSIONS
        case bjd_type_ext:
            if (left.exttype == right.exttype) {
                if (left.v.l == right.v.l)
                    return 0;
                return (left.v.l < right.v.l) ? -1 : 1;
            }
            return (int)left.exttype - (int)right.exttype;
        #endif

        // floats should not normally be compared for equality. we compare
        // with memcmp() to silence compiler warnings, but this will return
        // equal if both are NaNs with the same representation (though we may
        // want this, for instance if you are for some bizarre reason using
        // floats as map keys.) i'm not sure what the right thing to
        // do is here. check for NaN first? always return false if the type
        // is float? use operator== and pragmas to silence compiler warning?
        // please send me your suggestions.
        // note also that we don't convert floats to doubles, so when this is
        // used for ordering purposes, all floats are ordered before all
        // doubles.
        case bjd_type_float:
            return bjd_memcmp(&left.v.f, &right.v.f, sizeof(left.v.f));
        case bjd_type_double:
            return bjd_memcmp(&left.v.d, &right.v.d, sizeof(left.v.d));
    }

    bjd_assert(0, "unrecognized type %i", (int)left.type);
    return false;
}

#if BJDATA_DEBUG && BJDATA_STDIO
static char bjd_hex_char(uint8_t hex_value) {
    // Older compilers (e.g. GCC 4.4.7) promote the result of this ternary to
    // int and warn under -Wconversion, so we have to cast it back to char.
    return (char)((hex_value < 10) ? (char)('0' + hex_value) : (char)('a' + (hex_value - 10)));
}

static void bjd_tag_debug_complete_bin_ext(bjd_tag_t tag, size_t string_length, char* buffer, size_t buffer_size,
        const char* prefix, size_t prefix_size)
{
    // If at any point in this function we run out of space in the buffer, we
    // bail out. The outer tag print wrapper will make sure we have a
    // null-terminator.

    if (string_length == 0 || string_length >= buffer_size)
        return;
    buffer += string_length;
    buffer_size -= string_length;

    size_t total = bjd_tag_bytes(&tag);
    if (total == 0) {
        strncpy(buffer, ">", buffer_size);
        return;
    }

    strncpy(buffer, ": ", buffer_size);
    if (buffer_size < 2)
        return;
    buffer += 2;
    buffer_size -= 2;

    size_t hex_bytes = 0;
    for (size_t i = 0; i < BJDATA_PRINT_BYTE_COUNT && i < prefix_size && buffer_size > 2; ++i) {
        uint8_t byte = (uint8_t)prefix[i];
        buffer[0] = bjd_hex_char((uint8_t)(byte >> 4));
        buffer[1] = bjd_hex_char((uint8_t)(byte & 0xfu));
        buffer += 2;
        buffer_size -= 2;
        ++hex_bytes;
    }

    if (buffer_size != 0)
        bjd_snprintf(buffer, buffer_size, "%s>", (total > hex_bytes) ? "..." : "");
}

static void bjd_tag_debug_pseudo_json_bin(bjd_tag_t tag, char* buffer, size_t buffer_size,
        const char* prefix, size_t prefix_size)
{
    bjd_assert(bjd_tag_type(&tag) == bjd_type_bin);
    size_t length = (size_t)bjd_snprintf(buffer, buffer_size, "<binary data of length %u", tag.v.l);
    bjd_tag_debug_complete_bin_ext(tag, length, buffer, buffer_size, prefix, prefix_size);
}

#if BJDATA_EXTENSIONS
static void bjd_tag_debug_pseudo_json_ext(bjd_tag_t tag, char* buffer, size_t buffer_size,
        const char* prefix, size_t prefix_size)
{
    bjd_assert(bjd_tag_type(&tag) == bjd_type_ext);
    size_t length = (size_t)bjd_snprintf(buffer, buffer_size, "<ext data of type %i and length %u",
            bjd_tag_ext_exttype(&tag), bjd_tag_ext_length(&tag));
    bjd_tag_debug_complete_bin_ext(tag, length, buffer, buffer_size, prefix, prefix_size);
}
#endif

static void bjd_tag_debug_pseudo_json_impl(bjd_tag_t tag, char* buffer, size_t buffer_size,
        const char* prefix, size_t prefix_size)
{
    switch (tag.type) {
        case bjd_type_missing:
            bjd_snprintf(buffer, buffer_size, "<missing!>");
            return;
        case bjd_type_nil:
            bjd_snprintf(buffer, buffer_size, "null");
            return;
        case bjd_type_bool:
            bjd_snprintf(buffer, buffer_size, tag.v.b ? "true" : "false");
            return;
        case bjd_type_int:
            bjd_snprintf(buffer, buffer_size, "%" PRIi64, tag.v.i);
            return;
        case bjd_type_uint:
            bjd_snprintf(buffer, buffer_size, "%" PRIu64, tag.v.u);
            return;
        case bjd_type_float:
            bjd_snprintf(buffer, buffer_size, "%f", tag.v.f);
            return;
        case bjd_type_double:
            bjd_snprintf(buffer, buffer_size, "%f", tag.v.d);
            return;

        case bjd_type_str:
            bjd_snprintf(buffer, buffer_size, "<string of %u bytes>", tag.v.l);
            return;
        case bjd_type_bin:
            bjd_tag_debug_pseudo_json_bin(tag, buffer, buffer_size, prefix, prefix_size);
            return;
        #if BJDATA_EXTENSIONS
        case bjd_type_ext:
            bjd_tag_debug_pseudo_json_ext(tag, buffer, buffer_size, prefix, prefix_size);
            return;
        #endif

        case bjd_type_array:
            bjd_snprintf(buffer, buffer_size, "<array of %u elements>", tag.v.n);
            return;
        case bjd_type_map:
            bjd_snprintf(buffer, buffer_size, "<map of %u key-value pairs>", tag.v.n);
            return;
    }

    bjd_snprintf(buffer, buffer_size, "<unknown!>");
}

void bjd_tag_debug_pseudo_json(bjd_tag_t tag, char* buffer, size_t buffer_size,
        const char* prefix, size_t prefix_size)
{
    bjd_assert(buffer_size > 0, "buffer size cannot be zero!");
    buffer[0] = 0;

    bjd_tag_debug_pseudo_json_impl(tag, buffer, buffer_size, prefix, prefix_size);

    // We always null-terminate the buffer manually just in case the snprintf()
    // function doesn't null-terminate when the string doesn't fit.
    buffer[buffer_size - 1] = 0;
}

static void bjd_tag_debug_describe_impl(bjd_tag_t tag, char* buffer, size_t buffer_size) {
    switch (tag.type) {
        case bjd_type_missing:
            bjd_snprintf(buffer, buffer_size, "missing");
            return;
        case bjd_type_nil:
            bjd_snprintf(buffer, buffer_size, "nil");
            return;
        case bjd_type_bool:
            bjd_snprintf(buffer, buffer_size, tag.v.b ? "true" : "false");
            return;
        case bjd_type_int:
            bjd_snprintf(buffer, buffer_size, "int %" PRIi64, tag.v.i);
            return;
        case bjd_type_uint:
            bjd_snprintf(buffer, buffer_size, "uint %" PRIu64, tag.v.u);
            return;
        case bjd_type_float:
            bjd_snprintf(buffer, buffer_size, "float %f", tag.v.f);
            return;
        case bjd_type_double:
            bjd_snprintf(buffer, buffer_size, "double %f", tag.v.d);
            return;
        case bjd_type_str:
            bjd_snprintf(buffer, buffer_size, "str of %u bytes", tag.v.l);
            return;
        case bjd_type_bin:
            bjd_snprintf(buffer, buffer_size, "bin of %u bytes", tag.v.l);
            return;
        #if BJDATA_EXTENSIONS
        case bjd_type_ext:
            bjd_snprintf(buffer, buffer_size, "ext of type %i, %u bytes",
                    bjd_tag_ext_exttype(&tag), bjd_tag_ext_length(&tag));
            return;
        #endif
        case bjd_type_array:
            bjd_snprintf(buffer, buffer_size, "array of %u elements", tag.v.n);
            return;
        case bjd_type_map:
            bjd_snprintf(buffer, buffer_size, "map of %u key-value pairs", tag.v.n);
            return;
    }

    bjd_snprintf(buffer, buffer_size, "unknown!");
}

void bjd_tag_debug_describe(bjd_tag_t tag, char* buffer, size_t buffer_size) {
    bjd_assert(buffer_size > 0, "buffer size cannot be zero!");
    buffer[0] = 0;

    bjd_tag_debug_describe_impl(tag, buffer, buffer_size);

    // We always null-terminate the buffer manually just in case the snprintf()
    // function doesn't null-terminate when the string doesn't fit.
    buffer[buffer_size - 1] = 0;
}
#endif



#if BJDATA_READ_TRACKING || BJDATA_WRITE_TRACKING

#ifndef BJDATA_TRACKING_INITIAL_CAPACITY
// seems like a reasonable number. we grow by doubling, and it only
// needs to be as long as the maximum depth of the message.
#define BJDATA_TRACKING_INITIAL_CAPACITY 8
#endif

bjd_error_t bjd_track_init(bjd_track_t* track) {
    track->count = 0;
    track->capacity = BJDATA_TRACKING_INITIAL_CAPACITY;
    track->elements = (bjd_track_element_t*)BJDATA_MALLOC(sizeof(bjd_track_element_t) * track->capacity);
    if (track->elements == NULL)
        return bjd_error_memory;
    return bjd_ok;
}

bjd_error_t bjd_track_grow(bjd_track_t* track) {
    bjd_assert(track->elements, "null track elements!");
    bjd_assert(track->count == track->capacity, "incorrect growing?");

    size_t new_capacity = track->capacity * 2;

    bjd_track_element_t* new_elements = (bjd_track_element_t*)bjd_realloc(track->elements,
            sizeof(bjd_track_element_t) * track->count, sizeof(bjd_track_element_t) * new_capacity);
    if (new_elements == NULL)
        return bjd_error_memory;

    track->elements = new_elements;
    track->capacity = new_capacity;
    return bjd_ok;
}

bjd_error_t bjd_track_push(bjd_track_t* track, bjd_type_t type, uint32_t count) {
    bjd_assert(track->elements, "null track elements!");
    bjd_log("track pushing %s count %i\n", bjd_type_to_string(type), (int)count);

    // grow if needed
    if (track->count == track->capacity) {
        bjd_error_t error = bjd_track_grow(track);
        if (error != bjd_ok)
            return error;
    }

    // insert new track
    track->elements[track->count].type = type;
    track->elements[track->count].left = count;
    track->elements[track->count].key_needs_value = false;
    ++track->count;
    return bjd_ok;
}

bjd_error_t bjd_track_pop(bjd_track_t* track, bjd_type_t type) {
    bjd_assert(track->elements, "null track elements!");
    bjd_log("track popping %s\n", bjd_type_to_string(type));

    if (track->count == 0) {
        bjd_break("attempting to close a %s but nothing was opened!", bjd_type_to_string(type));
        return bjd_error_bug;
    }

    bjd_track_element_t* element = &track->elements[track->count - 1];

    if (element->type != type) {
        bjd_break("attempting to close a %s but the open element is a %s!",
                bjd_type_to_string(type), bjd_type_to_string(element->type));
        return bjd_error_bug;
    }

    if (element->key_needs_value) {
        bjd_assert(type == bjd_type_map, "key_needs_value can only be true for maps!");
        bjd_break("attempting to close a %s but an odd number of elements were written",
                bjd_type_to_string(type));
        return bjd_error_bug;
    }

    if (element->left != 0) {
        bjd_break("attempting to close a %s but there are %i %s left",
                bjd_type_to_string(type), element->left,
                (type == bjd_type_map || type == bjd_type_array) ? "elements" : "bytes");
        return bjd_error_bug;
    }

    --track->count;
    return bjd_ok;
}

bjd_error_t bjd_track_peek_element(bjd_track_t* track, bool read) {
    BJDATA_UNUSED(read);
    bjd_assert(track->elements, "null track elements!");

    // if there are no open elements, that's fine, we can read/write elements at will
    if (track->count == 0)
        return bjd_ok;

    bjd_track_element_t* element = &track->elements[track->count - 1];

    if (element->type != bjd_type_map && element->type != bjd_type_array) {
        bjd_break("elements cannot be %s within an %s", read ? "read" : "written",
                bjd_type_to_string(element->type));
        return bjd_error_bug;
    }

    if (element->left == 0 && !element->key_needs_value) {
        bjd_break("too many elements %s for %s", read ? "read" : "written",
                bjd_type_to_string(element->type));
        return bjd_error_bug;
    }

    return bjd_ok;
}

bjd_error_t bjd_track_element(bjd_track_t* track, bool read) {
    bjd_error_t error = bjd_track_peek_element(track, read);
    if (track->count == 0 || error != bjd_ok)
        return error;

    bjd_track_element_t* element = &track->elements[track->count - 1];

    if (element->type == bjd_type_map) {
        if (!element->key_needs_value) {
            element->key_needs_value = true;
            return bjd_ok; // don't decrement
        }
        element->key_needs_value = false;
    }

    --element->left;
    return bjd_ok;
}

bjd_error_t bjd_track_bytes(bjd_track_t* track, bool read, size_t count) {
    BJDATA_UNUSED(read);
    bjd_assert(track->elements, "null track elements!");

    if (count > UINT32_MAX) {
        bjd_break("%s more bytes than could possibly fit in a str/bin/ext!",
                read ? "reading" : "writing");
        return bjd_error_bug;
    }

    if (track->count == 0) {
        bjd_break("bytes cannot be %s with no open bin, str or ext", read ? "read" : "written");
        return bjd_error_bug;
    }

    bjd_track_element_t* element = &track->elements[track->count - 1];

    if (element->type == bjd_type_map || element->type == bjd_type_array) {
        bjd_break("bytes cannot be %s within an %s", read ? "read" : "written",
                bjd_type_to_string(element->type));
        return bjd_error_bug;
    }

    if (element->left < count) {
        bjd_break("too many bytes %s for %s", read ? "read" : "written",
                bjd_type_to_string(element->type));
        return bjd_error_bug;
    }

    element->left -= (uint32_t)count;
    return bjd_ok;
}

bjd_error_t bjd_track_str_bytes_all(bjd_track_t* track, bool read, size_t count) {
    bjd_error_t error = bjd_track_bytes(track, read, count);
    if (error != bjd_ok)
        return error;

    bjd_track_element_t* element = &track->elements[track->count - 1];

    if (element->type != bjd_type_str) {
        bjd_break("the open type must be a string, not a %s", bjd_type_to_string(element->type));
        return bjd_error_bug;
    }

    if (element->left != 0) {
        bjd_break("not all bytes were read; the wrong byte count was requested for a string read.");
        return bjd_error_bug;
    }

    return bjd_ok;
}

bjd_error_t bjd_track_check_empty(bjd_track_t* track) {
    if (track->count != 0) {
        bjd_break("unclosed %s", bjd_type_to_string(track->elements[0].type));
        return bjd_error_bug;
    }
    return bjd_ok;
}

bjd_error_t bjd_track_destroy(bjd_track_t* track, bool cancel) {
    bjd_error_t error = cancel ? bjd_ok : bjd_track_check_empty(track);
    if (track->elements) {
        BJDATA_FREE(track->elements);
        track->elements = NULL;
    }
    return error;
}
#endif



static bool bjd_utf8_check_impl(const uint8_t* str, size_t count, bool allow_null) {
    while (count > 0) {
        uint8_t lead = str[0];

        // NUL
        if (!allow_null && lead == '\0') // we don't allow NUL bytes in BJData C-strings
            return false;

        // ASCII
        if (lead <= 0x7F) {
            ++str;
            --count;

        // 2-byte sequence
        } else if ((lead & 0xE0) == 0xC0) {
            if (count < 2) // truncated sequence
                return false;

            uint8_t cont = str[1];
            if ((cont & 0xC0) != 0x80) // not a continuation byte
                return false;

            str += 2;
            count -= 2;

            uint32_t z = ((uint32_t)(lead & ~0xE0) << 6) |
                          (uint32_t)(cont & ~0xC0);

            if (z < 0x80) // overlong sequence
                return false;

        // 3-byte sequence
        } else if ((lead & 0xF0) == 0xE0) {
            if (count < 3) // truncated sequence
                return false;

            uint8_t cont1 = str[1];
            if ((cont1 & 0xC0) != 0x80) // not a continuation byte
                return false;
            uint8_t cont2 = str[2];
            if ((cont2 & 0xC0) != 0x80) // not a continuation byte
                return false;

            str += 3;
            count -= 3;

            uint32_t z = ((uint32_t)(lead  & ~0xF0) << 12) |
                         ((uint32_t)(cont1 & ~0xC0) <<  6) |
                          (uint32_t)(cont2 & ~0xC0);

            if (z < 0x800) // overlong sequence
                return false;
            if (z >= 0xD800 && z <= 0xDFFF) // surrogate
                return false;

        // 4-byte sequence
        } else if ((lead & 0xF8) == 0xF0) {
            if (count < 4) // truncated sequence
                return false;

            uint8_t cont1 = str[1];
            if ((cont1 & 0xC0) != 0x80) // not a continuation byte
                return false;
            uint8_t cont2 = str[2];
            if ((cont2 & 0xC0) != 0x80) // not a continuation byte
                return false;
            uint8_t cont3 = str[3];
            if ((cont3 & 0xC0) != 0x80) // not a continuation byte
                return false;

            str += 4;
            count -= 4;

            uint32_t z = ((uint32_t)(lead  & ~0xF8) << 18) |
                         ((uint32_t)(cont1 & ~0xC0) << 12) |
                         ((uint32_t)(cont2 & ~0xC0) <<  6) |
                          (uint32_t)(cont3 & ~0xC0);

            if (z < 0x10000) // overlong sequence
                return false;
            if (z > 0x10FFFF) // codepoint limit
                return false;

        } else {
            return false; // continuation byte without a lead, or lead for a 5-byte sequence or longer
        }
    }
    return true;
}

bool bjd_utf8_check(const char* str, size_t bytes) {
    return bjd_utf8_check_impl((const uint8_t*)str, bytes, true);
}

bool bjd_utf8_check_no_null(const char* str, size_t bytes) {
    return bjd_utf8_check_impl((const uint8_t*)str, bytes, false);
}

bool bjd_str_check_no_null(const char* str, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i)
        if (str[i] == '\0')
            return false;
    return true;
}

#if BJDATA_DEBUG && BJDATA_STDIO
void bjd_print_append(bjd_print_t* print, const char* data, size_t count) {

    // copy whatever fits into the buffer
    size_t copy = print->size - print->count;
    if (copy > count)
        copy = count;
    bjd_memcpy(print->buffer + print->count, data, copy);
    print->count += copy;
    data += copy;
    count -= copy;

    // if we don't need to flush or can't flush there's nothing else to do
    if (count == 0 || print->callback == NULL)
        return;

    // flush the buffer
    print->callback(print->context, print->buffer, print->count);

    if (count > print->size / 2) {
        // flush the rest of the data
        print->count = 0;
        print->callback(print->context, data, count);
    } else {
        // copy the rest of the data into the buffer
        bjd_memcpy(print->buffer, data, count);
        print->count = count;
    }

}

void bjd_print_flush(bjd_print_t* print) {
    if (print->count > 0 && print->callback != NULL) {
        print->callback(print->context, print->buffer, print->count);
        print->count = 0;
    }
}

void bjd_print_file_callback(void* context, const char* data, size_t count) {
    FILE* file = (FILE*)context;
    fwrite(data, 1, count, file);
}
#endif
