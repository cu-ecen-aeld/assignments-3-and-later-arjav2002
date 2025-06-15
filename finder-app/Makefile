GCC:=gcc
CC:=$(CROSS_COMPILE)$(GCC)
all: clean writer
writer : writer.o
	$(CC) -o writer writer.o

writer.o : writer.c
	$(CC) -g -Wall -c writer.c 

clean: 
	rm -f writer.o writer

