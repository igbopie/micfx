CC = gcc
CFLAGS = -O2 -Wall -Wextra
LDLIBS = -lasound -lm

all: tone micfx bench filefx test_hpf test_comp test_eq test_mix test_rev test_dly test_lim

bench: bench.c dsp.h
	$(CC) $(CFLAGS) -o $@ bench.c -lm

filefx: filefx.c dsp.h conf.h
	$(CC) $(CFLAGS) -o $@ filefx.c -lm

test_hpf: test_hpf.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_hpf.c -lm

test_comp: test_comp.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_comp.c -lm

test_eq: test_eq.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_eq.c -lm

test_mix: test_mix.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_mix.c -lm

test_rev: test_rev.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_rev.c -lm

test_dly: test_dly.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_dly.c -lm

test_lim: test_lim.c dsp.h
	$(CC) $(CFLAGS) -o $@ test_lim.c -lm

tone: tone.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

micfx: passthrough.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f tone micfx bench filefx test_hpf test_comp test_eq test_mix test_rev test_dly test_lim
