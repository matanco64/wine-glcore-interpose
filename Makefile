# Builds the x86_64 interposer (Wine on macOS runs x86_64 under Rosetta on Apple Silicon).
CFLAGS ?= -O2 -Wall -DGL_SILENCE_DEPRECATION -Wno-deprecated-declarations
all: libglcore_interpose.dylib
libglcore_interpose.dylib: glcore_interpose.c
	clang -arch x86_64 $(CFLAGS) -dynamiclib -o $@ $< -framework OpenGL -framework CoreFoundation
clean:
	rm -f libglcore_interpose.dylib
.PHONY: all clean
