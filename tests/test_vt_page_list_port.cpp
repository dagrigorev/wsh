#include "test_helpers.h"
#include "../vt/page_list.hpp"
using namespace wisp;
using namespace wisp::vt;
TEST(page_list, smoke) {
    PageList s;
    ASSERT_TRUE(PageList::init(zigstd::testing_allocator(), PageList::Options(80, 24), &s));
    s.deinit();
}
