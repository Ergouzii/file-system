CC:=gcc
WARN:=-Wall -Werror
CCOPTS:=-std=c99 -ggdb -D_GNU_SOURCE

all: compile fs

compile:
	$(CC) $(WARN) $(CCOPTS) -c FileSystem.c FileSystem.h

fs: FileSystem.o
	$(CC) $(WARN) $(CCOPTS) -o fs FileSystem.o

clean:
	rm -rf *.o *gch fs

compress:
	zip fs-sim.zip *.c *.h *.md Makefile