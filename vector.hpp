#ifndef MYSTD_VECTOR_HPP
#define MYSTD_VECTOR_HPP

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <new>
#include <stdexcept>
#include <utility>

#include "guards.hpp"
#include <type_traits>


namespace mystd {
// vector invariants:
// data_ points to cap_ slots of raw memory (or is null)
// the first size_ slots hold live, constructed T objects
// slots size_ .. cap_ hold raw bytes — no objects
// size_ <= cap_

// exception gaurantees: nothrow 
//                      > Strong (The object is exactly as it was before the call )
//                      > Basic(Invariants hold, value is unspecified) 
//                      > None (Anything else. UB, leaks, broken invariants. Never acceptable)

/*
What happens when an operation has no exception guarantee:

try{
    f();
} catch(...) {
    handle();
}

void f() {
    vector<std::string> vec;
    v.exceptional_func();
    
    // lets say exceptional_func() throws an exception
    // and leaves us at a stage where size=10 but live elements are only there till 5

    // exceptional_func() throws an exception => keeps getting propogated up the stack and terminating stuff till its caught
    // since it's not handled inside f(), all the elements inside the scope are destroyed properly and then the exception is propogated to the caller (where it's handled)

    // Soo, ~vector() is called on vec (since it's local to the scope)
    // Howeverr, ~vector is called on all elements from i to 10.
    // i - 6 to 10 have already destroyed
    // Calling a destructor on an already destroyed object is undefined behaviour in cpp.
    
    // Once you get a UB in cpp, all bets are off
    // Compiler is allowed to behave literally anyway.
    // Compiler can go and fill random values in a different object totally unrelated to the current objectss/function; can delete anyting; and all of that's allowed & valid according to the C++ standard after UB

    // What would realistically happen here:
    The first ~string() frees the heap buffer and, in a release build, does not null the pointer (why bother, the object is dead). 
    The second ~string() reads that same stale pointer and calls free on it again. 
    Double free. Heap corruption — the allocator's freelist now has the same block linked twice, so two later allocations hand out the same memory to different owners. 
    The crash, if any, happens somewhere unrelated, much later.
}

*/

// destructors are noexcept by default, but can throw exceptions / be marked except
// however:
// stl requires element types to satisfy:
// u.~T() — all resources are reclaimed, no exception is propagated.

// Cleanup is now handled by RAII guards from guards.hpp rather than try/catch:
// a guard's destructor runs during unwinding, so cleanup is the DEFAULT and
// release() on the success path is what cancels it. See guards.hpp.

template <class T>
class vector {
    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_ = 0;

   void relocate_and_commit(T* dest, std::size_t new_cap) {
        // strong gaurantee
        // dest[i] = std::move_if_noexcept(data_[i]); // wrong, dest[i] isn't initialised yet
        // even copy throws (eactually more then move, then why move only on move_if_noexcept)
        // becasue even if copy throws, the original is not destroyed, it's recoverable
        //
        // The guard destroys whatever was built into dest if a construction
        // throws. It does NOT free dest: this function doesn't own that block.
        // for ex : incase push_back is calling, push_back still needs to delete
        // the element it already put at the end.
        T* cur = dest;
        detail::construct_guard<T> cg(dest, cur);
        for (std::size_t i = 0; i < size_; ++i, ++cur)
            ::new (cur) T(std::move_if_noexcept(data_[i]));
        cg.release();

        // don't destroy in the same loop where you copy/move
        // why ??:
        // let's say you are copying, in midst, it throws a exception, reserve terminates, calling function handles it.
        // noww, you have half the elements of the current data_ destroyed
        //
        // Everything below this point is noexcept -- this is the commit.
        detail::destroy_range(data_, data_ + size_);

        ::operator delete(data_);
        data_ = dest;
        cap_ = new_cap; // must be done only after new_data is safely created/assigned, and data_ is safely updated;
        // else, if the program throws while doing the above, and caller catches it, you're in an broken invariant state
        // issue on further push_back() / destruction / anything
    }

    template <class... Args>
    void resize_impl(std::size_t n, const Args&... args) {
        // strong gaurantee
        //
        // args is taken by const& (not forwarded): it is used once per new
        // element, so it must not be moved from.
        if(size_ == n)
            return;

        if(size_ >= n) {
            detail::destroy_range(data_ + n, data_ + size_);
            size_ = n;
            return;
        }

        if (cap_ < n) {
            // Geometric, matching emplace_back. Exact allocation would make
            // repeated resize(size()+1) quadratic.
            const std::size_t new_cap = std::max(n, cap_ * 2);
            detail::alloc_guard<T> ag(detail::allocate_raw<T>(new_cap));
            T* nb = ag.get();

            // 1. new elements FIRST -- args... may alias data_, still alive
            T* k = nb + size_;
            detail::construct_guard<T> kg(nb + size_, k);
            for (; k != nb + n; ++k)
                ::new (k) T(args...);

            // 2. only now may the old buffer be touched
            relocate_and_commit(nb, new_cap);

            kg.release();
            ag.release();
            size_ = n;
            return;
        }

        // Enough capacity: construct in place. args cannot dangle here,
        // because nothing frees data_.
        T* i = data_ + size_;
        detail::construct_guard<T> g(data_ + size_, i);
        for( ; i != data_ + n; ++i)
            ::new (i) T(args...);
        g.release();

        size_ = n;
    }

public:

    template<bool isConst>
    class basic_iterator {
        using Elem = std::conditional_t<isConst, const T, T>;
        Elem *p_ = nullptr;
        explicit basic_iterator(Elem *p) : p_(p) {}
    public:
        // Signed and pointer-wide, i.e. what raw pointer subtraction gives.
        //   - not size_t: begin() - end() is negative, unsigned would wrap
        //   - not int: 32 bits, overflows past 2^31 elements
        // Also the type of n in it + n, it += n, it[n], so it + (-1) works.
        using difference_type = std::ptrdiff_t;

        // The type of a COPY of an element, so always T, never const T --
        // a copy of a const element is yours to modify. Same as raw pointers:
        // iter_value_t<const int*> is int. The library would NOT catch const T
        // here: iter_value_t and iterator_traits strip cv from the member, so
        // the concepts still pass (measured). Only code reading
        // It::value_type directly would get a const copy it can't assign to.
        // Without it the library can't name "a copy of an element" (std::sort
        // needs one for its pivot), so iterator_traits<It> is EMPTY and even
        // std::find fails to compile.
        using value_type = T;

        // Claims the top tier. Operators can prove random access, but not
        // that elements sit next to each other in memory -- that has to be
        // declared. Without it ITER_CONCEPT falls back to random_access.
        // Only read by C++20 concepts; classic algorithms read
        // iterator_traits<It>::iterator_category, which C++20 derives as
        // random_access_iterator_tag on its own (C++98 had no contiguous tag).
        using iterator_concept = std::contiguous_iterator_tag;

        // ex:
        // template <class It>
        // typename std::iterator_traits<It>::difference_type
        // distance(It first, It last) {
        //     using cat = typename std::iterator_traits<It>::iterator_category;
        //     return distance_impl(first, last, cat{});     // pick an implementation by TYPE
        // }

        // // two implementations; overload resolution chooses one at compile time
        // template <class It>
        // auto distance_impl(It first, It last, std::input_iterator_tag) {
        //     typename std::iterator_traits<It>::difference_type n = 0;
        //     while (first != last) { ++first; ++n; }       // O(n): walk
        //     return n;
        // }
        // template <class It>
        // auto distance_impl(It first, It last, std::random_access_iterator_tag) {
        //     return last - first;                          // O(1): subtract
        // }

        // Not declared:
        //   - iterator_category, reference, pointer: C++20 iterator_traits
        //     derives them from the operators (random_access tag, Elem&,
        //     decltype(operator->())), so writing them would only restate
        //     them, with a chance to disagree
        //   - element_type: read by pointer_traits, not needed by
        //     std::to_address on GCC 13 -- operator-> is what it uses

        basic_iterator(const basic_iterator<false>& it) requires isConst : p_(it.p_) {}
        // if we just left it at the above:
        //                         default ctor     copy ctor       move ctor
        // basic_iterator<false>    suppressed      suppressed      suppressed
        // basic_iterator<true>	    suppressed      generated       generated

        basic_iterator() = default;
        basic_iterator(const basic_iterator&) = default;
        // why is this required ?
        // bcz of how requires works
        // requires is unlike if constexpr
        //   - requires does NOT remove the declaration; it only makes it
        //     non-viable in overload resolution when the constraint is false
        //   - so in basic_iterator<false>, the ctor above is still DECLARED, and
        //     its signature (const basic_iterator<false>&) makes it a copy ctor
        //   - a declared copy ctor (even an unusable one) stops the compiler
        //     from generating the implicit copy/move ctors -> <false> uncopyable
        //   - so we declare the real copy ctor ourselves; the constrained one
        //     drops out, this one is used, and it stays trivial
        //   (if constexpr only discards statements inside function bodies,
        //    so it can't control whether a member exists at all)

        // declaring the copy ctor leaves copy assignment as deprecated:
        basic_iterator& operator=(const basic_iterator&) = default;
        // declaring the copy ctor stops the implicit move ops,
        // but doesn't matter here, since copy costs same as move here
        
        // basic_iterator(basic_iterator&&) = default;
        // basic_iterator& operator=(basic_iterator&&) = default;
        
        // Not declared (case here): moves quietly become copies. is_move_constructible_v<It> and is_move_assignable_v<It> are still true, because they only ask "can I construct or assign from an rvalue?" and the copy operations answer yes. 
        // basic_iterator(basic_iterator&&): the ctor is deleted, so std::move(a) would fail to compile. 


        // Rule of five: if you write any of the destructor, copy operations, or move operations because you manage a resource, write or = default all five.

        // Default constructor is generated only if you declare no constructors at all, of any kind: copy, move, converting, templated, constrained, anything.

        // Copy constructor is generated unless you declare one yourself. 
        // If you declare a move constructor or move assignment, the generated copy constructor is deleted. 
        // If you declare a copy assignment or destructor, it’s still generated, but that’s deprecated (GCC’s -Wdeprecated-copy).

        // Copy assignment follows the same pattern as the copy constructor. 
        // Declaring a move constructor or move assignment makes it deleted. 
        // Declaring a copy constructor or destructor leaves it generated but deprecated.

        // Move constructor is generated only if you declare none of: copy constructor, copy assignment, move assignment, destructor.

        // Move assignment is generated only if you declare none of: copy constructor, copy assignment, move constructor, destructor.

        // Destructor is always generated unless you declare one.

        Elem& operator*() const {
            return *p_;
        }

        // it->m for class elements, e.g. strings.begin()->size().
        // Returns Elem*, so through a const_iterator you get const T* and
        // can only call const members.
        // Also how std::to_address(it) turns an iterator into a raw pointer,
        // which contiguous_iterator requires. Unlike &*it it never
        // dereferences, so it is valid on end() too.
        Elem* operator->() const noexcept {
            return p_;
        }

        // Rejected:
        // 1.  bool operator==(const basic_iterator& o) const;
        //     bool operator==(const basic_iterator<!isConst>& o) const { return p_ == o.p_; }
        //     cross-type overload (It vs CIt): restates the It -> CIt conversion
        //     we already have, and has to be repeated for <=> and it - cit

        // 2.  bool operator==(const basic_iterator& o) const;
        //     member operator==: `this` is never converted, so it == cit only
        //     compiles thanks to C++20's reversed candidates (fails in C++17)

        // Equality: hidden friend, same-type only.
        //   - it == cit works by converting it -> const_iterator through the
        //     converting ctor above; a free function may convert EITHER operand
        //   - hidden: found only by ADL, when an argument is a basic_iterator
        //   - != is free in C++20: a != b is rewritten as !(a == b)
        friend bool operator==(const basic_iterator& a, const basic_iterator& b) noexcept { // = default
            return a.p_ == b.p_;
        }
        // A function defined inside a class with friend is special: ordinary lookup can never see it, not even from inside the same namespace. Only ADL finds it, and only when one of the arguments has that class type (or the class is associated with an argument's type).
        // No accidental matches. If some unrelated type could convert to basic_iterator, comparing two of those wouldn't pick your ==, because neither argument's type leads ADL to your class.
        // Smaller overload sets. Every other == in your program doesn't have to consider and reject it, which means faster compiles and shorter error messages.

        // Ordering: hidden friend, same-type only -- same reasoning as ==.
        //   - < <= > >= are rewritten as (a <=> b) < 0 etc.; == is NOT derived
        //     from <=>, so the == above stays
        //   - it < cit converts it -> const_iterator, exactly like ==
        //   - strong_ordering: pointer <=> already yields it; spelled out
        //     rather than auto so the category is part of the interface
        //   - only meaningful for iterators into the same vector, same as
        //     comparing raw pointers into different arrays
        // Not done:
        //   - = default: also correct today (p_ is the only member), but kept
        //     by hand to match ==, so both state exactly what is compared
        //   - weak/partial_ordering: two iterators to the same slot are
        //     interchangeable, and every pair in one vector is comparable
        friend std::strong_ordering operator<=>(const basic_iterator& a, const basic_iterator& b) noexcept { // = default;
            return a.p_ <=> b.p_;
        }
        
        basic_iterator& operator++() {
            // if(p == data+size_)
            //     throw exception;
            ++p_;
            return *this;
        }

        basic_iterator operator++(int) noexcept {
            basic_iterator old = *this;
            ++*this;
            return old;
        }

        basic_iterator& operator--() noexcept {
            --p_;
            return *this;
        }

        basic_iterator operator--(int) noexcept {
            basic_iterator old = *this;
            --*this;
            return old;
        }

        basic_iterator& operator+=(difference_type n) noexcept {
            p_ += n;
            return *this;
        }

        // Not `return *this += -n;`: -n overflows (UB) when n is PTRDIFF_MIN.
        basic_iterator& operator-=(difference_type n) noexcept {
            p_ -= n;
            return *this;
        }

        // const member returning Elem&, same as operator*: constness of the
        // ITERATOR (can't move it) is not constness of the ELEMENT -- like
        // T* const vs const T*. Only Elem decides whether you can write.
        // No bounds check, same as operator* and raw pointers.
        Elem& operator[](difference_type n) const noexcept {
            return p_[n];
        }

        // Binary + and -: hidden friends, built on += / -=.
        //   - n + it HAS to be a non-member: the left operand is an integer,
        //     and you can't add members to ptrdiff_t
        //   - it + n, it - n could be members; friends to keep the family
        //     uniform (one place, one lookup rule)
        //   - `it` taken BY VALUE: the parameter is already the copy we
        //     return, so no separate local
        //   - there is no n - it: "5 minus an iterator" means nothing
        friend basic_iterator operator+(basic_iterator it, difference_type n) noexcept {
            it += n;
            return it;
        }

        friend basic_iterator operator+(difference_type n, basic_iterator it) noexcept {
            it += n;
            return it;
        }

        friend basic_iterator operator-(basic_iterator it, difference_type n) noexcept {
            it -= n;
            return it;
        }

        // Distance: hidden friend, same-type only, and here that is REQUIRED,
        // not style. As a member, it - cit fails (`this` is never converted),
        // and unlike == / <=> C++20 has no reversed candidates for -.
        // As a friend, it - cit converts it -> const_iterator like ==.
        friend difference_type operator-(const basic_iterator& a, const basic_iterator& b) noexcept {
            return a.p_ - b.p_;
        }

        friend class vector;
        friend class basic_iterator<!isConst>;   // the const twin reads p_ for the conversion
        // works even without this since friendship is achieved anyways through vector's frienship, but better
    };

    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    // Reverse iteration is an ADAPTER over the forward iterators, not a
    // third iterator class: std::reverse_iterator flips ++/--, +/-, < / >,
    // and gets everything else (traits, It -> CIt conversion, mixed ==)
    // from what basic_iterator already does.
    // The std:: qualifier matters: inside this class (and in every member
    // function body, even ones above this line) plain `reverse_iterator`
    // names the member alias, not the std template.
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;


    vector() noexcept = default;

    void swap(vector& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(cap_,  other.cap_);
    }

    // Inside the class template, vector<T> and vector mean the same thing (injected-class-name)
    vector(const vector& other) : cap_(other.size_) {
        if(!other.size_)
            return;

        // No need to publish each element via ++size_ any more: the guards own
        // the partial work until the commit. (The destructor still can't help
        // here -- a dtor isn't called if the ctor throws.)
        detail::alloc_guard<T> ag(detail::allocate_raw<T>(other.size_));
        T* cur = ag.get();
        detail::construct_guard<T> cg(ag.get(), cur);
        for(std::size_t i = 0; i < other.size_; ++i, ++cur)
            ::new (cur) T(other.data_[i]);
        cg.release();

        data_ = ag.get();
        ag.release();
        size_ = other.size_;
    }

    vector& operator=(const vector& other) {
        // nahh, always allocates:
        // vector tmp(other);
        // swap(tmp);
        // return *this;
        if (this == &other) return *this;

        if(cap_ >= other.size_) {
            // nice optmization, but reduces exception guarantee from Strong to Basic
            // Callers needing the strong guarantee: vector tmp(other); v.swap(tmp);
            std::size_t i = 0;
            std::size_t common = std::min(other.size_, size_);
            for( ; i < common; ++i)
                data_[i] = other[i]; // destructor handles if it throws since size_ / cap_ are correct

            // we only enter this for loop if other.size_ > size_
            for( ; i < other.size_; ++i) {  
                ::new (data_ + i) T(other[i]);
                ++size_; // why not just size_ = other.size_; at the end ? to keep it in a valid state
            }

            // we only enter this for loop if other.size_ < size_
            for( ; i < size_; ++i) 
                data_[i].~T();

            size_ = other.size_; // no-op when growing (size_ was tracked above);
                                 // required when shrinking.
            return *this;
        }

        detail::alloc_guard<T> ag(detail::allocate_raw<T>(other.size_));
        T* cur = ag.get();
        detail::construct_guard<T> cg(ag.get(), cur);
        for (std::size_t i = 0; i < other.size_; ++i, ++cur)
            ::new (cur) T(other.data_[i]);
        cg.release();
        // size_, cap_, data_ unchanged if anything above threw -> strong

        // Commit. Nothing below can throw.
        detail::destroy_range(data_, data_ + size_);
        ::operator delete(data_);
        // can't we just do ::delete data_ instead of above 2 lines ?
        // No: delete runs ~T() on ONE object then frees, and pairs with new,
        // not with ::operator new. We need destroy and free kept separate.

        data_ = ag.get();
        ag.release();
        size_ = other.size_;
        this->cap_ = other.size_;

        return *this;

        // this->data_ = static_cast<T*>(::operator new(sizeof(T) * other.size_));        // if if throws here, no issues, nothing to handle/destroy
        // for(std::size_t i = 0; i < other.size_; ++i) {
        //     ::new (data_ + i) T(other.data_[i]); // destructor handles if it throws since size_ / cap_ are correct
        // }
        //     ++size_; // update size_ at every step so that in case of exception -> dtor, dtor cleans up properly
        // noo, it breaks invariant if it throws at  this->data_ = static_cast....
    }

    vector(vector && other) noexcept {
        swap(other);
    }
    
    vector& operator=(vector&& other) noexcept {
        // swap(other); // noo; whyy not ?
        // if u do a = move(b); , the original contents of a would get destroyed only when b goes outta scope;
        // this->~vector(); swap(other);
        // NOO
        // Calling a destructor on an object that isn't dying means the object's lifetime has ended — every subsequent use, 
        // including the swap on the next line and everything the caller does afterward, is UB. 
        // Explicit destructor calls are for objects you constructed with placement new, which you own the lifetime of. 
        // That's why they appear in your push_back and nowhere else.

        if (this == &other) return *this;        // self-move

        detail::destroy_range(data_, data_ + size_);
        ::operator delete(data_);

        data_ = other.data_;
        size_ = other.size_;
        cap_  = other.cap_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.cap_  = 0;

        return *this;
    }

    ~vector() {
        detail::destroy_range(data_, data_ + size_);
        ::operator delete(data_);
    }

    // std::Increase the capacity of the vector (the total number of elements that the vector can hold without requiring reallocation) to a value that's greater or equal to new_cap
    // mine: to exactly provided, unless smaller then current
    void reserve(std::size_t new_cap) {
        if(new_cap <= cap_)
            return;

        detail::alloc_guard<T> ag(detail::allocate_raw<T>(new_cap));
        relocate_and_commit(ag.get(), new_cap);
        ag.release();   // relocate_and_commit succeeded: the block is data_ now
    }

    void resize(std::size_t n) {
        resize_impl(n);
    }

    void resize(std::size_t n, const T& value) {
        resize_impl(n, value);
    }

    void shrink_to_fit() {
        if(cap_ == size_)
            return;

        if (size_ == 0) {
            ::operator delete(data_);
            data_ = nullptr;
            cap_ = 0;
            return;
        }

        detail::alloc_guard<T> ag(detail::allocate_raw<T>(size_));
        relocate_and_commit(ag.get(), size_);
        ag.release();
    }

    // class... Args — a template parameter pack: zero or more types, deduced per call.
    // Args&& — a forwarding reference (aka "universal reference"), not an rvalue reference.
    // args... — pack expansion at the call site.
    template <class... Args>
    T& emplace_back(Args&&... args) {
        if(size_ != cap_) {
            ::new (data_ + size_) T(std::forward<Args>(args)...); // std::forward is basically juist conditional move
            // std::forward(args...)                 // ONE call, all args passed to it
            // std::forward<Args>(args)...           // N calls, one per argument   ← correct
            // try (args) : expression contains unexpanded parameter pack args
            ++size_;
            return data_[size_ - 1];
        }
        
        const std::size_t new_cap = cap_ ? cap_ * 2 : 1;
        detail::alloc_guard<T> ag(detail::allocate_raw<T>(new_cap));
        T* nb = ag.get();

        // why must this always be done before the moving/copying to new buffer ?
        // to handle the case where we are moving, and value is one of the old
        // elements of the vector. The old buffer is still intact here.
        T* hole = nb + size_;
        detail::construct_guard<T> hg(nb + size_, hole);
        ::new (hole) T(std::forward<Args>(args)...);
        ++hole;

        // Two disjoint live ranges now exist in nb: the prefix relocate_and_commit
        // is about to build at [0, size_), and the new element at [size_, size_+1).
        // Each has its own guard; neither has to know about the other.
        relocate_and_commit(nb, new_cap);

        hg.release();
        ag.release();

        ++size_;
        return data_[size_ - 1];
    }

    void push_back(const T& value) {
        // does it add extra cost of an extra function call
        // Zero cost from -O1 up: GCC inlines push_back into its caller and
        // emplace_back with it. Exception: copy-constructing an allocating type
        // at -O1 (push_back(const std::string&)) keeps one call, because the
        // copy ctor's EH machinery exceeds the inliner's budget. -O2 handles it.
        // Measured with objdump -dr, not assumed.
        emplace_back(value);
    }

    void push_back(T&& value) {
        emplace_back(std::move(value));
    }

    /*
    Rn, I did 2 different overloads of push_back: const T&, T&&:
    cost of push_back(lvalue): 1 copy + others
    cost of push_back(rvalue): 1 move + others

    If I didn't define the const T& overload: wouldn't work for lvalues at all

    If I didn't define the T&&:
    everything still works, but the cost of push_back(rvalue) = 1 copy (since const T& binds to the temporary r value, and its a lvalue ref inside the function, evoking copy cost)

    If you wanna do it in 1 function (good enough for 99% of usecases):
    doo:
    void push_back(T x) {
        ::new (new_data + size_) T(std::move(value));
    }
    in case of rvalue: 1 move ctor (when T x is constructed outta the rvalue arg) / or 0 due to copy elision + 1 move ctor (when the new data_ element is contructed outta x)
    in case of lvalue: 1 copy ctor (when T x is copy constructed outta the lvalue arg) + 1 move ctor (when the new data_ element is contructed outta x)

    The real argument against by-value isn't the extra move: x is a full
    materialised T on the stack, so for vector<array<char,4096>> you memcpy 4 KB
    through a parameter on every push.

    */

    std::size_t size() const noexcept {
        return size_;
    }

    std::size_t capacity() const noexcept {
        return cap_;
    }

    bool empty() const noexcept {
        return !size_;
    }

    T& operator[](std::size_t i) {
        return data_[i];
    }

    T* data() noexcept {
        return data_;
    }

    T& at(std::size_t i) {
        if(i >= size_)
            throw std::out_of_range("vector::at: index out of range");
        return data_[i];
    }

    T& front() {
        // if(!size_)
        //     throw custom_exception;
        // calling front / back() on empty vector is undefined behaviour
        return data_[0];
    }

    T& back() {
        // if(!size_)
        //     throw custom_exception;
        return data_[size_ - 1];
    }

    const T& operator[](std::size_t i) const {
        return data_[i];
    }

    const T* data() const noexcept {
        return data_;
    }

    const T& at(std::size_t i) const {
        if(i >= size_)
            throw std::out_of_range("vector::at: index out of range");
        return data_[i];
    }

    const T& front() const {
        return data_[0];
    }

    const T& back() const {
        return data_[size_ - 1];
    }

    void clear() noexcept {
        detail::destroy_range(data_, data_ + size_);
        size_ = 0;
    }

    void pop_back() {
        --size_; // better, prevents dbl destructor call on dtor throw
        data_[size_].~T();
        // --size_; // still valid, since dtors aren't supposed to throw
    }

    /*
    Why you need T& operator[](std::size_t i) and const T& operator[](std::size_t i) const:

    if u only have T& operator[](std::size_t i):
    Ull not be allowed to do:

    const vector v(params);
    int x = v[6]; // not allowed, non-const method call on const variable

    if u only have const T& operator[](std::size_t i) const:
    Ull not be allowed to do:

    vector v2(params);
    v[6] = 24; // not allowed, trying to assign to a const reference


    Why can't I just have 1 method like this instead of the above 2:
    T& operator[](std::size_t i) const;     // callable on const variables / inside of const function, also allows assigning / modifying elements
    
    Bcz that would allow:
    const vector v3(params);
    v[6] = 29;

    */

    iterator begin() {
        return iterator(data_);
    }

    iterator end() {
        return iterator(data_+size_);
    }

    const_iterator begin() const {
        return const_iterator(data_);
    }

    const_iterator end() const {
        return const_iterator(data_+size_);
    }

    // cbegin/cend: a const_iterator from a NON-const vector, without a cast.
    //   - `auto it = v.cbegin();` says "read-only" at the call site
    //   - insert/erase (step 6) take const_iterator positions
    // Delegating: inside a const member *this is const, so begin() already
    // picks the const overload. One construction site, nothing to drift.
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend()   const noexcept { return end(); }

    // Reverse: rbegin wraps end(), rend wraps begin() -- NOT end()-1 and
    // begin()-1. reverse_iterator stores a base and dereferences the slot
    // BEFORE it (*r == *std::prev(r.base())), so:
    //   - begin()-1 is never formed: a pointer before the array is UB even
    //     if never dereferenced, and on an empty vector data_ is null
    //   - empty vector: rbegin() == rend() because end() == begin()
    //   - r.base() converts back to a forward iterator one past the element,
    //     which is exactly the position erase/insert want (step 6)
    reverse_iterator       rbegin()        noexcept { return reverse_iterator(end()); }
    reverse_iterator       rend()          noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin()  const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend()    const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    const_reverse_iterator crend()   const noexcept { return rend(); }
};

}

#endif  // MYSTD_VECTOR_HPP