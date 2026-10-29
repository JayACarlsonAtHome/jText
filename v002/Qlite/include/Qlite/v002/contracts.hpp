// Qlite contracts — QLITE_PRE / QLITE_POST / QLITE_ASSERT, one spelling for every compiler.
// For programmer errors only, such as reading a column with no current row or binding more
// values than the SQL has placeholders. What the database reports stays an Error.
// Included by Database.hpp (and the module) before the bodies, which use the macros.
//
//   NATIVE  : __cpp_contracts defined (GCC 16 at -std=c++26; the C++26 builds of Qlite,
//             jText and jac313 also pass -fcontracts -fcontract-evaluation-semantic=enforce):
//             the macros are C++26 contract_assert, and a violation goes to the
//             contract-violation handler and terminates. Link with -fcontracts.
//   RUNTIME : no __cpp_contracts (clang, gcc15, any C++23 build): a check that prints the
//             condition and where, then aborts.
//   OFF     : QLITE_CONTRACTS_ENABLED == 0: the macros compile to nothing.
//
// Pre/post are checked in the function body (a macro cannot put pre()/post() on the
// declarator portably), at entry and at the point of the result.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace Qlite::contracts {

[[noreturn]] inline void violation(const char* kind, const char* expr,
                                   const std::source_location loc = std::source_location::current())
{
    std::fprintf(stderr, "Qlite contract %s violated: (%s) at %s:%u in %s\n", kind, expr,
                 loc.file_name(), static_cast<unsigned>(loc.line()), loc.function_name());
    std::abort();
}

} // namespace Qlite::contracts

#ifndef QLITE_CONTRACTS_ENABLED
#  define QLITE_CONTRACTS_ENABLED 1
#endif

// The calls are spelled Qlite::contracts (no leading ::) so a jac313-prefix copy renames them.
#if QLITE_CONTRACTS_ENABLED && defined(__cpp_contracts)
#  define QLITE_CONTRACTS_NATIVE 1
#  define QLITE_ASSERT(expr) contract_assert(expr)
#  define QLITE_PRE(expr)    contract_assert(expr)
#  define QLITE_POST(expr)   contract_assert(expr)
#elif QLITE_CONTRACTS_ENABLED
#  define QLITE_CONTRACTS_NATIVE 0
#  define QLITE_ASSERT(expr) ((expr) ? (void)0 : Qlite::contracts::violation("assert", #expr))
#  define QLITE_PRE(expr)    ((expr) ? (void)0 : Qlite::contracts::violation("precondition", #expr))
#  define QLITE_POST(expr)   ((expr) ? (void)0 : Qlite::contracts::violation("postcondition", #expr))
#else
#  define QLITE_CONTRACTS_NATIVE 0
#  define QLITE_ASSERT(expr) ((void)0)
#  define QLITE_PRE(expr)    ((void)0)
#  define QLITE_POST(expr)   ((void)0)
#endif
