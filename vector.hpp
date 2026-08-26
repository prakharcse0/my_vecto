#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>


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

template <class T>
class vector {
    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_ = 0;

   void relocate_and_commit(T* dest, std::size_t new_cap) {
        // strong gaurantee
        for(std::size_t i = 0; i < size_; ++i) {
            // dest[i] = std::move_if_noexcept(data_[i]); // wrong, dest[i] isn't initialised yet
            // even copy throws (eactually more then move, then why move only on move_if_noexcept)
            // becasue even if copy throws, the original is not destroyed, it's recoverable
            try {
                ::new (dest + i) T(std::move_if_noexcept(data_[i]));
            } catch (...) {
                // we don't need to take care of anything that's handled by the destructor.
                for (std::size_t j = 0; j < i; ++j)
                    dest[j].~T();
                // ::operator delete(dest); // why not do this here ?
                // bca this function doesn't own dest
                // for ex : incase push_back is calling, puch_back still needs to delete the element at the end
                throw;
            }
        }

        // don't destroy in the same loop where you copy/move
        // why ??:
        // let's say you are copying, in midst, it throws a exception, reserve terminates, calling function handles it.
        // noww, you have half the elements of the current data_ destroyed
        for(std::size_t i = 0; i < size_; ++i)
            data_[i].~T();

        ::operator delete(data_);
        data_ = dest;
        cap_ = new_cap; // must be done only after new_data is safely created/assigned, and data_ is safely updated;
        // else, if the program throws while doing the above, and caller catches it, you're in an broken invariant state
        // issue on further push_back() / destruction / anything
    }

    template <class... Args>
    void resize_impl(std::size_t n, const Args&... args) {
        // strong gaurantee
        if(size_ == n)
            return;

        if(size_ >= n) {
            for(std::size_t i = n; i < size_; ++i)
                data_[i].~T();
            size_ = n;
            return;
        }

        if (cap_ < n) {
            const std::size_t new_cap = std::max(n, cap_ * 2);
            T* new_data = static_cast<T*>(::operator new(sizeof(T) * new_cap));

            std::size_t k = size_;
            try {
                for (; k < n; ++k)
                    ::new (new_data + k) T(args...);
            } catch (...) {
                while (k > size_) new_data[--k].~T();
                ::operator delete(new_data);
                throw;
            }

            try {
                relocate_and_commit(new_data, new_cap);
            } catch (...) {
                for (std::size_t j = size_; j < n; ++j) new_data[j].~T();
                ::operator delete(new_data);
                throw;
            }

            size_ = n;
            return;
        }

        std::size_t i = size_;
        try {
            for( ; i < n; ++i)
                ::new (data_ + i) T(args...);
        } catch(...) {
            // while(--i >= size_) // don't, segfault if size_ equals 0 // slways be careful when doing >= size_
            //     data_[i].~T();
            while (i > size_) 
                data_[--i].~T();
            throw;
        }

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

        data_ = static_cast<T*>(::operator new(sizeof(T) * other.size_));
        try {
            for(std::size_t i = 0; i < other.size_; ++i) {
                ::new (data_ + i) T(other.data_[i]); // destructor can't handle this - since dtor isn't called if ctor throws. isn't completed
                ++size_;
            }
        } catch(...) {
            for(std::size_t i = 0; i < size_; ++i)
                data_[i].~T();
            ::operator delete(data_);
            throw;
        }
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

            size_ = other.size_; // noo // why ? // above
            return *this;
        }

        T* new_data = static_cast<T*>(::operator new(sizeof(T) * other.size_));
        std::size_t i = 0;
        try {
            for (; i < other.size_; ++i)
                ::new (new_data + i) T(other.data_[i]);
        } catch (...) {
            while (i > 0) new_data[--i].~T();
            ::operator delete(new_data);
            throw;
            // size_, cap_, data same as when called
        }


        for(std::size_t i = 0; i < size_; ++i)
            data_[i].~T();
        ::operator delete(data_);
        // can't we just do ::delete data_ instead of above 4 lines ?
        
        data_ = new_data;
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

        for (std::size_t i = 0; i < size_; ++i) 
            data_[i].~T();
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
        for(std::size_t i = 0; i < size_; ++i)
            data_[i].~T();
        ::operator delete(data_);
    }

    // std::Increase the capacity of the vector (the total number of elements that the vector can hold without requiring reallocation) to a value that's greater or equal to new_cap
    // mine: to exactly provided, unless smaller then current
    void reserve(std::size_t new_cap) {
        if(new_cap <= cap_)
            return;

        T *new_data = static_cast<T*>(::operator new(sizeof(T)*new_cap)); // ::operator new returns void*, always cast

        try {
            relocate_and_commit(new_data, new_cap);
        } catch(...) {
            ::operator delete(new_data);
            throw;
        }
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

        T *new_data = static_cast<T*>(::operator new(sizeof(T)*size_)); // ::operator new returns void*, always cast

        try {
            relocate_and_commit(new_data, size_);
        } catch(...) {
            ::operator delete(new_data);
            throw;
        }
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
        T *new_data = static_cast<T*>(::operator new(sizeof(T)*new_cap));
       
        try {
            ::new (new_data + size_) T(std::forward<Args>(args)...);
        } catch (...) {
            ::operator delete(new_data);
            throw;
        }

        try {
            relocate_and_commit(new_data, new_cap);
        } catch(...) {
            new_data[size_].~T();
            ::operator delete(new_data);
            throw;
        }

        ++size_;
        return data_[size_ - 1];
    }

    void push_back(const T& value) {
        // does it add extra cost of an extra function call
        // Zero cost at -O2: GCC inlines emplace_back
        emplace_back(value);
    }

    void push_back(T&& value) {
      emplace_back(std::forward(value));
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
        for(std::size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
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