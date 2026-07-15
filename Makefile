CC      ?= cc
CFLAGS  ?= -O2 -std=c99
CFLAGS  += -Wall -Wextra -pedantic
LDFLAGS ?=
LIBS     = -lpthread -lm

all: demo ai_demo

demo: demo.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ demo.c $(LIBS)

ai_demo: ai_demo.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ai_demo.c $(LIBS)

# Tests
TESTS := tests/stress_test tests/mouse_parse_test tests/rate_test \
         tests/render_test tests/resize_fuzz tests/input_fuzz \
         tests/composer_test

tests/stress_test: tests/stress_test.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/stress_test.c $(LIBS)

tests/mouse_parse_test: tests/mouse_parse_test.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/mouse_parse_test.c $(LIBS)

tests/rate_test: tests/rate_test.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/rate_test.c $(LIBS)

tests/render_test: tests/render_test.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/render_test.c $(LIBS)

tests/resize_fuzz: tests/resize_fuzz.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/resize_fuzz.c $(LIBS)

tests/input_fuzz: tests/input_fuzz.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/input_fuzz.c $(LIBS)

tests/composer_test: tests/composer_test.c flux.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tests/composer_test.c $(LIBS)

test: $(TESTS)
	./tests/stress_test
	./tests/mouse_parse_test
	./tests/rate_test
	./tests/render_test
	./tests/resize_fuzz
	./tests/input_fuzz
	./tests/composer_test

# Same as test, but promote warnings to errors so any new widget
# code must be clean-on-gcc.
test-strict:
	$(MAKE) -B test CFLAGS="-O2 -std=c99 -Wall -Wextra -pedantic -Werror"

update-goldens: tests/render_test
	FLUX_UPDATE_GOLDENS=1 ./tests/render_test

clean:
	rm -f demo ai_demo $(TESTS)
	rm -f tests/golden/*.actual

.PHONY: all clean test test-strict update-goldens
