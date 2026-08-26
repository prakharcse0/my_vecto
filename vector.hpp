#ifndef MYSTD_VECTOR_HPP
#define MYSTD_VECTOR_HPP

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>

#include "guards.hpp"

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
};

}

#endif  // MYSTD_VECTOR_HPP