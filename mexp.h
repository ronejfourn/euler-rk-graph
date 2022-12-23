#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct mexp_token_t   mexp_token_t;
typedef struct mexp_node_t    mexp_node_t;
typedef struct mexp_pool_t    mexp_pool_t;
typedef struct mexp_state_t   mexp_state_t;
typedef struct mexp_stack_t   mexp_stack_t;
typedef struct mexp_parser_t  mexp_parser_t;
typedef struct mexp_tree_t    mexp_tree_t;
typedef double (*mexp_func_t) (mexp_node_t *);

int  mexp_init_parser(mexp_parser_t *parser);
int  mexp_init_tree(mexp_tree_t *tree);
int  mexp_generate_tree(mexp_tree_t *tree, mexp_parser_t *parser, const char *expr, int32_t length);
void mexp_free_parser(mexp_parser_t *parser);
void mexp_free_tree(mexp_tree_t *tree);
void mexp_print_tree(const mexp_tree_t *tree);
double mexp_eval_tree(mexp_tree_t *tree, double *v);
int mexp_add_variable(mexp_parser_t *parser, char var);

struct mexp_token_t
{
    uint32_t type;
    struct
    {
        const char *data;
        int length;
    } contents;
    union
    {
        char character;
        char string[8];
        double number;
        const char *error;
    };
};

struct mexp_node_t
{
    uint32_t type;
    double value;
    union
    {
        int32_t index;
        struct
        {
            char name;
            int32_t index;
        } var;
        struct
        {
            int32_t type;
            int32_t left;
            int32_t right;
        } oper;
        struct
        {
            int nargs;
            char name[8];
            mexp_func_t ptr;
        } func;
    };
};

struct mexp_pool_t
{
    mexp_node_t *pool;
    uint32_t cap;
    uint32_t count;
};

struct mexp_state_t
{
    int32_t head;
    int32_t last_operator;
    int32_t last_operand;
    int32_t function_call;
    int32_t function_arg_count;
};

struct mexp_stack_t
{
    mexp_state_t *buf;
    uint32_t cap;
    uint32_t count;
};

struct mexp_parser_t
{
    const char *start;
    const char *last;
    const char *at;
    char *variables;
    uint32_t var_count;
    uint32_t var_max;
    mexp_stack_t stack;
    mexp_token_t token;
};

struct mexp_tree_t
{
    mexp_pool_t pool;
    int32_t head;
};
