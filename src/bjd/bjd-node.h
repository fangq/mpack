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
 * Declares the BJData dynamic Node API.
 */

#ifndef BJDATA_NODE_H
#define BJDATA_NODE_H 1

#include "bjd-reader.h"

BJDATA_HEADER_START
BJDATA_EXTERN_C_START

#if BJDATA_NODE

/**
 * @defgroup node Node API
 *
 * The BJData Node API allows you to parse a chunk of Binary JData into a
 * dynamically typed data structure, providing random access to the parsed
 * data.
 *
 * See @ref docs/node.md for examples.
 *
 * @{
 */

/**
 * A handle to node data in a parsed BJData tree.
 *
 * Nodes represent either primitive values or compound types. If a
 * node is a compound type, it contains a pointer to its child nodes,
 * or a pointer to its underlying data.
 *
 * Nodes are immutable.
 *
 * @note @ref bjd_node_t is an opaque reference to the node data, not the
 * node data itself. (It contains pointers to both the node data and the tree.)
 * It is passed by value in the Node API.
 */
typedef struct bjd_node_t bjd_node_t;

/**
 * The storage for nodes in an BJData tree.
 *
 * You only need to use this if you intend to provide your own storage
 * for nodes instead of letting the tree allocate it.
 *
 * @ref bjd_node_data_t is 16 bytes on most common architectures (32-bit
 * and 64-bit.)
 */
typedef struct bjd_node_data_t bjd_node_data_t;

/**
 * An BJData tree parser to parse a blob or stream of Binary JData.
 *
 * When a message is parsed, the tree contains a single root node which
 * contains all parsed data. The tree and its nodes are immutable.
 */
typedef struct bjd_tree_t bjd_tree_t;

/**
 * An error handler function to be called when an error is flagged on
 * the tree.
 *
 * The error handler will only be called once on the first error flagged;
 * any subsequent node reads and errors are ignored, and the tree is
 * permanently in that error state.
 *
 * BJData is safe against non-local jumps out of error handler callbacks.
 * This means you are allowed to longjmp or throw an exception (in C++,
 * Objective-C, or with SEH) out of this callback.
 *
 * Bear in mind when using longjmp that local non-volatile variables that
 * have changed are undefined when setjmp() returns, so you can't put the
 * tree on the stack in the same activation frame as the setjmp without
 * declaring it volatile.
 *
 * You must still eventually destroy the tree. It is not destroyed
 * automatically when an error is flagged. It is safe to destroy the
 * tree within this error callback, but you will either need to perform
 * a non-local jump, or store something in your context to identify
 * that the tree is destroyed since any future accesses to it cause
 * undefined behavior.
 */
typedef void (*bjd_tree_error_t)(bjd_tree_t* tree, bjd_error_t error);

/**
 * The BJData tree's read function. It should fill the buffer with as many bytes
 * as are immediately available up to the given @c count, returning the number
 * of bytes written to the buffer.
 *
 * In case of error, it should flag an appropriate error on the reader
 * (usually @ref bjd_error_io.)
 *
 * The blocking or non-blocking behaviour of the read should match whether you
 * are using bjd_tree_parse() or bjd_tree_try_parse().
 *
 * If you are using bjd_tree_parse(), the read should block until at least
 * one byte is read. If you return 0, bjd_tree_parse() will raise @ref
 * bjd_error_io.
 *
 * If you are using bjd_tree_try_parse(), the read function can always
 * return 0, and must never block waiting for data (otherwise
 * bjd_tree_try_parse() would be equivalent to bjd_tree_parse().)
 * When you return 0, bjd_tree_try_parse() will return false without flagging
 * an error.
 */
typedef size_t (*bjd_tree_read_t)(bjd_tree_t* tree, char* buffer, size_t count);

/**
 * A teardown function to be called when the tree is destroyed.
 */
typedef void (*bjd_tree_teardown_t)(bjd_tree_t* tree);



/* Hide internals from documentation */
/** @cond */

struct bjd_node_t {
    bjd_node_data_t* data;
    bjd_tree_t* tree;
};

struct bjd_node_data_t {
    bjd_type_t type;

    /*
     * The element count if the type is an array;
     * the number of key/value pairs if the type is map;
     * or the number of bytes if the type is str, bin or ext.
     */
    uint32_t len;

    union
    {
        bool     b; /* The value if the type is bool. */
        float    f; /* The value if the type is float. */
        double   d; /* The value if the type is double. */
        int64_t  i; /* The value if the type is signed int. */
        uint64_t u; /* The value if the type is unsigned int. */
        size_t offset; /* The byte offset for str, bin and ext */
        bjd_node_data_t* children; /* The children for map or array */
    } value;
};

typedef struct bjd_tree_page_t {
    struct bjd_tree_page_t* next;
    bjd_node_data_t nodes[1]; // variable size
} bjd_tree_page_t;

typedef enum bjd_tree_parse_state_t {
    bjd_tree_parse_state_not_started,
    bjd_tree_parse_state_in_progress,
    bjd_tree_parse_state_parsed,
} bjd_tree_parse_state_t;

typedef struct bjd_level_t {
    bjd_node_data_t* child;
    size_t left; // children left in level
} bjd_level_t;

typedef struct bjd_tree_parser_t {
    bjd_tree_parse_state_t state;

    // We keep track of the number of "possible nodes" left in the data rather
    // than the number of bytes.
    //
    // When a map or array is parsed, we ensure at least one byte for each child
    // exists and subtract them right away. This ensures that if ever a map or
    // array declares more elements than could possibly be contained in the data,
    // we will error out immediately rather than allocating storage for them.
    //
    // For example malicious data that repeats 0xDE 0xFF 0xFF (start of a map
    // with 65536 key-value pairs) would otherwise cause us to run out of
    // memory. With this, the parser can allocate at most as many nodes as
    // there are bytes in the data (plus the paging overhead, 12%.) An error
    // will be flagged immediately if and when there isn't enough data left to
    // fully read all children of all open compound types on the parsing stack.
    //
    // Once an entire message has been parsed (and there are no nodes left to
    // parse whose bytes have been subtracted), this matches the number of left
    // over bytes in the data.
    size_t possible_nodes_left;

    bjd_node_data_t* nodes; // next node in current page/pool
    size_t nodes_left; // nodes left in current page/pool

    size_t current_node_reserved;
    size_t level;

    #ifdef BJDATA_MALLOC
    // It's much faster to allocate the initial parsing stack inline within the
    // parser. We replace it with a heap allocation if we need to grow it.
    bjd_level_t* stack;
    size_t stack_capacity;
    bool stack_owned;
    bjd_level_t stack_local[BJDATA_NODE_INITIAL_DEPTH];
    #else
    // Without malloc(), we have to reserve a parsing stack the maximum allowed
    // parsing depth.
    bjd_level_t stack[BJDATA_NODE_MAX_DEPTH_WITHOUT_MALLOC];
    #endif
} bjd_tree_parser_t;

struct bjd_tree_t {
    bjd_tree_error_t error_fn;    /* Function to call on error */
    bjd_tree_read_t read_fn;      /* Function to call to read more data */
    bjd_tree_teardown_t teardown; /* Function to teardown the context on destroy */
    void* context;                  /* Context for tree callbacks */

    bjd_node_data_t nil_node;     /* a nil node to be returned in case of error */
    bjd_node_data_t missing_node; /* a missing node to be returned in optional lookups */
    bjd_error_t error;

    #ifdef BJDATA_MALLOC
    char* buffer;
    size_t buffer_capacity;
    #endif

    const char* data;
    size_t data_length; // length of data (and content of buffer, if used)

    size_t size; // size in bytes of tree (usually matches data_length, but not if tree has trailing data)
    size_t node_count; // total number of nodes in tree (across all pages)

    size_t max_size;  // maximum message size
    size_t max_nodes; // maximum nodes in a message

    bjd_tree_parser_t parser;
    bjd_node_data_t* root;

    bjd_node_data_t* pool; // pool, or NULL if no pool provided
    size_t pool_count;

    #ifdef BJDATA_MALLOC
    bjd_tree_page_t* next;
    #endif
};

// internal functions

BJDATA_INLINE bjd_node_t bjd_node(bjd_tree_t* tree, bjd_node_data_t* data) {
    bjd_node_t node;
    node.data = data;
    node.tree = tree;
    return node;
}

BJDATA_INLINE bjd_node_data_t* bjd_node_child(bjd_node_t node, size_t child) {
    return node.data->value.children + child;
}

BJDATA_INLINE bjd_node_t bjd_tree_nil_node(bjd_tree_t* tree) {
    return bjd_node(tree, &tree->nil_node);
}

BJDATA_INLINE bjd_node_t bjd_tree_missing_node(bjd_tree_t* tree) {
    return bjd_node(tree, &tree->missing_node);
}

/** @endcond */



/**
 * @name Tree Initialization
 * @{
 */

#ifdef BJDATA_MALLOC
/**
 * Initializes a tree parser with the given data.
 *
 * Configure the tree if desired, then call bjd_tree_parse() to parse it. The
 * tree will allocate pages of nodes as needed and will free them when
 * destroyed.
 *
 * The tree must be destroyed with bjd_tree_destroy().
 *
 * Any string or blob data types reference the original data, so the given data
 * pointer must remain valid until after the tree is destroyed.
 */
void bjd_tree_init_data(bjd_tree_t* tree, const char* data, size_t length);

/**
 * Deprecated.
 *
 * \deprecated Renamed to bjd_tree_init_data().
 */
BJDATA_INLINE void bjd_tree_init(bjd_tree_t* tree, const char* data, size_t length) {
    bjd_tree_init_data(tree, data, length);
}

/**
 * Initializes a tree parser from an unbounded stream, or a stream of
 * unknown length.
 *
 * The parser can be used to read a single message from a stream of unknown
 * length, or multiple messages from an unbounded stream, allowing it to
 * be used for RPC communication. Call @ref bjd_tree_parse() to parse
 * a message from a blocking stream, or @ref bjd_tree_try_parse() for a
 * non-blocking stream.
 *
 * The stream will use a growable internal buffer to store the most recent
 * message, as well as allocated pages of nodes for the parse tree.
 *
 * Maximum allowances for message size and node count must be specified in this
 * function (since the stream is unbounded.) They can be changed later with
 * @ref bjd_tree_set_limits().
 *
 * @param tree The tree parser
 * @param read_fn The read function
 * @param context The context for the read function
 * @param max_message_size The maximum size of a message in bytes
 * @param max_message_nodes The maximum number of nodes per message. See
 *        @ref bjd_node_data_t for the size of nodes.
 *
 * @see bjd_tree_read_t
 * @see bjd_reader_context()
 */
void bjd_tree_init_stream(bjd_tree_t* tree, bjd_tree_read_t read_fn, void* context,
        size_t max_message_size, size_t max_message_nodes);
#endif

/**
 * Initializes a tree parser with the given data, using the given node data
 * pool to store the results.
 *
 * Configure the tree if desired, then call bjd_tree_parse() to parse it.
 *
 * If the data does not fit in the pool, @ref bjd_error_too_big will be flagged
 * on the tree.
 *
 * The tree must be destroyed with bjd_tree_destroy(), even if parsing fails.
 */
void bjd_tree_init_pool(bjd_tree_t* tree, const char* data, size_t length,
        bjd_node_data_t* node_pool, size_t node_pool_count);

/**
 * Initializes an BJData tree directly into an error state. Use this if you
 * are writing a wrapper to another <tt>bjd_tree_init*()</tt> function which
 * can fail its setup.
 */
void bjd_tree_init_error(bjd_tree_t* tree, bjd_error_t error);

#if BJDATA_STDIO
/**
 * Initializes a tree to parse the given file. The tree must be destroyed with
 * bjd_tree_destroy(), even if parsing fails.
 *
 * The file is opened, loaded fully into memory, and closed before this call
 * returns.
 *
 * @param tree The tree to initialize
 * @param filename The filename passed to fopen() to read the file
 * @param max_bytes The maximum size of file to load, or 0 for unlimited size.
 */
void bjd_tree_init_filename(bjd_tree_t* tree, const char* filename, size_t max_bytes);

/**
 * Deprecated.
 *
 * \deprecated Renamed to bjd_tree_init_filename().
 */
BJDATA_INLINE void bjd_tree_init_file(bjd_tree_t* tree, const char* filename, size_t max_bytes) {
    bjd_tree_init_filename(tree, filename, max_bytes);
}

/**
 * Initializes a tree to parse the given libc FILE. This can be used to
 * read from stdin, or from a file opened separately.
 *
 * The tree must be destroyed with bjd_tree_destroy(), even if parsing fails.
 *
 * The FILE is fully loaded fully into memory (and closed if requested) before
 * this call returns.
 *
 * @param tree The tree to initialize.
 * @param stdfile The FILE.
 * @param max_bytes The maximum size of file to load, or 0 for unlimited size.
 * @param close_when_done If true, fclose() will be called on the FILE when it
 *         is no longer needed. If false, the file will not be closed when
 *         reading is done.
 *
 * @warning The tree will read all data in the FILE before parsing it. If this
 *          is used on stdin, the parser will block until it is closed, even if
 *          a complete message has been written to it!
 */
void bjd_tree_init_stdfile(bjd_tree_t* tree, FILE* stdfile, size_t max_bytes, bool close_when_done);
#endif

/**
 * @}
 */

/**
 * @name Tree Functions
 * @{
 */

/**
 * Sets the maximum byte size and maximum number of nodes allowed per message.
 *
 * The default is SIZE_MAX (no limit) unless @ref bjd_tree_init_stream() is
 * called (where maximums are required.)
 *
 * If a pool of nodes is used, the node limit is the lesser of this limit and
 * the pool size.
 *
 * @param tree The tree parser
 * @param max_message_size The maximum size of a message in bytes
 * @param max_message_nodes The maximum number of nodes per message. See
 *        @ref bjd_node_data_t for the size of nodes.
 */
void bjd_tree_set_limits(bjd_tree_t* tree, size_t max_message_size,
        size_t max_message_nodes);

/**
 * Parses a Binary JData message into a tree of immutable nodes.
 *
 * If successful, the root node will be available under @ref bjd_tree_root().
 * If not, an appropriate error will be flagged.
 *
 * This can be called repeatedly to parse a series of messages from a data
 * source. When this is called, all previous nodes from this tree and their
 * contents (including the root node) are invalidated.
 *
 * If this is called with a stream (see @ref bjd_tree_init_stream()), the
 * stream must block until data is available. (Otherwise, if this is called on
 * a non-blocking stream, parsing will fail with @ref bjd_error_io when the
 * fill function returns 0.)
 *
 * There is no way to recover a tree in an error state. It must be destroyed.
 */
void bjd_tree_parse(bjd_tree_t* tree);

/**
 * Attempts to parse a Binary JData message from a non-blocking stream into a
 * tree of immutable nodes.
 *
 * A non-blocking read function must have been passed to the tree in
 * bjd_tree_init_stream().
 *
 * If this returns true, a message is available under
 * @ref bjd_tree_root(). The tree nodes and data will be valid until
 * the next time a parse is started.
 *
 * If this returns false, no message is available, because either not enough
 * data is available yet or an error has occurred. You must check the tree for
 * errors whenever this returns false. If there is no error, you should try
 * again later when more data is available. (You will want to select()/poll()
 * on the underlying socket or use some other asynchronous mechanism to
 * determine when it has data.)
 *
 * There is no way to recover a tree in an error state. It must be destroyed.
 *
 * @see bjd_tree_init_stream()
 */
bool bjd_tree_try_parse(bjd_tree_t* tree);

/**
 * Returns the root node of the tree, if the tree is not in an error state.
 * Returns a nil node otherwise.
 *
 * @warning You must call bjd_tree_parse() before calling this. If
 * @ref bjd_tree_parse() was never called, the tree will assert.
 */
bjd_node_t bjd_tree_root(bjd_tree_t* tree);

/**
 * Returns the error state of the tree.
 */
BJDATA_INLINE bjd_error_t bjd_tree_error(bjd_tree_t* tree) {
    return tree->error;
}

/**
 * Returns the size in bytes of the current parsed message.
 *
 * If there is something in the buffer after the Binary JData object, this can
 * be used to find it.
 *
 * This is zero if an error occurred during tree parsing (since the
 * portion of the data that the first complete object occupies cannot
 * be determined if the data is invalid or corrupted.)
 */
BJDATA_INLINE size_t bjd_tree_size(bjd_tree_t* tree) {
    return tree->size;
}

/**
 * Destroys the tree.
 */
bjd_error_t bjd_tree_destroy(bjd_tree_t* tree);

/**
 * Sets the custom pointer to pass to the tree callbacks, such as teardown.
 *
 * @param tree The BJData tree.
 * @param context User data to pass to the tree callbacks.
 *
 * @see bjd_reader_context()
 */
BJDATA_INLINE void bjd_tree_set_context(bjd_tree_t* tree, void* context) {
    tree->context = context;
}

/**
 * Returns the custom context for tree callbacks.
 *
 * @see bjd_tree_set_context
 * @see bjd_tree_init_stream
 */
BJDATA_INLINE void* bjd_tree_context(bjd_tree_t* tree) {
    return tree->context;
}

/**
 * Sets the error function to call when an error is flagged on the tree.
 *
 * This should normally be used with bjd_tree_set_context() to register
 * a custom pointer to pass to the error function.
 *
 * See the definition of bjd_tree_error_t for more information about
 * what you can do from an error callback.
 *
 * @see bjd_tree_error_t
 * @param tree The BJData tree.
 * @param error_fn The function to call when an error is flagged on the tree.
 */
BJDATA_INLINE void bjd_tree_set_error_handler(bjd_tree_t* tree, bjd_tree_error_t error_fn) {
    tree->error_fn = error_fn;
}

/**
 * Sets the teardown function to call when the tree is destroyed.
 *
 * This should normally be used with bjd_tree_set_context() to register
 * a custom pointer to pass to the teardown function.
 *
 * @param tree The BJData tree.
 * @param teardown The function to call when the tree is destroyed.
 */
BJDATA_INLINE void bjd_tree_set_teardown(bjd_tree_t* tree, bjd_tree_teardown_t teardown) {
    tree->teardown = teardown;
}

/**
 * Places the tree in the given error state, calling the error callback if one
 * is set.
 *
 * This allows you to externally flag errors, for example if you are validating
 * data as you read it.
 *
 * If the tree is already in an error state, this call is ignored and no
 * error callback is called.
 */
void bjd_tree_flag_error(bjd_tree_t* tree, bjd_error_t error);

/**
 * @}
 */

/**
 * @name Node Core Functions
 * @{
 */

/**
 * Places the node's tree in the given error state, calling the error callback
 * if one is set.
 *
 * This allows you to externally flag errors, for example if you are validating
 * data as you read it.
 *
 * If the tree is already in an error state, this call is ignored and no
 * error callback is called.
 */
void bjd_node_flag_error(bjd_node_t node, bjd_error_t error);

/**
 * Returns the error state of the node's tree.
 */
BJDATA_INLINE bjd_error_t bjd_node_error(bjd_node_t node) {
    return bjd_tree_error(node.tree);
}

/**
 * Returns a tag describing the given node, or a nil tag if the
 * tree is in an error state.
 */
bjd_tag_t bjd_node_tag(bjd_node_t node);

/** @cond */

#if BJDATA_DEBUG && BJDATA_STDIO
/*
 * Converts a node to a pseudo-JSON string for debugging purposes, placing the
 * result in the given buffer with a null-terminator.
 *
 * If the buffer does not have enough space, the result will be truncated (but
 * it is guaranteed to be null-terminated.)
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
void bjd_node_print_to_buffer(bjd_node_t node, char* buffer, size_t buffer_size);

/*
 * Converts a node to pseudo-JSON for debugging purposes, calling the given
 * callback as many times as is necessary to output the character data.
 *
 * No null-terminator or trailing newline will be written.
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
void bjd_node_print_to_callback(bjd_node_t node, bjd_print_callback_t callback, void* context);

/*
 * Converts a node to pseudo-JSON for debugging purposes
 * and pretty-prints it to the given file.
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
void bjd_node_print_to_file(bjd_node_t node, FILE* file);

/*
 * Converts a node to pseudo-JSON for debugging purposes
 * and pretty-prints it to stdout.
 *
 * This is only available in debug mode, and only if stdio is available (since
 * it uses snprintf().) It's strictly for debugging purposes.
 */
BJDATA_INLINE void bjd_node_print_to_stdout(bjd_node_t node) {
    bjd_node_print_to_file(node, stdout);
}

/*
 * Deprecated.
 *
 * \deprecated Renamed to bjd_node_print_to_stdout().
 */
BJDATA_INLINE void bjd_node_print(bjd_node_t node) {
    bjd_node_print_to_stdout(node);
}
#endif

/** @endcond */

/**
 * @}
 */

/**
 * @name Node Primitive Value Functions
 * @{
 */

/**
 * Returns the type of the node.
 */
bjd_type_t bjd_node_type(bjd_node_t node);

/**
 * Returns true if the given node is a nil node; false otherwise.
 *
 * To ensure that a node is nil and flag an error otherwise, use
 * bjd_node_nil().
 */
bool bjd_node_is_nil(bjd_node_t node);

/**
 * Returns true if the given node handle indicates a missing node; false otherwise.
 *
 * To ensure that a node is missing and flag an error otherwise, use
 * bjd_node_missing().
 */
bool bjd_node_is_missing(bjd_node_t node);

/**
 * Checks that the given node is of nil type, raising @ref bjd_error_type
 * otherwise.
 *
 * Use bjd_node_is_nil() to return whether the node is nil.
 */
void bjd_node_nil(bjd_node_t node);

/**
 * Checks that the given node indicates a missing node, raising @ref
 * bjd_error_type otherwise.
 *
 * Use bjd_node_is_missing() to return whether the node is missing.
 */
void bjd_node_missing(bjd_node_t node);

/**
 * Returns the bool value of the node. If this node is not of the correct
 * type, false is returned and bjd_error_type is raised.
 */
bool bjd_node_bool(bjd_node_t node);

/**
 * Checks if the given node is of bool type with value true, raising
 * bjd_error_type otherwise.
 */
void bjd_node_true(bjd_node_t node);

/**
 * Checks if the given node is of bool type with value false, raising
 * bjd_error_type otherwise.
 */
void bjd_node_false(bjd_node_t node);

/**
 * Returns the 8-bit unsigned value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
uint8_t bjd_node_u8(bjd_node_t node);

/**
 * Returns the 8-bit signed value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
int8_t bjd_node_i8(bjd_node_t node);

/**
 * Returns the 16-bit unsigned value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
uint16_t bjd_node_u16(bjd_node_t node);

/**
 * Returns the 16-bit signed value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
int16_t bjd_node_i16(bjd_node_t node);

/**
 * Returns the 32-bit unsigned value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
uint32_t bjd_node_u32(bjd_node_t node);

/**
 * Returns the 32-bit signed value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
int32_t bjd_node_i32(bjd_node_t node);

/**
 * Returns the 64-bit unsigned value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised, and zero is returned.
 */
uint64_t bjd_node_u64(bjd_node_t node);

/**
 * Returns the 64-bit signed value of the node. If this node is not
 * of a compatible type, @ref bjd_error_type is raised and zero is returned.
 */
int64_t bjd_node_i64(bjd_node_t node);

/**
 * Returns the unsigned int value of the node.
 *
 * Returns zero if an error occurs.
 *
 * @throws bjd_error_type If the node is not an integer type or does not fit in the range of an unsigned int
 */
unsigned int bjd_node_uint(bjd_node_t node);

/**
 * Returns the int value of the node.
 *
 * Returns zero if an error occurs.
 *
 * @throws bjd_error_type If the node is not an integer type or does not fit in the range of an int
 */
int bjd_node_int(bjd_node_t node);

/**
 * Returns the float value of the node. The underlying value can be an
 * integer, float or double; the value is converted to a float.
 *
 * @note Reading a double or a large integer with this function can incur a
 * loss of precision.
 *
 * @throws bjd_error_type if the underlying value is not a float, double or integer.
 */
float bjd_node_float(bjd_node_t node);

/**
 * Returns the double value of the node. The underlying value can be an
 * integer, float or double; the value is converted to a double.
 *
 * @note Reading a very large integer with this function can incur a
 * loss of precision.
 *
 * @throws bjd_error_type if the underlying value is not a float, double or integer.
 */
double bjd_node_double(bjd_node_t node);

/**
 * Returns the float value of the node. The underlying value must be a float,
 * not a double or an integer. This ensures no loss of precision can occur.
 *
 * @throws bjd_error_type if the underlying value is not a float.
 */
float bjd_node_float_strict(bjd_node_t node);

/**
 * Returns the double value of the node. The underlying value must be a float
 * or double, not an integer. This ensures no loss of precision can occur.
 *
 * @throws bjd_error_type if the underlying value is not a float or double.
 */
double bjd_node_double_strict(bjd_node_t node);

#if BJDATA_EXTENSIONS
/**
 * Returns a timestamp.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 *
 * @throws bjd_error_type if the underlying value is not a timestamp.
 */
bjd_timestamp_t bjd_node_timestamp(bjd_node_t node);

/**
 * Returns a timestamp's (signed) seconds since 1970-01-01T00:00:00Z.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 *
 * @throws bjd_error_type if the underlying value is not a timestamp.
 */
int64_t bjd_node_timestamp_seconds(bjd_node_t node);

/**
 * Returns a timestamp's additional nanoseconds.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 *
 * @return A nanosecond count between 0 and 999,999,999 inclusive.
 * @throws bjd_error_type if the underlying value is not a timestamp.
 */
uint32_t bjd_node_timestamp_nanoseconds(bjd_node_t node);
#endif

/**
 * @}
 */

/**
 * @name Node String and Data Functions
 * @{
 */

/**
 * Checks that the given node contains a valid UTF-8 string.
 *
 * If the string is invalid, this flags an error, which would cause subsequent calls
 * to bjd_node_str() to return NULL and bjd_node_strlen() to return zero. So you
 * can check the node for error immediately after calling this, or you can call those
 * functions to use the data anyway and check for errors later.
 *
 * @throws bjd_error_type If this node is not a string or does not contain valid UTF-8.
 *
 * @param node The string node to test
 *
 * @see bjd_node_str()
 * @see bjd_node_strlen()
 */
void bjd_node_check_utf8(bjd_node_t node);

/**
 * Checks that the given node contains a valid UTF-8 string with no NUL bytes.
 *
 * This does not check that the string has a null-terminator! It only checks whether
 * the string could safely be represented as a C-string by appending a null-terminator.
 * (If the string does already contain a null-terminator, this will flag an error.)
 *
 * This is performed automatically by other UTF-8 cstr helper functions. Only
 * call this if you will do something else with the data directly, but you still
 * want to ensure it will be valid as a UTF-8 C-string.
 *
 * @throws bjd_error_type If this node is not a string, does not contain valid UTF-8,
 *     or contains a NUL byte.
 *
 * @param node The string node to test
 *
 * @see bjd_node_str()
 * @see bjd_node_strlen()
 * @see bjd_node_copy_utf8_cstr()
 * @see bjd_node_utf8_cstr_alloc()
 */
void bjd_node_check_utf8_cstr(bjd_node_t node);

#if BJDATA_EXTENSIONS
/**
 * Returns the extension type of the given ext node.
 *
 * This returns zero if the tree is in an error state.
 *
 * @note This requires @ref BJDATA_EXTENSIONS.
 */
int8_t bjd_node_exttype(bjd_node_t node);
#endif

/**
 * Returns the number of bytes in the given bin node.
 *
 * This returns zero if the tree is in an error state.
 *
 * If this node is not a bin, @ref bjd_error_type is raised and zero is returned.
 */
size_t bjd_node_bin_size(bjd_node_t node);

/**
 * Returns the length of the given str, bin or ext node.
 *
 * This returns zero if the tree is in an error state.
 *
 * If this node is not a str, bin or map, @ref bjd_error_type is raised and zero
 * is returned.
 */
uint32_t bjd_node_data_len(bjd_node_t node);

/**
 * Returns the length in bytes of the given string node. This does not
 * include any null-terminator.
 *
 * This returns zero if the tree is in an error state.
 *
 * If this node is not a str, @ref bjd_error_type is raised and zero is returned.
 */
size_t bjd_node_strlen(bjd_node_t node);

/**
 * Returns a pointer to the data contained by this node, ensuring the node is a
 * string.
 *
 * @warning Strings are not null-terminated! Use one of the cstr functions
 * to get a null-terminated string.
 *
 * The pointer is valid as long as the data backing the tree is valid.
 *
 * If this node is not a string, @ref bjd_error_type is raised and @c NULL is returned.
 *
 * @see bjd_node_copy_cstr()
 * @see bjd_node_cstr_alloc()
 * @see bjd_node_utf8_cstr_alloc()
 */
const char* bjd_node_str(bjd_node_t node);

/**
 * Returns a pointer to the data contained by this node.
 *
 * @note Strings are not null-terminated! Use one of the cstr functions
 * to get a null-terminated string.
 *
 * The pointer is valid as long as the data backing the tree is valid.
 *
 * If this node is not of a str, bin or map, @ref bjd_error_type is raised, and
 * @c NULL is returned.
 *
 * @see bjd_node_copy_cstr()
 * @see bjd_node_cstr_alloc()
 * @see bjd_node_utf8_cstr_alloc()
 */
const char* bjd_node_data(bjd_node_t node);

/**
 * Returns a pointer to the data contained by this bin node.
 *
 * The pointer is valid as long as the data backing the tree is valid.
 *
 * If this node is not a bin, @ref bjd_error_type is raised and @c NULL is
 * returned.
 */
const char* bjd_node_bin_data(bjd_node_t node);

/**
 * Copies the bytes contained by this node into the given buffer, returning the
 * number of bytes in the node.
 *
 * @throws bjd_error_type If this node is not a str, bin or ext type
 * @throws bjd_error_too_big If the string does not fit in the given buffer
 *
 * @param node The string node from which to copy data
 * @param buffer A buffer in which to copy the node's bytes
 * @param bufsize The size of the given buffer
 *
 * @return The number of bytes in the node, or zero if an error occurs.
 */
size_t bjd_node_copy_data(bjd_node_t node, char* buffer, size_t bufsize);

/**
 * Checks that the given node contains a valid UTF-8 string and copies the
 * string into the given buffer, returning the number of bytes in the string.
 *
 * @throws bjd_error_type If this node is not a string
 * @throws bjd_error_too_big If the string does not fit in the given buffer
 *
 * @param node The string node from which to copy data
 * @param buffer A buffer in which to copy the node's bytes
 * @param bufsize The size of the given buffer
 *
 * @return The number of bytes in the node, or zero if an error occurs.
 */
size_t bjd_node_copy_utf8(bjd_node_t node, char* buffer, size_t bufsize);

/**
 * Checks that the given node contains a string with no NUL bytes, copies the string
 * into the given buffer, and adds a null terminator.
 *
 * If this node is not of a string type, @ref bjd_error_type is raised. If the string
 * does not fit, @ref bjd_error_data is raised.
 *
 * If any error occurs, the buffer will contain an empty null-terminated string.
 *
 * @param node The string node from which to copy data
 * @param buffer A buffer in which to copy the node's string
 * @param size The size of the given buffer
 */
void bjd_node_copy_cstr(bjd_node_t node, char* buffer, size_t size);

/**
 * Checks that the given node contains a valid UTF-8 string with no NUL bytes,
 * copies the string into the given buffer, and adds a null terminator.
 *
 * If this node is not of a string type, @ref bjd_error_type is raised. If the string
 * does not fit, @ref bjd_error_data is raised.
 *
 * If any error occurs, the buffer will contain an empty null-terminated string.
 *
 * @param node The string node from which to copy data
 * @param buffer A buffer in which to copy the node's string
 * @param size The size of the given buffer
 */
void bjd_node_copy_utf8_cstr(bjd_node_t node, char* buffer, size_t size);

#ifdef BJDATA_MALLOC
/**
 * Allocates a new chunk of data using BJDATA_MALLOC() with the bytes
 * contained by this node.
 *
 * The allocated data must be freed with BJDATA_FREE() (or simply free()
 * if BJData's allocator hasn't been customized.)
 *
 * @throws bjd_error_type If this node is not a str, bin or ext type
 * @throws bjd_error_too_big If the size of the data is larger than the
 *     given maximum size
 * @throws bjd_error_memory If an allocation failure occurs
 *
 * @param node The node from which to allocate and copy data
 * @param maxsize The maximum size to allocate
 *
 * @return The allocated data, or NULL if any error occurs.
 */
char* bjd_node_data_alloc(bjd_node_t node, size_t maxsize);

/**
 * Allocates a new null-terminated string using BJDATA_MALLOC() with the string
 * contained by this node.
 *
 * The allocated string must be freed with BJDATA_FREE() (or simply free()
 * if BJData's allocator hasn't been customized.)
 *
 * @throws bjd_error_type If this node is not a string or contains NUL bytes
 * @throws bjd_error_too_big If the size of the string plus null-terminator
 *     is larger than the given maximum size
 * @throws bjd_error_memory If an allocation failure occurs
 *
 * @param node The node from which to allocate and copy string data
 * @param maxsize The maximum size to allocate, including the null-terminator
 *
 * @return The allocated string, or NULL if any error occurs.
 */
char* bjd_node_cstr_alloc(bjd_node_t node, size_t maxsize);

/**
 * Allocates a new null-terminated string using BJDATA_MALLOC() with the UTF-8
 * string contained by this node.
 *
 * The allocated string must be freed with BJDATA_FREE() (or simply free()
 * if BJData's allocator hasn't been customized.)
 *
 * @throws bjd_error_type If this node is not a string, is not valid UTF-8,
 *     or contains NUL bytes
 * @throws bjd_error_too_big If the size of the string plus null-terminator
 *     is larger than the given maximum size
 * @throws bjd_error_memory If an allocation failure occurs
 *
 * @param node The node from which to allocate and copy string data
 * @param maxsize The maximum size to allocate, including the null-terminator
 *
 * @return The allocated string, or NULL if any error occurs.
 */
char* bjd_node_utf8_cstr_alloc(bjd_node_t node, size_t maxsize);
#endif

/**
 * Searches the given string array for a string matching the given
 * node and returns its index.
 *
 * If the node does not match any of the given strings,
 * @ref bjd_error_type is flagged. Use bjd_node_enum_optional()
 * if you want to allow values other than the given strings.
 *
 * If any error occurs or if the tree is in an error state, @a count
 * is returned.
 *
 * This can be used to quickly parse a string into an enum when the
 * enum values range from 0 to @a count-1. If the last value in the
 * enum is a special "count" value, it can be passed as the count,
 * and the return value can be cast directly to the enum type.
 *
 * @code{.c}
 * typedef enum           { APPLE ,  BANANA ,  ORANGE , COUNT} fruit_t;
 * const char* fruits[] = {"apple", "banana", "orange"};
 *
 * fruit_t fruit = (fruit_t)bjd_node_enum(node, fruits, COUNT);
 * @endcode
 *
 * @param node The node
 * @param strings An array of expected strings of length count
 * @param count The number of strings
 * @return The index of the matched string, or @a count in case of error
 */
size_t bjd_node_enum(bjd_node_t node, const char* strings[], size_t count);

/**
 * Searches the given string array for a string matching the given node,
 * returning its index or @a count if no strings match.
 *
 * If the value is not a string, or it does not match any of the
 * given strings, @a count is returned and no error is flagged.
 *
 * If any error occurs or if the tree is in an error state, @a count
 * is returned.
 *
 * This can be used to quickly parse a string into an enum when the
 * enum values range from 0 to @a count-1. If the last value in the
 * enum is a special "count" value, it can be passed as the count,
 * and the return value can be cast directly to the enum type.
 *
 * @code{.c}
 * typedef enum           { APPLE ,  BANANA ,  ORANGE , COUNT} fruit_t;
 * const char* fruits[] = {"apple", "banana", "orange"};
 *
 * fruit_t fruit = (fruit_t)bjd_node_enum_optional(node, fruits, COUNT);
 * @endcode
 *
 * @param node The node
 * @param strings An array of expected strings of length count
 * @param count The number of strings
 * @return The index of the matched string, or @a count in case of error
 */
size_t bjd_node_enum_optional(bjd_node_t node, const char* strings[], size_t count);

/**
 * @}
 */

/**
 * @name Compound Node Functions
 * @{
 */

/**
 * Returns the length of the given array node. Raises bjd_error_type
 * and returns 0 if the given node is not an array.
 */
size_t bjd_node_array_length(bjd_node_t node);

/**
 * Returns the node in the given array at the given index. If the node
 * is not an array, @ref bjd_error_type is raised and a nil node is returned.
 * If the given index is out of bounds, @ref bjd_error_data is raised and
 * a nil node is returned.
 */
bjd_node_t bjd_node_array_at(bjd_node_t node, size_t index);

/**
 * Returns the number of key/value pairs in the given map node. Raises
 * bjd_error_type and returns 0 if the given node is not a map.
 */
size_t bjd_node_map_count(bjd_node_t node);

/**
 * Returns the key node in the given map at the given index.
 *
 * A nil node is returned in case of error.
 *
 * @throws bjd_error_type if the node is not a map
 * @throws bjd_error_data if the given index is out of bounds
 */
bjd_node_t bjd_node_map_key_at(bjd_node_t node, size_t index);

/**
 * Returns the value node in the given map at the given index.
 *
 * A nil node is returned in case of error.
 *
 * @throws bjd_error_type if the node is not a map
 * @throws bjd_error_data if the given index is out of bounds
 */
bjd_node_t bjd_node_map_value_at(bjd_node_t node, size_t index);

/**
 * Returns the value node in the given map for the given integer key.
 *
 * The key must exist within the map. Use bjd_node_map_int_optional() to
 * check for optional keys.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node does not contain exactly one entry with the given key
 *
 * @return The value node for the given key, or a nil node in case of error
 */
bjd_node_t bjd_node_map_int(bjd_node_t node, int64_t num);

/**
 * Returns the value node in the given map for the given integer key, or a
 * missing node if the map does not contain the given key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 *
 * @return The value node for the given key, or a missing node if the key does
 *         not exist, or a nil node in case of error
 *
 * @see bjd_node_is_missing()
 */
bjd_node_t bjd_node_map_int_optional(bjd_node_t node, int64_t num);

/**
 * Returns the value node in the given map for the given unsigned integer key.
 *
 * The key must exist within the map. Use bjd_node_map_uint_optional() to
 * check for optional keys.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node does not contain exactly one entry with the given key
 *
 * @return The value node for the given key, or a nil node in case of error
 */
bjd_node_t bjd_node_map_uint(bjd_node_t node, uint64_t num);

/**
 * Returns the value node in the given map for the given unsigned integer
 * key, or a missing node if the map does not contain the given key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 *
 * @return The value node for the given key, or a missing node if the key does
 *         not exist, or a nil node in case of error
 *
 * @see bjd_node_is_missing()
 */
bjd_node_t bjd_node_map_uint_optional(bjd_node_t node, uint64_t num);

/**
 * Returns the value node in the given map for the given string key.
 *
 * The key must exist within the map. Use bjd_node_map_str_optional() to
 * check for optional keys.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node does not contain exactly one entry with the given key
 *
 * @return The value node for the given key, or a nil node in case of error
 */
bjd_node_t bjd_node_map_str(bjd_node_t node, const char* str, size_t length);

/**
 * Returns the value node in the given map for the given string key, or a missing
 * node if the map does not contain the given key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 *
 * @return The value node for the given key, or a missing node if the key does
 *         not exist, or a nil node in case of error
 *
 * @see bjd_node_is_missing()
 */
bjd_node_t bjd_node_map_str_optional(bjd_node_t node, const char* str, size_t length);

/**
 * Returns the value node in the given map for the given null-terminated
 * string key.
 *
 * The key must exist within the map. Use bjd_node_map_cstr_optional() to
 * check for optional keys.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node does not contain exactly one entry with the given key
 *
 * @return The value node for the given key, or a nil node in case of error
 */
bjd_node_t bjd_node_map_cstr(bjd_node_t node, const char* cstr);

/**
 * Returns the value node in the given map for the given null-terminated
 * string key, or a missing node if the map does not contain the given key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 *
 * @return The value node for the given key, or a missing node if the key does
 *         not exist, or a nil node in case of error
 *
 * @see bjd_node_is_missing()
 */
bjd_node_t bjd_node_map_cstr_optional(bjd_node_t node, const char* cstr);

/**
 * Returns true if the given node map contains exactly one entry with the
 * given integer key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 */
bool bjd_node_map_contains_int(bjd_node_t node, int64_t num);

/**
 * Returns true if the given node map contains exactly one entry with the
 * given unsigned integer key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 */
bool bjd_node_map_contains_uint(bjd_node_t node, uint64_t num);

/**
 * Returns true if the given node map contains exactly one entry with the
 * given string key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 */
bool bjd_node_map_contains_str(bjd_node_t node, const char* str, size_t length);

/**
 * Returns true if the given node map contains exactly one entry with the
 * given null-terminated string key.
 *
 * The key must be unique. An error is flagged if the node has multiple
 * entries with the given key.
 *
 * @throws bjd_error_type If the node is not a map
 * @throws bjd_error_data If the node contains more than one entry with the given key
 */
bool bjd_node_map_contains_cstr(bjd_node_t node, const char* cstr);

/**
 * @}
 */

/**
 * @}
 */

#endif

BJDATA_EXTERN_C_END
BJDATA_HEADER_END

#endif


