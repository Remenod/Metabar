#pragma once

#include <lib/types.h>

#ifndef EXPR_DEPTH_LIMIT
#define EXPR_DEPTH_LIMIT 256
#endif

typedef enum token_kind
{
    TOK_INVALID,
    TOK_EOF,
    TOK_NUM,
    TOK_VAR,
    TOK_PLUS,
    TOK_MINUS,
    TOK_ASTERISK,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_CARET,
    TOK_TILDE, // Used as unary minus.
    TOK_LE_PAREN,
    TOK_RI_PAREN,
} token_kind_t;

typedef struct token
{
    token_kind_t kind;
    size_t input_offset;
    size_t input_size; // TOK_INVALID, TOK_NUM, TOK_VAR
    char const *input; // TOK_INVALID, TOK_NUM, TOK_VAR
} token_t;

typedef void (*error_handler_t)(char const *message);

/**
 * Parses and evaluates `input` string as a simple mathematical expression.
 * Returns a count of valid values on the stack.
 * Calls `error_handler` function on any error and returns immediately.
 */
size_t eval_expr(char const *input, int stack[], size_t stack_size, error_handler_t error_handler);

/**
 * Parses and evaluates `input` string as a simple mathematical expression.
 * Returns a count of valid values on the stack.
 * Calls `error_handler` function on any error and returns immediately.
 *
 * - The `buf` parameter must be in reverse-polish notation.
 *   (i.e. result of `parse_expr()` call in the format of '1 2 +' for input '1 + 2')
 *
 * - The `buf_size` parameter specifies how much tokens the `buf` can store.
 *
 * - The `stack` parameter used to store intermidiate result of calculations.
 *
 * - The `stack_size` parameter specifies how much intermidiate value the `stack` can hold.
 */
size_t eval_expr_v(char const *input, token_t buf[], size_t buf_size, int stack[], size_t stack_size, error_handler_t error_handler);

/**
 * Evaluates specified token sequence.
 * Returns a count of valid values on the stack.
 * Calls `error_handler` function on any error and returns immediately.
 *
 * - The `seq` parameter must be in reverse-polish notation.
 *   (i.e. result of `parse_expr()` in the format of [1 2 +] for input "1 + 2")
 *
 * - The `seq_size` parameters specifies how many tokens are in the sequence.
 *
 * - The `stack` parameter used to store intermidiate result of calculations.
 *
 * - The `stack_size` parameter specifies how much intermidiate value the `stack` can hold.
 */
size_t eval_token_sequence(token_t seq[], size_t seq_size, int stack[], size_t stack_size, error_handler_t error_handler);

/**
 * Parses the `input` string as a simple mathematical expression.
 * Returns a count of tokens written in the buffer.
 * Calls `error_handler` function on any error and returns immediately.
 *
 * - The `buf` parameter must be in reverse-polish notation.
 *   (i.e. result of `parse_expr()` call in the format of '1 2 +' for input '1 + 2')
 *
 * - The `buf_size` parameter specifies how much tokens the `buf` can store.
 *
 * - The `stack` parameter used to store intermidiate result of calculations.
 *
 * - The `stack_size` parameter specifies how much intermidiate value the `stack` can hold.
 */
size_t parse_expr(char const *input, token_t buf[], size_t buf_size, error_handler_t error_handler);
