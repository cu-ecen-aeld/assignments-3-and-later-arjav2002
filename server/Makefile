GCC:=gcc
CC ?= $(CROSS_COMPILE)$(GCC)
CFLAGS ?= -g -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt
all: clean aesdsocket
aesdsocket : aesdsocket.o
	$(CC) ${CFLAGS} $^ -o $@ ${INCLUDES} ${LDFLAGS}

aesdsocket.o : aesdsocket.c
	$(CC) -g -Wall -c aesdsocket.c 

clean: 
	rm -f aesdsocket.o aesdsocket


