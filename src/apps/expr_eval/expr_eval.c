#include "expr_eval.h"

#include <lib/setjmp.h>
#include <lib/string.h>
#include <lib/mem.h>

#define DIGIT_CHAR_SET "0123456789"
#define LETTER_CHAR_SET "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

typedef struct parser
{
    char const *input;
    size_t index;
    error_handler_t handler;
    size_t depth; // To prevent stack overflow
    token_t token;
    size_t token_count;
    size_t token_capacity;
    token_t *token_buffer;
} parser_t;

static jmp_buf_t restore_point;

static inline _Noreturn void error(error_handler_t handler, char const *message)
{
    if (handler)
        handler(message);
    longjmp(&restore_point, 1);
}

static inline void push_token(parser_t *p, token_t token)
{
    if (p->token_count >= p->token_capacity)
    {
        error(p->handler, "out of memory");
    }
    p->token_buffer[p->token_count++] = token;
}

static size_t token_precedence(token_kind_t kind)
{
    switch (kind)
    {
    case TOK_PLUS:
    case TOK_MINUS:
        return 1;

    case TOK_ASTERISK:
    case TOK_SLASH:
    case TOK_PERCENT:
        return 2;

    case TOK_CARET:
        return 3;

    default:
        return 0;
    }
}

static size_t str_count_leading_chars(char const *str, char const *chars)
{
    size_t n;
    char const *p;

    for (n = 0; *str; ++str, ++n)
    {
        for (p = chars; *p && *p != *str; p++)
        {
        }
        if (!*p)
            break;
    }

    return n;
}

static token_t eat_token(parser_t *p)
{
    token_t previous = p->token;

    if (previous.kind == TOK_EOF)
        return previous;

    while (is_space(p->input[p->index]))
        ++p->index;

    char peeked = p->input[p->index];
    size_t to_advance = 1;

    p->token.input_offset = p->index;

    if (peeked == '\0')
        p->token.kind = TOK_EOF;
    else if (peeked == '+')
        p->token.kind = TOK_PLUS;
    else if (peeked == '-')
        p->token.kind = TOK_MINUS;
    else if (peeked == '*')
        p->token.kind = TOK_ASTERISK;
    else if (peeked == '/')
        p->token.kind = TOK_SLASH;
    else if (peeked == '%')
        p->token.kind = TOK_PERCENT;
    else if (peeked == '^')
        p->token.kind = TOK_CARET;
    else if (peeked == '~')
        p->token.kind = TOK_TILDE;
    else if (peeked == '(')
        p->token.kind = TOK_LE_PAREN;
    else if (peeked == ')')
        p->token.kind = TOK_RI_PAREN;
    else if (is_digit(peeked))
    {
        to_advance = str_count_leading_chars(&p->input[p->index], DIGIT_CHAR_SET);
        p->token.kind = TOK_NUM;
        p->token.input = &p->input[p->index];
        p->token.input_size = to_advance;
    }
    else if (is_letter(peeked))
    {
        to_advance = str_count_leading_chars(&p->input[p->index], LETTER_CHAR_SET);
        p->token.kind = TOK_VAR;
        p->token.input = &p->input[p->index];
        p->token.input_size = to_advance;
    }
    else
        error(p->handler, "invalid token");

    p->index += to_advance;
    return previous;
}

static void parse_binary(parser_t *p, size_t last_precedence);

static void parse_operand(parser_t *p)
{
    switch (p->token.kind)
    {
    case TOK_NUM:
    case TOK_VAR:
        push_token(p, eat_token(p));
        break;

    case TOK_LE_PAREN:
        eat_token(p);
        ++p->depth;

        if (p->depth >= EXPR_DEPTH_LIMIT)
            error(p->handler, "parenthesis depth limit reached");

        parse_binary(p, 1);
        if (p->token.kind != TOK_RI_PAREN)
            error(p->handler, "expected closing parenthesis");

        eat_token(p);
        --p->depth;
        break;

    default:
        error(p->handler, "expected operand");
        break;
    }
}

static void parse_prefixed(parser_t *p)
{
    switch (p->token.kind)
    {
    case TOK_PLUS:
    {
        token_t prefix = eat_token(p);
        parse_prefixed(p);
        push_token(p, prefix);
    }
    break;

    case TOK_MINUS:
    {
        token_t prefix = eat_token(p);
        prefix.kind = TOK_TILDE;
        parse_prefixed(p);
        push_token(p, prefix);
    }
    break;

    default:
        parse_operand(p);
        break;
    }
}

static void parse_binary(parser_t *p, size_t last_precedence)
{
    parse_prefixed(p);
    size_t precedence = 0;

    while ((precedence = token_precedence(p->token.kind)) >= last_precedence)
    {
        token_t operator = eat_token(p);
        parse_binary(p, precedence + 1);
        push_token(p, operator);
    }
}

size_t eval_expr(char const *input, int stack[], size_t stack_size, error_handler_t error_handler)
{
    token_t buffer[256];
    return eval_expr_v(input, buffer, sizeof(buffer) / sizeof(token_t), stack, stack_size, error_handler);
}

size_t eval_expr_v(char const *input, token_t buf[], size_t buf_size, int stack[], size_t stack_size, error_handler_t error_handler)
{
    size_t tokens_count = parse_expr(input, buf, buf_size, error_handler);
    if (setjmp(&restore_point))
        return 0;
    size_t eval_count = eval_token_sequence(buf, tokens_count, stack, stack_size, error_handler);
    return eval_count;
}

size_t eval_token_sequence(token_t seq[], size_t seq_size, int stack[], size_t stack_size, error_handler_t error_handler)
{
    int sp = -1;

    for (size_t i = 0; i < seq_size; ++i)
    {
        token_t *tok = &seq[i];

        switch (tok->kind)
        {
        case TOK_NUM:
            if (sp >= (int)stack_size)
                error(error_handler, "out of memory");
            if (tok->input_size > 31)
                error(error_handler, "number too big");
            char buf[32];
            memcpy(buf, tok->input, tok->input_size);
            buf[tok->input_size] = '\0';
            stack[++sp] = str_to_int(buf);
            break;

        case TOK_VAR:
            error(error_handler, "TODO: var lookup");
            return sp + 1;

        case TOK_PLUS:
        {
            if (sp < 1)
                error(error_handler, "not enough item on the stack");
            int b = stack[sp--];
            int a = stack[sp--];
            stack[++sp] = a + b;
        }
        break;

        case TOK_MINUS:
        {
            if (sp < 1)
                error(error_handler, "not enough item on the stack");
            int b = stack[sp--];
            int a = stack[sp--];
            stack[++sp] = b - a;
        }
        break;

        case TOK_ASTERISK:
        {
            if (sp < 1)
                error(error_handler, "not enough item on the stack");
            int b = stack[sp--];
            int a = stack[sp--];
            stack[++sp] = a * b;
        }
        break;

        case TOK_SLASH:
        {
            if (sp < 1)
                error(error_handler, "not enough item on the stack");
            int b = stack[sp--];
            int a = stack[sp--];
            if (b == 0)
                error(error_handler, "division by zero");
            stack[++sp] = a / b;
        }
        break;

        case TOK_PERCENT:
        {
            if (sp < 1)
                error(error_handler, "not enough item on the stack");
            int b = stack[sp--];
            int a = stack[sp--];
            if (b == 0)
                error(error_handler, "division by zero");
            stack[++sp] = a % b;
        }
        break;

        case TOK_CARET:
        {
            if (sp < 1)
                error(error_handler, "not enough item on the stack");
            int exp = stack[sp - 1];
            int base = stack[sp];
            int result = 1;
            while (exp != 0)
            {
                if ((exp % 2) != 0)
                    result *= base;
                exp /= 2;
                base *= base;
            }
            stack[++sp] = result;
        }
        break;

        case TOK_TILDE:
        {
            if (sp < 0)
                error(error_handler, "empty stack");
            stack[sp] = -stack[sp];
        }
        break;

        case TOK_INVALID:
        case TOK_EOF:
        case TOK_LE_PAREN:
        case TOK_RI_PAREN:
            error(error_handler, "ill-formed data");
            return sp + 1;
        }
    }

    return sp + 1;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers" // volatile local variable

size_t parse_expr(char const *input, token_t buf[], size_t buf_size, error_handler_t error_handler)
{
    parser_t volatile p = {
        .input = input,
        .index = 0,
        .depth = 0,
        .token = (token_t){0},
        .token_count = 0,
        .token_capacity = buf_size,
        .token_buffer = buf,
        .handler = error_handler,
    };

    volatile int code = setjmp(&restore_point);

    if (code != 0)
        return 0;

    eat_token(&p);
    parse_binary(&p, 1);
    return p.token_count;
}

#pragma GCC diagnostic pop
