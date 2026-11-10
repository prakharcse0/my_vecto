# ===========================================================================
# Makefile for the mystd::vector learning project.
#
# WHAT make IS
#   make builds files from other files. You describe each target with a RULE:
#
#       target: prerequisites
#       <TAB>command to build target
#
#   make compares timestamps. If any prerequisite is NEWER than the target (or
#   the target doesn't exist), it reruns the command. Otherwise it does nothing.
#   That is the whole idea: rebuild only what is out of date.
#
#   !! The command lines MUST start with a real TAB character, not spaces.
#   !! Spaces give "missing separator", the classic make error.
#
# HOW TO USE
#   make           build ./main (the first target in the file is the default)
#   make run       build if needed, then run it with leak detection on
#   make matrix    build and run at -O0, -O1, -O2, -O3 (bugs can hide at one level)
#   make clean     delete build outputs
#
#   Override a variable from the command line, no editing needed:
#       make run OPT=-O3
#       make run STD=c++20
# ===========================================================================

# ---------------------------------------------------------------------------
# VARIABLES
#   NAME = value     defines one; $(NAME) uses it.
#   ?= means "set only if not already set", so the command line or the
#   environment can override it.
# ---------------------------------------------------------------------------
#
# CXX is the exception: make PREDEFINES it (CXX = g++, see `make -p`), so
# `CXX ?= g++-15` would be silently ignored and you'd still get g++ (13.3).
# := assigns unconditionally. `make CXX=g++-13` still wins: a command-line
# variable overrides any assignment in the makefile. (Environment doesn't.)
# g++-15: newest stable GCC here. 13.3 miscompiles a constexpr pseudo-
# destructor call (see destroy_range in guards.hpp).
CXX      := g++-15
STD      ?= c++23
OPT      ?= -O1

# Warnings: -Wall -Wextra -Wpedantic catch most mistakes the compiler can see.
WARN     := -Wall -Wextra -Wpedantic

# Sanitizers: instrument the binary to detect bugs at runtime.
#   address   (ASan): out-of-bounds, use-after-free, double free, leaks
#   undefined (UBSan): signed overflow, misaligned access, bad casts, ...
# -g adds debug info so sanitizer reports show file:line.
SAN      := -fsanitize=address,undefined -g

# := expands the right-hand side NOW (once); = would re-expand on every use.
# For plain flag lists there's no visible difference; := is the safer habit.
CXXFLAGS := -std=$(STD) $(WARN) $(OPT) $(SAN)

# Headers main.cpp includes. Listed as prerequisites below so that editing
# vector.hpp or guards.hpp triggers a rebuild. make cannot discover #include
# on its own -- if a header isn't listed, changing it rebuilds nothing.
HEADERS  := vector.hpp guards.hpp

# ---------------------------------------------------------------------------
# .PHONY marks targets that are NOT files. Without it, a file accidentally
# named "clean" or "run" would look up to date and make would skip the rule.
# ---------------------------------------------------------------------------
.PHONY: all run matrix clean

# Default target (first in the file). "all" is just a name for "main".
all: main

# ---------------------------------------------------------------------------
# The real build rule.
#   target:        main
#   prerequisites: main.cpp and the headers
#   automatic variables, filled in by make:
#     $@  the target            -> main
#     $<  the FIRST prerequisite -> main.cpp
# So the command expands to:
#   g++ -std=c++20 -Wall ... main.cpp -o main
# ---------------------------------------------------------------------------
main: main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@

# ---------------------------------------------------------------------------
# run depends on main, so make builds main first (only if it's out of date),
# then runs it. VAR=value before a command sets an environment variable just
# for that command; detect_leaks=1 makes ASan report leaked memory at exit.
# ---------------------------------------------------------------------------
run: main
	ASAN_OPTIONS=detect_leaks=1 ./main

# ---------------------------------------------------------------------------
# Build and run at every optimisation level. A shell for-loop, so:
#   - a recipe is passed to the shell line by line; the trailing \ joins the
#     lines into ONE shell command, so the loop variable survives.
#   - $$ is make's escape for a literal $ (a single $ is make's own syntax),
#     so $$o reaches the shell as $o.
#   - "|| exit 1" stops at the first failing level instead of carrying on.
#   - @ at the start of a line stops make from echoing the command itself.
# Outputs go to main-O0, main-O1, ... so they don't clobber ./main.
# ---------------------------------------------------------------------------
matrix: main.cpp $(HEADERS)
	@for o in -O0 -O1 -O2 -O3; do \
		echo "== $$o"; \
		$(CXX) -std=$(STD) $(WARN) $$o $(SAN) main.cpp -o main$$o || exit 1; \
		ASAN_OPTIONS=detect_leaks=1 ./main$$o || exit 1; \
	done

# -f: don't complain if the files are already gone.
clean:
	rm -f main main-O0 main-O1 main-O2 main-O3
