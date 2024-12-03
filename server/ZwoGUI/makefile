#
# makefile for ZWO camera software
#
# 2019-01-07  Christoph C. Birk  (birk@carnegiescience.edu)
#             OCIW, Pasadena, CA
#
# -----------------------------------------------------------------

# flags and libraries ---------------------------------------------

OPT     = -g # -O
BINDIR	= ../bin

# LINUX 64-bit -- Release
#CC	= gcc -m64
#CFLAGS	= -DLINUX -DNDEBUG -Wall
#LIBS	= -lm -lpthread -lASICamera2
#LIBX	= -L/usr/X11R6/lib -lX11 -L../../CXT -lcxt64 

# LINUX 64-bit -- Debug
CC	= gcc
CFLAGS	= -DLINUX -Wall -I../../CXT
LIBA	= -lASICamera2 -lEFWFilter # -Wl,-Bdynamic
LIBS	= -lm -lpthread # -L/usr/local/lib
LIBX	= -L/usr/X11R6/lib -lX11 -L../../CXT -lcxt64 

# LINUX 64-bit -- SIMULATOR
#CC	= gcc -m64
#CFLAGS	= -DLINUX -DSIM_ONLY -Wall
#LIBS	= -lm -lpthread
#LIBX	= -L/usr/X11R6/lib -lX11 -L../../CXT -lcxt64 

# MacOS 64-bit -- SIMULATOR
#CC	= /usr/bin/gcc -arch x86_64
#CFLAGS	= -DMACOSX -DSIM_ONLY -Wall
#LIBS	= # -lm -lpthread 
#LIBX	= -L../../CXT -lcxt64 -L/opt/X11/lib -lX11

# -----------------------------------------------------------------

HEADER	= zwo.h utils.h ptlib.h tcpip.h

# object definitions ----------------------------------------------

# main modules

Oserver = zwoserver.o tcpip.o utils.o random.o ptlib.o fits.o

Ogui = zwogui.o zwotcp.o qltool.o tcpip.o utils.o fits.o ptlib.o random.o

# targets ---------------------------------------------------------

all: 	zwoserver zwogui

clean: 
	rm -f zwoserver zwogui *.o

zwoserver: $(Oserver)
	$(CC) -o zwoserver $(Oserver) $(LIBS) $(LIBA)
	@echo zwoserver done

zwogui: $(Ogui)
	$(CC) -o zwogui $(Ogui) $(LIBS) $(LIBX)
	@echo zwogui done

recompile: 
	@make clean
	@make all

# dependencies ----------------------------------------------------

asiusb.o:	asiusb.c asiusb.h zwo.h ptlib.h utils.h random.h ASICamera2.h
		# $(CC) $(CFLAGS) $(OPT) -c asiusb.c

efw.o:		efw.c efw.h # zwo.h ptlib.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c efw.c

zwotcp.o:	zwotcp.c zwotcp.h zwo.h tcpip.h ptlib.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c zwotcp.c

zwoserver.o:	zwoserver.c $(HEADER) random.h EFW_filter.h ASICamera2.h fits.h
		$(CC) $(CFLAGS) $(OPT) -c zwoserver.c

zwogui.o:	zwogui.c zwogui.h $(HEADER) zwotcp.h random.h
		$(CC) $(CFLAGS) $(OPT) -c zwogui.c

fits.o:		fits.c fits.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c fits.c

ptlib.o:	ptlib.c ptlib.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c ptlib.c

qltool.o:	qltool.c qltool.h utils.h random.h
		$(CC) $(CFLAGS) $(OPT) -c qltool.c

random.o:	random.c random.h
		$(CC) $(CFLAGS) $(OPT) -D_REENTRANT -c random.c

tcpip.o:        tcpip.c tcpip.h utils.h ptlib.h
		$(CC) $(CFLAGS) $(OPT) -c tcpip.c

utils.o:	utils.c utils.h
		$(CC) $(CFLAGS) $(OPT) -c utils.c

# -----------------------------------------------------------------

