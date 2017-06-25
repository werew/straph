
# Folders 
OBJDIR = obj
BINDIR = bin
SRCDIR = src
INCDIR = include
LIBDIR = lib
TESTDIR= test

# Targets
STATICLIB = $(LIBDIR)/libstraph.a
SHAREDLIB = $(LIBDIR)/libstraph.so

# Compiler
CC = gcc
CFLAGS = -pthread     \
         -pedantic    \
         -Wall        \
         -Wextra      \
         -Werror      \
         -I $(INCDIR) \
         -std=gnu99   \
         -g


# Files
SOURCES := io.c             \
           linked_fifo.c    \
           straph.c
SOURCES := $(addprefix $(SRCDIR)/,$(SOURCES))

INCLUDES := common.h        \
            io.h            \
            linked_fifo.h   \
            straph.h        
INCLUDES := $(addprefix $(INCDIR)/,$(INCLUDES))

OBJECTS := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

DEP = $(OBJECTS:%.o=%.d)

TESTS := $(notdir $(basename $(wildcard $(TESTDIR)/*.c)))
TESTSBIN := $(addsuffix .t, $(addprefix $(TESTDIR)/, $(TESTS)))








lib: $(STATICLIB)

# Dependencies to the headers are
# covered by this include
-include $(DEP) 		

$(STATICLIB): $(OBJECTS)
	@mkdir -p $(LIBDIR)
	ar rcs $@ $(OBJECTS)

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(TESTSBIN): $(TESTDIR)/%.t : $(TESTDIR)/%.c $(STATICLIB) 
	$(CC) -static $< $(CFLAGS) -L$(LIBDIR) -lstraph -o $@

.PHONY: alltests $(TESTS)

alltests: $(TESTS)

$(TESTS): % : $(TESTDIR)/%.t
	@echo "Running test: $@"
	@./$< 2>&1 > $(TESTDIR)/log.$@ && echo "----> OK" 

clean:
	rm -f $(OBJECTS) $(STATICLIB) $(SHAREDLIB) $(TESTSBIN) $(TESTDIR)/log.*

