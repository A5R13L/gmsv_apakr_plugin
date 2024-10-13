AR = $(CROSS_PREFIX)ar
CC = $(CROSS_PREFIX)gcc
CCLD = $(CC)
SRCS = $(wildcard src/*.c src/hde/*.c)
OBJS = $(SRCS:%.c=%.o)
DEPS = $(SRCS:%.c=%.d)
INCS = -Isrc -Iinclude
CFLAGS += -Wall -Werror -std=gnu11 -fPIC -pedantic

ifeq ($(OS),Windows_NT)
	TARGET_OS = Windows
	ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
		TARGET_ARCH = AMD64
	else
		ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
			TARGET_ARCH = AMD64
		endif
		ifeq ($(PROCESSOR_ARCHITECTURE),x86)
			TARGET_ARCH = IA32
		endif
	endif
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		TARGET_OS = Linux
	endif
	ifeq ($(UNAME_S),Darwin)
		TARGET_OS = MacOSX
	endif
	UNAME_P := $(shell uname -p)
	ifeq ($(UNAME_P),x86_64)
		TARGET_ARCH = AMD64
	endif
	ifneq ($(filter %86,$(UNAME_P)),)
		TARGET_ARCH = IA32
	endif
endif

ifeq ($(TARGET_OS),Windows)
	EXT = dll
	LDFLAGS += -s
	RM = del
endif
ifeq ($(TARGET_OS),Linux)
	PREFIX = lib
	EXT = so
	LDFLAGS += -s
	RM = rm -f
endif
ifeq ($(TARGET_OS),MacOSX)
	PREFIX = lib
	EXT = dylib
	RM = rm -f
endif

ifeq ($(TARGET_ARCH),AMD64)
	CFLAGS += -m64
	LDFLAGS += -m64
endif
ifeq ($(TARGET_ARCH),IA32)
	CFLAGS += -m32
	LDFLAGS += -m32
endif

all: $(PREFIX)MinHook.$(EXT) $(PREFIX)MinHook.a

-include $(DEPS)

$(PREFIX)MinHook.a: $(OBJS)
	$(AR) r $@ $^
$(PREFIX)MinHook.$(EXT): $(OBJS)
	$(CCLD) -o $@ -shared $(LDFLAGS) $^

.c.o:
	$(CC) -o $@ -c -MMD -MP $(INCS) $(CFLAGS) $<

clean:
	$(RM) $(OBJS) $(DEPS) $(PREFIX)MinHook.$(EXT) $(PREFIX)MinHook.a

.PHONY: clean
