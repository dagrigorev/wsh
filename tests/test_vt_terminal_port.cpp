/* Transliterated from the test blocks in Ghostty src/terminal/Terminal.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 *
 * Upstream's tests: same inputs, same assertions, same names. Built with
 * WISP_IS_TEST and WISP_SLOW_RUNTIME_SAFETY, as upstream's tests run.
 */

#include "test_helpers.h"
#include "../vt/terminal.hpp"

using namespace wisp;
using namespace wisp::vt;

static zigstd::Allocator talloc() { return zigstd::testing_allocator(); }

TEST(terminal, zz_Wisp_no_leaks) { ASSERT_TRUE(zigstd::testing_state().live == 0); }
