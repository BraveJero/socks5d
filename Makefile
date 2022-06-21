SOURCE_DIR := src
BUILD_DIR := build
INCLUDE_DIR := include

TARGET := socks5d

C_SOURCES := $(shell find $(SOURCE_DIR)/ -type f -name "*.c")
OBJS := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)

RE2C := re2c

DEP_FLAGS := -MMD -MP
# TODO: Remove debug option
CFLAGS += -Wall -g -lpthread -std=c11 -pedantic -pedantic-errors -Wextra -Werror -Wno-unused-parameter -Wno-implicit-fallthrough -D_POSIX_C_SOURCE=200112L -fsanitize=address
CFLAGS += -I$(SOURCE_DIR) -I$(INCLUDE_DIR) $(DEP_FLAGS)
RE2CFLAGS += -W -f --no-generation-date

all: proxy client

lexer:
	$(RE2C) $(RE2CFLAGS) ./lexer/mgmt_protocol.re -c -o ./src/mgmt_protocol.re.c -t ./include/mgmt_protocol.re.h

proxy: $(BUILD_DIR)/$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
	cd client; $(MAKE) clean;

client:
	cd client; $(MAKE);

rebuild: clean
	$(MAKE) all

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c Makefile
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(OBJS:%.o=%.d)

MKDIR_P ?= mkdir -p

.PHONY: all proxy clean rebuild client