SOURCE_DIR := src
BUILD_DIR := build
INCLUDE_DIR := include

TARGET := client

C_SOURCES := $(shell find $(SOURCE_DIR)/ -type f -name "*.c")
OBJS := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)

CFLAGS += -Wall -g -lpthread -std=c11 -pedantic -pedantic-errors -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200112L -fsanitize=address
CFLAGS += -I$(SOURCE_DIR) -I$(INCLUDE_DIR)

all: c

c: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c Makefile
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(OBJS:%.o=%.d)

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean
	$(MAKE) all

MKDIR_P ?= mkdir -p

.PHONY: all c clean rebuild