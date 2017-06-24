
# Folders 
OBJDIR = obj
BINDIR = bin
SRCDIR = src
INCDIR = include
LIBDIR = lib

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

clean:
	rm -f $(OBJECTS) $(STATICLIB) $(SHAREDLIB)
