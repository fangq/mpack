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

#include "bjd-node.h"

#if BJDATA_NODE

BJDATA_STATIC_INLINE const char* bjd_node_data_unchecked(bjd_node_t node) {
    bjd_assert(bjd_node_error(node) == bjd_ok, "tree is in an error state!");

    bjd_type_t type = node.data->type;
    BJDATA_UNUSED(type);
    #if BJDATA_EXTENSIONS
    bjd_assert(type == bjd_type_str || type == bjd_type_huge || type == bjd_type_ext,
            "node of type %i (%s) is not a data type!", type, bjd_type_to_string(type));
    #else
    bjd_assert(type == bjd_type_str || type == bjd_type_huge,
            "node of type %i (%s) is not a data type!", type, bjd_type_to_string(type));
    #endif

    return node.tree->data + node.data->value.offset;
}

#if BJDATA_EXTENSIONS
BJDATA_STATIC_INLINE int8_t bjd_node_exttype_unchecked(bjd_node_t node) {
    bjd_assert(bjd_node_error(node) == bjd_ok, "tree is in an error state!");

    bjd_type_t type = node.data->type;
    BJDATA_UNUSED(type);
    bjd_assert(type == bjd_type_ext, "node of type %i (%s) is not an ext type!",
            type, bjd_type_to_string(type));

    // the exttype of an ext node is stored in the byte preceding the data
    return bjd_load_i8(bjd_node_data_unchecked(node) - 1);
}
#endif



/*
 * Tree Parsing
 */

#ifdef BJDATA_MALLOC

// fix up the alloc size to make sure it exactly fits the
// maximum number of nodes it can contain (the allocator will
// waste it back anyway, but we round it down just in case)

#define BJDATA_NODES_PER_PAGE \
    ((BJDATA_NODE_PAGE_SIZE - sizeof(bjd_tree_page_t)) / sizeof(bjd_node_data_t) + 1)

#define BJDATA_PAGE_ALLOC_SIZE \
    (sizeof(bjd_tree_page_t) + sizeof(bjd_node_data_t) * (BJDATA_NODES_PER_PAGE - 1))

#endif

#ifdef BJDATA_MALLOC
/*
 * Fills the tree until we have at least enough bytes for the current node.
 */
static bool bjd_tree_reserve_fill(bjd_tree_t* tree) {
    bjd_assert(tree->parser.state == bjd_tree_parse_state_in_progress);

    size_t bytes = tree->parser.current_node_reserved;
    bjd_assert(bytes > tree->parser.possible_nodes_left,
            "there are already enough bytes! call bjd_tree_ensure() instead.");
    bjd_log("filling to reserve %i bytes\n", (int)bytes);

    // if the necessary bytes would put us over the maximum tree
    // size, fail right away.
    // TODO: check for overflow?
    if (tree->data_length + bytes > tree->max_size) {
        bjd_tree_flag_error(tree, bjd_error_too_big);
        return false;
    }

    // we'll need a read function to fetch more data. if there's
    // no read function, the data should contain an entire message
    // (or messages), so we flag it as invalid.
    if (tree->read_fn == NULL) {
        bjd_log("tree has no read function!\n");
        bjd_tree_flag_error(tree, bjd_error_invalid);
        return false;
    }

    // expand the buffer if needed
    if (tree->data_length + bytes > tree->buffer_capacity) {

        // TODO: check for overflow?
        size_t new_capacity = (tree->buffer_capacity == 0) ? BJDATA_BUFFER_SIZE : tree->buffer_capacity;
        while (new_capacity < tree->data_length + bytes)
            new_capacity *= 2;
        if (new_capacity > tree->max_size)
            new_capacity = tree->max_size;

        bjd_log("expanding buffer from %i to %i\n", (int)tree->buffer_capacity, (int)new_capacity);

        char* new_buffer;
        if (tree->buffer == NULL)
            new_buffer = (char*)BJDATA_MALLOC(new_capacity);
        else
            new_buffer = (char*)bjd_realloc(tree->buffer, tree->data_length, new_capacity);

        if (new_buffer == NULL) {
            bjd_tree_flag_error(tree, bjd_error_memory);
            return false;
        }

        tree->data = new_buffer;
        tree->buffer = new_buffer;
        tree->buffer_capacity = new_capacity;
    }

    // request as much data as possible, looping until we have
    // all the data we need
    do {
        size_t read = tree->read_fn(tree, tree->buffer + tree->data_length, tree->buffer_capacity - tree->data_length);

        // If the fill function encounters an error, it should flag an error on
        // the tree.
        if (bjd_tree_error(tree) != bjd_ok)
            return false;

        // We guard against fill functions that return -1 just in case.
        if (read == (size_t)(-1)) {
            bjd_tree_flag_error(tree, bjd_error_io);
            return false;
        }

        // If the fill function returns 0, the data is not available yet. We
        // return false to stop parsing the current node.
        if (read == 0) {
            bjd_log("not enough data.\n");
            return false;
        }

        bjd_log("read %u more bytes\n", (uint32_t)read);
        tree->data_length += read;
        tree->parser.possible_nodes_left += read;
    } while (tree->parser.possible_nodes_left < bytes);

    return true;
}
#endif

/*
 * Ensures there are enough additional bytes in the tree for the current node
 * (including reserved bytes for the children of this node, and in addition to
 * the reserved bytes for children of previous compound nodes), reading more
 * data if needed.
 *
 * extra_bytes is the number of additional bytes to reserve for the current
 * node beyond the type byte (since one byte is already reserved for each node
 * by its parent array or map.)
 *
 * This may reallocate the tree, which means the tree->data pointer may change!
 *
 * Returns false if not enough bytes could be read.
 */
BJDATA_STATIC_INLINE bool bjd_tree_reserve_bytes(bjd_tree_t* tree, size_t extra_bytes) {
    bjd_assert(tree->parser.state == bjd_tree_parse_state_in_progress);

    // We guard against overflow here. A compound type could declare more than
    // UINT32_MAX contents which overflows SIZE_MAX on 32-bit platforms. We
    // flag bjd_error_invalid instead of bjd_error_too_big since it's far
    // more likely that the message is corrupt than that the data is valid but
    // not parseable on this architecture (see test_read_node_possible() in
    // test-node.c .)
    if ((uint64_t)tree->parser.current_node_reserved + (uint64_t)extra_bytes > SIZE_MAX) {
        bjd_tree_flag_error(tree, bjd_error_invalid);
        return false;
    }

    tree->parser.current_node_reserved += extra_bytes;

    // Note that possible_nodes_left already accounts for reserved bytes for
    // children of previous compound nodes. So even if there are hundreds of
    // bytes left in the buffer, we might need to read anyway.
    if (tree->parser.current_node_reserved <= tree->parser.possible_nodes_left)
        return true;

    #ifdef BJDATA_MALLOC
    return bjd_tree_reserve_fill(tree);
    #else
    return false;
    #endif
}

BJDATA_STATIC_INLINE size_t bjd_tree_parser_stack_capacity(bjd_tree_t* tree) {
    #ifdef BJDATA_MALLOC
    return tree->parser.stack_capacity;
    #else
    return sizeof(tree->parser.stack) / sizeof(tree->parser.stack[0]);
    #endif
}

static bool bjd_tree_push_stack(bjd_tree_t* tree, bjd_node_data_t* first_child, size_t total) {
    bjd_tree_parser_t* parser = &tree->parser;
    bjd_assert(parser->state == bjd_tree_parse_state_in_progress);

    // No need to push empty containers
    if (total == 0)
        return true;

    // Make sure we have enough room in the stack
    if (parser->level + 1 == bjd_tree_parser_stack_capacity(tree)) {
        #ifdef BJDATA_MALLOC
        size_t new_capacity = parser->stack_capacity * 2;
        bjd_log("growing parse stack to capacity %i\n", (int)new_capacity);

        // Replace the stack-allocated parsing stack
        if (!parser->stack_owned) {
            bjd_level_t* new_stack = (bjd_level_t*)BJDATA_MALLOC(sizeof(bjd_level_t) * new_capacity);
            if (!new_stack) {
                bjd_tree_flag_error(tree, bjd_error_memory);
                return false;
            }
            bjd_memcpy(new_stack, parser->stack, sizeof(bjd_level_t) * parser->stack_capacity);
            parser->stack = new_stack;
            parser->stack_owned = true;

        // Realloc the allocated parsing stack
        } else {
            bjd_level_t* new_stack = (bjd_level_t*)bjd_realloc(parser->stack,
                    sizeof(bjd_level_t) * parser->stack_capacity, sizeof(bjd_level_t) * new_capacity);
            if (!new_stack) {
                bjd_tree_flag_error(tree, bjd_error_memory);
                return false;
            }
            parser->stack = new_stack;
        }
        parser->stack_capacity = new_capacity;
        #else
        bjd_tree_flag_error(tree, bjd_error_too_big);
        return false;
        #endif
    }

    // Push the contents of this node onto the parsing stack
    ++parser->level;
    parser->stack[parser->level].child = first_child;
    parser->stack[parser->level].left = total;
    return true;
}

static bool bjd_tree_parse_children(bjd_tree_t* tree, bjd_node_data_t* node) {
    bjd_tree_parser_t* parser = &tree->parser;
    bjd_assert(parser->state == bjd_tree_parse_state_in_progress);

    bjd_type_t type = node->type;
    size_t total = node->len;

    // Calculate total elements to read
    if (type == bjd_type_map) {
        if ((uint64_t)total * 2 > SIZE_MAX) {
            bjd_tree_flag_error(tree, bjd_error_too_big);
            return false;
        }
        total *= 2;
    }

    // Make sure we are under our total node limit (TODO can this overflow?)
    tree->node_count += total;
    if (tree->node_count > tree->max_nodes) {
        bjd_tree_flag_error(tree, bjd_error_too_big);
        return false;
    }

    // Each node is at least one byte. Count these bytes now to make
    // sure there is enough data left.
    if (!bjd_tree_reserve_bytes(tree, total))
        return false;

    // If there are enough nodes left in the current page, no need to grow
    if (total <= parser->nodes_left) {
        node->value.children = parser->nodes;
        parser->nodes += total;
        parser->nodes_left -= total;

    } else {

        #ifdef BJDATA_MALLOC

        // We can't grow if we're using a fixed pool (i.e. we didn't start with a page)
        if (!tree->next) {
            bjd_tree_flag_error(tree, bjd_error_too_big);
            return false;
        }

        // Otherwise we need to grow, and the node's children need to be contiguous.
        // This is a heuristic to decide whether we should waste the remaining space
        // in the current page and start a new one, or give the children their
        // own page. With a fraction of 1/8, this causes at most 12% additional
        // waste. Note that reducing this too much causes less cache coherence and
        // more malloc() overhead due to smaller allocations, so there's a tradeoff
        // here. This heuristic could use some improvement, especially with custom
        // page sizes.

        bjd_tree_page_t* page;

        if (total > BJDATA_NODES_PER_PAGE || parser->nodes_left > BJDATA_NODES_PER_PAGE / 8) {
            // TODO: this should check for overflow
            page = (bjd_tree_page_t*)BJDATA_MALLOC(
                    sizeof(bjd_tree_page_t) + sizeof(bjd_node_data_t) * (total - 1));
            if (page == NULL) {
                bjd_tree_flag_error(tree, bjd_error_memory);
                return false;
            }
            bjd_log("allocated seperate page %p for %i children, %i left in page of %i total\n",
                    (void*)page, (int)total, (int)parser->nodes_left, (int)BJDATA_NODES_PER_PAGE);

            node->value.children = page->nodes;

        } else {
            page = (bjd_tree_page_t*)BJDATA_MALLOC(BJDATA_PAGE_ALLOC_SIZE);
            if (page == NULL) {
                bjd_tree_flag_error(tree, bjd_error_memory);
                return false;
            }
            bjd_log("allocated new page %p for %i children, wasting %i in page of %i total\n",
                    (void*)page, (int)total, (int)parser->nodes_left, (int)BJDATA_NODES_PER_PAGE);

            node->value.children = page->nodes;
            parser->nodes = page->nodes + total;
            parser->nodes_left = BJDATA_NODES_PER_PAGE - total;
        }

        page->next = tree->next;
        tree->next = page;

        #else
        // We can't grow if we don't have an allocator
        bjd_tree_flag_error(tree, bjd_error_too_big);
        return false;
        #endif
    }

    return bjd_tree_push_stack(tree, node->value.children, total);
}

static bool bjd_tree_parse_bytes(bjd_tree_t* tree, bjd_node_data_t* node) {
    node->value.offset = tree->size + tree->parser.current_node_reserved + 1;
    return bjd_tree_reserve_bytes(tree, node->len);
}

#if BJDATA_EXTENSIONS
static bool bjd_tree_parse_ext(bjd_tree_t* tree, bjd_node_data_t* node) {
    // reserve space for exttype
    tree->parser.current_node_reserved += sizeof(int8_t);
    node->type = bjd_type_ext;
    return bjd_tree_parse_bytes(tree, node);
}
#endif

static bool bjd_tree_parse_node_contents(bjd_tree_t* tree, bjd_node_data_t* node) {
    bjd_assert(tree->parser.state == bjd_tree_parse_state_in_progress);
    bjd_assert(node != NULL, "null node?");

    // read the type. we've already accounted for this byte in
    // possible_nodes_left, so we already know it is in bounds, and we don't
    // need to reserve it for this node.
    bjd_assert(tree->data_length > tree->size);
    uint8_t type = bjd_load_u8(tree->data + tree->size);
    bjd_log("node type %x\n", type);
    tree->parser.current_node_reserved = 0;

    // as with bjd_read_tag(), the fastest way to parse a node is to switch
    // on the first byte, and to explicitly list every possible byte. we switch
    // on the first four bits in size-optimized builds.

    #if BJDATA_OPTIMIZE_FOR_SIZE
    switch (type >> 4) {

        // positive fixnum
        case 0x0: case 0x1: case 0x2: case 0x3:
        case 0x4: case 0x5: case 0x6: case 0x7:
            node->type = bjd_type_uint;
            node->value.u = type;
            return true;

        // negative fixnum
        case 0xe: case 0xf:
            node->type = bjd_type_int;
            node->value.i = (int8_t)type;
            return true;

        // fixmap
        case 0x8:
            node->type = bjd_type_map;
            node->len = (uint32_t)(type & ~0xf0);
            return bjd_tree_parse_children(tree, node);

        // fixarray
        case 0x9:
            node->type = bjd_type_array;
            node->len = (uint32_t)(type & ~0xf0);
            return bjd_tree_parse_children(tree, node);

        // fixstr
        case 0xa: case 0xb:
            node->type = bjd_type_str;
            node->len = (uint32_t)(type & ~0xe0);
            return bjd_tree_parse_bytes(tree, node);

        // not one of the common infix types
        default:
            break;
    }
    #endif

    switch (type) {

        #if !BJDATA_OPTIMIZE_FOR_SIZE
        // positive fixnum
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
        case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
        case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            node->type = bjd_type_uint;
            node->value.u = type;
            return true;

        // negative fixnum
        case 0xe0: case 0xe1: case 0xe2: case 0xe3: case 0xe4: case 0xe5: case 0xe6: case 0xe7:
        case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
        case 0xf0: case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf7:
        case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
            node->type = bjd_type_int;
            node->value.i = (int8_t)type;
            return true;

        // fixmap
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f:
            node->type = bjd_type_map;
            node->len = (uint32_t)(type & ~0xf0);
            return bjd_tree_parse_children(tree, node);

        // fixarray
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f:
            node->type = bjd_type_array;
            node->len = (uint32_t)(type & ~0xf0);
            return bjd_tree_parse_children(tree, node);

        // fixstr
        case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0xa6: case 0xa7:
        case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            node->type = bjd_type_str;
            node->len = (uint32_t)(type & ~0xe0);
            return bjd_tree_parse_bytes(tree, node);
        #endif

        // nil
        case 0xc0:
            node->type = bjd_type_nil;
            return true;

        // bool
        case 0xc2: case 0xc3:
            node->type = bjd_type_bool;
            node->value.b = type & 1;
            return true;

        // bin8
        case 0xc4:
            node->type = bjd_type_huge;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint8_t)))
                return false;
            node->len = bjd_load_u8(tree->data + tree->size + 1);
            return bjd_tree_parse_bytes(tree, node);

        // bin16
        case 0xc5:
            node->type = bjd_type_huge;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint16_t)))
                return false;
            node->len = bjd_load_u16(tree->data + tree->size + 1);
            return bjd_tree_parse_bytes(tree, node);

        // bin32
        case 0xc6:
            node->type = bjd_type_huge;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint32_t)))
                return false;
            node->len = bjd_load_u32(tree->data + tree->size + 1);
            return bjd_tree_parse_bytes(tree, node);

        #if BJDATA_EXTENSIONS
        // ext8
        case 0xc7:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint8_t)))
                return false;
            node->len = bjd_load_u8(tree->data + tree->size + 1);
            return bjd_tree_parse_ext(tree, node);

        // ext16
        case 0xc8:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint16_t)))
                return false;
            node->len = bjd_load_u16(tree->data + tree->size + 1);
            return bjd_tree_parse_ext(tree, node);

        // ext32
        case 0xc9:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint32_t)))
                return false;
            node->len = bjd_load_u32(tree->data + tree->size + 1);
            return bjd_tree_parse_ext(tree, node);
        #endif

        // float
        case 0xca:
            if (!bjd_tree_reserve_bytes(tree, sizeof(float)))
                return false;
            node->value.f = bjd_load_float(tree->data + tree->size + 1);
            node->type = bjd_type_float;
            return true;

        // double
        case 0xcb:
            if (!bjd_tree_reserve_bytes(tree, sizeof(double)))
                return false;
            node->value.d = bjd_load_double(tree->data + tree->size + 1);
            node->type = bjd_type_double;
            return true;

        // uint8
        case 0xcc:
            node->type = bjd_type_uint;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint8_t)))
                return false;
            node->value.u = bjd_load_u8(tree->data + tree->size + 1);
            return true;

        // uint16
        case 0xcd:
            node->type = bjd_type_uint;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint16_t)))
                return false;
            node->value.u = bjd_load_u16(tree->data + tree->size + 1);
            return true;

        // uint32
        case 0xce:
            node->type = bjd_type_uint;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint32_t)))
                return false;
            node->value.u = bjd_load_u32(tree->data + tree->size + 1);
            return true;

        // uint64
        case 0xcf:
            node->type = bjd_type_uint;
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint64_t)))
                return false;
            node->value.u = bjd_load_u64(tree->data + tree->size + 1);
            return true;

        // int8
        case 0xd0:
            node->type = bjd_type_int;
            if (!bjd_tree_reserve_bytes(tree, sizeof(int8_t)))
                return false;
            node->value.i = bjd_load_i8(tree->data + tree->size + 1);
            return true;

        // int16
        case 0xd1:
            node->type = bjd_type_int;
            if (!bjd_tree_reserve_bytes(tree, sizeof(int16_t)))
                return false;
            node->value.i = bjd_load_i16(tree->data + tree->size + 1);
            return true;

        // int32
        case 0xd2:
            node->type = bjd_type_int;
            if (!bjd_tree_reserve_bytes(tree, sizeof(int32_t)))
                return false;
            node->value.i = bjd_load_i32(tree->data + tree->size + 1);
            return true;

        // int64
        case 0xd3:
            node->type = bjd_type_int;
            if (!bjd_tree_reserve_bytes(tree, sizeof(int64_t)))
                return false;
            node->value.i = bjd_load_i64(tree->data + tree->size + 1);
            return true;

        #if BJDATA_EXTENSIONS
        // fixext1
        case 0xd4:
            node->len = 1;
            return bjd_tree_parse_ext(tree, node);

        // fixext2
        case 0xd5:
            node->len = 2;
            return bjd_tree_parse_ext(tree, node);

        // fixext4
        case 0xd6:
            node->len = 4;
            return bjd_tree_parse_ext(tree, node);

        // fixext8
        case 0xd7:
            node->len = 8;
            return bjd_tree_parse_ext(tree, node);

        // fixext16
        case 0xd8:
            node->len = 16;
            return bjd_tree_parse_ext(tree, node);
        #endif

        // str8
        case 0xd9:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint8_t)))
                return false;
            node->len = bjd_load_u8(tree->data + tree->size + 1);
            node->type = bjd_type_str;
            return bjd_tree_parse_bytes(tree, node);

        // str16
        case 0xda:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint16_t)))
                return false;
            node->len = bjd_load_u16(tree->data + tree->size + 1);
            node->type = bjd_type_str;
            return bjd_tree_parse_bytes(tree, node);

        // str32
        case 0xdb:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint32_t)))
                return false;
            node->len = bjd_load_u32(tree->data + tree->size + 1);
            node->type = bjd_type_str;
            return bjd_tree_parse_bytes(tree, node);

        // array16
        case 0xdc:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint16_t)))
                return false;
            node->len = bjd_load_u16(tree->data + tree->size + 1);
            node->type = bjd_type_array;
            return bjd_tree_parse_children(tree, node);

        // array32
        case 0xdd:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint32_t)))
                return false;
            node->len = bjd_load_u32(tree->data + tree->size + 1);
            node->type = bjd_type_array;
            return bjd_tree_parse_children(tree, node);

        // map16
        case 0xde:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint16_t)))
                return false;
            node->len = bjd_load_u16(tree->data + tree->size + 1);
            node->type = bjd_type_map;
            return bjd_tree_parse_children(tree, node);

        // map32
        case 0xdf:
            if (!bjd_tree_reserve_bytes(tree, sizeof(uint32_t)))
                return false;
            node->len = bjd_load_u32(tree->data + tree->size + 1);
            node->type = bjd_type_map;
            return bjd_tree_parse_children(tree, node);

        // reserved
        case 0xc1:
            bjd_tree_flag_error(tree, bjd_error_invalid);
            return false;

        #if !BJDATA_EXTENSIONS
        // ext
        case 0xc7: // fallthrough
        case 0xc8: // fallthrough
        case 0xc9: // fallthrough
        // fixext
        case 0xd4: // fallthrough
        case 0xd5: // fallthrough
        case 0xd6: // fallthrough
        case 0xd7: // fallthrough
        case 0xd8:
            bjd_tree_flag_error(tree, bjd_error_unsupported);
            return false;
        #endif

        #if BJDATA_OPTIMIZE_FOR_SIZE
        // any other bytes should have been handled by the infix switch
        default:
            break;
        #endif
    }

    bjd_assert(0, "unreachable");
    return false;
}

static bool bjd_tree_parse_node(bjd_tree_t* tree, bjd_node_data_t* node) {
    bjd_log("parsing a node at position %i in level %i\n",
            (int)tree->size, (int)tree->parser.level);

    if (!bjd_tree_parse_node_contents(tree, node)) {
        bjd_log("node parsing returned false\n");
        return false;
    }

    tree->parser.possible_nodes_left -= tree->parser.current_node_reserved;

    // The reserve for the current node does not include the initial byte
    // previously reserved as part of its parent.
    size_t node_size = tree->parser.current_node_reserved + 1;

    // If the parsed type is a map or array, the reserve includes one byte for
    // each child. We want to subtract these out of possible_nodes_left, but
    // not out of the current size of the tree.
    if (node->type == bjd_type_array)
        node_size -= node->len;
    else if (node->type == bjd_type_map)
        node_size -= node->len * 2;
    tree->size += node_size;

    bjd_log("parsed a node of type %s of %i bytes and "
            "%i additional bytes reserved for children.\n",
            bjd_type_to_string(node->type), (int)node_size,
            (int)tree->parser.current_node_reserved + 1 - (int)node_size);

    return true;
}

/*
 * We read nodes in a loop instead of recursively for maximum performance. The
 * stack holds the amount of children left to read in each level of the tree.
 * Parsing can pause and resume when more data becomes available.
 */
static bool bjd_tree_continue_parsing(bjd_tree_t* tree) {
    if (bjd_tree_error(tree) != bjd_ok)
        return false;

    bjd_tree_parser_t* parser = &tree->parser;
    bjd_assert(parser->state == bjd_tree_parse_state_in_progress);
    bjd_log("parsing tree elements, %i bytes in buffer\n", (int)tree->data_length);

    // we loop parsing nodes until the parse stack is empty. we break
    // by returning out of the function.
    while (true) {
        bjd_node_data_t* node = parser->stack[parser->level].child;
        size_t level = parser->level;
        if (!bjd_tree_parse_node(tree, node))
            return false;
        --parser->stack[level].left;
        ++parser->stack[level].child;

        bjd_assert(bjd_tree_error(tree) == bjd_ok,
                "bjd_tree_parse_node() should have returned false due to error!");

        // pop empty stack levels, exiting the outer loop when the stack is empty.
        // (we could tail-optimize containers by pre-emptively popping empty
        // stack levels before reading the new element, this way we wouldn't
        // have to loop. but we eventually want to use the parse stack to give
        // better error messages that contain the location of the error, so
        // it needs to be complete.)
        while (parser->stack[parser->level].left == 0) {
            if (parser->level == 0)
                return true;
            --parser->level;
        }
    }
}

static void bjd_tree_cleanup(bjd_tree_t* tree) {
    BJDATA_UNUSED(tree);

    #ifdef BJDATA_MALLOC
    if (tree->parser.stack_owned) {
        BJDATA_FREE(tree->parser.stack);
        tree->parser.stack = NULL;
        tree->parser.stack_owned = false;
    }

    bjd_tree_page_t* page = tree->next;
    while (page != NULL) {
        bjd_tree_page_t* next = page->next;
        bjd_log("freeing page %p\n", (void*)page);
        BJDATA_FREE(page);
        page = next;
    }
    tree->next = NULL;
    #endif
}

static bool bjd_tree_parse_start(bjd_tree_t* tree) {
    if (bjd_tree_error(tree) != bjd_ok)
        return false;

    bjd_tree_parser_t* parser = &tree->parser;
    bjd_assert(parser->state != bjd_tree_parse_state_in_progress,
            "previous parsing was not finished!");

    if (parser->state == bjd_tree_parse_state_parsed)
        bjd_tree_cleanup(tree);

    bjd_log("starting parse\n");
    tree->parser.state = bjd_tree_parse_state_in_progress;
    tree->parser.current_node_reserved = 0;

    // check if we previously parsed a tree
    if (tree->size > 0) {
        #ifdef BJDATA_MALLOC
        // if we're buffered, move the remaining data back to the
        // start of the buffer
        // TODO: This is not ideal performance-wise. We should only move data
        // when we need to call the fill function.
        // TODO: We could consider shrinking the buffer here, especially if we
        // determine that the fill function is providing less than a quarter of
        // the buffer size or if messages take up less than a quarter of the
        // buffer size. Maybe this should be configurable.
        if (tree->buffer != NULL) {
            bjd_memmove(tree->buffer, tree->buffer + tree->size, tree->data_length - tree->size);
        }
        else
        #endif
        // otherwise advance past the parsed data
        {
            tree->data += tree->size;
        }
        tree->data_length -= tree->size;
        tree->size = 0;
        tree->node_count = 0;
    }

    // make sure we have at least one byte available before allocating anything
    parser->possible_nodes_left = tree->data_length;
    if (!bjd_tree_reserve_bytes(tree, sizeof(uint8_t))) {
        tree->parser.state = bjd_tree_parse_state_not_started;
        return false;
    }
    bjd_log("parsing tree at %p starting with byte %x\n", tree->data, (uint8_t)tree->data[0]);
    parser->possible_nodes_left -= 1;
    tree->node_count = 1;

    #ifdef BJDATA_MALLOC
    parser->stack = parser->stack_local;
    parser->stack_owned = false;
    parser->stack_capacity = sizeof(parser->stack_local) / sizeof(*parser->stack_local);

    if (tree->pool == NULL) {

        // allocate first page
        bjd_tree_page_t* page = (bjd_tree_page_t*)BJDATA_MALLOC(BJDATA_PAGE_ALLOC_SIZE);
        bjd_log("allocated initial page %p of size %i count %i\n",
                (void*)page, (int)BJDATA_PAGE_ALLOC_SIZE, (int)BJDATA_NODES_PER_PAGE);
        if (page == NULL) {
            tree->error = bjd_error_memory;
            return false;
        }
        page->next = NULL;
        tree->next = page;

        parser->nodes = page->nodes;
        parser->nodes_left = BJDATA_NODES_PER_PAGE;
    }
    else
    #endif
    {
        // otherwise use the provided pool
        bjd_assert(tree->pool != NULL, "no pool provided?");
        parser->nodes = tree->pool;
        parser->nodes_left = tree->pool_count;
    }

    tree->root = parser->nodes;
    ++parser->nodes;
    --parser->nodes_left;

    parser->level = 0;
    parser->stack[0].child = tree->root;
    parser->stack[0].left = 1;

    return true;
}

void bjd_tree_parse(bjd_tree_t* tree) {
    if (bjd_tree_error(tree) != bjd_ok)
        return;

    if (tree->parser.state != bjd_tree_parse_state_in_progress) {
        if (!bjd_tree_parse_start(tree)) {
            bjd_tree_flag_error(tree, (tree->read_fn == NULL) ?
                    bjd_error_invalid : bjd_error_io);
            return;
        }
    }

    if (!bjd_tree_continue_parsing(tree)) {
        if (bjd_tree_error(tree) != bjd_ok)
            return;

        // We're parsing synchronously on a blocking fill function. If we
        // didn't completely finish parsing the tree, it's an error.
        bjd_log("tree parsing incomplete. flagging error.\n");
        bjd_tree_flag_error(tree, (tree->read_fn == NULL) ?
                bjd_error_invalid : bjd_error_io);
        return;
    }

    bjd_assert(bjd_tree_error(tree) == bjd_ok);
    bjd_assert(tree->parser.level == 0);
    tree->parser.state = bjd_tree_parse_state_parsed;
    bjd_log("parsed tree of %i bytes, %i bytes left\n", (int)tree->size, (int)tree->parser.possible_nodes_left);
    bjd_log("%i nodes in final page\n", (int)tree->parser.nodes_left);
}

bool bjd_tree_try_parse(bjd_tree_t* tree) {
    if (bjd_tree_error(tree) != bjd_ok)
        return false;

    if (tree->parser.state != bjd_tree_parse_state_in_progress)
        if (!bjd_tree_parse_start(tree))
            return false;

    if (!bjd_tree_continue_parsing(tree))
        return false;

    bjd_assert(bjd_tree_error(tree) == bjd_ok);
    bjd_assert(tree->parser.level == 0);
    tree->parser.state = bjd_tree_parse_state_parsed;
    return true;
}



/*
 * Tree functions
 */

bjd_node_t bjd_tree_root(bjd_tree_t* tree) {
    if (bjd_tree_error(tree) != bjd_ok)
        return bjd_tree_nil_node(tree);

    // We check that a tree was parsed successfully and assert if not. You must
    // call bjd_tree_parse() (or bjd_tree_try_parse() with a success
    // result) in order to access the root node.
    if (tree->parser.state != bjd_tree_parse_state_parsed) {
        bjd_break("Tree has not been parsed! "
                "Did you call bjd_tree_parse() or bjd_tree_try_parse()?");
        bjd_tree_flag_error(tree, bjd_error_bug);
        return bjd_tree_nil_node(tree);
    }

    return bjd_node(tree, tree->root);
}

static void bjd_tree_init_clear(bjd_tree_t* tree) {
    bjd_memset(tree, 0, sizeof(*tree));
    tree->nil_node.type = bjd_type_nil;
    tree->missing_node.type = bjd_type_missing;
    tree->max_size = SIZE_MAX;
    tree->max_nodes = SIZE_MAX;
}

#ifdef BJDATA_MALLOC
void bjd_tree_init_data(bjd_tree_t* tree, const char* data, size_t length) {
    bjd_tree_init_clear(tree);

    BJDATA_STATIC_ASSERT(BJDATA_NODE_PAGE_SIZE >= sizeof(bjd_tree_page_t),
            "BJDATA_NODE_PAGE_SIZE is too small");

    BJDATA_STATIC_ASSERT(BJDATA_PAGE_ALLOC_SIZE <= BJDATA_NODE_PAGE_SIZE,
            "incorrect page rounding?");

    tree->data = data;
    tree->data_length = length;
    tree->pool = NULL;
    tree->pool_count = 0;
    tree->next = NULL;

    bjd_log("===========================\n");
    bjd_log("initializing tree with data of size %i\n", (int)length);
}
#endif

void bjd_tree_init_pool(bjd_tree_t* tree, const char* data, size_t length,
        bjd_node_data_t* node_pool, size_t node_pool_count)
{
    bjd_tree_init_clear(tree);
    #ifdef BJDATA_MALLOC
    tree->next = NULL;
    #endif

    if (node_pool_count == 0) {
        bjd_break("initial page has no nodes!");
        bjd_tree_flag_error(tree, bjd_error_bug);
        return;
    }

    tree->data = data;
    tree->data_length = length;
    tree->pool = node_pool;
    tree->pool_count = node_pool_count;

    bjd_log("===========================\n");
    bjd_log("initializing tree with data of size %i and pool of count %i\n",
            (int)length, (int)node_pool_count);
}

void bjd_tree_init_error(bjd_tree_t* tree, bjd_error_t error) {
    bjd_tree_init_clear(tree);
    tree->error = error;

    bjd_log("===========================\n");
    bjd_log("initializing tree error state %i\n", (int)error);
}

#ifdef BJDATA_MALLOC
void bjd_tree_init_stream(bjd_tree_t* tree, bjd_tree_read_t read_fn, void* context,
        size_t max_message_size, size_t max_message_nodes) {
    bjd_tree_init_clear(tree);

    tree->read_fn = read_fn;
    tree->context = context;

    bjd_tree_set_limits(tree, max_message_size, max_message_nodes);
    tree->max_size = max_message_size;
    tree->max_nodes = max_message_nodes;

    bjd_log("===========================\n");
    bjd_log("initializing tree with stream, max size %i max nodes %i\n",
            (int)max_message_size, (int)max_message_nodes);
}
#endif

void bjd_tree_set_limits(bjd_tree_t* tree, size_t max_message_size, size_t max_message_nodes) {
    bjd_assert(max_message_size > 0);
    bjd_assert(max_message_nodes > 0);
    tree->max_size = max_message_size;
    tree->max_nodes = max_message_nodes;
}

#if BJDATA_STDIO
typedef struct bjd_file_tree_t {
    char* data;
    size_t size;
    char buffer[BJDATA_BUFFER_SIZE];
} bjd_file_tree_t;

static void bjd_file_tree_teardown(bjd_tree_t* tree) {
    bjd_file_tree_t* file_tree = (bjd_file_tree_t*)tree->context;
    BJDATA_FREE(file_tree->data);
    BJDATA_FREE(file_tree);
}

static bool bjd_file_tree_read(bjd_tree_t* tree, bjd_file_tree_t* file_tree, FILE* file, size_t max_bytes) {

    // get the file size
    errno = 0;
    int error = 0;
    fseek(file, 0, SEEK_END);
    error |= errno;
    long size = ftell(file);
    error |= errno;
    fseek(file, 0, SEEK_SET);
    error |= errno;

    // check for errors
    if (error != 0 || size < 0) {
        bjd_tree_init_error(tree, bjd_error_io);
        return false;
    }
    if (size == 0) {
        bjd_tree_init_error(tree, bjd_error_invalid);
        return false;
    }

    // make sure the size is less than max_bytes
    // (this mess exists to safely convert between long and size_t regardless of their widths)
    if (max_bytes != 0 && (((uint64_t)LONG_MAX > (uint64_t)SIZE_MAX && size > (long)SIZE_MAX) || (size_t)size > max_bytes)) {
        bjd_tree_init_error(tree, bjd_error_too_big);
        return false;
    }

    // allocate data
    file_tree->data = (char*)BJDATA_MALLOC((size_t)size);
    if (file_tree->data == NULL) {
        bjd_tree_init_error(tree, bjd_error_memory);
        return false;
    }

    // read the file
    long total = 0;
    while (total < size) {
        size_t read = fread(file_tree->data + total, 1, (size_t)(size - total), file);
        if (read <= 0) {
            bjd_tree_init_error(tree, bjd_error_io);
            BJDATA_FREE(file_tree->data);
            return false;
        }
        total += (long)read;
    }

    file_tree->size = (size_t)size;
    return true;
}

static bool bjd_tree_file_check_max_bytes(bjd_tree_t* tree, size_t max_bytes) {

    // the C STDIO family of file functions use long (e.g. ftell)
    if (max_bytes > LONG_MAX) {
        bjd_break("max_bytes of %" PRIu64 " is invalid, maximum is LONG_MAX", (uint64_t)max_bytes);
        bjd_tree_init_error(tree, bjd_error_bug);
        return false;
    }

    return true;
}

static void bjd_tree_init_stdfile_noclose(bjd_tree_t* tree, FILE* stdfile, size_t max_bytes) {

    // allocate file tree
    bjd_file_tree_t* file_tree = (bjd_file_tree_t*) BJDATA_MALLOC(sizeof(bjd_file_tree_t));
    if (file_tree == NULL) {
        bjd_tree_init_error(tree, bjd_error_memory);
        return;
    }

    // read all data
    if (!bjd_file_tree_read(tree, file_tree, stdfile, max_bytes)) {
        BJDATA_FREE(file_tree);
        return;
    }

    bjd_tree_init_data(tree, file_tree->data, file_tree->size);
    bjd_tree_set_context(tree, file_tree);
    bjd_tree_set_teardown(tree, bjd_file_tree_teardown);
}

void bjd_tree_init_stdfile(bjd_tree_t* tree, FILE* stdfile, size_t max_bytes, bool close_when_done) {
    if (!bjd_tree_file_check_max_bytes(tree, max_bytes))
        return;

    bjd_tree_init_stdfile_noclose(tree, stdfile, max_bytes);

    if (close_when_done)
        fclose(stdfile);
}

void bjd_tree_init_filename(bjd_tree_t* tree, const char* filename, size_t max_bytes) {
    if (!bjd_tree_file_check_max_bytes(tree, max_bytes))
        return;

    // open the file
    FILE* file = fopen(filename, "rb");
    if (!file) {
        bjd_tree_init_error(tree, bjd_error_io);
        return;
    }

    bjd_tree_init_stdfile(tree, file, max_bytes, true);
}
#endif

bjd_error_t bjd_tree_destroy(bjd_tree_t* tree) {
    bjd_tree_cleanup(tree);

    #ifdef BJDATA_MALLOC
    if (tree->buffer)
        BJDATA_FREE(tree->buffer);
    #endif

    if (tree->teardown)
        tree->teardown(tree);
    tree->teardown = NULL;

    return tree->error;
}

void bjd_tree_flag_error(bjd_tree_t* tree, bjd_error_t error) {
    if (tree->error == bjd_ok) {
        bjd_log("tree %p setting error %i: %s\n", (void*)tree, (int)error, bjd_error_to_string(error));
        tree->error = error;
        if (tree->error_fn)
            tree->error_fn(tree, error);
    }

}



/*
 * Node misc functions
 */

void bjd_node_flag_error(bjd_node_t node, bjd_error_t error) {
    bjd_tree_flag_error(node.tree, error);
}

bjd_tag_t bjd_node_tag(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return bjd_tag_nil();

    bjd_tag_t tag = BJDATA_TAG_ZERO;

    tag.type = node.data->type;
    switch (node.data->type) {
        case bjd_type_missing:
            // If a node is missing, I don't know if it makes sense to ask for
            // a tag for it. We'll return a missing tag to match the missing
            // node I guess, but attempting to use the tag for anything (like
            // writing it for example) will flag bjd_error_bug.
            break;
        case bjd_type_nil:                                            break;
        case bjd_type_bool:    tag.v.b = node.data->value.b;          break;
        case bjd_type_float:   tag.v.f = node.data->value.f;          break;
        case bjd_type_double:  tag.v.d = node.data->value.d;          break;
        case bjd_type_int:     tag.v.i = node.data->value.i;          break;
        case bjd_type_uint:    tag.v.u = node.data->value.u;          break;

        case bjd_type_str:     tag.v.l = node.data->len;     break;
        case bjd_type_huge:     tag.v.l = node.data->len;     break;

        #if BJDATA_EXTENSIONS
        case bjd_type_ext:
            tag.v.l = node.data->len;
            tag.exttype = bjd_node_exttype_unchecked(node);
            break;
        #endif

        case bjd_type_array:   tag.v.n = node.data->len;  break;
        case bjd_type_map:     tag.v.n = node.data->len;  break;

        default:
            bjd_assert(0, "unrecognized type %i", (int)node.data->type);
            break;
    }
    return tag;
}

#if BJDATA_DEBUG && BJDATA_STDIO
static void bjd_node_print_element(bjd_node_t node, bjd_print_t* print, size_t depth) {
    bjd_node_data_t* data = node.data;
    switch (data->type) {
        case bjd_type_str:
            {
                bjd_print_append_cstr(print, "\"");
                const char* bytes = bjd_node_data_unchecked(node);
                for (size_t i = 0; i < data->len; ++i) {
                    char c = bytes[i];
                    switch (c) {
                        case '\n': bjd_print_append_cstr(print, "\\n"); break;
                        case '\\': bjd_print_append_cstr(print, "\\\\"); break;
                        case '"': bjd_print_append_cstr(print, "\\\""); break;
                        default: bjd_print_append(print, &c, 1); break;
                    }
                }
                bjd_print_append_cstr(print, "\"");
            }
            break;

        case bjd_type_array:
            bjd_print_append_cstr(print, "[\n");
            for (size_t i = 0; i < data->len; ++i) {
                for (size_t j = 0; j < depth + 1; ++j)
                    bjd_print_append_cstr(print, "    ");
                bjd_node_print_element(bjd_node_array_at(node, i), print, depth + 1);
                if (i != data->len - 1)
                    bjd_print_append_cstr(print, ",");
                bjd_print_append_cstr(print, "\n");
            }
            for (size_t i = 0; i < depth; ++i)
                bjd_print_append_cstr(print, "    ");
            bjd_print_append_cstr(print, "]");
            break;

        case bjd_type_map:
            bjd_print_append_cstr(print, "{\n");
            for (size_t i = 0; i < data->len; ++i) {
                for (size_t j = 0; j < depth + 1; ++j)
                    bjd_print_append_cstr(print, "    ");
                bjd_node_print_element(bjd_node_map_key_at(node, i), print, depth + 1);
                bjd_print_append_cstr(print, ": ");
                bjd_node_print_element(bjd_node_map_value_at(node, i), print, depth + 1);
                if (i != data->len - 1)
                    bjd_print_append_cstr(print, ",");
                bjd_print_append_cstr(print, "\n");
            }
            for (size_t i = 0; i < depth; ++i)
                bjd_print_append_cstr(print, "    ");
            bjd_print_append_cstr(print, "}");
            break;

        default:
            {
                const char* prefix = NULL;
                size_t prefix_length = 0;
                if (bjd_node_type(node) == bjd_type_huge
                        #if BJDATA_EXTENSIONS
                        || bjd_node_type(node) == bjd_type_ext
                        #endif
                ) {
                    prefix = bjd_node_data(node);
                    prefix_length = bjd_node_data_len(node);
                }

                char buf[256];
                bjd_tag_t tag = bjd_node_tag(node);
                bjd_tag_debug_pseudo_json(tag, buf, sizeof(buf), prefix, prefix_length);
                bjd_print_append_cstr(print, buf);
            }
            break;
    }
}

void bjd_node_print_to_buffer(bjd_node_t node, char* buffer, size_t buffer_size) {
    if (buffer_size == 0) {
        bjd_assert(false, "buffer size is zero!");
        return;
    }

    bjd_print_t print;
    bjd_memset(&print, 0, sizeof(print));
    print.buffer = buffer;
    print.size = buffer_size;
    bjd_node_print_element(node, &print, 0);
    bjd_print_append(&print, "",  1); // null-terminator
    bjd_print_flush(&print);

    // we always make sure there's a null-terminator at the end of the buffer
    // in case we ran out of space.
    print.buffer[print.size - 1] = '\0';
}

void bjd_node_print_to_callback(bjd_node_t node, bjd_print_callback_t callback, void* context) {
    char buffer[1024];
    bjd_print_t print;
    bjd_memset(&print, 0, sizeof(print));
    print.buffer = buffer;
    print.size = sizeof(buffer);
    print.callback = callback;
    print.context = context;
    bjd_node_print_element(node, &print, 0);
    bjd_print_flush(&print);
}

void bjd_node_print_to_file(bjd_node_t node, FILE* file) {
    bjd_assert(file != NULL, "file is NULL");

    char buffer[1024];
    bjd_print_t print;
    bjd_memset(&print, 0, sizeof(print));
    print.buffer = buffer;
    print.size = sizeof(buffer);
    print.callback = &bjd_print_file_callback;
    print.context = file;

    size_t depth = 2;
    for (size_t i = 0; i < depth; ++i)
        bjd_print_append_cstr(&print, "    ");
    bjd_node_print_element(node, &print, depth);
    bjd_print_append_cstr(&print, "\n");
    bjd_print_flush(&print);
}
#endif
 

 
/*
 * Node Value Functions
 */

#if BJDATA_EXTENSIONS
bjd_timestamp_t bjd_node_timestamp(bjd_node_t node) {
    bjd_timestamp_t timestamp = {0, 0};

    // we'll let bjd_node_exttype() do most checks
    if (bjd_node_exttype(node) != BJDATA_EXTTYPE_TIMESTAMP) {
        bjd_log("exttype %i\n", bjd_node_exttype(node));
        bjd_node_flag_error(node, bjd_error_type);
        return timestamp;
    }

    const char* p = bjd_node_data_unchecked(node);

    switch (node.data->len) {
        case 4:
            timestamp.nanoseconds = 0;
            timestamp.seconds = bjd_load_u32(p);
            break;

        case 8: {
            uint64_t value = bjd_load_u64(p);
            timestamp.nanoseconds = (uint32_t)(value >> 34);
            timestamp.seconds = value & ((UINT64_C(1) << 34) - 1);
            break;
        }

        case 12:
            timestamp.nanoseconds = bjd_load_u32(p);
            timestamp.seconds = bjd_load_i64(p + 4);
            break;

        default:
            bjd_tree_flag_error(node.tree, bjd_error_invalid);
            return timestamp;
    }

    if (timestamp.nanoseconds > BJDATA_TIMESTAMP_NANOSECONDS_MAX) {
        bjd_tree_flag_error(node.tree, bjd_error_invalid);
        bjd_timestamp_t zero = {0, 0};
        return zero;
    }

    return timestamp;
}

int64_t bjd_node_timestamp_seconds(bjd_node_t node) {
    return bjd_node_timestamp(node).seconds;
}

uint32_t bjd_node_timestamp_nanoseconds(bjd_node_t node) {
    return bjd_node_timestamp(node).nanoseconds;
}
#endif



/*
 * Node Data Functions
 */

void bjd_node_check_utf8(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return;
    bjd_node_data_t* data = node.data;
    if (data->type != bjd_type_str || !bjd_utf8_check(bjd_node_data_unchecked(node), data->len))
        bjd_node_flag_error(node, bjd_error_type);
}

void bjd_node_check_utf8_cstr(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return;
    bjd_node_data_t* data = node.data;
    if (data->type != bjd_type_str || !bjd_utf8_check_no_null(bjd_node_data_unchecked(node), data->len))
        bjd_node_flag_error(node, bjd_error_type);
}

size_t bjd_node_copy_data(bjd_node_t node, char* buffer, size_t bufsize) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    bjd_assert(bufsize == 0 || buffer != NULL, "buffer is NULL for maximum of %i bytes", (int)bufsize);

    bjd_type_t type = node.data->type;
    if (type != bjd_type_str && type != bjd_type_huge
            #if BJDATA_EXTENSIONS
            && type != bjd_type_ext
            #endif
    ) {
        bjd_node_flag_error(node, bjd_error_type);
        return 0;
    }

    if (node.data->len > bufsize) {
        bjd_node_flag_error(node, bjd_error_too_big);
        return 0;
    }

    bjd_memcpy(buffer, bjd_node_data_unchecked(node), node.data->len);
    return (size_t)node.data->len;
}

size_t bjd_node_copy_utf8(bjd_node_t node, char* buffer, size_t bufsize) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    bjd_assert(bufsize == 0 || buffer != NULL, "buffer is NULL for maximum of %i bytes", (int)bufsize);

    bjd_type_t type = node.data->type;
    if (type != bjd_type_str) {
        bjd_node_flag_error(node, bjd_error_type);
        return 0;
    }

    if (node.data->len > bufsize) {
        bjd_node_flag_error(node, bjd_error_too_big);
        return 0;
    }

    if (!bjd_utf8_check(bjd_node_data_unchecked(node), node.data->len)) {
        bjd_node_flag_error(node, bjd_error_type);
        return 0;
    }

    bjd_memcpy(buffer, bjd_node_data_unchecked(node), node.data->len);
    return (size_t)node.data->len;
}

void bjd_node_copy_cstr(bjd_node_t node, char* buffer, size_t bufsize) {

    // we can't break here because the error isn't recoverable; we
    // have to add a null-terminator.
    bjd_assert(buffer != NULL, "buffer is NULL");
    bjd_assert(bufsize >= 1, "buffer size is zero; you must have room for at least a null-terminator");

    if (bjd_node_error(node) != bjd_ok) {
        buffer[0] = '\0';
        return;
    }

    if (node.data->type != bjd_type_str) {
        buffer[0] = '\0';
        bjd_node_flag_error(node, bjd_error_type);
        return;
    }

    if (node.data->len > bufsize - 1) {
        buffer[0] = '\0';
        bjd_node_flag_error(node, bjd_error_too_big);
        return;
    }

    if (!bjd_str_check_no_null(bjd_node_data_unchecked(node), node.data->len)) {
        buffer[0] = '\0';
        bjd_node_flag_error(node, bjd_error_type);
        return;
    }

    bjd_memcpy(buffer, bjd_node_data_unchecked(node), node.data->len);
    buffer[node.data->len] = '\0';
}

void bjd_node_copy_utf8_cstr(bjd_node_t node, char* buffer, size_t bufsize) {

    // we can't break here because the error isn't recoverable; we
    // have to add a null-terminator.
    bjd_assert(buffer != NULL, "buffer is NULL");
    bjd_assert(bufsize >= 1, "buffer size is zero; you must have room for at least a null-terminator");

    if (bjd_node_error(node) != bjd_ok) {
        buffer[0] = '\0';
        return;
    }

    if (node.data->type != bjd_type_str) {
        buffer[0] = '\0';
        bjd_node_flag_error(node, bjd_error_type);
        return;
    }

    if (node.data->len > bufsize - 1) {
        buffer[0] = '\0';
        bjd_node_flag_error(node, bjd_error_too_big);
        return;
    }

    if (!bjd_utf8_check_no_null(bjd_node_data_unchecked(node), node.data->len)) {
        buffer[0] = '\0';
        bjd_node_flag_error(node, bjd_error_type);
        return;
    }

    bjd_memcpy(buffer, bjd_node_data_unchecked(node), node.data->len);
    buffer[node.data->len] = '\0';
}

#ifdef BJDATA_MALLOC
char* bjd_node_data_alloc(bjd_node_t node, size_t maxlen) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    // make sure this is a valid data type
    bjd_type_t type = node.data->type;
    if (type != bjd_type_str && type != bjd_type_huge
            #if BJDATA_EXTENSIONS
            && type != bjd_type_ext
            #endif
    ) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    if (node.data->len > maxlen) {
        bjd_node_flag_error(node, bjd_error_too_big);
        return NULL;
    }

    char* ret = (char*) BJDATA_MALLOC((size_t)node.data->len);
    if (ret == NULL) {
        bjd_node_flag_error(node, bjd_error_memory);
        return NULL;
    }

    bjd_memcpy(ret, bjd_node_data_unchecked(node), node.data->len);
    return ret;
}

char* bjd_node_cstr_alloc(bjd_node_t node, size_t maxlen) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    // make sure maxlen makes sense
    if (maxlen < 1) {
        bjd_break("maxlen is zero; you must have room for at least a null-terminator");
        bjd_node_flag_error(node, bjd_error_bug);
        return NULL;
    }

    if (node.data->type != bjd_type_str) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    if (node.data->len > maxlen - 1) {
        bjd_node_flag_error(node, bjd_error_too_big);
        return NULL;
    }

    if (!bjd_str_check_no_null(bjd_node_data_unchecked(node), node.data->len)) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    char* ret = (char*) BJDATA_MALLOC((size_t)(node.data->len + 1));
    if (ret == NULL) {
        bjd_node_flag_error(node, bjd_error_memory);
        return NULL;
    }

    bjd_memcpy(ret, bjd_node_data_unchecked(node), node.data->len);
    ret[node.data->len] = '\0';
    return ret;
}

char* bjd_node_utf8_cstr_alloc(bjd_node_t node, size_t maxlen) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    // make sure maxlen makes sense
    if (maxlen < 1) {
        bjd_break("maxlen is zero; you must have room for at least a null-terminator");
        bjd_node_flag_error(node, bjd_error_bug);
        return NULL;
    }

    if (node.data->type != bjd_type_str) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    if (node.data->len > maxlen - 1) {
        bjd_node_flag_error(node, bjd_error_too_big);
        return NULL;
    }

    if (!bjd_utf8_check_no_null(bjd_node_data_unchecked(node), node.data->len)) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    char* ret = (char*) BJDATA_MALLOC((size_t)(node.data->len + 1));
    if (ret == NULL) {
        bjd_node_flag_error(node, bjd_error_memory);
        return NULL;
    }

    bjd_memcpy(ret, bjd_node_data_unchecked(node), node.data->len);
    ret[node.data->len] = '\0';
    return ret;
}
#endif


/*
 * Compound Node Functions
 */

static bjd_node_data_t* bjd_node_map_int_impl(bjd_node_t node, int64_t num) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    if (node.data->type != bjd_type_map) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    bjd_node_data_t* found = NULL;

    for (size_t i = 0; i < node.data->len; ++i) {
        bjd_node_data_t* key = bjd_node_child(node, i * 2);

        if ((key->type == bjd_type_int && key->value.i == num) ||
            (key->type == bjd_type_uint && num >= 0 && key->value.u == (uint64_t)num))
        {
            if (found) {
                bjd_node_flag_error(node, bjd_error_data);
                return NULL;
            }
            found = bjd_node_child(node, i * 2 + 1);
        }
    }

    if (found)
        return found;

    return NULL;
}

static bjd_node_data_t* bjd_node_map_uint_impl(bjd_node_t node, uint64_t num) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    if (node.data->type != bjd_type_map) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    bjd_node_data_t* found = NULL;

    for (size_t i = 0; i < node.data->len; ++i) {
        bjd_node_data_t* key = bjd_node_child(node, i * 2);

        if ((key->type == bjd_type_uint && key->value.u == num) ||
            (key->type == bjd_type_int && key->value.i >= 0 && (uint64_t)key->value.i == num))
        {
            if (found) {
                bjd_node_flag_error(node, bjd_error_data);
                return NULL;
            }
            found = bjd_node_child(node, i * 2 + 1);
        }
    }

    if (found)
        return found;

    return NULL;
}

static bjd_node_data_t* bjd_node_map_str_impl(bjd_node_t node, const char* str, size_t length) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    bjd_assert(length == 0 || str != NULL, "str of length %i is NULL", (int)length);

    if (node.data->type != bjd_type_map) {
        bjd_node_flag_error(node, bjd_error_type);
        return NULL;
    }

    bjd_tree_t* tree = node.tree;
    bjd_node_data_t* found = NULL;

    for (size_t i = 0; i < node.data->len; ++i) {
        bjd_node_data_t* key = bjd_node_child(node, i * 2);

        if (key->type == bjd_type_str && key->len == length &&
                bjd_memcmp(str, bjd_node_data_unchecked(bjd_node(tree, key)), length) == 0) {
            if (found) {
                bjd_node_flag_error(node, bjd_error_data);
                return NULL;
            }
            found = bjd_node_child(node, i * 2 + 1);
        }
    }

    if (found)
        return found;

    return NULL;
}

static bjd_node_t bjd_node_wrap_lookup(bjd_tree_t* tree, bjd_node_data_t* data) {
    if (!data) {
        if (tree->error == bjd_ok)
            bjd_tree_flag_error(tree, bjd_error_data);
        return bjd_tree_nil_node(tree);
    }
    return bjd_node(tree, data);
}

static bjd_node_t bjd_node_wrap_lookup_optional(bjd_tree_t* tree, bjd_node_data_t* data) {
    if (!data) {
        if (tree->error == bjd_ok)
            return bjd_tree_missing_node(tree);
        return bjd_tree_nil_node(tree);
    }
    return bjd_node(tree, data);
}

bjd_node_t bjd_node_map_int(bjd_node_t node, int64_t num) {
    return bjd_node_wrap_lookup(node.tree, bjd_node_map_int_impl(node, num));
}

bjd_node_t bjd_node_map_int_optional(bjd_node_t node, int64_t num) {
    return bjd_node_wrap_lookup_optional(node.tree, bjd_node_map_int_impl(node, num));
}

bjd_node_t bjd_node_map_uint(bjd_node_t node, uint64_t num) {
    return bjd_node_wrap_lookup(node.tree, bjd_node_map_uint_impl(node, num));
}

bjd_node_t bjd_node_map_uint_optional(bjd_node_t node, uint64_t num) {
    return bjd_node_wrap_lookup_optional(node.tree, bjd_node_map_uint_impl(node, num));
}

bjd_node_t bjd_node_map_str(bjd_node_t node, const char* str, size_t length) {
    return bjd_node_wrap_lookup(node.tree, bjd_node_map_str_impl(node, str, length));
}

bjd_node_t bjd_node_map_str_optional(bjd_node_t node, const char* str, size_t length) {
    return bjd_node_wrap_lookup_optional(node.tree, bjd_node_map_str_impl(node, str, length));
}

bjd_node_t bjd_node_map_cstr(bjd_node_t node, const char* cstr) {
    bjd_assert(cstr != NULL, "cstr is NULL");
    return bjd_node_map_str(node, cstr, bjd_strlen(cstr));
}

bjd_node_t bjd_node_map_cstr_optional(bjd_node_t node, const char* cstr) {
    bjd_assert(cstr != NULL, "cstr is NULL");
    return bjd_node_map_str_optional(node, cstr, bjd_strlen(cstr));
}

bool bjd_node_map_contains_int(bjd_node_t node, int64_t num) {
    return bjd_node_map_int_impl(node, num) != NULL;
}

bool bjd_node_map_contains_uint(bjd_node_t node, uint64_t num) {
    return bjd_node_map_uint_impl(node, num) != NULL;
}

bool bjd_node_map_contains_str(bjd_node_t node, const char* str, size_t length) {
    return bjd_node_map_str_impl(node, str, length) != NULL;
}

bool bjd_node_map_contains_cstr(bjd_node_t node, const char* cstr) {
    bjd_assert(cstr != NULL, "cstr is NULL");
    return bjd_node_map_contains_str(node, cstr, bjd_strlen(cstr));
}

size_t bjd_node_enum_optional(bjd_node_t node, const char* strings[], size_t count) {
    if (bjd_node_error(node) != bjd_ok)
        return count;

    // the value is only recognized if it is a string
    if (bjd_node_type(node) != bjd_type_str)
        return count;

    // fetch the string
    const char* key = bjd_node_str(node);
    size_t keylen = bjd_node_strlen(node);
    bjd_assert(bjd_node_error(node) == bjd_ok, "these should not fail");

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

size_t bjd_node_enum(bjd_node_t node, const char* strings[], size_t count) {
    size_t value = bjd_node_enum_optional(node, strings, count);
    if (value == count)
        bjd_node_flag_error(node, bjd_error_type);
    return value;
}

bjd_type_t bjd_node_type(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return bjd_type_nil;
    return node.data->type;
}

bool bjd_node_is_nil(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok) {
        // All nodes are treated as nil nodes when we are in error.
        return true;
    }
    return node.data->type == bjd_type_nil;
}

bool bjd_node_is_missing(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok) {
        // errors still return nil nodes, not missing nodes.
        return false;
    }
    return node.data->type == bjd_type_missing;
}

void bjd_node_nil(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return;
    if (node.data->type != bjd_type_nil)
        bjd_node_flag_error(node, bjd_error_type);
}

void bjd_node_missing(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return;
    if (node.data->type != bjd_type_missing)
        bjd_node_flag_error(node, bjd_error_type);
}

bool bjd_node_bool(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return false;

    if (node.data->type == bjd_type_bool)
        return node.data->value.b;

    bjd_node_flag_error(node, bjd_error_type);
    return false;
}

void bjd_node_true(bjd_node_t node) {
    if (bjd_node_bool(node) != true)
        bjd_node_flag_error(node, bjd_error_type);
}

void bjd_node_false(bjd_node_t node) {
    if (bjd_node_bool(node) != false)
        bjd_node_flag_error(node, bjd_error_type);
}

uint8_t bjd_node_u8(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= UINT8_MAX)
            return (uint8_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= 0 && node.data->value.i <= UINT8_MAX)
            return (uint8_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

int8_t bjd_node_i8(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= INT8_MAX)
            return (int8_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= INT8_MIN && node.data->value.i <= INT8_MAX)
            return (int8_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

uint16_t bjd_node_u16(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= UINT16_MAX)
            return (uint16_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= 0 && node.data->value.i <= UINT16_MAX)
            return (uint16_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

int16_t bjd_node_i16(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= INT16_MAX)
            return (int16_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= INT16_MIN && node.data->value.i <= INT16_MAX)
            return (int16_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

uint32_t bjd_node_u32(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= UINT32_MAX)
            return (uint32_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= 0 && node.data->value.i <= UINT32_MAX)
            return (uint32_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

int32_t bjd_node_i32(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= INT32_MAX)
            return (int32_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= INT32_MIN && node.data->value.i <= INT32_MAX)
            return (int32_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

uint64_t bjd_node_u64(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        return node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        if (node.data->value.i >= 0)
            return (uint64_t)node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

int64_t bjd_node_i64(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_uint) {
        if (node.data->value.u <= (uint64_t)INT64_MAX)
            return (int64_t)node.data->value.u;
    } else if (node.data->type == bjd_type_int) {
        return node.data->value.i;
    }

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

unsigned int bjd_node_uint(bjd_node_t node) {

    // This should be true at compile-time, so this just wraps the 32-bit function.
    if (sizeof(unsigned int) == 4)
        return (unsigned int)bjd_node_u32(node);

    // Otherwise we use u64 and check the range.
    uint64_t val = bjd_node_u64(node);
    if (val <= UINT_MAX)
        return (unsigned int)val;

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

int bjd_node_int(bjd_node_t node) {

    // This should be true at compile-time, so this just wraps the 32-bit function.
    if (sizeof(int) == 4)
        return (int)bjd_node_i32(node);

    // Otherwise we use i64 and check the range.
    int64_t val = bjd_node_i64(node);
    if (val >= INT_MIN && val <= INT_MAX)
        return (int)val;

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

float bjd_node_float(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0.0f;

    if (node.data->type == bjd_type_uint)
        return (float)node.data->value.u;
    else if (node.data->type == bjd_type_int)
        return (float)node.data->value.i;
    else if (node.data->type == bjd_type_float)
        return node.data->value.f;
    else if (node.data->type == bjd_type_double)
        return (float)node.data->value.d;

    bjd_node_flag_error(node, bjd_error_type);
    return 0.0f;
}

double bjd_node_double(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0.0;

    if (node.data->type == bjd_type_uint)
        return (double)node.data->value.u;
    else if (node.data->type == bjd_type_int)
        return (double)node.data->value.i;
    else if (node.data->type == bjd_type_float)
        return (double)node.data->value.f;
    else if (node.data->type == bjd_type_double)
        return node.data->value.d;

    bjd_node_flag_error(node, bjd_error_type);
    return 0.0;
}

float bjd_node_float_strict(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0.0f;

    if (node.data->type == bjd_type_float)
        return node.data->value.f;

    bjd_node_flag_error(node, bjd_error_type);
    return 0.0f;
}

double bjd_node_double_strict(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0.0;

    if (node.data->type == bjd_type_float)
        return (double)node.data->value.f;
    else if (node.data->type == bjd_type_double)
        return node.data->value.d;

    bjd_node_flag_error(node, bjd_error_type);
    return 0.0;
}

#if BJDATA_EXTENSIONS
int8_t bjd_node_exttype(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_ext)
        return bjd_node_exttype_unchecked(node);

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}
#endif

uint32_t bjd_node_data_len(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    bjd_type_t type = node.data->type;
    if (type == bjd_type_str || type == bjd_type_huge
            #if BJDATA_EXTENSIONS
            || type == bjd_type_ext
            #endif
            )
        return (uint32_t)node.data->len;

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

size_t bjd_node_strlen(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_str)
        return (size_t)node.data->len;

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

const char* bjd_node_str(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    bjd_type_t type = node.data->type;
    if (type == bjd_type_str)
        return bjd_node_data_unchecked(node);

    bjd_node_flag_error(node, bjd_error_type);
    return NULL;
}

const char* bjd_node_data(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    bjd_type_t type = node.data->type;
    if (type == bjd_type_str || type == bjd_type_huge
            #if BJDATA_EXTENSIONS
            || type == bjd_type_ext
            #endif
            )
        return bjd_node_data_unchecked(node);

    bjd_node_flag_error(node, bjd_error_type);
    return NULL;
}

const char* bjd_node_bin_data(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return NULL;

    if (node.data->type == bjd_type_huge)
        return bjd_node_data_unchecked(node);

    bjd_node_flag_error(node, bjd_error_type);
    return NULL;
}

size_t bjd_node_bin_size(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type == bjd_type_huge)
        return (size_t)node.data->len;

    bjd_node_flag_error(node, bjd_error_type);
    return 0;
}

size_t bjd_node_array_length(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type != bjd_type_array) {
        bjd_node_flag_error(node, bjd_error_type);
        return 0;
    }

    return (size_t)node.data->len;
}

bjd_node_t bjd_node_array_at(bjd_node_t node, size_t index) {
    if (bjd_node_error(node) != bjd_ok)
        return bjd_tree_nil_node(node.tree);

    if (node.data->type != bjd_type_array) {
        bjd_node_flag_error(node, bjd_error_type);
        return bjd_tree_nil_node(node.tree);
    }

    if (index >= node.data->len) {
        bjd_node_flag_error(node, bjd_error_data);
        return bjd_tree_nil_node(node.tree);
    }

    return bjd_node(node.tree, bjd_node_child(node, index));
}

size_t bjd_node_map_count(bjd_node_t node) {
    if (bjd_node_error(node) != bjd_ok)
        return 0;

    if (node.data->type != bjd_type_map) {
        bjd_node_flag_error(node, bjd_error_type);
        return 0;
    }

    return node.data->len;
}

// internal node map lookup
static bjd_node_t bjd_node_map_at(bjd_node_t node, size_t index, size_t offset) {
    if (bjd_node_error(node) != bjd_ok)
        return bjd_tree_nil_node(node.tree);

    if (node.data->type != bjd_type_map) {
        bjd_node_flag_error(node, bjd_error_type);
        return bjd_tree_nil_node(node.tree);
    }

    if (index >= node.data->len) {
        bjd_node_flag_error(node, bjd_error_data);
        return bjd_tree_nil_node(node.tree);
    }

    return bjd_node(node.tree, bjd_node_child(node, index * 2 + offset));
}

bjd_node_t bjd_node_map_key_at(bjd_node_t node, size_t index) {
    return bjd_node_map_at(node, index, 0);
}

bjd_node_t bjd_node_map_value_at(bjd_node_t node, size_t index) {
    return bjd_node_map_at(node, index, 1);
}

#endif
