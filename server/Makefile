GCC:=gcc
CC:=$(CROSS_COMPILE)$(GCC)
all: clean aesdsocket
aesdsocket : aesdsocket.o
	$(CC) -o aesdsocket aesdsocket.o

aesdsocket.o : aesdsocket.c
	$(CC) -g -Wall -c aesdsocket.c 

clean: 
	rm -f aesdsocket.o aesdsocket


