GCC:=gcc
CC ?= $(CROSS_COMPILE)$(GCC)
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt
all: aesdsocket
aesdsocket : aesdsocket.o
	ls
	$(CC) ${CFLAGS} $^ -o $@ ${INCLUDES} ${LDFLAGS}

aesdsocket.o : aesdsocket.c
	echo "Compiling!"
	$(CC) -g -Wall -c aesdsocket.c 

clean: 
	rm -f aesdsocket.o aesdsocket


