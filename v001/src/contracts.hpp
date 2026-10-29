// jText contracts — JTEXT_PRE / JTEXT_POST / JTEXT_ASSERT, one spelling for every compiler.
// Private to the library (included by jText.cpp only); for programmer errors, such as
// calling the writer out of order. Bad input from a file is reported, never a contract.
//
//   NATIVE  : __cpp_contracts defined (v002 builds with gcc16 -fcontracts
//             -fcontract-evaluation-semantic=enforce): the macros are C++26 contract_assert,
//             and a violation goes to the contract-violation handler and terminates.
//   RUNTIME : no native contracts (clang, gcc15, v001's C++23): a check that prints the
//             condition and where, then aborts.
//   OFF     : JTEXT_CONTRACTS_ENABLED == 0: the macros compile to nothing.
//
// Pre/post are checked in the function body (a macro cannot put pre()/post() on the
// declarator portably), at entry and at the point of the result.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace jtext::contracts {

[[noreturn]] inline void violation(const char* kind, const char* expr,
                                   const std::source_location loc = std::source_location::current())
{
    std::fprintf(stderr, "jText contract %s violated: (%s) at %s:%u in %s\n", kind, expr,
                 loc.file_name(), static_cast<unsigned>(loc.line()), loc.function_name());
    std::abort();
}

} // namespace jtext::contracts

#ifndef JTEXT_CONTRACTS_ENABLED
#  define JTEXT_CONTRACTS_ENABLED 1
#endif

#if JTEXT_CONTRACTS_ENABLED && defined(__cpp_contracts)
#  define JTEXT_CONTRACTS_NATIVE 1
#  define JTEXT_ASSERT(expr) contract_assert(expr)
#  define JTEXT_PRE(expr)    contract_assert(expr)
#  define JTEXT_POST(expr)   contract_assert(expr)
#elif JTEXT_CONTRACTS_ENABLED
#  define JTEXT_CONTRACTS_NATIVE 0
#  define JTEXT_ASSERT(expr) ((expr) ? (void)0 : ::jtext::contracts::violation("assert", #expr))
#  define JTEXT_PRE(expr)    ((expr) ? (void)0 : ::jtext::contracts::violation("precondition", #expr))
#  define JTEXT_POST(expr)   ((expr) ? (void)0 : ::jtext::contracts::violation("postcondition", #expr))
#else
#  define JTEXT_CONTRACTS_NATIVE 0
#  define JTEXT_ASSERT(expr) ((void)0)
#  define JTEXT_PRE(expr)    ((void)0)
#  define JTEXT_POST(expr)   ((void)0)
#endif
