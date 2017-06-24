# Project name
TARGET = straph

# Folders 
OBJDIR = obj
BINDIR = bin
SRCDIR = src
INCDIR = include

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
SOURCES := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(INCDIR)/*.h
OBJECTS := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o) 
DEP = $(OBJECTS:%.o=%.d)
EXECUTABLE := $(BINDIR)/$(TARGET)



$(TARGET): $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	@echo "\n-----------------> Linking ... "
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@


# Dependencies to the headers are
# covered by this include
-include $(DEP) 		


$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@echo "\n-----------------> Compiling $@ ... "
	$(CC) $(CFLAGS) -MMD -c $< -o $@

clean:
	@echo "----------------- Cleaning -----------------"
	rm -f $(OBJECTS) $(EXECUTABLE) 
