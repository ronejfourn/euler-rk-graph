#include "mexp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mexp__advance_whitespace(mexp_parser_t *parser);
static void mexp__parser_get_next(mexp_parser_t *parser);
static int  mexp__is_variable(const mexp_parser_t *parser, char c);
static int  mexp__push_node(mexp_tree_t *tree, const mexp_node_t node);
static int  mexp__push_state(mexp_parser_t *parser, const mexp_state_t *state);
static int  mexp__pop_state(mexp_parser_t *parser, mexp_state_t *state);
static int  mexp__precedence(char t);
static int  mexp__match_builtin(const char name[8]);
static void mexp__print_node(const mexp_tree_t *tree, int32_t index, int level);
static double mexp__eval_node(mexp_tree_t *tree, int32_t index);

static double mexp__builtin_sin(mexp_node_t *n) {return sin(n[0].value);}
static double mexp__builtin_cos(mexp_node_t *n) {return cos(n[0].value);}
static double mexp__builtin_tan(mexp_node_t *n) {return tan(n[0].value);}
static double mexp__builtin_log(mexp_node_t *n) {return log(n[0].value);}
static double mexp__builtin_exp(mexp_node_t *n) {return exp(n[0].value);}
static double mexp__builtin_sqrt(mexp_node_t *n) {return sqrt(n[0].value);}
static const struct
{
    const char name[8];
    mexp_func_t func;
    uint32_t nargs;
}
mexp__builtin_funcs[] =
{
#define NEWFUNC(f, n) {#f, mexp__builtin_##f, n}
    NEWFUNC(sin, 1),
    NEWFUNC(cos, 1),
    NEWFUNC(tan, 1),
    NEWFUNC(log, 1),
    NEWFUNC(exp, 1),
    NEWFUNC(sqrt, 1),
#undef NEWFUNC
};

enum
{
    NODE_DUMMY    = 3,
    NODE_NUMBER   = 5,
    NODE_VARIABLE = 7,
    NODE_OPERATOR = 9,
    NODE_FUNCTION = 11,
};

enum
{
    TOKEN_NONE      = 0,
    TOKEN_NUMBER    = (1 << 0),
    TOKEN_CHAR      = (1 << 1),
    TOKEN_STRING    = (1 << 2),
    TOKEN_OPERATOR  = (1 << 3),
    TOKEN_OBRACKET  = (1 << 4),
    TOKEN_CBRACKET  = (1 << 5),
    TOKEN_END       = (1 << 6),
    TOKEN_COMMA     = (1 << 7),
    TOKEN_ANY       = (TOKEN_NUMBER | TOKEN_CHAR | TOKEN_STRING | TOKEN_OBRACKET | TOKEN_CBRACKET | TOKEN_OPERATOR | TOKEN_END | TOKEN_COMMA),
    TOKEN_ERROR     = (1 << 8),
};

int mexp_init_parser(mexp_parser_t *parser)
{
    parser->stack.cap   = 8;
    parser->stack.count = 0;
    parser->stack.buf = (mexp_state_t*)malloc(sizeof(*parser->stack.buf) * 8);
    if (!parser->stack.buf)
        return 0;

    parser->var_max   = 8;
    parser->var_count = 0;
    parser->variables = (char *)malloc(parser->var_max);
    if (!parser->variables)
        return 0;

    parser->start = NULL;
    parser->last = NULL;
    parser->at = NULL;

    parser->token.type = TOKEN_NONE;
    parser->token.contents.data   = NULL;
    parser->token.contents.length = 0;
    memset(parser->token.error, 0, sizeof(parser->token.error));

    return 1;
}

int mexp_init_tree(mexp_tree_t *tree)
{
    tree->head = -1;
    tree->pool.count = 0;
    tree->pool.pool = (mexp_node_t*)malloc(sizeof(*tree->pool.pool) * 16);
    if (!tree->pool.pool)
        return 0;
    tree->pool.cap  = 16;
    return 1;
}

const char *mexp_get_error(mexp_parser_t *parser)
{
    if (parser->token.type == TOKEN_ERROR)
        return parser->token.error;
    return "(no error)";
}

int mexp_generate_tree(mexp_tree_t *tree, mexp_parser_t *parser, const char *expr, int32_t length)
{
    tree->head = -1;
    tree->pool.count = 0;
    parser->stack.count = 0;
    parser->start = expr;
    parser->at    = expr;
    parser->last  = expr + length;
    parser->token.type = TOKEN_NONE;

    mexp_token_t *token = &parser->token;

    mexp_node_t node;
    mexp_state_t state;
    state.head = -1;
    state.last_operand  = -1;
    state.last_operator = -1;
    state.function_call = 0;

    const mexp_state_t reset_state = {-1, -1, -1, 0, 0};
    uint32_t expected = TOKEN_ANY & (~(TOKEN_CBRACKET | TOKEN_COMMA));

    while (token->type != TOKEN_END)
    {
        mexp__parser_get_next(parser);
        if (token->type == TOKEN_ERROR)
            return 0;

        if (!(token->type & expected))
        {
            snprintf(token->error, MEXP_ERROR_LENGTH, "unexpected token '%.*s'", token->contents.length, token->contents.data);
            token->type = TOKEN_ERROR;
            return 0;
        }

        if (token->type == TOKEN_CHAR)
        {
            int index = mexp__is_variable(parser, token->character);
            if (index == -1)
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "unknown variable '%c'", token->character);
                token->type = TOKEN_ERROR;
                return 0;
            }
            node.type = NODE_VARIABLE;
            node.var.name  = token->character;
            node.var.index = index;
            if (!mexp__push_node(tree, node))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                token->type = TOKEN_ERROR;
                return 0;
            }
            state.last_operand = tree->pool.count - 1;
            expected = TOKEN_OPERATOR | TOKEN_END | TOKEN_CBRACKET | TOKEN_COMMA;
        }

        if (token->type == TOKEN_NUMBER)
        {
            node.type  = NODE_NUMBER;
            node.value = token->number;
            if (!mexp__push_node(tree, node))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                token->type = TOKEN_ERROR;
                return 0;
            }
            state.last_operand = tree->pool.count - 1;
            expected = TOKEN_OPERATOR | TOKEN_END | TOKEN_CBRACKET | TOKEN_COMMA;
        }

        if (token->type == TOKEN_STRING)
        {
            int index = mexp__match_builtin(token->string);
            // TODO: user defined functions
            if (index == -1)
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "unknown string '%.8s'", token->string);
                token->type = TOKEN_ERROR;
                return 0;
            }
            node.type = NODE_FUNCTION;
            node.func.ptr = mexp__builtin_funcs[index].func;
            node.func.nargs = mexp__builtin_funcs[index].nargs;
            for (int i = 0; i < sizeof(node.func.name); i ++)
                node.func.name[i] = mexp__builtin_funcs[index].name[i];
            if (!mexp__push_node(tree, node))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                token->type = TOKEN_ERROR;
                return 0;
            }
            state.last_operand = tree->pool.count - 1;
            for (int i = 0; i < node.func.nargs; i ++)
            {
                if (!mexp__push_node(tree, node))
                {
                    snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                    token->type = TOKEN_ERROR;
                    return 0;
                }
            }
            state.function_call = 1;
            state.function_arg_count = 0;
            expected = TOKEN_OBRACKET;
        }

        if (token->type == TOKEN_OPERATOR)
        {
            node.type = NODE_OPERATOR;
            node.oper.type  = token->character;
            node.oper.left  = -1;
            node.oper.right = -1;
            if (!mexp__push_node(tree, node))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                token->type = TOKEN_ERROR;
                return 0;
            }
            state.last_operator = tree->pool.count - 1;
            mexp_node_t *self = &tree->pool.pool[state.last_operator];

            if (state.head == -1)
            {
                if (state.last_operand != -1)
                {
                    self->oper.left = state.last_operand;
                    state.head = state.last_operator;
                }
                else if (node.oper.type == '+' || node.oper.type == '-')
                {
                    mexp_node_t dummy = {NODE_NUMBER, 0};
                    if (!mexp__push_node(tree, dummy))
                    {
                        snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                        token->type = TOKEN_ERROR;
                        return 0;
                    }
                    self->oper.left = tree->pool.count - 1;
                    state.head = state.last_operator;
                }
                else
                {
                    snprintf(token->error, MEXP_ERROR_LENGTH, "\'%c\' cant be used as unary operator", node.oper.type);
                    token->type = TOKEN_ERROR;
                    return 0;
                }
            }
            else
            {
                mexp_node_t *head = &tree->pool.pool[state.head];
                int ps = mexp__precedence(self->oper.type);
                int ph = mexp__precedence(head->oper.type);
                if (ps <= ph)
                {
                    head->oper.right = state.last_operand;
                    self->oper.left  = state.head;
                    state.head = state.last_operator;
                }
                else
                {
                    mexp_node_t *right;
                    while (1)
                    {
                        if (head->oper.right == -1)
                        {
                            head->oper.right = state.last_operator;
                            self->oper.left  = state.last_operand;
                            break;
                        }
                        right = &tree->pool.pool[head->oper.right];
                        if (ps <= mexp__precedence(right->oper.type))
                        {
                            self->oper.left   = head->oper.right;
                            head->oper.right  = state.last_operator;
                            right->oper.right = state.last_operand;
                            break;
                        }
                        head = right;
                    }
                }
            }
            expected = TOKEN_CHAR | TOKEN_NUMBER | TOKEN_OBRACKET | TOKEN_STRING;
        }

        if (token->type == TOKEN_COMMA)
        {
            mexp_state_t temp = {-1, -1, -1, 0, 0};
            if (!mexp__pop_state(parser, &temp) || !temp.function_call)
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "unexpected ','");
                token->type = TOKEN_ERROR;
                return 0;
            }
            if (state.head != -1)
                tree->pool.pool[state.last_operator].oper.right = state.last_operand;
            else if (state.last_operand != -1)
                state.head = state.last_operand;
            temp.function_arg_count ++;
            int32_t index = temp.last_operand + temp.function_arg_count;
            mexp_node_t *dst = &tree->pool.pool[index];
            dst->type  = NODE_DUMMY;
            dst->index = state.head;
            if (!mexp__push_state(parser, &temp))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                token->type = TOKEN_ERROR;
                return 0;
            }
            state = reset_state;
            expected = TOKEN_OBRACKET | TOKEN_NUMBER | TOKEN_CHAR | TOKEN_STRING | TOKEN_OPERATOR;
        }

        if (token->type == TOKEN_OBRACKET)
        {
            if (!mexp__push_state(parser, &state))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "out of memory");
                token->type = TOKEN_ERROR;
                return 0;
            }
            state = reset_state;
            expected = TOKEN_ANY & ~TOKEN_COMMA;
        }

        if (token->type == TOKEN_CBRACKET)
        {
            mexp_state_t temp = {-1, -1, -1, 0, 0};
            if (!mexp__pop_state(parser, &temp))
            {
                snprintf(token->error, MEXP_ERROR_LENGTH, "unexpected ')'");
                token->type = TOKEN_ERROR;
                return 0;
            }
            if (state.head != -1)
                tree->pool.pool[state.last_operator].oper.right = state.last_operand;
            else if (state.last_operand != -1)
                state.head = state.last_operand;

            if (temp.function_call)
            {
                if (state.head != -1)
                {
                    temp.function_arg_count += 1;
                    int32_t index = temp.last_operand + temp.function_arg_count;
                    mexp_node_t *dst = &tree->pool.pool[index];
                    dst->type  = NODE_DUMMY;
                    dst->index = state.head;
                }
                if (temp.function_arg_count != tree->pool.pool[temp.last_operand].func.nargs)
                {
                    snprintf(token->error, MEXP_ERROR_LENGTH,
                            "function '%.8s' expects %d arguments but got %d",
                            tree->pool.pool[temp.last_operand].func.name,
                            tree->pool.pool[temp.last_operand].func.nargs,
                            temp.function_arg_count);
                    token->type = TOKEN_ERROR;
                    return 0;
                }
                temp.function_call = 0;
                temp.function_arg_count = 0;
            }
            else
            {
                if (state.head == -1)
                {
                    snprintf(token->error, MEXP_ERROR_LENGTH, "() is undefined");
                    token->type = TOKEN_ERROR;
                    return 0;
                }
                temp.last_operand = state.head;
            }

            expected = TOKEN_OPERATOR | TOKEN_END | TOKEN_CBRACKET;
            state = temp;
        }
    }

    mexp_state_t temp;
    if (mexp__pop_state(parser, &temp))
    {
        snprintf(token->error, MEXP_ERROR_LENGTH, "unterminated '('");
        token->type = TOKEN_ERROR;
        return 0;
    }

    if (state.head != -1)
    {
        tree->head = state.head;
        tree->pool.pool[state.last_operator].oper.right = state.last_operand;
    }
    else tree->head = state.last_operand;

    if (tree->head == -1)
    {
        snprintf(token->error, MEXP_ERROR_LENGTH, "empty expression");
        token->type = TOKEN_ERROR;
        return 0;
    }

    return 1;
}

void mexp_free_parser(mexp_parser_t *parser)
{
    if (parser->stack.buf)
        free(parser->stack.buf);
    parser->stack.buf   = NULL;
    parser->stack.cap   = 0;
    parser->stack.count = 0;
    parser->at    = NULL;
    parser->last  = NULL;
    parser->start = NULL;
    parser->token.type = TOKEN_NONE;
}

void mexp_free_tree(mexp_tree_t *tree)
{
    if (tree->pool.pool)
        free(tree->pool.pool);
    tree->pool.pool  = NULL;
    tree->pool.cap   = 0;
    tree->pool.count = 0;
    tree->head = -1;
}

void mexp_print_tree(const mexp_tree_t *tree)
{
    if (!tree || tree->head == -1)
        return;
    mexp__print_node(tree, tree->head, 0);
}

double mexp_eval_tree(mexp_tree_t *tree, double *v)
{
    if (!tree || tree->head == -1)
        return 0;

    for (int i = 0; i < tree->pool.count; i++)
    {
        mexp_node_t *node = &tree->pool.pool[i];
        if (node->type == NODE_VARIABLE)
            node->value = v[node->var.index];
    }

    return mexp__eval_node(tree, tree->head);
}

int mexp_add_variable(mexp_parser_t *parser, char var)
{
    if (parser->var_count >= parser->var_max)
    {
        parser->var_max  *= 2;
        parser->variables = (char *)realloc(parser->variables, parser->var_max);
        if (!parser->variables)
            return 0;
    }

    parser->variables[parser->var_count ++] = var;
    return 1;
}

static void mexp__advance_whitespace(mexp_parser_t *parser)
{
#define ISSPACE(ch) ((ch) == '\t' || (ch) == '\n' || (ch) == '\v' || (ch) == '\f' || (ch) == '\r' || (ch) == ' ')
    while (parser->at < parser->last)
    {
        if (!ISSPACE(*parser->at))
            return;
        parser->at++;
    }
#undef ISSPACE
}

static void mexp__parser_get_next(mexp_parser_t *parser)
{
#define ISOP(ch) ((ch) == '+' || (ch) == '-' || (ch) == '*' || (ch) == '/' || (ch) == '^')
#define ISNUM(ch) ((ch) >= '0' && (ch) <= '9')
#define ISALPHA(ch) (((ch) >= 'a' && (ch) <= 'z') || ((ch) >= 'A' && (ch) <= 'Z'))
#define ISALNUM(ch) (ISNUM(ch) || ISALPHA(ch))
    mexp_token_t *token = &parser->token;
    mexp__advance_whitespace(parser);
    if (parser->at >= parser->last)
    {
        token->type = TOKEN_END;
        token->character = 0;
        return;
    }

    token->contents.data   = parser->at;
    token->contents.length = 0;
    char a = *parser->at;
    char b = (parser->at + 1 >= parser->last) ? 0 : *(parser->at + 1);

    if (ISOP(a))
    {
        token->type = TOKEN_OPERATOR;
        token->character = a;
        parser->at ++;
        token->contents.length = 1;
        return;
    }

    if (a == '(')
    {
        token->type = TOKEN_OBRACKET;
        token->character = a;
        parser->at ++;
        token->contents.length = 1;
        return;
    }

    if (a == ')')
    {
        token->type = TOKEN_CBRACKET;
        token->character = a;
        parser->at ++;
        token->contents.length = 1;
        return;
    }

    if (a == ',')
    {
        token->type = TOKEN_COMMA;
        token->character = a;
        parser->at ++;
        token->contents.length = 1;
        return;
    }

    if (ISALPHA(a))
    {
        if (!ISALPHA(b))
        {
            token->type = TOKEN_CHAR;
            token->character = a;
            parser->at ++;
        }
        else
        {
            token->type = TOKEN_STRING;
            for (size_t i = 0; i < sizeof(token->string); i++)
                token->string[i] = 0;
            uint32_t i = 0;
            while(ISALNUM(*parser->at) && parser->at < parser->last)
            {
                if (i >= sizeof(token->string))
                {
                    token->type = TOKEN_ERROR;
                    token->contents.length = parser->at - token->contents.data;
                    snprintf(token->error, MEXP_ERROR_LENGTH, "string overflow '%.*s...'", token->contents.length, token->contents.data);
                    return;
                }
                token->string[i++] = *parser->at;
                parser->at ++;
            }
        }
        token->contents.length = parser->at - token->contents.data;
        return;
    }

    if (ISNUM(a))
    {
        token->type = TOKEN_NUMBER;
        token->number = 0;

        while(ISNUM(*parser->at) && parser->at < parser->last)
        {
            token->number *= 10;
            token->number += *parser->at - '0';
            parser->at ++;
        }

        if (*parser->at != '.')
        {
            token->contents.length = parser->at - token->contents.data;
            return;
        }
        parser->at ++;

        double div = 1;
        while(ISNUM(*parser->at) && parser->at < parser->last)
        {
            div *= 10;
            token->number += (*parser->at - '0') / div;
            parser->at ++;
        }
        token->contents.length = parser->at - token->contents.data;
        return;
    }

    token->type = TOKEN_ERROR;
    snprintf(token->error, MEXP_ERROR_LENGTH, "bad character '%c'", a);
    token->contents.length = parser->at - token->contents.data;
#undef ISALNUM
#undef ISALPHA
#undef ISNUM
#undef ISOP
}

static int mexp__is_variable(const mexp_parser_t *parser, char c)
{
    for (int i = 0; i < parser->var_count; i ++)
        if (parser->variables[i] == c)
            return i;
    return -1;
}

static int mexp__push_node(mexp_tree_t *tree, const mexp_node_t node)
{
    mexp_pool_t *node_pool = &tree->pool;
    if (node_pool->count < node_pool->cap)
    {
        node_pool->pool[node_pool->count++] = node;
        return 1;
    }

    node_pool->pool = (mexp_node_t*)realloc(node_pool->pool, node_pool->cap * 2 * sizeof(*node_pool->pool));
    if (!node_pool->pool)
        return 0;
    node_pool->cap *= 2;
    node_pool->pool[node_pool->count++] = node;
    return 1;
}

static int mexp__push_state(mexp_parser_t *parser, const mexp_state_t *state)
{
    mexp_stack_t *stack = &parser->stack;
    if (stack->count < stack->cap)
    {
        stack->buf[stack->count++] = *state;
        return 1;
    }

    stack->buf = (mexp_state_t*)realloc(stack->buf, stack->cap * 2 * sizeof(*stack->buf));
    if (!stack->buf)
        return 0;
    stack->cap *= 2;
    stack->buf[stack->count++] = *state;
    return 1;
}

static int mexp__pop_state(mexp_parser_t *parser, mexp_state_t *state)
{
    if (parser->stack.count == 0)
        return 0;
    *state = parser->stack.buf[--parser->stack.count];
    return 1;
}

static int mexp__precedence(char t)
{
    switch(t)
    {
        case '+' : return 0;
        case '-' : return 0;
        case '*' : return 1;
        case '/' : return 1;
        case '^' : return 2;
        default : return -1;
    }
}

static int mexp__match_builtin(const char name[8])
{
    static const uint32_t count = sizeof(mexp__builtin_funcs) / sizeof(mexp__builtin_funcs[0]);
    uint64_t n64 = *(uint64_t*)name;
    for (int i = 0; i < count; i ++)
        if (n64 == *(uint64_t*)mexp__builtin_funcs[i].name)
            return i;
    return -1;
}

static void mexp__print_node(const mexp_tree_t *tree, int32_t index, int level)
{
#define INDENT printf("%*s", 2 * (level + 1), "")
    const mexp_node_t *node = &tree->pool.pool[index];
    switch (node->type)
    {
        case NODE_DUMMY:
            printf("dummy -> ");
            mexp__print_node(tree, node->index, level + 1);
            return;
        case NODE_NUMBER:
            printf("number: %f\n", node->value);
            return;
        case NODE_VARIABLE:
            printf("variable: %c\n", node->var.name);
            return;
        case NODE_FUNCTION:
            printf("function: %.8s (%d)\n", node->func.name, node->func.nargs);
            for (int i = 0; i < node->func.nargs; i ++)
            {
                INDENT;printf("arg%d: ", i);
                mexp__print_node(tree, index + i + 1, level + 1);
            }
            return;
        case NODE_OPERATOR:
            printf("operator: %c\n", node->oper.type);
            INDENT;printf("left: ");
            mexp__print_node(tree, node->oper.left, level + 1);
            INDENT;printf("right: ");
            mexp__print_node(tree, node->oper.right, level + 1);
            return;
    }
#undef INDENT
}

static double mexp__eval_node(mexp_tree_t *tree, int32_t index)
{
    mexp_node_t *node = &tree->pool.pool[index];
    switch (node->type)
    {
        case NODE_DUMMY:
            node->value = mexp__eval_node(tree, node->index);
            break;
        case NODE_NUMBER:
        case NODE_VARIABLE:
            break;
        case NODE_FUNCTION:
            for (int i = 0; i < node->func.nargs; i ++)
                mexp__eval_node(tree, index + i + 1);
            node->value = node->func.ptr(node + 1);
            break;
        case NODE_OPERATOR:
        {
            double l = mexp__eval_node(tree, node->oper.left);
            double r = mexp__eval_node(tree, node->oper.right);
            switch (node->oper.type)
            {
                case '+' : node->value = l + r; break;
                case '-' : node->value = l - r; break;
                case '*' : node->value = l * r; break;
                case '/' : node->value = l / r; break;
                case '^' : node->value = pow(l, r); break;
            }
            break;
        }
    }
    return node->value;
}
