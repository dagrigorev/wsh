#include "test_helpers.h"
#include "../src/core/arena.h"
#include "../src/shell/lexer.h"
#include <string.h>

static Token next_tok(Lexer *l) { return lex_next(l); }

TEST(Lexer, SimpleWords) {
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "echo hello world", a);

    Token t = next_tok(&l);
    ASSERT_EQ(t.kind, TOK_WORD);
    ASSERT_STR_EQ(t.text, "echo");

    t = next_tok(&l);
    ASSERT_EQ(t.kind, TOK_WORD);
    ASSERT_STR_EQ(t.text, "hello");

    t = next_tok(&l);
    ASSERT_EQ(t.kind, TOK_WORD);
    ASSERT_STR_EQ(t.text, "world");

    t = next_tok(&l);
    ASSERT_EQ(t.kind, TOK_EOF);

    arena_destroy(a);
}

TEST(Lexer, Operators) {
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "a | b && c || d ; e &", a);

    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_PIPE);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_AND);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_OR);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_SEMI);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_BG);
    ASSERT_EQ(next_tok(&l).kind, TOK_EOF);

    arena_destroy(a);
}

TEST(Lexer, Keywords) {
    /* Keywords are now promoted by the parser, not the lexer (BUG-005). */
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "if then else elif fi while do done for in", a);

    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);

    arena_destroy(a);
}

TEST(Lexer, SingleQuotedPreservesContent) {
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "'hello $world'", a);
    Token t = next_tok(&l);
    ASSERT_EQ(t.kind, TOK_WORD);
    ASSERT_STR_EQ(t.text, "hello $world"); /* dollar not expanded */
    arena_destroy(a);
}

TEST(Lexer, AssignmentDetection) {
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "FOO=bar", a);
    Token t = next_tok(&l);
    ASSERT_EQ(t.kind, TOK_ASSIGN);
    ASSERT_STR_EQ(t.text, "FOO=bar");
    arena_destroy(a);
}

TEST(Lexer, CommentSkipped) {
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "echo hi # this is a comment", a);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);  /* echo */
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);  /* hi */
    ASSERT_EQ(next_tok(&l).kind, TOK_EOF);   /* comment gone */
    arena_destroy(a);
}

TEST(Lexer, Redirections) {
    Arena *a = arena_create(4096);
    Lexer l; lex_init(&l, "cmd < in.txt > out.txt 2>> err.log", a);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_REDIR_IN);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_REDIR_OUT);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    ASSERT_EQ(next_tok(&l).kind, TOK_REDIR_APPEND);
    ASSERT_EQ(next_tok(&l).kind, TOK_WORD);
    arena_destroy(a);
}

TEST(Lexer, ParsesFdDupRedirection) {
    Arena *arena = arena_create(4096);
    Lexer l;
    lex_init(&l, "echo hi 2>&1", arena);
    Token t;
    do { t = lex_next(&l); } while (t.kind != TOK_REDIR_DUP && t.kind != TOK_EOF);
    ASSERT_EQ(t.kind, TOK_REDIR_DUP);
    ASSERT_EQ(t.fd, 2);
    ASSERT_STR_EQ(t.text, "1");
    arena_destroy(arena);
}
