CC ?= gcc
AR ?= ar

CSTD := -std=c11
OPT ?= -O2
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wformat -Wformat-security -Wstrict-prototypes -Wmissing-prototypes
HARDEN := -fstack-protector-strong -fPIE -D_FORTIFY_SOURCE=2 -fno-common

CPPFLAGS := -Iinclude
CFLAGS := $(CSTD) $(OPT) $(WARN) $(HARDEN)
LDFLAGS :=
LDLIBS := -lpthread -lm

ifeq ($(UNAME_S),)
UNAME_S := $(shell uname -s)
endif
ifeq ($(UNAME_S),Linux)
LDFLAGS += -pie
LDFLAGS += -Wl,-z,relro,-z,now
endif

ifeq ($(COV),1)
CFLAGS += -O0 -g --coverage
LDFLAGS += --coverage
endif

ifeq ($(SAN),address)
CFLAGS += -O1 -g -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif
ifeq ($(SAN),undefined)
CFLAGS += -O1 -g -fsanitize=undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=undefined
endif

BIN_DIR := bin
BUILD_DIR := build
SRC_DIR := src
EX_DIR := examples

LIB_SRCS := $(SRC_DIR)/net.c $(SRC_DIR)/manager.c $(SRC_DIR)/worker.c
LIB_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
LIB := $(BUILD_DIR)/libdistr.a
APP_SRCS := $(EX_DIR)/integral_app.c

all: $(BIN_DIR)/manager $(BIN_DIR)/worker

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c include/distr.h src/internal.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(BIN_DIR)/manager: $(EX_DIR)/manager_main.c $(APP_SRCS) $(LIB) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(EX_DIR)/manager_main.c $(APP_SRCS) $(LIB) $(LDFLAGS) $(LDLIBS)

$(BIN_DIR)/worker: $(EX_DIR)/worker_main.c $(APP_SRCS) $(LIB) | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(EX_DIR)/worker_main.c $(APP_SRCS) $(LIB) $(LDFLAGS) $(LDLIBS)

test: all
	bash scripts/test.sh

bench: all
	bash scripts/bench.sh

analyze:
	clang --analyze $(CPPFLAGS) $(CSTD) $(WARN) $(SRC_DIR)/*.c $(EX_DIR)/*.c

docs:
	doxygen Doxyfile

coverage:
	@command -v lcov >/dev/null 2>&1 || (echo "lcov is required for coverage target"; exit 1)
	$(MAKE) clean
	$(MAKE) COV=1 test
	lcov --capture --directory . --output-file tests/coverage.info
	lcov --remove tests/coverage.info '/usr/*' --output-file tests/coverage.info
	genhtml tests/coverage.info --output-directory tests/coverage_html
	@echo "Coverage report: tests/coverage_html/index.html"

asan:
	$(MAKE) clean
	$(MAKE) SAN=address all

ubsan:
	$(MAKE) clean
	$(MAKE) SAN=undefined all

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) tests/out tests/bench tests/coverage.info tests/coverage_html docs

.PHONY: all test bench analyze docs coverage asan ubsan clean

