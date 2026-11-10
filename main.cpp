#include "vector.hpp"
#include <algorithm>
#include <concepts>
#include <cstdio>
#include <iterator>
#include <list>
#include <memory>
#include <numeric>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>

// ---------------------------------------------------------------------------
// Tiny test harness. CHECK records a failure instead of aborting, so one run
// reports every broken check rather than just the first.
// ---------------------------------------------------------------------------
static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

using V   = mystd::vector<int>;
using It  = V::iterator;
using CIt = V::const_iterator;

// ---------------------------------------------------------------------------
// Compile-time checks: these encode the design decisions. If one fails, the
// build fails, which is the point: the compiler rejects the bad code.
// ---------------------------------------------------------------------------

// iterator -> const_iterator is implicit; the reverse must not exist at all.
static_assert(std::is_convertible_v<It, CIt>);
static_assert(!std::is_convertible_v<CIt, It>);
static_assert(!std::is_constructible_v<It, CIt>);

// Bugs (a), (b), (c): no way to turn a raw pointer or the literal 0 into an
// iterator from outside the vector. Not implicitly, not explicitly.
static_assert(!std::is_convertible_v<int*, It>);
static_assert(!std::is_constructible_v<It, int*>);
static_assert(!std::is_convertible_v<int, CIt>);
static_assert(!std::is_constructible_v<CIt, const int*>);

// Writing through an iterator works; through a const_iterator it does not.
static_assert(std::is_assignable_v<decltype(*std::declval<It>()), int>);
static_assert(!std::is_assignable_v<decltype(*std::declval<CIt>()), int>);

// Iterators are copied constantly (passed by value, stored, returned).
static_assert(std::is_copy_constructible_v<It>);
static_assert(std::is_copy_constructible_v<CIt>);
static_assert(std::is_copy_assignable_v<It>);
static_assert(std::is_copy_assignable_v<CIt>);

// The copy ctor is compiler-generated, so copying an iterator is trivial.
static_assert(std::is_trivially_copyable_v<It>);
static_assert(std::is_trivially_copyable_v<CIt>);

// const vector hands out const_iterator, non-const hands out iterator.
static_assert(std::is_same_v<decltype(std::declval<V&>().begin()), It>);
static_assert(std::is_same_v<decltype(std::declval<const V&>().begin()), CIt>);
static_assert(std::is_same_v<decltype(std::declval<const V&>().end()), CIt>);

// == and != work within each type and across It / CIt, in both orders.
// equality_comparable_with checks all four directions of == and !=, plus
// that both convert to a common type (CIt).
static_assert(std::equality_comparable<It>);
static_assert(std::equality_comparable<CIt>);
static_assert(std::equality_comparable_with<It, CIt>);

// < <= > >= likewise, all derived from <=>; the category is strong.
static_assert(std::totally_ordered<It>);
static_assert(std::totally_ordered<CIt>);
static_assert(std::totally_ordered_with<It, CIt>);
static_assert(std::is_same_v<decltype(It{} <=> It{}), std::strong_ordering>);
static_assert(std::is_same_v<decltype(It{} <=> CIt{}), std::strong_ordering>);

// Arithmetic: distance is signed ptrdiff_t, in every It / CIt combination.
// sized_sentinel_for<S, I> checks both s - i and i - s; the mixed ones only
// compile because operator-(it, it) is a hidden friend (no C++20 rescue for -).
static_assert(std::is_same_v<It::difference_type, std::ptrdiff_t>);
static_assert(std::is_same_v<decltype(It{} - It{}), std::ptrdiff_t>);
static_assert(std::is_same_v<decltype(It{} - CIt{}), std::ptrdiff_t>);
static_assert(std::is_same_v<decltype(CIt{} - It{}), std::ptrdiff_t>);
static_assert(std::sized_sentinel_for<It, It>);
static_assert(std::sized_sentinel_for<It, CIt>);
static_assert(std::sized_sentinel_for<CIt, It>);

// it +/- n keeps the iterator's own type: It stays It (no decay to CIt).
static_assert(std::is_same_v<decltype(It{} + 1), It>);
static_assert(std::is_same_v<decltype(1 + It{}), It>);
static_assert(std::is_same_v<decltype(It{} - 1), It>);
static_assert(std::is_same_v<decltype(CIt{} + 1), CIt>);
static_assert(std::is_same_v<decltype(It{}++), It>);
static_assert(std::is_same_v<decltype(++std::declval<It&>()), It&>);
static_assert(std::is_same_v<decltype(std::declval<It&>() += 1), It&>);

// [] writes through It, not through CIt -- even on a const iterator object.
static_assert(std::is_same_v<decltype(std::declval<const It&>()[0]), int&>);
static_assert(std::is_same_v<decltype(std::declval<const CIt&>()[0]), const int&>);

// The finish line: one concept that checks everything above at once.
static_assert(std::contiguous_iterator<It>);
static_assert(std::contiguous_iterator<CIt>);

// value_type is T for both: a copy of a const element is a plain int.
// The member itself is checked directly: iter_value_t strips cv, so the
// lines below would still pass if value_type were const int.
static_assert(std::is_same_v<CIt::value_type, int>);
static_assert(std::is_same_v<std::iter_value_t<It>, int>);
static_assert(std::is_same_v<std::iter_value_t<CIt>, int>);
static_assert(std::is_same_v<std::iter_reference_t<CIt>, const int&>);

// What classic (pre-C++20) algorithms see: iterator_traits, derived by
// C++20 from our members and operators, none of it written by hand.
// Category is random_access, not contiguous: C++98 had no contiguous tag,
// iterator_traits never reports one.
using Tr  = std::iterator_traits<It>;
using CTr = std::iterator_traits<CIt>;
static_assert(std::is_same_v<Tr::iterator_category, std::random_access_iterator_tag>);
static_assert(std::is_same_v<Tr::value_type,        int>);
static_assert(std::is_same_v<Tr::difference_type,   std::ptrdiff_t>);
static_assert(std::is_same_v<Tr::reference,         int&>);
static_assert(std::is_same_v<Tr::pointer,           int*>);       // from operator->
static_assert(std::is_same_v<CTr::reference,        const int&>);
static_assert(std::is_same_v<CTr::pointer,          const int*>);

// -> through a const_iterator can only reach const members.
static_assert(std::is_same_v<decltype(std::declval<CIt>().operator->()), const int*>);

// The vector itself now models the ranges concepts (begin/end + size).
static_assert(std::ranges::contiguous_range<V>);
static_assert(std::ranges::contiguous_range<const V>);
static_assert(std::ranges::sized_range<V>);

// cbegin/cend hand out const_iterator even from a non-const vector.
static_assert(std::is_same_v<decltype(std::declval<V&>().cbegin()), CIt>);
static_assert(std::is_same_v<decltype(std::declval<V&>().cend()), CIt>);

// Reverse iterators: non-const vector -> reverse_iterator, const -> const_.
using RIt  = V::reverse_iterator;
using CRIt = V::const_reverse_iterator;
static_assert(std::is_same_v<RIt,  std::reverse_iterator<It>>);
static_assert(std::is_same_v<CRIt, std::reverse_iterator<CIt>>);
static_assert(std::is_same_v<decltype(std::declval<V&>().rbegin()), RIt>);
static_assert(std::is_same_v<decltype(std::declval<const V&>().rbegin()), CRIt>);
static_assert(std::is_same_v<decltype(std::declval<V&>().crbegin()), CRIt>);
static_assert(std::is_same_v<decltype(std::declval<V&>().crend()), CRIt>);

// The adapter inherits our conversion rules: RIt -> CRIt yes, back no.
static_assert(std::is_convertible_v<RIt, CRIt>);
static_assert(!std::is_convertible_v<CRIt, RIt>);
static_assert(std::is_same_v<decltype(*std::declval<CRIt>()), const int&>);

// Reverse is random access but NOT contiguous: memory runs the other way,
// so there is no "pointer + n" view of it.
static_assert(std::random_access_iterator<RIt>);
static_assert(!std::contiguous_iterator<RIt>);

// noexcept: nothing here can fail -- it's all pointer arithmetic. Checked
// with noexcept(expr), which asks the compiler without evaluating anything.
static_assert(noexcept(std::declval<V&>().begin()));
static_assert(noexcept(std::declval<V&>().end()));
static_assert(noexcept(std::declval<const V&>().begin()));
static_assert(noexcept(std::declval<const V&>().end()));
static_assert(noexcept(std::declval<V&>().cbegin()));
static_assert(noexcept(std::declval<V&>().rbegin()));
static_assert(noexcept(*std::declval<It>()));
static_assert(noexcept(*std::declval<CIt>()));
static_assert(noexcept(++std::declval<It&>()));
static_assert(noexcept(std::declval<It&>()++));
static_assert(noexcept(--std::declval<It&>()));
static_assert(noexcept(std::declval<It>() + 1));
static_assert(noexcept(std::declval<It>() - std::declval<CIt>()));
static_assert(noexcept(std::declval<It>() == std::declval<CIt>()));
static_assert(noexcept(std::declval<It>() <=> std::declval<CIt>()));
static_assert(std::is_nothrow_convertible_v<It, CIt>);

// ---------------------------------------------------------------------------
// Runtime checks.
// ---------------------------------------------------------------------------
static void test_deref_and_increment() {
    V v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    It it = v.begin();
    CHECK(*it == 10);

    ++it;
    CHECK(*it == 20);

    // Prefix ++ must return the SAME iterator object, already advanced:
    // ++(++it) moves `it` itself twice, not a temporary copy.
    It it2 = v.begin();
    ++(++it2);
    CHECK(*it2 == 30);

    // The value of ++it is the advanced iterator.
    It it3 = v.begin();
    CHECK(*++it3 == 20);
}

static void test_write_through_iterator() {
    V v;
    v.push_back(1);
    v.push_back(2);

    It it = v.begin();
    *it = 100;
    ++it;
    *it = 200;

    CHECK(v[0] == 100);
    CHECK(v[1] == 200);
}

static void test_const_vector() {
    V v;
    v.push_back(7);
    v.push_back(8);

    const V& cv = v;
    CIt cit = cv.begin();
    CHECK(*cit == 7);
    ++cit;
    CHECK(*cit == 8);
}

static void test_conversion() {
    V v;
    v.push_back(5);

    It  it  = v.begin();
    CIt cit = it;           // implicit iterator -> const_iterator
    CHECK(*cit == 5);
    CHECK(&*cit == &*it);   // same element, not a copy
}

static void test_copy() {
    V v;
    v.push_back(1);
    v.push_back(2);

    // Copying from an LVALUE iterator. (`It a = v.begin();` doesn't count:
    // initialising from a prvalue is guaranteed elision, no copy ctor runs.)
    It a = v.begin();
    It b = a;
    ++b;                    // advancing the copy must not move the original
    CHECK(*a == 1);
    CHECK(*b == 2);

    b = a;                  // copy assignment
    CHECK(*b == 1);

    CIt ca = a;
    CIt cb = ca;            // const_iterator copy too
    CHECK(*cb == 1);
}

static void test_element_types() {
    // Non-trivial element: iterator must hand out a reference to the real
    // object, so modifying through it is visible in the vector.
    mystd::vector<std::string> s;
    s.push_back("hello");
    s.push_back("world");
    auto it = s.begin();
    *it += "!";
    CHECK(s[0] == "hello!");
    ++it;
    CHECK(*it == "world");

    // Move-only element: dereferencing must not try to copy.
    mystd::vector<std::unique_ptr<int>> u;
    u.push_back(std::make_unique<int>(42));
    CHECK(**u.begin() == 42);
}

static void test_equality() {
    V v;
    v.push_back(1);
    v.push_back(2);

    It  it  = v.begin();
    CIt cit = static_cast<const V&>(v).begin();

    // Same type and mixed, both orders: mixed goes through It -> CIt.
    CHECK(it == v.begin());
    CHECK(cit == static_cast<const V&>(v).begin());
    CHECK(it == cit);
    CHECK(cit == it);

    // != is never written; C++20 rewrites a != b as !(a == b).
    It other = it;
    ++other;
    CHECK(it != other);
    CHECK(other != cit);
    CHECK(cit != other);

    // Walking to end(): the first runtime use of end().
    int n = 0;
    for (It i = v.begin(); i != v.end(); ++i) ++n;
    CHECK(n == 2);

    // Empty vector: data_ is null, so begin() and end() are both null.
    V e;
    CHECK(e.begin() == e.end());
    CHECK(static_cast<const V&>(e).begin() == e.end());

    // Value-initialised iterators compare equal (forward iterator requirement).
    CHECK(It{} == It{});
    CHECK(CIt{} == It{});
}

static void test_ordering() {
    V v;
    v.push_back(1);
    v.push_back(2);

    It  a  = v.begin();
    It  b  = a;
    ++b;
    CIt ca = a;
    CIt cb = b;

    // Same type: each operator, true and false cases.
    CHECK(a < b);
    CHECK(!(b < a));
    CHECK(a <= b);
    CHECK(a <= a);
    CHECK(b > a);
    CHECK(!(a > a));
    CHECK(b >= a);
    CHECK(b >= b);

    // Mixed, both orders.
    CHECK(a < cb);
    CHECK(ca < b);
    CHECK(b > ca);
    CHECK(cb > a);
    CHECK(a <= ca);
    CHECK(ca >= a);

    // <=> itself.
    CHECK((a <=> b) < 0);
    CHECK((b <=> a) > 0);
    CHECK((a <=> ca) == 0);

    // end() is greater than every dereferenceable iterator.
    CHECK(b < v.end());
    CHECK(v.begin() < v.end());

    // Empty vector: begin and end are both null, so neither is less.
    V e;
    CHECK(!(e.begin() < e.end()));
    CHECK(e.begin() <= e.end());
}

static void test_arithmetic() {
    V v;
    for (int i = 0; i < 5; ++i) v.push_back(i * 10);   // 0 10 20 30 40
    const V& cv = v;

    // Postfix ++ / --: the RESULT is the old position, the iterator moves.
    It a = v.begin();
    It old = a++;
    CHECK(*old == 0);
    CHECK(*a == 10);
    old = a--;
    CHECK(*old == 10);
    CHECK(*a == 0);

    // *it++ = x writes through the snapshot (old slot), then moves on.
    It w = v.begin();
    *w++ = 1;
    *w++ = 11;
    CHECK(v[0] == 1);
    CHECK(v[1] == 11);
    CHECK(*w == 20);
    v[0] = 0;
    v[1] = 10;

    // Prefix --, and --(--it) chains on the same object.
    It b = v.end();
    --b;
    CHECK(*b == 40);
    --(--b);
    CHECK(*b == 20);

    // += / -= return the same object, so they chain.
    It c = v.begin();
    (c += 1) += 2;
    CHECK(*c == 30);
    c -= 3;
    CHECK(*c == 0);
    c += 0;
    CHECK(c == v.begin());

    // it + n, n + it, it - n, including negative n.
    It d = v.begin();
    CHECK(*(d + 2) == 20);
    CHECK(*(2 + d) == 20);
    CHECK(*(v.end() - 1) == 40);
    CHECK(*(v.end() + (-1)) == 40);
    CHECK(*(d - (-4)) == 40);           // minus a negative moves forward
    CHECK(*d == 0);                     // + and - never touch their operand

    // Round trips.
    CHECK((d + 3) - 3 == d);
    CHECK(3 + d - d == 3);

    // Distance: signed, both directions, all four type combinations.
    CHECK(v.end() - v.begin() == 5);
    CHECK(v.begin() - v.end() == -5);   // negative: size_t would wrap here
    CHECK(v.end() - cv.begin() == 5);   // It - CIt
    CHECK(cv.begin() - v.end() == -5);  // CIt - It
    CHECK(cv.end() - cv.begin() == 5);

    // [] relative to the iterator, writes through It, negative index works.
    It m = v.begin() + 2;
    CHECK(m[0] == 20);
    CHECK(m[2] == 40);
    CHECK(m[-2] == 0);
    m[1] = 31;
    CHECK(v[3] == 31);
    v[3] = 30;
    CIt cm = cv.begin();
    CHECK(cm[4] == 40);

    // const_iterator arithmetic, and n + cit.
    CIt ce = cv.end();
    CHECK(*(ce - 1) == 40);
    CHECK(*(1 + cv.begin()) == 10);

    // Empty vector: begin and end are both null, distance is 0.
    V e;
    CHECK(e.end() - e.begin() == 0);
    CHECK(e.begin() + 0 == e.end());
}

static void test_arrow_and_to_address() {
    mystd::vector<std::string> s;
    s.push_back("hello");
    s.push_back("hi");

    auto it = s.begin();
    CHECK(it->size() == 5);
    it->append("!");                    // non-const member through It
    CHECK(s[0] == "hello!");

    const auto& cs = s;
    auto cit = cs.begin() + 1;
    CHECK(cit->size() == 2);            // const member through CIt is fine

    // to_address: iterator -> raw pointer, same address as the element.
    CHECK(std::to_address(it) == &s[0]);
    CHECK(std::to_address(cit) == &s[1]);
    // Valid on end() too, where &*end() would be UB: it never dereferences.
    CHECK(std::to_address(s.end()) == std::to_address(s.begin()) + 2);
}

static void test_cbegin_cend() {
    V v;
    v.push_back(1);
    v.push_back(2);

    auto it = v.cbegin();               // CIt from a non-const vector
    CHECK(*it == 1);
    CHECK(it == v.begin());             // mixed CIt == It
    CHECK(v.cend() - v.cbegin() == 2);
    CHECK(v.cend() == v.end());

    V e;
    CHECK(e.cbegin() == e.cend());
}

static void test_reverse() {
    V v;
    for (int i = 1; i <= 4; ++i) v.push_back(i);   // 1 2 3 4
    const V& cv = v;

    // Walk backwards.
    int expect = 4;
    for (auto r = v.rbegin(); r != v.rend(); ++r) CHECK(*r == expect--);
    CHECK(expect == 0);
    expect = 4;
    for (auto r = cv.crbegin(); r != cv.crend(); ++r) CHECK(*r == expect--);

    // rbegin wraps end(), and derefs the slot before its base.
    RIt r = v.rbegin();
    CHECK(r.base() == v.end());
    CHECK(&*r == &v[3]);
    CHECK(v.rend().base() == v.begin());

    // Arithmetic is flipped: + goes towards the front, < means "later".
    CHECK(r[1] == 3);
    CHECK(*(r + 3) == 1);
    CHECK(v.rend() - v.rbegin() == 4);
    CHECK(v.rbegin() < v.rend());

    // Write through a reverse_iterator.
    *v.rbegin() = 40;
    CHECK(v[3] == 40);
    v[3] = 4;

    // Mixed RIt / CRIt comparison and conversion, via our It -> CIt.
    CRIt cr = v.rbegin();
    CHECK(cr == v.rbegin());
    CHECK(v.crend() == v.rend());

    // Reverse a range by algorithm: sorting backwards gives descending order.
    std::sort(v.rbegin(), v.rend());
    CHECK(v[0] == 4 && v[1] == 3 && v[2] == 2 && v[3] == 1);

    // Find the LAST match: base() of a reverse hit is one past the element.
    v.push_back(3);                     // 4 3 2 1 3
    auto last3 = std::find(v.rbegin(), v.rend(), 3);
    CHECK(last3.base() - 1 - v.begin() == 4);

    // Ranges view on top of our container.
    auto rv = v | std::views::reverse;
    CHECK(*rv.begin() == 3);
    CHECK(std::ranges::distance(rv) == 5);

    // Empty: rbegin == rend, no begin()-1 is ever formed (data_ is null).
    V e;
    CHECK(e.rbegin() == e.rend());
    CHECK(e.crbegin() == e.crend());
    CHECK(std::distance(e.rbegin(), e.rend()) == 0);
}

// The payoff: standard algorithms, classic and ranges, on our iterators.
static void test_std_algorithms() {
    V v;
    for (int x : {5, 3, 9, 1, 7}) v.push_back(x);
    const V& cv = v;

    // Range-for: needs only begin()/end(), !=, ++, *.
    int sum = 0;
    for (int x : v) sum += x;
    CHECK(sum == 25);
    for (int& x : v) x *= 2;            // writes through It
    CHECK(v[0] == 10);
    for (int& x : v) x /= 2;

    // Read-only algorithms on const_iterator.
    CHECK(std::accumulate(cv.begin(), cv.end(), 0) == 25);
    CHECK(std::find(cv.begin(), cv.end(), 9) - cv.begin() == 2);
    CHECK(std::find(cv.begin(), cv.end(), 42) == cv.end());
    CHECK(std::count_if(cv.begin(), cv.end(), [](int x) { return x > 4; }) == 3);
    CHECK(*std::max_element(cv.begin(), cv.end()) == 9);

    // std::distance dispatches on iterator_category: random_access -> it
    // computes last - first in O(1) instead of walking.
    CHECK(std::distance(v.begin(), v.end()) == 5);
    CHECK(std::distance(v.end(), v.begin()) == -5);

    // sort needs random access AND value_type (for its pivot temporary).
    std::sort(v.begin(), v.end());
    CHECK(v[0] == 1 && v[1] == 3 && v[2] == 5 && v[3] == 7 && v[4] == 9);
    CHECK(std::is_sorted(cv.begin(), cv.end()));

    // Binary search: O(log n) only because it - it and it + n are O(1).
    auto lb = std::lower_bound(cv.begin(), cv.end(), 6);
    CHECK(*lb == 7);
    CHECK(std::binary_search(cv.begin(), cv.end(), 3));
    CHECK(!std::binary_search(cv.begin(), cv.end(), 4));

    // Mutating algorithms.
    std::reverse(v.begin(), v.end());
    CHECK(v[0] == 9 && v[4] == 1);
    std::fill(v.begin() + 1, v.end() - 1, 0);
    CHECK(v[0] == 9 && v[1] == 0 && v[3] == 0 && v[4] == 1);

    // Mixed iterator / const_iterator as one range ([first, last) of
    // different types) only works with the ranges algorithms, which take a
    // separate sentinel type; classic ones need both ends the same type.
    CHECK(std::ranges::count(v.begin(), cv.end(), 0) == 3);

    // Ranges algorithms take the whole container: they need contiguous_range.
    std::ranges::sort(v);
    CHECK(std::ranges::is_sorted(v));
    CHECK(std::ranges::find(cv, 9) == cv.end() - 1);

    // std::span requires contiguous_iterator: this is what the concept buys.
    std::span<int> sp(v.begin(), v.end());
    CHECK(sp.size() == 5);
    CHECK(sp.data() == &v[0]);
    std::span<const int> csp(cv);       // straight from the container
    CHECK(csp.size() == 5);

    // Strings: sort moves strings around via value_type temporaries.
    mystd::vector<std::string> s;
    for (const char* w : {"pear", "apple", "fig"}) s.push_back(w);
    std::sort(s.begin(), s.end());
    CHECK(s[0] == "apple" && s[1] == "fig" && s[2] == "pear");
    auto longest = std::ranges::max_element(s, {}, &std::string::size);
    CHECK(*longest == "apple");

    // Empty vector: every algorithm sees an empty range, nothing derefs null.
    V e;
    std::sort(e.begin(), e.end());
    CHECK(std::find(e.begin(), e.end(), 1) == e.end());
    CHECK(std::distance(e.begin(), e.end()) == 0);
    CHECK(std::ranges::empty(e));
}

// ---------------------------------------------------------------------------
// erase(pos).
//
// Tracked counts live objects, move assignments and destructor calls, and can
// throw from its Nth move assignment. Copy assignment is implicitly deleted
// (a move assignment is declared), so this type only compiles if erase shifts
// with std::move: move_if_noexcept would pick the deleted copy assignment.
// ---------------------------------------------------------------------------
struct Tracked {
    static inline int live         = 0;
    static inline int move_assigns = 0;
    static inline int destroyed    = 0;
    static inline int throw_after  = -1;   // -1: never throw

    int value;

    explicit Tracked(int v) : value(v) { ++live; }
    Tracked(const Tracked& o) : value(o.value) { ++live; }
    Tracked& operator=(Tracked&& o) {
        if (throw_after == 0) throw 1;
        if (throw_after > 0) --throw_after;
        ++move_assigns;
        value = o.value;
        o.value = -1;                      // visibly moved-from
        return *this;
    }
    ~Tracked() { --live; ++destroyed; }
};

static void fill_tracked(mystd::vector<Tracked>& v, int n) {
    v.reserve(n);                          // no relocation noise in the counts
    for (int i = 0; i < n; ++i) v.emplace_back(i);
}

static void test_erase_positions() {
    V v;
    v.reserve(8);
    for (int i = 0; i < 5; ++i) v.push_back(i);     // 0 1 2 3 4
    const int* const buffer = v.data();
    It before = v.begin();

    It r = v.erase(v.cbegin() + 2);                 // middle -> 0 1 3 4
    CHECK(v.size() == 4);
    CHECK(*r == 3);
    CHECK(r == v.begin() + 2);
    CHECK(v[0] == 0 && v[1] == 1 && v[2] == 3 && v[3] == 4);

    r = v.erase(v.cbegin());                        // first -> 1 3 4
    CHECK(r == v.begin());
    CHECK(*r == 1);

    r = v.erase(v.cend() - 1);                      // last -> 1 3
    CHECK(r == v.end());
    CHECK(v.size() == 2);
    CHECK(v[0] == 1 && v[1] == 3);

    // Never reallocates, and iterators before pos stay valid.
    CHECK(v.data() == buffer);
    CHECK(v.capacity() == 8);
    CHECK(before == v.begin());

    // A plain iterator converts to the const_iterator parameter.
    r = v.erase(v.begin());
    CHECK(*r == 3);

    r = v.erase(v.cbegin());                        // only element
    CHECK(r == v.end());
    CHECK(v.empty());
}

static void test_erase_element_types() {
    // Longer than the SSO buffer, so each string owns heap memory and ASan
    // sees any assignment into a destroyed string.
    mystd::vector<std::string> s;
    const std::string a(40, 'a'), b(40, 'b'), c(40, 'c');
    s.push_back(a);
    s.push_back(b);
    s.push_back(c);
    auto r = s.erase(s.cbegin());
    CHECK(s.size() == 2);
    CHECK(*r == b);
    CHECK(s[1] == c);

    // Move-only: erase must never need a copy.
    mystd::vector<std::unique_ptr<int>> u;
    u.push_back(std::make_unique<int>(1));
    u.push_back(std::make_unique<int>(2));
    auto ru = u.erase(u.cbegin());
    CHECK(u.size() == 1);
    CHECK(**ru == 2);
}

static void test_erase_counts() {
    {
        mystd::vector<Tracked> v;
        fill_tracked(v, 5);
        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;

        v.erase(v.cbegin() + 1);            // 3 elements after pos
        CHECK(Tracked::move_assigns == 3);
        CHECK(Tracked::destroyed == 1);
        CHECK(Tracked::live == 4);

        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;
        v.erase(v.cend() - 1);              // nothing after pos
        CHECK(Tracked::move_assigns == 0);
        CHECK(Tracked::destroyed == 1);
        CHECK(Tracked::live == 3);
    }
    CHECK(Tracked::live == 0);
}

// Basic guarantee: hit every throw point. Erasing the first of 4 elements does
// 3 assignments, so n = 0, 1, 2 throw from the first, second and third.
static void test_erase_throwing_assign() {
    for (int n = 0; n < 3; ++n) {
        {
            mystd::vector<Tracked> v;
            fill_tracked(v, 4);             // 0 1 2 3

            bool threw = false;
            Tracked::throw_after = n;
            try {
                v.erase(v.cbegin());
            } catch (int) {
                threw = true;
            }
            Tracked::throw_after = -1;

            CHECK(threw);
            CHECK(v.size() == 4);           // nothing was removed
            CHECK(Tracked::live == 4);      // and nothing was destroyed

            // Contents changed, as basic allows: n elements shifted, then the
            // moved-from slot (slot 0 is the untouched original when n == 0).
            for (int i = 0; i < n; ++i) CHECK(v[i].value == i + 1);
            CHECK(v[n].value == (n == 0 ? 0 : -1));
            for (int i = n + 1; i < 4; ++i) CHECK(v[i].value == i);

            // Still a working vector.
            v.erase(v.cend() - 1);
            CHECK(v.size() == 3);
            CHECK(Tracked::live == 3);
        }
        CHECK(Tracked::live == 0);          // the destructor destroyed each once
    }
}

// ---------------------------------------------------------------------------
// erase(first, last).
// ---------------------------------------------------------------------------
static V make_0_to_5() {
    V v;
    for (int i = 0; i < 6; ++i) v.push_back(i);
    return v;
}

static void test_erase_range_positions() {
    {   // middle
        V v = make_0_to_5();
        const int* const buffer = v.data();
        const std::size_t cap = v.capacity();
        It before = v.begin() + 1;

        It r = v.erase(v.cbegin() + 2, v.cbegin() + 4);     // 0 1 4 5
        CHECK(v.size() == 4);
        CHECK(r == v.begin() + 2);
        CHECK(*r == 4);
        CHECK(v[0] == 0 && v[1] == 1 && v[2] == 4 && v[3] == 5);

        CHECK(v.data() == buffer);      // never reallocates
        CHECK(v.capacity() == cap);
        CHECK(*before == 1);            // before first stays valid
    }
    {   // front
        V v = make_0_to_5();
        It r = v.erase(v.cbegin(), v.cbegin() + 2);         // 2 3 4 5
        CHECK(r == v.begin());
        CHECK(*r == 2);
        CHECK(v.size() == 4);
    }
    {   // tail: nothing to shift, returns the new end()
        V v = make_0_to_5();
        It r = v.erase(v.cbegin() + 4, v.cend());           // 0 1 2 3
        CHECK(r == v.end());
        CHECK(v.size() == 4);
        CHECK(v[3] == 3);
    }
    {   // everything
        V v = make_0_to_5();
        It r = v.erase(v.cbegin(), v.cend());
        CHECK(r == v.end());
        CHECK(v.empty());
        CHECK(v.capacity() != 0);       // storage is kept
    }
    {   // plain iterators convert to the const_iterator parameters
        V v = make_0_to_5();
        It r = v.erase(v.begin() + 1, v.end() - 1);         // 0 5
        CHECK(*r == 5);
        CHECK(v.size() == 2);
    }
}

static void test_erase_range_empty() {
    {   // first == last in the middle: no-op, returns that position
        V v = make_0_to_5();
        It r = v.erase(v.cbegin() + 2, v.cbegin() + 2);
        CHECK(r == v.begin() + 2);
        CHECK(v.size() == 6);
        for (int i = 0; i < 6; ++i) CHECK(v[i] == i);
    }
    {   // first == last == end()
        V v = make_0_to_5();
        It r = v.erase(v.cend(), v.cend());
        CHECK(r == v.end());
        CHECK(v.size() == 6);
    }
    {   // empty vector: data_ is null
        V v;
        It r = v.erase(v.cbegin(), v.cend());
        CHECK(r == v.end());
        CHECK(v.empty());
    }
    {   // the no-op must not touch the elements: no self-move-assignment
        mystd::vector<Tracked> v;
        fill_tracked(v, 4);
        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;
        v.erase(v.cbegin() + 1, v.cbegin() + 1);
        CHECK(Tracked::move_assigns == 0);
        CHECK(Tracked::destroyed == 0);
        for (int i = 0; i < 4; ++i) CHECK(v[i].value == i);
    }
    CHECK(Tracked::live == 0);
}

static void test_erase_range_element_types() {
    // Long enough to own heap memory, as in test_erase_element_types.
    mystd::vector<std::string> s;
    for (int i = 0; i < 5; ++i) s.push_back(std::string(40, char('a' + i)));
    auto r = s.erase(s.cbegin() + 1, s.cbegin() + 3);       // a d e
    CHECK(s.size() == 3);
    CHECK(*r == std::string(40, 'd'));
    CHECK(s[0] == std::string(40, 'a'));
    CHECK(s[2] == std::string(40, 'e'));

    mystd::vector<std::unique_ptr<int>> u;
    for (int i = 0; i < 4; ++i) u.push_back(std::make_unique<int>(i));
    auto ru = u.erase(u.cbegin(), u.cbegin() + 2);
    CHECK(u.size() == 2);
    CHECK(**ru == 2);
}

static void test_erase_range_counts() {
    {
        mystd::vector<Tracked> v;
        fill_tracked(v, 6);
        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;

        // 3 elements after last, 2 removed. Looping erase(pos) would do 7.
        v.erase(v.cbegin() + 1, v.cbegin() + 3);
        CHECK(Tracked::move_assigns == 3);
        CHECK(Tracked::destroyed == 2);
        CHECK(Tracked::live == 4);

        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;
        v.erase(v.cbegin() + 2, v.cend());                  // nothing after last
        CHECK(Tracked::move_assigns == 0);
        CHECK(Tracked::destroyed == 2);
        CHECK(Tracked::live == 2);
    }
    CHECK(Tracked::live == 0);
}

// Erasing [0, 3) of 6 does 3 assignments; n = 0, 1, 2 hits each throw point.
static void test_erase_range_throwing_assign() {
    for (int n = 0; n < 3; ++n) {
        {
            mystd::vector<Tracked> v;
            fill_tracked(v, 6);             // 0 1 2 3 4 5

            bool threw = false;
            Tracked::throw_after = n;
            try {
                v.erase(v.cbegin(), v.cbegin() + 3);
            } catch (int) {
                threw = true;
            }
            Tracked::throw_after = -1;

            CHECK(threw);
            CHECK(v.size() == 6);           // nothing was removed
            CHECK(Tracked::live == 6);      // and nothing was destroyed

            // n elements landed in [0, n); their sources [3, 3+n) are
            // moved-from; everything else is untouched.
            for (int i = 0; i < 6; ++i) {
                if (i < n)                    CHECK(v[i].value == i + 3);
                else if (i >= 3 && i < 3 + n) CHECK(v[i].value == -1);
                else                          CHECK(v[i].value == i);
            }

            // Still a working vector.
            v.erase(v.cbegin() + 4, v.cend());
            CHECK(v.size() == 4);
            CHECK(Tracked::live == 4);
        }
        CHECK(Tracked::live == 0);
    }
}

// ---------------------------------------------------------------------------
// emplace(pos, args...).
// ---------------------------------------------------------------------------
static bool equals(const V& v, std::initializer_list<int> want) {
    return std::equal(v.begin(), v.end(), want.begin(), want.end());
}

static void test_emplace_positions() {
    {   // a) room, at the end
        V v = make_0_to_5();
        v.reserve(8);
        const int* const buffer = v.data();
        It r = v.emplace(v.cend(), 9);
        CHECK(equals(v, {0, 1, 2, 3, 4, 5, 9}));
        CHECK(r == v.begin() + 6 && *r == 9);
        CHECK(v.data() == buffer);
    }
    {   // b) room, front and middle
        V v = make_0_to_5();
        v.reserve(8);
        const int* const buffer = v.data();
        It before = v.begin() + 1;
        It r = v.emplace(v.cbegin() + 2, 9);
        CHECK(equals(v, {0, 1, 9, 2, 3, 4, 5}));
        CHECK(r == v.begin() + 2 && *r == 9);
        CHECK(v.data() == buffer);
        CHECK(*before == 1);                        // before pos: still valid
        r = v.emplace(v.cbegin(), 8);
        CHECK(equals(v, {8, 0, 1, 9, 2, 3, 4, 5}));
        CHECK(r == v.begin());
    }
    {   // c) no room: front, middle, end
        for (std::size_t at = 0; at <= 4; at += 2) {
            V v;
            for (int i = 0; i < 4; ++i) v.push_back(i);
            CHECK(v.size() == v.capacity());
            It r = v.emplace(v.cbegin() + at, 9);
            CHECK(v.size() == 5);
            CHECK(v.capacity() == 8);
            CHECK(r == v.begin() + at && *r == 9);
            if (at == 0) CHECK(equals(v, {9, 0, 1, 2, 3}));
            if (at == 2) CHECK(equals(v, {0, 1, 9, 2, 3}));
            if (at == 4) CHECK(equals(v, {0, 1, 2, 3, 9}));
        }
    }
    {   // empty vector (cap 0): path c with index 0 and nothing to relocate
        V v;
        It r = v.emplace(v.cend(), 7);
        CHECK(equals(v, {7}));
        CHECK(r == v.begin());
    }
    {   // no args: value-initialised T, not a function declaration
        V v = make_0_to_5();
        v.emplace(v.cbegin() + 1);
        CHECK(equals(v, {0, 0, 1, 2, 3, 4, 5}));
    }
}

// args referring to an element of *this. Long strings, so they live on the
// heap and a dangling read is a real heap-use-after-free for ASan.
static void test_emplace_aliasing() {
    auto make = [] {
        mystd::vector<std::string> v;
        for (char c : {'a', 'b', 'c', 'd'}) v.push_back(std::string(40, c));
        return v;
    };
    {   // c) full: args point into the buffer that relocation frees
        auto v = make();
        CHECK(v.size() == v.capacity());
        v.emplace(v.begin(), v.back());
        CHECK(v.size() == 5);
        CHECK(v[0] == std::string(40, 'd'));
        CHECK(v[4] == std::string(40, 'd'));
    }
    {   // b) room: args point at an element the shift moves
        auto v = make();
        v.reserve(8);
        v.emplace(v.begin(), v.back());             // back is moved-constructed
        CHECK(v[0] == std::string(40, 'd'));
        v.emplace(v.begin() + 1, v[2]);             // v[2] is move-assigned
        CHECK(v[1] == std::string(40, 'b'));
        CHECK(v.size() == 6);
        const char want[] = {'d', 'b', 'a', 'b', 'c', 'd'};
        for (std::size_t i = 0; i < 6; ++i) CHECK(v[i] == std::string(40, want[i]));
    }
}

static void test_emplace_element_types() {
    {   // move-only, both paths
        mystd::vector<std::unique_ptr<int>> v;
        for (int i = 0; i < 4; ++i) v.push_back(std::make_unique<int>(i));
        v.emplace(v.begin() + 1, std::make_unique<int>(9));     // c
        v.emplace(v.begin() + 1, new int(8));                   // b, raw ptr arg
        CHECK(v.size() == 6);
        const int want[] = {0, 8, 9, 1, 2, 3};
        for (std::size_t i = 0; i < 6; ++i) CHECK(*v[i] == want[i]);
    }
    {   // several args forwarded to the constructor
        mystd::vector<std::string> v;
        v.push_back("x");
        v.emplace(v.begin(), 3, 'a');
        v.emplace(v.begin() + 1, "hello", 2);
        CHECK(v.size() == 3);
        CHECK(v[0] == "aaa" && v[1] == "he" && v[2] == "x");
    }
}

// Path b with room: the work counted.
static void test_emplace_counts() {
    {
        mystd::vector<Tracked> v;
        fill_tracked(v, 4);
        v.reserve(8);
        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;

        v.emplace(v.cbegin(), 9);   // tmp, 1 construction into raw,
        CHECK(Tracked::move_assigns == 4);  // 3 shift + tmp into *pos
        CHECK(Tracked::destroyed == 1);     // tmp
        CHECK(Tracked::live == 5);

        Tracked::move_assigns = 0;
        Tracked::destroyed    = 0;
        v.emplace(v.cend(), 8);             // a: no shift, no tmp
        CHECK(Tracked::move_assigns == 0);
        CHECK(Tracked::destroyed == 0);
        CHECK(Tracked::live == 6);
    }
    CHECK(Tracked::live == 0);
}

// Copy-only, and every construction (from int or a copy) can be made to
// throw. No move constructor, so relocation's move_if_noexcept COPIES --
// the throwing-relocation case.
struct Probe {
    static inline int live = 0, throw_after = -1;
    int value;
    static void maybe_throw() {
        if (throw_after == 0) throw 2;
        if (throw_after > 0) --throw_after;
    }
    explicit Probe(int v) : value(v) { maybe_throw(); ++live; }
    Probe(const Probe& o) : value(o.value) { maybe_throw(); ++live; }
    Probe& operator=(const Probe&) = default;
    ~Probe() { --live; }
};
static_assert(!std::is_nothrow_move_constructible_v<Probe>);

static void fill_probe(mystd::vector<Probe>& v, int n) {
    for (int i = 0; i < n; ++i) v.emplace_back(i);
}

// STRONG on every construction throw point, both paths.
static void test_emplace_throwing_construct() {
    // c) full vector of 4, insert at index 2: 1 new element, 2 prefix
    // copies, 2 suffix copies = constructions 0..4.
    for (int n = 0; n < 5; ++n) {
        {
            mystd::vector<Probe> v;
            fill_probe(v, 4);
            v.shrink_to_fit();
            const Probe* const buffer = v.data();

            bool threw = false;
            Probe::throw_after = n;
            try {
                v.emplace(v.cbegin() + 2, 9);
            } catch (int) {
                threw = true;
            }
            Probe::throw_after = -1;

            CHECK(threw);
            CHECK(v.size() == 4 && v.capacity() == 4 && v.data() == buffer);
            for (int i = 0; i < 4; ++i) CHECK(v[i].value == i);
            CHECK(Probe::live == 4);        // every partial copy destroyed
        }
        CHECK(Probe::live == 0);
    }
    // b) room: tmp, then the last element copied into the raw slot.
    for (int n = 0; n < 2; ++n) {
        {
            mystd::vector<Probe> v;
            fill_probe(v, 4);
            v.reserve(8);

            bool threw = false;
            Probe::throw_after = n;
            try {
                v.emplace(v.cbegin() + 1, 9);
            } catch (int) {
                threw = true;
            }
            Probe::throw_after = -1;

            CHECK(threw);
            CHECK(v.size() == 4);
            for (int i = 0; i < 4; ++i) CHECK(v[i].value == i);
            CHECK(Probe::live == 4);
        }
        CHECK(Probe::live == 0);
    }
}

// BASIC on assignment throws in path b: size_ was already published, every
// slot is a live object, the vector is usable and destroys cleanly.
// emplace at the front of 4 with room: 3 shift assigns + 1 for tmp.
static void test_emplace_throwing_assign() {
    for (int n = 0; n < 4; ++n) {
        {
            mystd::vector<Tracked> v;
            fill_tracked(v, 4);
            v.reserve(8);

            bool threw = false;
            Tracked::throw_after = n;
            try {
                v.emplace(v.cbegin(), 9);
            } catch (int) {
                threw = true;
            }
            Tracked::throw_after = -1;

            CHECK(threw);
            CHECK(v.size() == 5);           // the raw slot was published
            CHECK(Tracked::live == 5);      // tmp destroyed, nothing leaked
            CHECK(v[4].value == 3);         // the constructed copy of the last

            v.erase(v.cbegin());            // still a working vector
            CHECK(v.size() == 4);
        }
        CHECK(Tracked::live == 0);
    }
}

// ---------------------------------------------------------------------------
// insert(pos, value): delegates to emplace, so only the overload choice and
// copy-vs-move need testing; emplace's tests cover the paths.
// ---------------------------------------------------------------------------
static void test_insert_value() {
    {   // lvalue: copied, source untouched -- both paths
        mystd::vector<std::string> v;
        v.push_back("b");
        const std::string s(40, 'a');
        auto r = v.insert(v.cbegin(), s);           // c (cap 1 -> 2)
        CHECK(s == std::string(40, 'a'));
        CHECK(r == v.begin() && *r == s);
        v.reserve(4);
        r = v.insert(v.cbegin() + 1, s);            // b
        CHECK(s == std::string(40, 'a'));
        CHECK(v.size() == 3 && v[1] == s && v[2] == "b");
    }
    {   // rvalue: moved. Move-only type proves the T&& overload is picked.
        mystd::vector<std::unique_ptr<int>> v;
        auto p = std::make_unique<int>(7);
        v.insert(v.cend(), std::move(p));           // c
        CHECK(p == nullptr);
        v.insert(v.cbegin(), std::make_unique<int>(6)); // c (cap 1 -> 2)
        v.reserve(4);
        v.insert(v.cbegin() + 1, std::make_unique<int>(5)); // b
        CHECK(v.size() == 3 && *v[0] == 6 && *v[1] == 5 && *v[2] == 7);
    }
    {   // aliasing through the const T& overload, full and with room
        mystd::vector<std::string> v;
        for (char c : {'a', 'b', 'c', 'd'}) v.push_back(std::string(40, c));
        v.insert(v.cbegin(), v.back());             // c
        v.insert(v.cbegin() + 1, v[3]);             // b: v[3] is shifted
        CHECK(v.size() == 6);
        const char want[] = {'d', 'c', 'a', 'b', 'c', 'd'};
        for (std::size_t i = 0; i < 6; ++i) CHECK(v[i] == std::string(40, want[i]));
    }
    {   // implicit conversion to T: const char* -> std::string temporary
        mystd::vector<std::string> v;
        v.insert(v.cend(), "hi");
        CHECK(v.size() == 1 && v[0] == "hi");
    }
}

// ---------------------------------------------------------------------------
// insert(pos, n, value).
// n is a std::size_t VARIABLE on purpose: insert(it, 3, 7) with two ints
// still picks the unconstrained range template (the TODO(constraint)).
// ---------------------------------------------------------------------------
static V make_0_to_5_with_room(std::size_t cap) {
    V v = make_0_to_5();
    v.reserve(cap);
    return v;
}

static void test_insert_fill_room() {
    struct Case { std::size_t at, n; std::initializer_list<int> want; };
    const Case cases[] = {
        {2, 2, {0, 1, 9, 9, 2, 3, 4, 5}},           // n <  tail (4)
        {4, 2, {0, 1, 2, 3, 9, 9, 4, 5}},           // n == tail
        {5, 3, {0, 1, 2, 3, 4, 9, 9, 9, 5}},        // n >  tail (1)
        {0, 3, {9, 9, 9, 0, 1, 2, 3, 4, 5}},        // front, n < tail
        {6, 2, {0, 1, 2, 3, 4, 5, 9, 9}},           // end: tail 0
        {1, 6, {0, 9, 9, 9, 9, 9, 9, 1, 2, 3, 4, 5}}, // n > tail, fills cap
    };
    for (const Case& c : cases) {
        V v = make_0_to_5_with_room(12);
        const int* const buffer = v.data();
        It r = v.insert(v.cbegin() + c.at, c.n, 9);
        CHECK(equals(v, c.want));
        CHECK(r == v.begin() + c.at);
        CHECK(v.data() == buffer);
    }
    {   // n == 0: returns pos, changes nothing, even on a full vector
        V v = make_0_to_5();
        v.shrink_to_fit();
        const int* const buffer = v.data();
        const std::size_t zero = 0;
        It r = v.insert(v.cbegin() + 2, zero, 9);
        CHECK(r == v.begin() + 2);
        CHECK(equals(v, {0, 1, 2, 3, 4, 5}) && v.data() == buffer);
    }
}

static void test_insert_fill_realloc() {
    const std::size_t two = 2, ten = 10;
    {
        V v = make_0_to_5();
        v.shrink_to_fit();                          // size 6 == cap 6
        It r = v.insert(v.cbegin() + 2, two, 9);
        CHECK(equals(v, {0, 1, 9, 9, 2, 3, 4, 5}));
        CHECK(r == v.begin() + 2);
        CHECK(v.capacity() == 12);                  // 6 + max(6, 2)
    }
    {   // n bigger than size: doubling wouldn't be enough
        V v = make_0_to_5();
        v.shrink_to_fit();
        v.insert(v.cend(), ten, 9);
        CHECK(v.size() == 16 && v.capacity() == 16); // 6 + max(6, 10)
        CHECK(v[5] == 5 && v[6] == 9 && v[15] == 9);
    }
    {   // empty vector
        V v;
        It r = v.insert(v.cend(), two, 7);
        CHECK(equals(v, {7, 7}) && r == v.begin());
    }
    {   // size_ + n would wrap: length_error, nothing changed
        V v = make_0_to_5();
        bool threw = false;
        try {
            v.insert(v.cbegin(), std::numeric_limits<std::size_t>::max() - 2, 9);
        } catch (const std::length_error&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(equals(v, {0, 1, 2, 3, 4, 5}));
    }
}

// value refers to an element the room path overwrites, in both room cases,
// and into the buffer the realloc path frees.
static void test_insert_fill_aliasing() {
    auto make = [](std::size_t cap) {
        mystd::vector<std::string> v;
        v.reserve(cap);
        for (char c : {'a', 'b', 'c', 'd'}) v.push_back(std::string(40, c));
        return v;
    };
    auto check = [](const mystd::vector<std::string>& v, const char* want) {
        CHECK(v.size() == std::char_traits<char>::length(want));
        for (std::size_t i = 0; i < v.size(); ++i)
            CHECK(v[i] == std::string(40, want[i]));
    };
    const std::size_t two = 2, three = 3;
    { auto v = make(8); v.insert(v.cbegin(), two,   v[1]); check(v, "bbabcd"); }  // n < tail
    { auto v = make(8); v.insert(v.cbegin() + 3, three, v[3]); check(v, "abcdddd"); } // n > tail
    { auto v = make(4); v.insert(v.cbegin() + 1, three, v[3]); check(v, "adddbcd"); } // realloc
}

// Room path: BASIC. Every throw point, and whatever the contents, size()
// counts exactly the live objects and nothing leaks.
// Realloc path: STRONG (Probe is copy-only: relocation copies, can throw).
static void test_insert_fill_throwing() {
    const std::size_t two = 2, three = 3;
    // Constructions, counting Probe(9) as 0 and the aliasing copy as 1:
    //   n = 2 at index 1 (tail 3, n < tail): 2 tail moves into raw -> 0..3
    //   n = 3 at index 3 (tail 1, n > tail): 2 copies into raw,
    //                                        1 tail move into raw -> 0..4
    for (std::size_t n : {two, three}) {
        const std::size_t at = n == 2 ? 1 : 3;
        const int points = n == 2 ? 4 : 5;
        for (int k = 0; k < points; ++k) {
            {
                mystd::vector<Probe> v;
                v.reserve(8);
                fill_probe(v, 4);
                bool threw = false;
                Probe::throw_after = k;
                try {
                    v.insert(v.cbegin() + at, n, Probe(9));  // Probe(9) is construction 0
                } catch (int) {
                    threw = true;
                }
                Probe::throw_after = -1;
                CHECK(threw);
                CHECK(v.size() >= 4 && v.size() <= 4 + n);
                CHECK(Probe::live == static_cast<int>(v.size()));
            }
            CHECK(Probe::live == 0);
        }
    }
    // realloc: full vector of 4, 2 copies at index 2: Probe(9), 2 gap
    // copies, 2 prefix + 2 suffix relocations = constructions 0..6.
    for (int k = 0; k < 7; ++k) {
        {
            mystd::vector<Probe> v;
            fill_probe(v, 4);
            const Probe* const buffer = v.data();
            bool threw = false;
            Probe::throw_after = k;
            try {
                v.insert(v.cbegin() + 2, two, Probe(9));
            } catch (int) {
                threw = true;
            }
            Probe::throw_after = -1;
            CHECK(threw);
            CHECK(v.size() == 4 && v.capacity() == 4 && v.data() == buffer);
            for (int i = 0; i < 4; ++i) CHECK(v[i].value == i);
            CHECK(Probe::live == 4);
        }
        CHECK(Probe::live == 0);
    }
}

// ---------------------------------------------------------------------------
// insert(pos, first, last) and insert(pos, {..}).
// ---------------------------------------------------------------------------

// The constraint: two ints are not iterators, so the fill overload is the
// only candidate. Without the constraint this would pick the template and
// fail to compile (*first on an int).
template <class Vec, class A, class B>
concept can_insert = requires(Vec v, A a, B b) { v.insert(v.cbegin(), a, b); };
static_assert(!std::input_iterator<int>);
static_assert(can_insert<V, int, int>);             // -> fill overload
static_assert(can_insert<V, int*, int*>);           // -> range overload
static_assert(!can_insert<V, int*, long*>);         // mismatched iterator types

static void test_insert_overload_choice() {
    V v = make_0_to_5();
    v.insert(v.cbegin() + 1, 3, 7);                 // int, int: n = 3, value 7
    CHECK(equals(v, {0, 7, 7, 7, 1, 2, 3, 4, 5}));
}

// Single-pass input iterator over an int array: post-increment returns
// void, so it is NOT a forward_iterator. Throws when *'d at `bomb`.
struct OneShot {
    using iterator_concept = std::input_iterator_tag;
    using difference_type  = std::ptrdiff_t;
    using value_type       = int;
    const int* p    = nullptr;
    const int* bomb = nullptr;
    int operator*() const { if (p == bomb) throw 3; return *p; }
    OneShot& operator++() { ++p; return *this; }
    void operator++(int) { ++p; }
    bool operator==(const OneShot& o) const { return p == o.p; }
};
static_assert(std::input_iterator<OneShot>);
static_assert(!std::forward_iterator<OneShot>);

// Same region cases as insert(pos, n, value), from a std::list (forward but
// not random access: std::distance and std::next really walk it).
static void test_insert_range_forward() {
    const std::list<int> src = {7, 8, 9};
    struct Case { std::size_t at; std::initializer_list<int> want; };
    const Case room[] = {
        {1, {0, 7, 8, 9, 1, 2, 3, 4, 5}},           // n <  tail (5)
        {3, {0, 1, 2, 7, 8, 9, 3, 4, 5}},           // n == tail
        {5, {0, 1, 2, 3, 4, 7, 8, 9, 5}},           // n >  tail (1)
        {6, {0, 1, 2, 3, 4, 5, 7, 8, 9}},           // end
    };
    for (const Case& c : room) {
        V v = make_0_to_5_with_room(12);
        const int* const buffer = v.data();
        It r = v.insert(v.cbegin() + c.at, src.begin(), src.end());
        CHECK(equals(v, c.want));
        CHECK(r == v.begin() + c.at && v.data() == buffer);
    }
    {   // realloc
        V v = make_0_to_5();
        v.shrink_to_fit();
        It r = v.insert(v.cbegin() + 2, src.begin(), src.end());
        CHECK(equals(v, {0, 1, 7, 8, 9, 2, 3, 4, 5}));
        CHECK(r == v.begin() + 2 && v.capacity() == 12);
    }
    {   // empty range: returns pos, no change
        V v = make_0_to_5();
        It r = v.insert(v.cbegin() + 2, src.end(), src.end());
        CHECK(r == v.begin() + 2 && equals(v, {0, 1, 2, 3, 4, 5}));
    }
    {   // *first is not T: const char* -> std::string, in both room cases
        const char* words[] = {"x", "y", "z"};
        mystd::vector<std::string> v;
        v.reserve(8);
        v.push_back("a");
        v.push_back("b");
        v.insert(v.cbegin() + 1, std::begin(words), std::end(words)); // n > tail
        v.insert(v.cbegin(), std::begin(words), std::begin(words) + 1); // n < tail
        CHECK(v.size() == 6);
        const char* want[] = {"x", "a", "x", "y", "z", "b"};
        for (std::size_t i = 0; i < 6; ++i) CHECK(v[i] == want[i]);
    }
}

static void test_insert_range_input() {
    {   // istream_iterator at the end: the emplace_back path
        std::istringstream in("7 8 9");
        V v = make_0_to_5();
        It r = v.insert(v.cend(), std::istream_iterator<int>(in), std::istream_iterator<int>());
        CHECK(equals(v, {0, 1, 2, 3, 4, 5, 7, 8, 9}));
        CHECK(r == v.begin() + 6);
    }
    {   // in the middle: through the temporary, with room and without
        for (std::size_t cap : {std::size_t{6}, std::size_t{12}}) {
            std::istringstream in("7 8 9");
            V v = make_0_to_5_with_room(cap);
            if (cap == 6) v.shrink_to_fit();
            It r = v.insert(v.cbegin() + 2, std::istream_iterator<int>(in), std::istream_iterator<int>());
            CHECK(equals(v, {0, 1, 7, 8, 9, 2, 3, 4, 5}));
            CHECK(r == v.begin() + 2);
        }
    }
    {   // empty input range in the middle
        std::istringstream in("");
        V v = make_0_to_5();
        It r = v.insert(v.cbegin() + 2, std::istream_iterator<int>(in), std::istream_iterator<int>());
        CHECK(r == v.begin() + 2 && equals(v, {0, 1, 2, 3, 4, 5}));
    }
}

static void test_insert_range_move_and_il() {
    {   // move_iterator over move-only elements: they are moved, not copied
        std::unique_ptr<int> src[3] = {std::make_unique<int>(7), std::make_unique<int>(8),
                                       std::make_unique<int>(9)};
        mystd::vector<std::unique_ptr<int>> v;
        v.push_back(std::make_unique<int>(0));
        v.push_back(std::make_unique<int>(1));
        v.insert(v.cbegin() + 1, std::make_move_iterator(std::begin(src)),
                 std::make_move_iterator(std::end(src)));
        CHECK(v.size() == 5 && src[0] == nullptr && src[2] == nullptr);
        const int want[] = {0, 7, 8, 9, 1};
        for (std::size_t i = 0; i < 5; ++i) CHECK(*v[i] == want[i]);
    }
    {   // initializer_list
        V v = make_0_to_5();
        It r = v.insert(v.cbegin() + 3, {7, 8});
        CHECK(equals(v, {0, 1, 2, 7, 8, 3, 4, 5}) && r == v.begin() + 3);
        r = v.insert(v.cend(), {});
        CHECK(r == v.end() && v.size() == 8);
    }
}

static void test_insert_range_throwing() {
    // Input path, middle: a throw while READING the range leaves *this
    // untouched (the temporary absorbs it) -- strong.
    {
        const int src[] = {7, 8, 9, 10};
        V v = make_0_to_5();
        const int* const buffer = v.data();
        bool threw = false;
        try {
            v.insert(v.cbegin() + 2, OneShot{src, src + 2}, OneShot{src + 4, nullptr});
        } catch (int) {
            threw = true;
        }
        CHECK(threw);
        CHECK(equals(v, {0, 1, 2, 3, 4, 5}) && v.data() == buffer);
    }
    // Forward path with Probe (copy-only, every construction can throw).
    // Assignments don't throw, so only constructions are throw points.
    //   room n=2 at 1 (tail 3): 2 tail moves into raw         -> 0..1
    //   room n=3 at 3 (tail 1): 2 from range into raw, 1 tail -> 0..2
    //   realloc n=2 at 2 of 4 : 2 into gap, 2+2 relocations   -> 0..5
    const Probe src[3] = {Probe(9), Probe(9), Probe(9)};
    struct Case { std::size_t at, n; bool room; int points; };
    const Case cases[] = {{1, 2, true, 2}, {3, 3, true, 3}, {2, 2, false, 6}};
    for (const Case& c : cases) {
        for (int k = 0; k < c.points; ++k) {
            {
                mystd::vector<Probe> v;
                if (c.room) v.reserve(8);
                fill_probe(v, 4);
                const Probe* const buffer = v.data();
                bool threw = false;
                Probe::throw_after = k;
                try {
                    v.insert(v.cbegin() + c.at, src, src + c.n);
                } catch (int) {
                    threw = true;
                }
                Probe::throw_after = -1;
                CHECK(threw);
                if (c.room) {   // basic: size counts exactly the live ones
                    CHECK(v.size() >= 4 && v.size() <= 4 + c.n);
                    CHECK(Probe::live == 3 + static_cast<int>(v.size()));
                } else {        // strong
                    CHECK(v.size() == 4 && v.capacity() == 4 && v.data() == buffer);
                    for (int i = 0; i < 4; ++i) CHECK(v[i].value == i);
                    CHECK(Probe::live == 3 + 4);
                }
            }
            CHECK(Probe::live == 3);                // only src is left
        }
    }
}

// The rbegin note: from a reverse hit r, insert takes r.base(), erase takes
// std::prev(r.base()).
static void test_reverse_erase_insert() {
    {   // erase the LAST 3
        V v = make_0_to_5();
        v.push_back(3);                                 // 0 1 2 3 4 5 3
        auto r = std::find(v.rbegin(), v.rend(), 3);
        It after = v.erase(std::prev(r.base()));
        CHECK(equals(v, {0, 1, 2, 3, 4, 5}));
        CHECK(after == v.end());
    }
    {   // r.base() erases the element AFTER the hit -- the bug in the old note
        V v = make_0_to_5();
        auto r = std::find(v.rbegin(), v.rend(), 2);
        v.erase(r.base());
        CHECK(equals(v, {0, 1, 2, 4, 5}));              // 3 is gone, not 2
    }
    {   // insert at r.base(): right after *r in forward order
        V v = make_0_to_5();
        auto r = std::find(v.rbegin(), v.rend(), 2);
        v.insert(r.base(), 9);
        CHECK(equals(v, {0, 1, 2, 9, 3, 4, 5}));
    }
}

// The allocation itself fails. Every path that allocates goes through
// alloc_guard's constructor, so this is the test that catches it being
// marked noexcept (terminate instead of a catchable exception).
//
// SIZE_MAX / 2 ints is more than SIZE_MAX bytes: std::allocator::allocate
// throws bad_array_new_length (a bad_alloc) BEFORE calling operator new, so
// ASan never sees the request. Something merely huge but representable would
// reach ASan's malloc, which aborts by default instead of throwing.
static void test_allocation_failure() {
    const std::size_t huge = std::numeric_limits<std::size_t>::max() / 2;

    auto fails_cleanly = [](auto op) {
        V v = make_0_to_5_with_room(8);
        bool threw = false;
        try {
            op(v);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        CHECK(threw);
        CHECK(equals(v, {0, 1, 2, 3, 4, 5}));   // strong: nothing changed
        CHECK(v.capacity() == 8);
    };
    fails_cleanly([&](V& v) { v.reserve(huge); });
    fails_cleanly([&](V& v) { v.resize(huge); });
    fails_cleanly([&](V& v) { v.resize(huge, 7); });
    fails_cleanly([&](V& v) { v.insert(v.cbegin() + 2, huge, 7); });   // grown_cap doesn't wrap, allocate refuses
}

// ===========================================================================
// COMPILE-TIME TESTS
//
// Each scenario is a constexpr function returning 0, or the __LINE__ of the
// first failing CT_CHECK. It runs twice:
//   - inside the compiler, via static_assert(f() == 0). A failure doesn't
//     compile, and GCC prints "the comparison reduces to (LINE == 0)".
//   - in the program, via CT_RUN in main(), under ASan/UBSan.
// The two catch different bugs (measured by mutation testing):
//   compile time only:  deallocate(nullptr) -- a no-op at runtime
//   runtime only:       deallocate with the wrong size (new-delete-type-mismatch)
//   both:               leaks, use after free, double free
//
// During constant evaluation the compiler itself rejects (measured, g++-15):
// memory still allocated when the evaluation ends, use after deallocation,
// deallocating twice, deallocating what wasn't allocated. It does NOT reject
// destroying the same object twice, or freeing storage whose objects were
// never destroyed (that one isn't even UB). Counted's counter covers those.
//
// Three element types, each catching different bugs:
//   int          trivial; the scalar pseudo-destructor path (the GCC 13 bug)
//   Counted      owns no memory; counts live objects through a POINTER to a
//                counter that's a local of the scenario. Tracked's static
//                counter can't be used: a constant evaluation may only modify
//                objects it created itself.
//   std::string  40 chars, past the small-string buffer, so every element
//                owns heap memory the compiler tracks.
//
// Failure paths can't be tested here: a throw that's actually reached during
// constant evaluation is a compile error. Those stay in the runtime tests.
// ===========================================================================
namespace ct {

using Str = std::string;

struct Counted {
    int* live;   // the scenario's counter
    int  v;      // -1 once moved from

    constexpr Counted(int* l, int x) : live(l), v(x) { ++*live; }
    constexpr Counted(const Counted& o) : live(o.live), v(o.v) { ++*live; }
    constexpr Counted(Counted&& o) noexcept : live(o.live), v(o.v) { o.v = -1; ++*live; }
    constexpr Counted& operator=(const Counted& o) { v = o.v; return *this; }
    constexpr Counted& operator=(Counted&& o) noexcept { v = o.v; o.v = -1; return *this; }
    constexpr ~Counted() { --*live; }
};

struct Ctx { int live = 0; };

template <class T>
constexpr T make(Ctx& c, int i) {
    if constexpr (std::is_same_v<T, Counted>) return Counted(&c.live, i);
    else if constexpr (std::is_same_v<T, Str>) return Str(40, char('a' + i));
    else return i;
}

// The int an element stands for; -1 for a moved-from Counted or string.
template <class T>
constexpr int val(const T& x) {
    if constexpr (std::is_same_v<T, Counted>) return x.v;
    else if constexpr (std::is_same_v<T, Str>) return x.size() == 40 ? x[0] - 'a' : -1;
    else return x;
}

template <class T>
constexpr bool same(const mystd::vector<T>& v, std::initializer_list<int> il) {
    if (v.size() != il.size()) return false;
    std::size_t i = 0;
    for (int x : il)
        if (val(v[i++]) != x) return false;
    return true;
}

template <class T>
constexpr mystd::vector<T> make_n(Ctx& c, int n) {   // 0, 1, ..., n-1
    mystd::vector<T> v;
    for (int i = 0; i < n; ++i) v.push_back(make<T>(c, i));
    return v;
}

// Single-pass, like OneShot, but for any T and usable at compile time.
template <class T>
struct Input {
    using iterator_concept = std::input_iterator_tag;
    using difference_type  = std::ptrdiff_t;
    using value_type       = T;
    const T* p;
    constexpr const T& operator*() const { return *p; }
    constexpr Input& operator++() { ++p; return *this; }
    constexpr void operator++(int) { ++p; }
    constexpr bool operator==(const Input&) const = default;
};
static_assert(std::input_iterator<Input<Str>>);
static_assert(!std::forward_iterator<Input<Str>>);

#define CT_CHECK(...) do { if (!(__VA_ARGS__)) return __LINE__; } while (0)   // ... : commas in template args
// Live objects. Exact for Counted; int and string have no counter.
#define CT_LIVE(n) CT_CHECK(!std::is_same_v<T, Counted> || c.live == int(n))

template <class T>
constexpr int push_pop() {
    Ctx c;
    {
        mystd::vector<T> v;
        CT_CHECK(v.empty() && v.capacity() == 0 && v.data() == nullptr);
        std::size_t caps[5]{};
        for (int i = 0; i < 5; ++i) {
            v.push_back(make<T>(c, i));                 // rvalue
            caps[i] = v.capacity();
        }
        CT_CHECK(caps[0] == 1 && caps[1] == 2 && caps[2] == 4 && caps[3] == 4 && caps[4] == 8);
        T x = make<T>(c, 5);
        v.push_back(x);                                 // lvalue: copied, x untouched
        v.emplace_back(make<T>(c, 6));
        CT_CHECK(same(v, {0, 1, 2, 3, 4, 5, 6}) && val(x) == 5);
        CT_LIVE(8);                                     // 7 in v, plus x
        v.pop_back();
        CT_LIVE(7);
        CT_CHECK(val(v.front()) == 0 && val(v.back()) == 5 && val(v.at(3)) == 3 && val(*v.data()) == 0);
        const mystd::vector<T>& cv = v;
        CT_CHECK(val(cv[1]) == 1 && val(cv.at(2)) == 2 && val(cv.front()) == 0 && val(cv.back()) == 5);
        v.shrink_to_fit();                              // 6 of 6: the next push reallocates
        v.push_back(v[0]);                              // ...and its argument is in the old buffer
        CT_CHECK(same(v, {0, 1, 2, 3, 4, 5, 0}) && v.capacity() == 12);
        v.clear();
        CT_CHECK(v.empty() && v.capacity() == 12);
        CT_LIVE(1);                                     // x
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int reserve_shrink() {
    Ctx c;
    {
        mystd::vector<T> v;
        v.reserve(0);
        CT_CHECK(v.capacity() == 0 && v.data() == nullptr);
        v.reserve(10);                                  // relocates nothing from a null buffer
        CT_CHECK(v.capacity() == 10 && v.empty());
        for (int i = 0; i < 3; ++i) v.push_back(make<T>(c, i));
        v.reserve(5);                                   // never shrinks
        CT_CHECK(v.capacity() == 10);
        v.shrink_to_fit();
        CT_CHECK(v.capacity() == 3 && same(v, {0, 1, 2}));
        v.shrink_to_fit();                              // already tight
        CT_CHECK(v.capacity() == 3);
        v.clear();
        CT_LIVE(0);
        v.shrink_to_fit();                              // empty WITH capacity: must free
        CT_CHECK(v.capacity() == 0 && v.data() == nullptr);
        v.push_back(make<T>(c, 7));                     // still usable
        CT_CHECK(same(v, {7}));
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int resize() {
    Ctx c;
    {
        // resize(n) needs a default constructor; Counted has none.
        if constexpr (std::is_default_constructible_v<T>) {
            const int d = std::is_same_v<T, Str> ? -1 : 0;   // value-initialised: "" or 0
            mystd::vector<T> v;
            v.resize(3);                                // realloc path
            CT_CHECK(same(v, {d, d, d}) && v.capacity() == 3);
            v.resize(1);
            v.resize(3);                                // room path
            CT_CHECK(same(v, {d, d, d}) && v.capacity() == 3);
        }
        mystd::vector<T> w;
        w.resize(2, make<T>(c, 4));
        CT_CHECK(same(w, {4, 4}) && w.capacity() == 2);
        w.resize(5, make<T>(c, 6));                     // max(5, 2 * 2)
        CT_CHECK(same(w, {4, 4, 6, 6, 6}) && w.capacity() == 5);
        w.resize(1, make<T>(c, 9));                     // shrinking ignores the value
        CT_CHECK(same(w, {4}) && w.capacity() == 5);
        CT_LIVE(1);
        w.resize(3, w[0]);                              // value is an element; room path
        CT_CHECK(same(w, {4, 4, 4}));
        w.resize(7, w[0]);                              // ...realloc path: built before the old buffer goes
        CT_CHECK(same(w, {4, 4, 4, 4, 4, 4, 4}) && w.capacity() == 10);   // max(7, 2 * 5); std gives 3 + max(3, 4)
        CT_LIVE(7);
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int erase() {
    Ctx c;
    {
        auto v = make_n<T>(c, 6);
        auto it = v.erase(v.begin() + 2);
        CT_CHECK(same(v, {0, 1, 3, 4, 5}) && val(*it) == 3);
        it = v.erase(v.begin());
        CT_CHECK(same(v, {1, 3, 4, 5}) && it == v.begin());
        it = v.erase(v.end() - 1);
        CT_CHECK(same(v, {1, 3, 4}) && it == v.end());
        CT_LIVE(3);

        auto w = make_n<T>(c, 6);
        it = w.erase(w.begin() + 1, w.begin() + 3);
        CT_CHECK(same(w, {0, 3, 4, 5}) && val(*it) == 3);
        it = w.erase(w.begin() + 2, w.begin() + 2);     // empty range
        CT_CHECK(same(w, {0, 3, 4, 5}) && val(*it) == 4);
        it = w.erase(w.begin() + 1, w.end());
        CT_CHECK(same(w, {0}) && it == w.end());
        w.erase(w.begin(), w.end());
        CT_CHECK(w.empty() && w.capacity() == 8);
        CT_LIVE(3);
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int insert_one() {
    Ctx c;
    {
        auto v = make_n<T>(c, 3);                       // 0 1 2, cap 4
        auto it = v.emplace(v.begin() + 1, make<T>(c, 7));          // room
        CT_CHECK(same(v, {0, 7, 1, 2}) && val(*it) == 7 && v.capacity() == 4);
        it = v.insert(v.begin(), make<T>(c, 8));                    // full: realloc
        CT_CHECK(same(v, {8, 0, 7, 1, 2}) && it == v.begin() && v.capacity() == 8);
        T x = make<T>(c, 9);
        it = v.insert(v.end(), x);                                  // lvalue, at the end
        CT_CHECK(same(v, {8, 0, 7, 1, 2, 9}) && val(*it) == 9 && val(x) == 9);
        v.insert(v.begin() + 1, v[4]);                  // value is an element the shift moves
        CT_CHECK(same(v, {8, 2, 0, 7, 1, 2, 9}));
        v.insert(v.begin(), v.back());                  // room left: 7 of 8
        v.insert(v.begin() + 3, v[0]);                  // full: realloc, value in the old buffer
        CT_CHECK(same(v, {9, 8, 2, 9, 0, 7, 1, 2, 9}) && v.capacity() == 16);
        CT_LIVE(10);                                    // 9 in v, plus x
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int insert_fill() {
    Ctx c;
    {
        auto v = make_n<T>(c, 4);                       // 0 1 2 3
        v.reserve(10);
        v.insert(v.begin() + 3, 2, make<T>(c, 7));      // room, n = 2 > tail = 1
        CT_CHECK(same(v, {0, 1, 2, 7, 7, 3}));
        v.insert(v.begin() + 1, 2, make<T>(c, 8));      // room, n = 2 <= tail = 5
        CT_CHECK(same(v, {0, 8, 8, 1, 2, 7, 7, 3}) && v.capacity() == 10);
        v.insert(v.end(), 0, make<T>(c, 5));            // n = 0: nothing
        v.insert(v.begin() + 2, 3, v[0]);               // realloc 8 + max(8, 3); value in the old buffer
        CT_CHECK(same(v, {0, 8, 0, 0, 0, 8, 1, 2, 7, 7, 3}) && v.capacity() == 16);
        v.insert(v.begin(), 2, v[1]);                   // room, n <= tail, value shifts
        CT_CHECK(same(v, {8, 8, 0, 8, 0, 0, 0, 8, 1, 2, 7, 7, 3}));
        v.insert(v.end() - 1, 3, v.back());             // room, n > tail, value is the tail
        CT_CHECK(same(v, {8, 8, 0, 8, 0, 0, 0, 8, 1, 2, 7, 7, 3, 3, 3, 3}) && v.capacity() == 16);
        CT_LIVE(16);
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int insert_range() {
    Ctx c;
    {
        auto v = make_n<T>(c, 3);                       // 0 1 2
        {
            T src[3] = {make<T>(c, 7), make<T>(c, 8), make<T>(c, 9)};
            v.reserve(8);
            auto it = v.insert(v.begin() + 1, src, src + 2);        // forward, room, n <= tail
            CT_CHECK(same(v, {0, 7, 8, 1, 2}) && val(*it) == 7);
            v.insert(v.begin() + 4, src, src + 3);                  // forward, room, n > tail
            CT_CHECK(same(v, {0, 7, 8, 1, 7, 8, 9, 2}) && v.capacity() == 8);
            v.insert(v.begin(), src + 2, src + 3);                  // forward, realloc
            CT_CHECK(same(v, {9, 0, 7, 8, 1, 7, 8, 9, 2}) && v.capacity() == 16);
            v.insert(v.begin() + 1, Input<T>{src}, Input<T>{src + 2});      // input, middle
            CT_CHECK(same(v, {9, 7, 8, 0, 7, 8, 1, 7, 8, 9, 2}));
            v.insert(v.end(), Input<T>{src + 2}, Input<T>{src + 3});        // input, end
            CT_CHECK(same(v, {9, 7, 8, 0, 7, 8, 1, 7, 8, 9, 2, 9}));
            CT_LIVE(12 + 3);
            v.insert(v.end(), std::make_move_iterator(src), std::make_move_iterator(src + 1));
            CT_CHECK(val(v.back()) == 7 && val(src[0]) == (std::is_same_v<T, int> ? 7 : -1));
        }
        CT_LIVE(13);
        v.insert(v.begin() + 2, {make<T>(c, 5), make<T>(c, 6)});   // initializer_list
        CT_CHECK(same(v, {9, 7, 5, 6, 8, 0, 7, 8, 1, 7, 8, 9, 2, 9, 7}));
        CT_LIVE(15);
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int copy_move() {
    Ctx c;
    {
        auto a = make_n<T>(c, 3);                       // 0 1 2, cap 4
        mystd::vector<T> b(a);                          // copy ctor: exact capacity, own buffer
        CT_CHECK(same(b, {0, 1, 2}) && same(a, {0, 1, 2}) && b.capacity() == 3 && b.data() != a.data());
        const mystd::vector<T> e;
        mystd::vector<T> e2(e);                         // copy of empty: no allocation
        CT_CHECK(e2.empty() && e2.data() == nullptr);

        mystd::vector<T> d;
        d = a;                                          // copy-assign into null: new buffer
        CT_CHECK(same(d, {0, 1, 2}) && d.capacity() == 3);
        auto big = make_n<T>(c, 5);                     // cap 8
        big = a;                                        // reuse the buffer, 5 -> 3
        CT_CHECK(same(big, {0, 1, 2}) && big.capacity() == 8);
        auto small = make_n<T>(c, 1);
        small.reserve(6);
        small = a;                                      // reuse the buffer, 1 -> 3
        CT_CHECK(same(small, {0, 1, 2}) && small.capacity() == 6);
        mystd::vector<T>& dr = d;
        d = dr;                                         // self-assign
        CT_CHECK(same(d, {0, 1, 2}));
        CT_LIVE(15);                                    // a b d big small

        mystd::vector<T> m(std::move(b));               // move ctor: steals the buffer
        CT_CHECK(same(m, {0, 1, 2}) && b.empty() && b.capacity() == 0 && b.data() == nullptr);
        CT_LIVE(15);
        m = std::move(big);                             // into non-empty: m's old elements die
        CT_CHECK(same(m, {0, 1, 2}) && m.capacity() == 8 && big.empty() && big.data() == nullptr);
        CT_LIVE(12);
        mystd::vector<T> z;
        z = std::move(m);                               // into null
        CT_CHECK(same(z, {0, 1, 2}) && m.empty());
        mystd::vector<T>& zr = z;
        z = std::move(zr);                              // self-move: unchanged (mystd promises it;
                                                        // std::vector leaves it empty, measured)
        CT_CHECK(same(z, {0, 1, 2}));
        m.push_back(make<T>(c, 4));                     // moved-from is usable
        z.swap(m);
        CT_CHECK(same(z, {4}) && same(m, {0, 1, 2}));
        CT_LIVE(13);                                    // a d small m z
    }
    CT_LIVE(0);
    return 0;
}

template <class T>
constexpr int iterators() {
    Ctx c;
    {
        auto v = make_n<T>(c, 4);
        int fwd = 0, rev = 0, cfwd = 0, crev = 0;
        for (auto it = v.begin(); it != v.end(); ++it) fwd = fwd * 10 + val(*it);
        for (auto it = v.rbegin(); it != v.rend(); ++it) rev = rev * 10 + val(*it);
        for (auto it = v.cbegin(); it != v.cend(); ++it) cfwd = cfwd * 10 + val(*it);
        for (auto it = v.crbegin(); it != v.crend(); ++it) crev = crev * 10 + val(*it);
        CT_CHECK(fwd == 123 && rev == 3210 && cfwd == 123 && crev == 3210);   // 0123 and 3210
        CT_CHECK(v.end() - v.begin() == 4 && val(v.begin()[2]) == 2 && val(*(v.end() - 1)) == 3);
        std::reverse(v.begin(), v.end());               // a std algorithm on our iterators
        CT_CHECK(same(v, {3, 2, 1, 0}));
        auto hit = std::find_if(v.begin(), v.end(), [](const T& x) { return val(x) == 1; });
        CT_CHECK(hit - v.begin() == 2);
        int sum = 0;
        for (const T& x : v) sum += val(x);             // range-for
        CT_CHECK(sum == 6);
        CT_LIVE(4);
    }
    CT_LIVE(0);
    return 0;
}

#undef CT_LIVE
#undef CT_CHECK

static_assert(push_pop<int>() == 0);
static_assert(push_pop<Counted>() == 0);
static_assert(push_pop<Str>() == 0);
static_assert(reserve_shrink<int>() == 0);
static_assert(reserve_shrink<Counted>() == 0);
static_assert(reserve_shrink<Str>() == 0);
static_assert(resize<int>() == 0);
static_assert(resize<Counted>() == 0);
static_assert(resize<Str>() == 0);
static_assert(erase<int>() == 0);
static_assert(erase<Counted>() == 0);
static_assert(erase<Str>() == 0);
static_assert(insert_one<int>() == 0);
static_assert(insert_one<Counted>() == 0);
static_assert(insert_one<Str>() == 0);
static_assert(insert_fill<int>() == 0);
static_assert(insert_fill<Counted>() == 0);
static_assert(insert_fill<Str>() == 0);
static_assert(insert_range<int>() == 0);
static_assert(insert_range<Counted>() == 0);
static_assert(insert_range<Str>() == 0);
static_assert(copy_move<int>() == 0);
static_assert(copy_move<Counted>() == 0);
static_assert(copy_move<Str>() == 0);
static_assert(iterators<int>() == 0);
static_assert(iterators<Counted>() == 0);
static_assert(iterators<Str>() == 0);

// detail::default_construct_at (a demo; nothing in the vector calls it). At
// compile time it must take the value-init branch -- placement new isn't
// allowed there -- so the int reads as 0. At runtime it would be garbage.
static_assert([] {
    std::allocator<int> a;
    int* p = a.allocate(1);
    mystd::detail::default_construct_at(p);
    int r = *p;
    a.deallocate(p, 1);
    return r;
}() == 0);

}  // namespace ct

// The same scenarios, run by the program under ASan/UBSan.
#define CT_RUN(f)                                                         \
    do {                                                                  \
        if (int line = ct::f) {                                           \
            std::printf("FAIL ct::%s: check at line %d\n", #f, line);     \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static void test_constexpr_scenarios_at_runtime() {
    CT_RUN(push_pop<int>());       CT_RUN(push_pop<ct::Counted>());       CT_RUN(push_pop<ct::Str>());
    CT_RUN(reserve_shrink<int>()); CT_RUN(reserve_shrink<ct::Counted>()); CT_RUN(reserve_shrink<ct::Str>());
    CT_RUN(resize<int>());         CT_RUN(resize<ct::Counted>());         CT_RUN(resize<ct::Str>());
    CT_RUN(erase<int>());          CT_RUN(erase<ct::Counted>());          CT_RUN(erase<ct::Str>());
    CT_RUN(insert_one<int>());     CT_RUN(insert_one<ct::Counted>());     CT_RUN(insert_one<ct::Str>());
    CT_RUN(insert_fill<int>());    CT_RUN(insert_fill<ct::Counted>());    CT_RUN(insert_fill<ct::Str>());
    CT_RUN(insert_range<int>());   CT_RUN(insert_range<ct::Counted>());   CT_RUN(insert_range<ct::Str>());
    CT_RUN(copy_move<int>());      CT_RUN(copy_move<ct::Counted>());      CT_RUN(copy_move<ct::Str>());
    CT_RUN(iterators<int>());      CT_RUN(iterators<ct::Counted>());      CT_RUN(iterators<ct::Str>());
}

int main() {
    test_deref_and_increment();
    test_write_through_iterator();
    test_const_vector();
    test_conversion();
    test_copy();
    test_element_types();
    test_equality();
    test_ordering();
    test_arithmetic();
    test_arrow_and_to_address();
    test_std_algorithms();
    test_cbegin_cend();
    test_reverse();
    test_erase_positions();
    test_erase_element_types();
    test_erase_counts();
    test_erase_throwing_assign();
    test_erase_range_positions();
    test_erase_range_empty();
    test_erase_range_element_types();
    test_erase_range_counts();
    test_erase_range_throwing_assign();
    test_emplace_positions();
    test_emplace_aliasing();
    test_emplace_element_types();
    test_emplace_counts();
    test_emplace_throwing_construct();
    test_emplace_throwing_assign();
    test_insert_value();
    test_insert_fill_room();
    test_insert_fill_realloc();
    test_insert_fill_aliasing();
    test_insert_fill_throwing();
    test_insert_overload_choice();
    test_insert_range_forward();
    test_insert_range_input();
    test_insert_range_move_and_il();
    test_insert_range_throwing();
    test_reverse_erase_insert();
    test_allocation_failure();
    test_constexpr_scenarios_at_runtime();

    if (failures == 0) std::printf("all tests passed\n");
    else               std::printf("%d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
