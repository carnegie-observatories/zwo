#
# makefile for ZWO guider software
#
# 2016-10-24  Christoph C. Birk  (birk@carnegiescience.edu)
#             OCIW, Pasadena, CA
#
# -----------------------------------------------------------------

# flags and libraries ---------------------------------------------

OPT     = -O -g
BINDIR	= ../bin

# LINUX 64-bit -- Release
#CC	= gcc -m64
#CFLAGS	= -DLINUX -DNDEBUG -Wall -I../../CXT
#LIBS	= -lm -lpthread -latcore
#LIBX	= -L/usr/X11R6/lib -lX11 -L../../CXT -lcxt64 

# LINUX 64-bit -- Debug
CC	= gcc -m64 -fcommon 
CFLAGS	= -DLINUX -Wall -I../cxt/src/
#LIBS	= -lm -lpthread -latcore
LIBS	= -lm -lpthread
LIBX	= -L/usr/X11R6/lib -lX11 -L../cxt/src/ -lcxt

# LINUX 64-bit -- SIMULATOR
#CC	= gcc -m64
#CFLAGS	= -DLINUX -DSIM_ONLY -Wall -I../../CXT
#LIBS	= -lm -lpthread 
#LIBX	= -L/usr/X11R6/lib -lX11 -L../../CXT -lcxt64 

# MacOS 64-bit -- engineering mode
#CC	= /usr/bin/gcc -arch x86_64
#CFLAGS	= -DMACOSX -DENG_MODE -Wall -I../../CXT -I/opt/X11/include
#LIBS	= # -lm -lpthread 
#LIBX	= -L../../CXT -lcxt64 -L/opt/X11/lib -lX11

# MACOSX 64-bit 10.9+ (ARM/M2)
CC      = /usr/bin/gcc
CFLAGS  = -DMACOSX -Wall -I../cxt/src/ -I/opt/X11/include
LIBS    = # -lm -lpthread
LIBX    = -L../cxt/src/ -lcxt -L/opt/X11/lib -lX11

# MACOSX 64-bit 10.9+ (ARM/M2) -- SIMULATOR
CC      = /usr/bin/gcc
CFLAGS  = -DMACOSX -DSIM_ONLY -Wall -I../cxt/src/ -I/opt/X11/include
LIBS    = # -lm -lpthread
LIBX    = -L../cxt/src/ -lcxt -L/opt/X11/lib -lX11

# -----------------------------------------------------------------

HEADER	= utils.h ptlib.h tcpip.h

# object definitions ----------------------------------------------

# main modules

Ogui	= zwogcam.o zwotcp.o qltool.o graph.o tcpip.o utils.o \
	  fits.o ptlib.o random.o gcpho.o telio.o eds.o guider.o

Oget   	= getimages.o

# targets ---------------------------------------------------------

all: 	zwo getimages

zwo:	gcamzwo

clean: 
	rm -f gcamzwo getimages *.o

install: all
	cp *gcam* $(BINDIR)
	strip $(BINDIR)/*

zwogcam: gcamzwo 
	ln -s gcamzwo zwogcam

gcamzwo: $(Ogui)
	$(CC) -o gcamzwo $(Ogui) $(LIBS) $(LIBX)
	@echo gcamzwo done

getimages: $(Oget)
	$(CC) -o getimages $(Oget) 
	@echo getimages done

recompile: 
	@make clean
	@make all

# dependencies ----------------------------------------------------

zwotcp.o:	zwotcp.c zwotcp.h zwogcam.h tcpip.h ptlib.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c zwotcp.c

zwogcam.o:	zwogcam.c zwogcam.h $(HEADER) zwotcp.h guider.h \
		qltool.h graph.h fits.h gcpho.h telio.h random.h
		$(CC) $(CFLAGS) $(OPT) -c zwogcam.c

eds.o:		eds.c eds.h tcpip.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c eds.c

fits.o:		fits.c fits.h utils.h
		$(CC) $(CFLAGS) $(OPT) -c fits.c

gcpho.o:	gcpho.c gcpho.h
		$(CC) $(CFLAGS) $(OPT) -c gcpho.c

graph.o:	graph.c graph.h
		$(CC) $(CFLAGS) $(OPT) -c graph.c

guider.o:	guider.c guider.h
		$(CC) $(CFLAGS) $(OPT) -c guider.c

ptlib.o:	ptlib.c ptlib.h utils.h
		$(CC) $(CFLAGS) -DPROJECT_ID=12 $(OPT) -c ptlib.c

qltool.o:	qltool.c qltool.h utils.h random.h
		$(CC) $(CFLAGS) $(OPT) -c qltool.c

random.o:	random.c random.h
		$(CC) $(CFLAGS) $(OPT) -D_REENTRANT -c random.c

tcpip.o:        tcpip.c tcpip.h utils.h ptlib.h
		$(CC) $(CFLAGS) $(OPT) -c tcpip.c

telio.o:        telio.c telio.h utils.h tcpip.h
		$(CC) $(CFLAGS) $(OPT) -c telio.c

utils.o:	utils.c utils.h
		$(CC) $(CFLAGS) $(OPT) -c utils.c

getimages.o:	getimages.c
		$(CC) $(CFLAGS) -c getimages.c

# -----------------------------------------------------------------

