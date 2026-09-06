CC = gcc
CFLAGS = -O2 -Wall -Wextra
LDLIBS = -lasound -lm

all: tone micfx test_hpf test_comp test_eq test_mix

test_hpf: test_hpf.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_hpf.c -lm

test_comp: test_comp.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_comp.c -lm

test_eq: test_eq.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_eq.c -lm

test_mix: test_mix.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_mix.c -lm

tone: tone.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

micfx: passthrough.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f tone micfx test_hpf test_comp test_eq test_mix
