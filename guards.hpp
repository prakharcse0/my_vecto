#ifndef MYSTD_GUARDS_HPP
#define MYSTD_GUARDS_HPP

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <memory>

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
//
// std::destroy_at, not (--last)->~T(): for a scalar T that's a pseudo-
// destructor call, and GCC 13 drops the side effect in front of the -> during
// constant evaluation. --last never runs, the loop never ends, and the
// compiler gives up at its constexpr iteration limit. Runtime was fine;
// GCC 15.2 and 16 are fixed (14 untested).
// Minimal repro + results: gcc_bugs/pseudo_dtor_constexpr.cpp destroy_at(p) receives an already-computed pointer, so
// there's no side effect left in the destructor call to lose.
template <class T>
constexpr void destroy_range(T* first, T* last) noexcept {
    while (last != first) std::destroy_at(--last);
}

// ---------------------------------------------------------------------------
// default_construct_at -- DEFAULT-initialise a T at p: `T`, no parens.
// Class with a default ctor: same as value-init, the ctor runs either way.
// int, or an aggregate of them: the bytes are left as they were, no zeroing.
//
// std::construct_at can't do this. It's specified as ::new (p) T(args...),
// always WITH parens, so with no args it value-initialises (zeroes an int).
// And placement new -- the only way to spell default-init into raw storage --
// isn't allowed in constant evaluation until C++26 (P2747).
//
// So at compile time, fall back to value-init. No valid program can tell the
// difference: reading a default-initialised int is UB, and UB during constant
// evaluation is a compile error, so nobody can observe the zero.
//
// Demo only -- nothing calls it yet (resize(n) must value-init, as std does).
// ---------------------------------------------------------------------------
template <class T>
constexpr void default_construct_at(T* p) {
    if consteval {
        std::construct_at(p);
    } else {
        ::new (static_cast<void*>(p)) T;
    }
}

// ===========================================================================
// alloc_guard -- allocates a raw block of n slots (std::allocator<T>) and owns
// it until release().
//
// Knows nothing about objects; never calls a destructor. Its only job is
// "somebody has to free this pointer if we bail out".
//
// It allocates in its own constructor, so there's no moment at all when the
// block exists but nobody owns it. (It used to take a pointer from
// allocate_raw; that was safe only because nothing between the two could
// throw.) It stores n because std::allocator::deallocate needs the size.
//
// Pair it with construct_guard when objects also need destroying. Keeping the
// two responsibilities apart is what lets several disjoint constructed ranges
// share one block -- exactly the case in emplace_back.
// ===========================================================================
template <class T>
class alloc_guard {
    T* p_;
    std::size_t n_;

public:
    // Allocates n slots and owns them. Throws bad_alloc (or its subclass
    // bad_array_new_length when n * sizeof(T) would overflow) -- nothing to
    // clean up then: no object was constructed, so the destructor doesn't run.
    // not noexcept anymore, since it's allocating
    explicit constexpr alloc_guard(std::size_t n) : p_(std::allocator<T>{}.allocate(n)), n_(n) {}

    // noexcept: this runs while another exception is in flight, and two live
    // exceptions at once is std::terminate.
    // `if (p_)` needed -- std::allocator<T>{}.deallocate(nullptr) isn't a guaranteed no-op.
    constexpr ~alloc_guard() { 
        if(p_) 
            std::allocator<T>{}.deallocate(p_, n_); 
    }

    // Disarms. The caller now owns the block.
    constexpr void release() noexcept { p_ = nullptr; }

    // Observe without giving up ownership.
    constexpr T* get() const noexcept { return p_; }

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
// allocate_raw -- RETIRED for the constexpr work. Kept for the lesson below.
//   - ::operator new isn't allowed in constant evaluation; std::allocator is.
//   - std::allocator::allocate does the overflow check itself (throws
//     bad_array_new_length), so the check here would be duplicate work.
//   - alloc_guard now allocates in its own constructor, which closes the
//     unowned window by construction instead of by "nothing can throw here".
//
// What it was: n slots of uninitialised storage for T, or throw. The caller
// guarded the result itself:
//
//     alloc_guard<T> ag(allocate_raw<T>(n));
//
// Nothing could throw between the two, since alloc_guard's constructor was
// noexcept -- there was no window in which the block was unowned.
//
// The lesson that still holds: never write sizeof(T) * n inline. Written inline at
// every call site, sizeof(T) * n silently wraps on overflow: ask for
// SIZE_MAX/4 + 1 ints and the product is 0, ::operator new(0) succeeds, and
// the writes that follow run off the end of a tiny block.
// ---------------------------------------------------------------------------
// template <class T>
// [[nodiscard]] T* allocate_raw(std::size_t n) {
//     if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
//         throw std::length_error("mystd: allocation size overflow");
//     return static_cast<T*>(::operator new(sizeof(T) * n));
// }

// ===========================================================================
// construct_guard -- owns a range of constructed objects whose extent GROWS
// as a loop makes progress.
//
// The cursor is captured BY ADDRESS, not by value. The guard must read how
// far the loop got at the moment of the throw; a copy taken at construction
// would always describe an empty range and destroy nothing.
//
// Caller's obligation: `cur` always points one past the last SUCCESSFULLY
// constructed object -- advance it only after construct_at returns. This is the same
// rule as size_ in the container: publish after the object exists.
//
// Does NOT free memory. That is alloc_guard's job.
// ===========================================================================
template <class T>
class construct_guard {
    T*        first_;
    T* const* cur_;   // null once released // why not std::optional<T* const&> ? : not optional reference not valid before cpp-26

public:
    // Guards [first, cur). `cur` must outlive the guard -- in practice both
    // are locals in the same scope, with `cur` declared first.
    constexpr construct_guard(T* first, T*& cur) noexcept
        : first_(first), cur_(&cur) {}

    constexpr ~construct_guard() {
        if (cur_) destroy_range(first_, *cur_);
    }

    // Disarms. The constructed objects now belong to whoever committed them.
    constexpr void release() noexcept { cur_ = nullptr; }

    construct_guard(const construct_guard&)            = delete;
    construct_guard& operator=(const construct_guard&) = delete;
};

}  // namespace detail
}  // namespace mystd

#endif  // MYSTD_GUARDS_HPP