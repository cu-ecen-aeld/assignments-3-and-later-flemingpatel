# Compiler
CC := gcc
CFLAGS := -Wall -Werror -O2 -I./ctwisted/include

# Target executable
TARGET := aesdsocket

# Source files
SRC := aesdsocket.c $(wildcard ./ctwisted/src/*.c)
OBJ := $(SRC:.c=.o)

# Cross-compile variable (optional)
CROSS_COMPILE ?=

# Default target
.PHONY: all clean

all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJ)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $^

# Compile each source file into an object file
%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

# Clean up the compiled files
clean:
	rm -f $(TARGET) $(OBJ)