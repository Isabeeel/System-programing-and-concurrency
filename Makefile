# Makefile, ECE252  
# Yiqing Huang

CC = gcc 
CFLAGS_XML2 = $(shell xml2-config --cflags)
CFLAGS_CURL = $(shell curl-config --cflags)
CFLAGS = -Wall $(CFLAGS_XML2) $(CFLAGS_CURL) -std=gnu99 -g
LD = gcc
LDFLAGS = -std=gnu99 -g 
LDLIBS_XML2 = $(shell xml2-config --libs)
LDLIBS_CURL = $(shell curl-config --libs)
LIBS_PTHREAD = -pthread
LDLIBS = $(LDLIBS_XML2) $(LDLIBS_CURL) ${LIBS_PTHREAD}

# For students 
LIB_UTIL = crc.o
SRCS     = crc.c
OBJS     = main.o $(LIB_UTIL) 

TARGETS= findpng2 

all: ${TARGETS}

findpng2: $(OBJS) 
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *.d *.o $(TARGETS) 
