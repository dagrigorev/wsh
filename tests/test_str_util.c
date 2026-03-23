#include "test_helpers.h"
#include "../src/core/str_util.h"
#include <string.h>

TEST(StrUtil, StrDup) {
    char *s = str_dup("hello world");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello world");
    str_free(s);
}

TEST(StrUtil, StrNDup) {
    char *s = str_ndup("abcdef", 3);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "abc");
    str_free(s);
}

TEST(StrUtil, StrJoin) {
    char *parts[] = { "a", "b", "c" };
    char *j = str_join(parts, 3, ":");
    ASSERT_NOT_NULL(j);
    ASSERT_STR_EQ(j, "a:b:c");
    str_free(j);
}

TEST(StrUtil, StrTrim) {
    char buf[] = "  hello  ";
    char *r = str_trim(buf);
    ASSERT_STR_EQ(r, "hello");
}

TEST(StrUtil, StrStartsWith) {
    ASSERT_TRUE(str_startswith("foobar", "foo"));
    ASSERT_FALSE(str_startswith("foobar", "bar"));
    ASSERT_FALSE(str_startswith("", "x"));
    ASSERT_TRUE(str_startswith("x", ""));
}

TEST(StrUtil, StrSplit) {
    char **parts = NULL;
    int n = str_split("a:b:c:d", ':', &parts);
    ASSERT_EQ(n, 4);
    ASSERT_STR_EQ(parts[0], "a");
    ASSERT_STR_EQ(parts[1], "b");
    ASSERT_STR_EQ(parts[2], "c");
    ASSERT_STR_EQ(parts[3], "d");
    ASSERT_NULL(parts[4]);   /* sentinel */
    str_split_free(parts, n);
}

TEST(StrUtil, UTF8RoundTrip) {
    const char *utf8 = "Hello, 世界! 🎉";
    wchar_t *wide = u8_to_u16(utf8, NULL);
    ASSERT_NOT_NULL(wide);
    char *back = u16_to_u8(wide, NULL);
    ASSERT_NOT_NULL(back);
    ASSERT_STR_EQ(back, utf8);
    str_free(wide);
    str_free(back);
}

TEST(StrUtil, UTF8Decode) {
    /* ASCII */
    const char *p = "A";
    unsigned int cp = utf8_decode(&p);
    ASSERT_EQ(cp, 'A');

    /* 2-byte: © U+00A9 */
    const char *c2 = "\xC2\xA9";
    cp = utf8_decode(&c2);
    ASSERT_EQ(cp, 0xA9);

    /* 3-byte: ∞ U+221E */
    const char *c3 = "\xE2\x88\x9E";
    cp = utf8_decode(&c3);
    ASSERT_EQ(cp, 0x221E);
}
