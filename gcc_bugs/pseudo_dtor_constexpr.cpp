// ===========================================================================
// gcc_bugs/pseudo_dtor_constexpr.cpp -- GCC 13 skips the side effect in
// (--p)->~T() during constant evaluation, when T is a scalar like int.
//
// Found while making mystd::vector constexpr: destroy_range's loop
//     while (last != first) (--last)->~T();
// never ended at compile time for vector<int> ("constexpr loop iteration
// count exceeds limit of 262144"), because --last never happened. It ran
// correctly at runtime, so the runtime test suite never noticed.
//
// WHAT THE CODE MEANS
//   (--p)->~T() is two steps: evaluate --p (a side effect), then destroy the
//   object p now points to. For a class type, ~T() is a real destructor call.
//   For a scalar (int, double, pointer, enum) there is no destructor: it's a
//   PSEUDO-DESTRUCTOR call. Since C++20 it ends the object's lifetime, and
//   the expression in front of -> must still be evaluated in full.
//
// WHEN IT BITES -- all three at once:
//   1. T is a scalar type (std::string was fine)
//   2. the code runs during constant evaluation (runtime is correct)
//   3. the expression in front of -> or . has a side effect
//
// HOW TO REPRODUCE (from the vector/ folder)
//
//   1. compile time, GCC 13 -- fails:
//        g++-13 -std=c++20 -fsyntax-only gcc_bugs/pseudo_dtor_constexpr.cpp
//      gives
//        error: static assertion failed
//        note: the comparison reduces to '(1 == 0)'
//      1 means p never moved: --p was skipped.
//
//   2. the same function at runtime, GCC 13 -- correct. Build without the
//      static_assert (-DNO_STATIC_ASSERT) and look at the exit code:
//        g++-13 -std=c++20 -DNO_STATIC_ASSERT gcc_bugs/pseudo_dtor_constexpr.cpp -o /tmp/pd && /tmp/pd; echo $?
//      prints 0.
//
//   3. compile time, GCC 15 -- fixed, compiles cleanly:
//        g++-15 -std=c++20 -fsyntax-only gcc_bugs/pseudo_dtor_constexpr.cpp
//
// RESULTS (measured 2026-11-11)
//   g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1)   compile time 1, runtime 0  BUG
//   g++ 15.2.0 (Ubuntu 15.2.0-14ubuntu1~24~ppa1)  compiles cleanly           fixed
//   g++ 16.0.1 20260315 (trunk snapshot)          compiles cleanly           fixed
//   g++ 14: not tested, so the fix landed in 14 or 15.
//
// THE WORKAROUND (what destroy_range in guards.hpp does now)
//   std::destroy_at(--p);
//   --p is now an ordinary function argument, evaluated before the call.
//   destroy_at only receives the finished pointer; the pseudo-destructor
//   inside it has no side effect left to lose. Kept even though the Makefile
//   uses g++-15, so the vector still works on Ubuntu 24.04's default g++.
//
// NOTES
//   - `using T = int;` is needed: p->~int() is not valid syntax. A pseudo-
//     destructor needs a type NAME -- an alias or a template parameter (in
//     the vector, the template parameter T plays that role).
//   - No loop is needed: the skipped decrement shows up directly as 1.
//   - No #include, no allocation, no construct_at: plain C++20.
// ===========================================================================

using T = int;

constexpr long f() {
    int a[1]{};
    int* p = a + 1;
    (--p)->~T();   // must evaluate --p, then end the lifetime of a[0]
    return p - a;  // 0 if --p ran, 1 if it was skipped
}

#ifndef NO_STATIC_ASSERT
static_assert(f() == 0);
#endif

int main() { return f(); }   // same function at runtime
