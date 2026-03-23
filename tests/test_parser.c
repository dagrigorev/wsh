/*
 * test_parser.c — Parser unit tests.
 *
 * We verify AST structure rather than execution results, so these tests
 * don't need a live ShellContext or I/O.
 */
#include "test_helpers.h"
#include "../src/core/arena.h"
#include "../src/shell/lexer.h"
#include "../src/shell/parser.h"
#include <string.h>

static ASTNode *parse_str(const char *s, Arena *a) {
    Lexer  l; lex_init(&l, s, a);
    Parser p; parser_init(&p, &l, a);
    return parser_parse(&p);
}

TEST(Parser, SimpleCommand) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("echo hello world", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_CMD);
    ASSERT_EQ(n->cmd.argc, 3);
    ASSERT_STR_EQ(n->cmd.argv[0], "echo");
    ASSERT_STR_EQ(n->cmd.argv[1], "hello");
    ASSERT_STR_EQ(n->cmd.argv[2], "world");
    ASSERT_NULL(n->cmd.argv[3]);
    arena_destroy(a);
}

TEST(Parser, Pipeline) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("ls | grep foo", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_PIPE);
    ASSERT_EQ(n->binary.left->kind,  NODE_CMD);
    ASSERT_EQ(n->binary.right->kind, NODE_CMD);
    ASSERT_STR_EQ(n->binary.left->cmd.argv[0],  "ls");
    ASSERT_STR_EQ(n->binary.right->cmd.argv[0], "grep");
    arena_destroy(a);
}

TEST(Parser, AndOr) {
    Arena *a = arena_create(8192);

    ASTNode *n = parse_str("cmd1 && cmd2", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_AND);

    n = parse_str("cmd1 || cmd2", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_OR);

    arena_destroy(a);
}

TEST(Parser, IfStatement) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("if true; then echo yes; fi", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_IF);
    ASSERT_NOT_NULL(n->ifnode.cond);
    ASSERT_NOT_NULL(n->ifnode.body);
    ASSERT_NULL(n->ifnode.alt);
    arena_destroy(a);
}

TEST(Parser, IfElse) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("if false; then echo no; else echo yes; fi", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_IF);
    ASSERT_NOT_NULL(n->ifnode.alt);
    arena_destroy(a);
}

TEST(Parser, ForLoop) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("for x in a b c; do echo $x; done", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_FOR);
    ASSERT_STR_EQ(n->fornode.var, "x");
    ASSERT_EQ(n->fornode.word_count, 3);
    ASSERT_STR_EQ(n->fornode.words[0], "a");
    arena_destroy(a);
}

TEST(Parser, FunctionDefinition) {
    Arena *a = arena_create(8192);

    /* 'function' keyword form */
    ASTNode *n = parse_str("function greet { echo hi; }", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_FUNCTION);
    ASSERT_STR_EQ(n->func.name, "greet");

    /* name() {} form */
    n = parse_str("greet() { echo hi; }", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_FUNCTION);
    ASSERT_STR_EQ(n->func.name, "greet");

    arena_destroy(a);
}

TEST(Parser, Sequence) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("cmd1; cmd2; cmd3", a);
    ASSERT_NOT_NULL(n);
    /* Should be left-associative: ((cmd1;cmd2);cmd3) */
    ASSERT_EQ(n->kind, NODE_SEQ);
    arena_destroy(a);
}

TEST(Parser, Background) {
    Arena *a = arena_create(8192);
    ASTNode *n = parse_str("sleep 5 &", a);
    ASSERT_NOT_NULL(n);
    ASSERT_EQ(n->kind, NODE_BG);
    ASSERT_EQ(n->wrap.child->kind, NODE_CMD);
    arena_destroy(a);
}

TEST(Parser, EmptyInput) {
    Arena *a = arena_create(4096);
    ASTNode *n = parse_str("", a);
    ASSERT_NULL(n);   /* valid: empty list */
    n = parse_str("   \n  # comment\n  ", a);
    ASSERT_NULL(n);
    arena_destroy(a);
}
