PROJNAME := doser

WARNINGS := -Wall
CFLAGS =-g -std=c99 $(WARNINGS) -D_GNU_SOURCE -D__USE_XOPEN2K
CC=gcc
USERLIBS = $(SYSLIBS) -lev
DEPLIBS =
LDLIBS = -lev

SRC  := $(wildcard *.c)               # list of source files
OBJS := $(patsubst %.c, .obj/%.o, $(SRC)) # list of object files

.PHONY: all clean 

all: $(PROJNAME)

clean:
	-@$(RM) $(wildcard $(OBJS) $(PROJNAME))
	-@$(RM) -r .obj

-include $(DEPS)

$(PROJNAME): $(OBJS) 
	$(CC) $(LDFLAGS)  $^ -o $(PROJNAME) $(LDLIBS)

.obj/%.o: %.c Makefile .obj/
	$(CC) $(CFLAGS) -c $< -o $@

.obj/:
	mkdir -p $@