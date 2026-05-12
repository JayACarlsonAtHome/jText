// File: tests/manual/main.cpp
//
// Entry point: registers framework self-tests AND the line-grammar
// parser tests with the suite, then runs them.
//
// Adding a new test category:
//   1. #include its category header below
//   2. Call its register_*_tests() function with the suite

#include "framework/runner.hpp"

#include "categories/meta_basic.hpp"
#include "categories/parser_basic.hpp"

int main(int argc, char* argv[])
{
    jtext::test::suite s;

    // Framework self-tests.
    register_meta_basic_tests(s);

    // jText line-grammar parser tests.
    register_parser_basic_tests(s);

    // Section parser, file parser, validator, emitter test categories
    // register here as they come online.

    return s.run(argc, argv);
}
