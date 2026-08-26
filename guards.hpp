#ifndef MYSTD_GUARDS_HPP
#define MYSTD_GUARDS_HPP

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>

// ===========================================================================
// Scope-bound cleanup guards for building containers.
//
// The pattern: cleanup is the DEFAULT, performed by a local's destructor
// during stack unwinding. Success is what cancels it, via release().
//
// This inverts hand-written try/catch, where cleanup lives in the failure
// path. Forgetting release() breaks the happy path and shows up in the first
// test you run; forgetting a catch clause breaks a path tests rarely reach.
//
// Everything here is an implementation detail. Nothing in mystd::detail is
// part of the container's public interface.
// ===========================================================================

namespace mystd {
namespace detail {

// ---------------------------------------------------------------------------
// destroy_range -- destroy [first, last), in reverse order of construction.
//
// Free function rather than a member so both guards and commit paths can use
// it. noexcept because element destructors are required not to throw; if one
// does, std::terminate is the correct outcome, not a half-destroyed range.
// ---------------------------------------------------------------------------
template <class T>
void destroy_range(T* first, T* last) noexcept {
    while (last != first) (--last)->~T();
}

// ===========================================================================
// alloc_guard -- owns a raw block from ::operator new.
//
// Knows nothing about objects; never calls a destructor. Its only job is
// "somebody has to free this pointer if we bail out".
//
// Pair it with construct_guard when objects also need destroying. Keeping the
// two responsibilities apart is what lets several disjoint constructed ranges
// share one block -- exactly the case in emplace_back.
// ===========================================================================
template <class T>
class alloc_guard {
    T* p_;

public:
    // Takes ownership of p, which must come from ::operator new (or be null).
    explicit alloc_guard(T* p) noexcept : p_(p) {}

    // noexcept: this runs while another exception is in flight, and two live
    // exceptions at once is std::terminate.
    //
    // No `if (p_)` needed -- ::operator delete(nullptr) is a guaranteed no-op.
    ~alloc_guard() { ::operator delete(p_); }

    // Disarms. The caller now owns the block.
    void release() noexcept { p_ = nullptr; }

    // Observe without giving up ownership.
    T* get() const noexcept { return p_; }

    // Two guards owning one block is a double free. Not movable either: a
    // guard whose coverage can leave its scope is a guard you can silently
    // lose.
    alloc_guard(const alloc_guard&)            = delete;
    alloc_guard& operator=(const alloc_guard&) = delete;
};


// =============================================================================
// unique_ptr with a raw ::operator delete deleter instead of alloc_gaurd
// =============================================================================
//
// WHY THE OBVIOUS SPELLINGS FAIL
//   std::unique_ptr<T, ::operator delete>
//     -> error: the Deleter parameter must be a TYPE; ::operator delete is a
//        function (a value).
//   std::unique_ptr<T, decltype(&::operator delete)>
//     -> error: ::operator delete is an OVERLOAD SET (plain, sized, aligned,
//        nothrow, ...). It has no single type, and decltype gives the compiler
//        no target type to pick an overload with.
//
// WHAT WORKS

// 1. Named stateless functor: works in any standard, safe in headers.
//    sizeof(unique_ptr) == sizeof(T*), because an empty deleter is stored
//    for free (empty base optimization / [[no_unique_address]]).
/*
struct raw_deleter {
    void operator()(void* p) const noexcept { ::operator delete(p); }
};
template <class T>
using raw_ptr = std::unique_ptr<T, raw_deleter>;
*/

// 2. C++20 lambda inside decltype: same size and behaviour as option 1.
//    The deleter is called with a T*, which converts implicitly to void*.
/*
template <class T>
using raw_ptr20 = std::unique_ptr<T, decltype([](void* p) noexcept { ::operator delete(p); })>;
*/
//    CAVEATS
//    - Every lambda expression has its own unique type. raw_ptr20<int> and
//      raw_ptr20<int> are the same type, but a second alias written with an
//      identical lambda is NOT compatible with this one.
//      ptr_a<int> x = /*...*/;
//      ptr_b<int> y = std::move(x);   // ERROR: different deleter types, no conversion
//    - Defined in a header, it can trigger ODR or linkage warnings
//      (e.g. GCC -Wsubobject-linkage). Prefer option 1 in headers.
//    - The closure type is default-constructible only because the lambda
//      captures nothing. Never add a capture.

// 3. Function pointer type: works, but costs more.
//    sizeof == 2 * sizeof(void*): the function pointer is stored at runtime,
//    calls go through it indirectly, and a default-constructed one holds a
//    null deleter. Overload resolution succeeds here because the parameter
//    type void(*)(void*) gives the compiler a target.
/*
template <class T>
using raw_ptr_fp = std::unique_ptr<T, void (*)(void*)>;
*/
//    usage: raw_ptr_fp<int> p(raw, ::operator delete);

// USAGE
//   raw_ptr<int> p{static_cast<int*>(::operator new(sizeof(int)))};
//   ::operator new gives raw memory only: no constructor runs, and the
//   deleter runs no destructor. That's fine for trivial types; for anything
//   else, use std::construct_at / std::destroy_at yourself.
// sizeof(raw_ptr_fp<int>) == 2 * sizeof(void*):
// since it gets the function at runtime

// static_assert(sizeof(raw_ptr<int>)    == sizeof(int*));
// static_assert(sizeof(raw_ptr20<int>)  == sizeof(int*));
// static_assert(sizeof(raw_ptr_fp<int>) == 2 * sizeof(void*));

// ---------------------------------------------------------------------------
// allocate_raw -- n slots of uninitialised storage for T, or throw.
//
// Does one thing. The caller guards the result itself, so that the guard is
// declared at the site it protects rather than hidden behind a factory:
//
//     alloc_guard<T> ag(allocate_raw<T>(n));
//
// Nothing can throw between the two, since alloc_guard's constructor is
// noexcept -- there is no window in which the block is unowned.
//
// This is also the single place the size computation lives. Written inline at
// every call site, sizeof(T) * n silently wraps on overflow: ask for
// SIZE_MAX/4 + 1 ints and the product is 0, ::operator new(0) succeeds, and
// the writes that follow run off the end of a tiny block.
// ---------------------------------------------------------------------------
template <class T>
[[nodiscard]] T* allocate_raw(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
        throw std::length_error("mystd: allocation size overflow");
    return static_cast<T*>(::operator new(sizeof(T) * n));
}

// ===========================================================================
// construct_guard -- owns a range of constructed objects whose extent GROWS
// as a loop makes progress.
//
// The cursor is captured BY ADDRESS, not by value. The guard must read how
// far the loop got at the moment of the throw; a copy taken at construction
// would always describe an empty range and destroy nothing.
//
// Caller's obligation: `cur` always points one past the last SUCCESSFULLY
// constructed object -- advance it only after ::new returns. This is the same
// rule as size_ in the container: publish after the object exists.
//
// Does NOT free memory. That is alloc_guard's job.
// ===========================================================================
template <class T>
class construct_guard {
    T*        first_;
    T* const* cur_;   // null once released

public:
    // Guards [first, cur). `cur` must outlive the guard -- in practice both
    // are locals in the same scope, with `cur` declared first.
    construct_guard(T* first, T*& cur) noexcept
        : first_(first), cur_(&cur) {}

    ~construct_guard() {
        if (cur_) destroy_range(first_, *cur_);
    }

    // Disarms. The constructed objects now belong to whoever committed them.
    void release() noexcept { cur_ = nullptr; }

    construct_guard(const construct_guard&)            = delete;
    construct_guard& operator=(const construct_guard&) = delete;
};

}  // namespace detail
}  // namespace mystd

#endif  // MYSTD_GUARDS_HPP