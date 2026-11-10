// ===========================================================================
// asm_lab/shift.cpp -- the erase(first, last) shift loop, on its own, so its
// machine code can be read without the rest of the vector around it.
//
// Run these from the vector/ folder:
//
//   1. compile only (-c: no linking, -g: keep source lines for -S below)
//        g++ -std=c++20 -O2 -g -c asm_lab/shift.cpp -o asm_lab/shift.o
//
//   2. disassemble into a text file you can open in the editor
//        objdump -d -S --no-show-raw-insn --demangle asm_lab/shift.o > asm_lab/shift.asm
//
//   3. list the functions in it, with their sizes in bytes (hex)
//        nm -SC --size-sort asm_lab/shift.o | grep Shift
//
//   4. same disassembly, but with relocations (-r): a "call" in a .o shows a
//      placeholder address until linking; -r prints the REAL target below it
//        objdump -dr --no-show-raw-insn --demangle asm_lab/shift.o > asm_lab/shift-r.asm
//
// Optional: the same in Intel syntax (mov dst, src), if you find it easier
//        objdump -d -S --no-show-raw-insn --demangle -M intel asm_lab/shift.o > asm_lab/shift-intel.asm
// Optional: try -O0, -O1, -O3 in step 1 and compare the files.
//
// In shift.asm, search for (Ctrl+F):
//   "Shift<int>::with_i"     the int loop: 15 instructions, no calls. The
//                            loop is from "20:" to "jne 20" (a jump
//                            BACKWARDS is what a loop looks like)
//   "Shift<int>::with_while" your while version: the same instructions
//   "basic_string"           the string loop: about 90 instructions,
//                            because std::string's move assignment was
//                            INLINED into it -- no call to operator=.
//                            Look for "call": in shift.asm its target is
//                            junk; in shift-r.asm the line below it says
//                            memcpy (copying a short string's inline bytes)
// ===========================================================================
#include <cstddef>
#include <string>
#include <utility>

template <class T>
struct Shift {
    T*          data_;      // this + 0
    std::size_t size_;      // this + 8

    // The loop from erase(first, last).
    void with_i(std::size_t s, std::size_t e) {
        for (std::size_t i = 0; e + i < size_; ++i)
            data_[s + i] = std::move(data_[e + i]);
    }

    // Your two-index version.
    void with_while(std::size_t s, std::size_t e) {
        while (e < size_)
            data_[s++] = std::move(data_[e++]);
    }
};

// A template only produces machine code when it is instantiated. These two
// lines force both versions to be compiled for int and for std::string.
template struct Shift<int>;
template struct Shift<std::string>;

/*
The file
It has the erase shift loop on its own, in your i version and your while version, for both int and std::string. The commands to run are in its top comment.

The commands
Run them from the vector/ folder:


# 1. compile only: -c = no linking, -g = keep source lines for step 2
g++ -std=c++20 -O2 -g -c asm_lab/shift.cpp -o asm_lab/shift.o

# 2. disassemble into a text file, with your source lines mixed in (-S)
objdump -d -S --no-show-raw-insn --demangle asm_lab/shift.o > asm_lab/shift.asm

# 3. list the functions and their sizes in bytes (hex)
nm -SC --size-sort asm_lab/shift.o | grep Shift

# 4. same, with relocations (-r): shows where each "call" really goes
objdump -dr --no-show-raw-insn --demangle asm_lab/shift.o > asm_lab/shift-r.asm
The .asm files are already generated, so you can open them right away. You'll get 4 files: shift.o (binary, don't open it), shift.asm, shift-r.asm, and the .cpp.

Step 3 output

000000000000002f W Shift<int>::with_while(...)          0x2f  = 47 bytes
000000000000002f W Shift<int>::with_i(...)              same size
0000000000000131 W Shift<std::string>::with_while(...)  0x131 = 305 bytes
0000000000000131 W Shift<std::string>::with_i(...)      same size
The W means "weak", the kind of symbol templates produce. Equal sizes are only a hint, not proof, so check the instructions themselves.

What to search for in shift.asm (Ctrl+F)
1. Shift<int>::with_i. Your source lines appear above the instructions they produced:


        for (std::size_t i = 0; e + i < size_; ++i)
   4:	mov    0x8(%rdi),%rcx         rcx = size_ (this + 8)
   8:	cmp    %rcx,%rdx              e vs size_
   b:	jae    2e                     nothing to shift -> skip to ret
            data_[s + i] = std::move(data_[e + i]);
   d:	mov    (%rdi),%rdi            rdi = data_ (this + 0)
  10:	sub    %rdx,%rsi              rsi = s - e
  13:	lea    (%rdi,%rdx,4),%rax     rax = &data_[e]       source pointer
  17:	lea    (%rdi,%rcx,4),%rcx     rcx = &data_[size_]   end pointer
  20:	mov    (%rax),%edx            load            <- loop starts at 20
  22:	mov    %edx,(%rax,%rsi,4)     store
  25:	add    $0x4,%rax              advance
  29:	cmp    %rcx,%rax
  2c:	jne    20                     jump BACK to 20 <- loop ends here
  2e:	ret
To find the loop, look for a jump to a lower address. Here that's jne 20 at 2c, and everything from 20: to it repeats once per element.

2. Shift<int>::with_while. Compare it with the one above. The instructions are the same, which is the evidence for last time's answer.

3. basic_string. The string loop is about 90 instructions. Look at what's not there: there's no call to std::string's operator=. The compiler copied the whole move assignment into the loop (inlining).

4. call inside the string loop. In shift.asm its target looks like junk (call cd <...+0xcd>). That's normal in a .o file: the address isn't filled in until linking. Search for the same call in shift-r.asm, and the line below it shows the real target:


  c8:	call   cd <...>
			c9: R_X86_64_PLT32	memcpy-0x4
So that call is memcpy, which copies the bytes of a short string stored inside the std::string object, not a call to the move assignment.

Correction: last time I said the call inside the string loop "is the std::string move assignment". That was wrong. The move assignment is inlined, and the only call is memcpy. Checking the relocations is what showed this, and it's exactly the mistake your notes warn about: look at the real call targets before claiming anything about inlining.

Things to try
Change -O2 to -O0 in step 1 and regenerate. You'll see i stored in memory and reloaded every time round the loop, and real calls to operator= and std::move.
Try -O3 and look for movdqu/movups in the int loop. Those are vector instructions that copy 4 ints at once.
Paste the file into godbolt.org (x86-64 gcc 13, -O2) to see the same thing with colours matching source lines to instructions.
asm_lab/ isn't tracked by git. Commit it as a reference or delete it when you're done; don't commit shift.o.




*/