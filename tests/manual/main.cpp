// File: tests/manual/main.cpp
//
// Session 1 entry point. Calls each test-category's registration
// function (just one of them in Session 1) and prints a summary.
//
// As more tests are added, the body of main() grows by one or two
// lines per category — the registration list is part of the
// documentation of what's tested.

#include "framework/assertion.hpp"
#include "categories/meta_basic.hpp"

int main()
{
    using namespace jtext::test;

    std::println("══════════════════════════════════════════");
    std::println(" jText test framework — meta self-test");
    std::println("══════════════════════════════════════════");

    // Session 1: just the framework's self-tests.
    // Session 3+ will add parser, validator, emitter test categories here.
    run_meta_basic_tests();

    return print_summary();
}
