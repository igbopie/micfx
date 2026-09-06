# micfx — Portable voice processor (Raspberry Pi 3)

Context to resume work in future sessions (Muse Code).

## Final goal

Receiver + realtime DSP for two wireless microphones, no monitor/keyboard/GUI, USB/battery powered.

```text
Mic 1 wireless receiver → Audio Input L ─┐
                                         ├→ Raspberry Pi 3 → DSP → Audio Output
Mic 2 wireless receiver → Audio Input R ─┘
```

Each microphone stays an independent channel through processing.

Per-channel chain: `High-pass → Compressor → EQ`, then:

```text
MIC 1 → HPF → Comp → EQ ─┐
                         ├→ Mix → Reverb/Delay → Limiter → Output
MIC 2 → HPF → Comp → EQ ─┘
```

## Current status (2026-09-05)

- Raspberry Pi 3 running Raspberry Pi OS Lite, SSH working.
- Dev session from Mac (`/Users/nacho/git/micfx`).
- Direct SSH access: **works** from a sandbox-disabled session
  (verified 2026-09-05): `ssh -i ~/.ssh/muse_karaoke igbopie@192.168.1.137`.
  Dedicated key `~/.ssh/muse_karaoke` (pub already in the Pi's `authorized_keys`).
  Historical note: with the sandbox enabled it was impossible (`Operation not permitted`).
- Pi: user `igbopie`, IP `192.168.1.137` (DHCP, may change), hostname `karaoke`.
- Phase: fast prototyping over SSH. **No** Buildroot or bare-metal.
- Capture hardware: user's M-Audio USB interface (not plugged in yet).
- sudo on the Pi: user granted `NOPASSWD` for
  `/usr/bin/apt, /usr/bin/apt-get, /usr/bin/dpkg` (`/etc/sudoers.d/`).

## Constraints

- No desktop or graphical dependencies. Only required dependencies.
- Use ALSA directly unless a concrete JACK/PipeWire/Carla advantage justifies it.
- Final DSP: a single lightweight process, C or C++, ALSA, 48 kHz, small buffers,
  internal float32, no GUI, no Python in the realtime audio path.
- Initial latency target: round-trip **<10 ms** if the hardware allows it.
- The realtime callback/loop must not: allocate, log heavily, touch disk, or block.
  Robust against xruns.

## DSP prototype (sequential, with gates)

Don't move to stage N+1 until CPU, xruns and latency of stage N are verified:

1. Audio passthrough
2. Independent CH1/CH2 gain
3. Per-channel high-pass (~80–100 Hz)
4. Independent per-channel compressor
5. Simple EQ
6. Mix of both channels
7. Shared reverb (send)
8. Optional delay/echo
9. Final limiter

All 9 stages are implemented and verified offline (see Phase 2 below).

## Parameters (editable without recompiling; file first, pots/encoders later)

```text
mic1_gain, mic2_gain
hpf_frequency
compressor_threshold, compressor_ratio, compressor_attack, compressor_release
eq_low, eq_mid, eq_high
reverb_send_1, reverb_send_2, reverb_amount
delay_ms, delay_feedback, delay_mix
master_gain, limiter_threshold
```

## Target deployment

- systemd service `micfx`: autostart, automatic restart on failure.
- SSH available during development. No desktop.

```text
POWER ON → Linux boot → ALSA → micfx service → Audio processing active
```

## Priorities (in order)

1. Latency 2. Stability / no clicks or xruns 3. Voice quality
4. Low CPU usage 5. Simple boot 6. Minimal system size

No premature optimization, no unnecessary software.

## Agreed way of working

- Incremental. Before major changes: inspect state, explain findings, say what/to change and why.
- After each stage: exact test command, expected result, CPU/xrun/latency measurement, and rollback path.

## Phase 1 (DONE 2026-09-05): low-latency platform + what audio the Pi sees

Result (full output verified over direct SSH):
- Pi 3 Model B Rev 1.2, Debian 13 trixie, kernel 6.18.34+rpt-rpi-v8 aarch64,
  `SMP PREEMPT` (not RT, no `/sys/kernel/realtime`).
- Playback only: card 0 `bcm2835 Headphones` (8 subdevs), card 1 `vc4-hdmi`.
  **Zero capture devices** (`arecord -l` empty).
- `lsusb`: hub + Ethernet only. No USB/I2S audio interface connected.
- `dtparam=audio=on`, `dtparam=i2s=on` commented out.

## Phase 2 (IN PROGRESS): base platform + capture hardware

Done 2026-09-05 (all over direct SSH):
- Repo: `src/tone.c` (playback test), `src/passthrough.c` (DSP stages),
  `src/Makefile`, `config/micfx.conf`, `systemd/micfx.service`, `tools/xrun_test.sh`.
- Installed `libasound2-dev` via apt.
- Build on the Pi (`~/micfx/src`, plain `make`). Zero warnings with
  `-Wall -Wextra`.
- Playback baseline (`hw:0,0` Headphones, 48 kHz S16 stereo, 20 ms buffer,
  real period 480): **10 s, 0 xruns, 2.4 % CPU**. With a 5 ms buffer:
  499 underruns (expected; the gate is 0 xruns with buffer ≥20 ms).
- Stage 3 (HPF) without hardware: `src/dsp.h` (RBJ biquad),
  per-channel in `micfx` (`hpf_frequency`, 0 = bypass).
  Verified offline on the Pi with `src/test_hpf.c`: fc=100 Hz →
  50 Hz at −12.3 dB (theory −12.0), 1 kHz at 0.0 dB. `HPF_OK`.
  Also caught a real bug: the config parser stalled on comment lines
  (`fscanf`); now reads line by line with `fgets`+`sscanf`.
- Stage 4 (compressor) without hardware: `comp_t` in `dsp.h`
  (peak follower + exponential attack/release, linear threshold/ratio curve,
  independent state per channel, ratio ≤1 = bypass).
  Verified offline with `src/test_comp.c`: input 0.5 with threshold 0.25 and
  4:1 ratio → 0.330 (theory 0.3125, ±10 % tolerance); below-threshold intact;
  bit-exact bypass. `COMP_OK`. Integrated in `micfx` after the HPF, works
  in [−1,1] (normalized from S16).
- Stage 5 (EQ) without hardware: `eq3_t` in `dsp.h`
  (250 Hz low-shelf, 1 kHz peak Q=1, 4 kHz high-shelf, RBJ).
  Verified with `src/test_eq.c`: flat passes 0.5000 exactly, +6 dB boosts
  (≈×2) only in their band, rest untouched. `EQ_OK`.
  Commits: `aff5b4d` (stages 1–4), `420ba95` (stage 5).
- Stage 6 (mix) + refactor: `chan_t` in `dsp.h` wraps the per-channel chain;
  `mix_out` averages to dual mono with master. `micfx` outputs
  L=R=mix with clip at [−1,1]. Verified with `src/test_mix.c`
  (`MIX_OK`, exact values). Commit `9e3d04e`.
- Stage 7 (shared reverb): mono Schroeder in `dsp.h` (4 combs
  0.84 + 2 allpasses 0.5, static ~115 KB buffers). Per-channel sends and
  `reverb_amount`, wet added pre-master. `test_rev`: `REV_OK`
  (amount=0 silent, decaying bounded tail). Commit `eb2b2b2`.
- Stage 8 (optional delay): mono echo with feedback, up to 1 s, `mix=0` =
  bypass. `test_dly`: `DLY_OK` (exact 0.5/0.25 echoes).
- Stage 9 (final limiter): brickwall at `limiter_threshold`, last stage
  after master. `test_lim`: `LIM_OK`. Full chain:
  HPF → comp → EQ → mix → reverb → delay → master → limiter.
  Commit `bde432e`. Suite 7/7 green, zero warnings.
- Pending: the user will plug in their M-Audio USB interface; then
  rescan (`arecord -l`), pick `hw:X,Y`, and run the full-duplex test.

## Repo layout

```text
micfx/
  README.md        # this file
  src/             # DSP in C (tone, micfx, offline tests)
  config/          # parameter file
  systemd/         # micfx.service unit
  tools/           # measurement scripts (xruns, latency, CPU)
```
