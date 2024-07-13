CC := gcc
CFLAGS := -Wall -Werror -O2
TARGET := writer
SRC := writer.c
OBJ := $(SRC:.c=.o)
CROSS_COMPILE ?=

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
