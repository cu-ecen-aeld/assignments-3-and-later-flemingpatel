# Compiler
CC ?= gcc
CFLAGS ?= -Wall -Werror -O2
LDFLAGS ?= -lpthread

# Target executable
TARGET := aesdsocket

# Source files
SRC := aesdsocket.c
OBJ := $(SRC:.c=.o)

# Cross-compile variable (optional)
CROSS_COMPILE ?=

# args
USE_AESD_CHAR_DEVICE ?= 1
CFLAGS += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)

# Default target
.PHONY: all clean

all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJ)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile each source file into an object file
%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

# Clean up the compiled files
clean:
	rm -f $(TARGET) $(OBJ)
