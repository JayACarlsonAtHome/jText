// File: tests/manual/main.cpp
//
// Entry point: registers all test categories with the suite, then runs them.
//
// Adding a new test category:
//   1. #include its category header below
//   2. Call its register_*_tests() function with the suite

#include "framework/runner.hpp"

#include "categories/meta_basic.hpp"
#include "categories/parser_basic.hpp"
#include "categories/section_basic.hpp"
#include "categories/validator_basic.hpp"

int main(int argc, char* argv[])
{
    jtext::test::suite s;

    // Framework self-tests.
    register_meta_basic_tests(s);

    // jText line-grammar parser tests.
    register_parser_basic_tests(s);

    // jText file/section structure parser tests.
    register_section_basic_tests(s);

    // jText validator tests.
    register_validator_basic_tests(s);

    // Emitter and other categories will register here as they come online.

    return s.run(argc, argv);
}

