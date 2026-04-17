#
# makefile for zwo_benchmark (ZWO server FPS benchmark client)
#
# Builds a thin client that measures client-side FPS over TCP/IP against
# zwoserver. Reuses tcpip.c / utils.c / ptlib.c from src/server/ but
# compiles them locally so this Makefile works on both Linux and macOS
# without dragging in the server's cross-compile flags.
#
# -----------------------------------------------------------------

OPT     = -O2
CC      = gcc
CFLAGS  = -Wall -I../server
LIBS    = -lm -lpthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CFLAGS += -DLINUX
endif
ifeq ($(UNAME_S),Darwin)
  CFLAGS += -DMACOSX
endif

SERVER_DIR = ../server
OBJS = zwo_benchmark.o tcpip.o utils.o ptlib.o

all: zwo_benchmark

zwo_benchmark: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

zwo_benchmark.o: zwo_benchmark.c $(SERVER_DIR)/tcpip.h $(SERVER_DIR)/utils.h $(SERVER_DIR)/zwo.h
	$(CC) $(CFLAGS) $(OPT) -c zwo_benchmark.c

tcpip.o: $(SERVER_DIR)/tcpip.c $(SERVER_DIR)/tcpip.h
	$(CC) $(CFLAGS) $(OPT) -c $(SERVER_DIR)/tcpip.c

utils.o: $(SERVER_DIR)/utils.c $(SERVER_DIR)/utils.h
	$(CC) $(CFLAGS) $(OPT) -c $(SERVER_DIR)/utils.c

ptlib.o: $(SERVER_DIR)/ptlib.c $(SERVER_DIR)/ptlib.h
	$(CC) $(CFLAGS) $(OPT) -c $(SERVER_DIR)/ptlib.c

clean:
	rm -f zwo_benchmark *.o
