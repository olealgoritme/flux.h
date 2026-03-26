CC      ?= cc
CFLAGS  ?= -O2 -std=c99
CFLAGS  += -Wall -Wextra -pedantic
LDFLAGS ?=
LIBS     = -lpthread -lm

all: demo

demo: demo.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ demo.c $(LIBS)

clean:
	rm -f demo

.PHONY: all clean
