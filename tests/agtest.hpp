// agtest — doctest compatibility shim for acp-cpp's test suite.
//
// The suite historically used <cassert>'s assert(). This header remaps assert
// onto doctest's CHECK so a test migrates by: dropping <cassert>, including
// this header, and wrapping the old `int main()` body in a TEST_CASE. The
// assert(...) calls inside then route into doctest unchanged (extra parens so
// doctest never rejects assert(a && b) as "Expression Too Complex").
//
// Include this INSTEAD of <doctest/doctest.h>. main() comes from test_main.cpp.
#ifndef ACP_TESTS_AGTEST_HPP
#define ACP_TESTS_AGTEST_HPP

#include <doctest/doctest.h>

#ifdef assert
#  undef assert
#endif
#define assert(cond) DOCTEST_CHECK((cond))

#endif // ACP_TESTS_AGTEST_HPP
