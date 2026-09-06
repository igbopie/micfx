# micfx — Procesador de voz portátil (Raspberry Pi 3)

Contexto para retomar el trabajo en futuras sesiones (Muse Code).

## Objetivo final

Receptor + DSP en tiempo real para dos micrófonos inalámbricos, sin monitor/teclado/GUI, alimentado por USB/batería.

```text
Mic 1 wireless receiver → Audio Input L ─┐
                                         ├→ Raspberry Pi 3 → DSP → Audio Output
Mic 2 wireless receiver → Audio Input R ─┘
```

Cada micrófono se mantiene como canal independiente durante el procesado.

Cadena por canal: `High-pass → Compressor → EQ`, luego:

```text
MIC 1 → HPF → Comp → EQ ─┐
                         ├→ Mix → Reverb/Delay → Limiter → Output
MIC 2 → HPF → Comp → EQ ─┘
```

## Estado actual (2026-09-05)

- Raspberry Pi 3 con Raspberry Pi OS Lite, SSH funcionando.
- Sesión de desarrollo desde Mac (`/Users/nacho/git/micfx`).
- Acceso SSH directo: **funciona** desde sesión con sandbox desactivado
  (verificado 2026-09-05): `ssh -i ~/.ssh/muse_karaoke igbopie@192.168.1.137`.
  Clave dedicada `~/.ssh/muse_karaoke` (pub ya en `authorized_keys` de la Pi).
  Nota histórica: con sandbox activado era imposible (`Operation not permitted`).
- Pi: usuario `igbopie`, IP `192.168.1.137` (DHCP, puede cambiar), hostname `karaoke`.
- Fase: prototipado rápido por SSH. **No** Buildroot ni bare-metal.
- Hardware de audio definitivo **sin decidir**.
- No se ha instalado ni construido nada de DSP todavía.

## Restricciones

- No instalar escritorio ni dependencias gráficas. Solo dependencias necesarias.
- Usar ALSA directamente salvo ventaja concreta de JACK/PipeWire/Carla (debe justificarse).
- DSP definitivo: un único proceso ligero, C o C++, ALSA, 48 kHz, buffers pequeños, float32 interno, sin GUI, sin Python en el audio realtime path.
- Latencia objetivo inicial: round-trip **<10 ms** si el hardware lo permite.
- El callback/loop realtime no debe hacer: allocations, logging pesado, acceso a disco ni operaciones bloqueantes. Robusto ante xruns.

## Prototipo DSP (secuencial, con gates)

No pasar a la etapa N+1 hasta comprobar CPU, xruns y latencia de la etapa N:

1. Audio passthrough
2. Gain independiente CH1/CH2
3. High-pass por canal (~80–100 Hz)
4. Compressor independiente por canal
5. EQ sencillo
6. Mezcla de los dos canales
7. Reverb compartida (send)
8. Delay/echo opcional
9. Limiter final

## Parámetros (editables sin recompilar; fichero primero, potenciómetros/encoders después)

```text
mic1_gain, mic2_gain
hpf_frequency
compressor_threshold, compressor_ratio, compressor_attack, compressor_release
eq_low, eq_mid, eq_high
reverb_send_1, reverb_send_2, reverb_amount
delay_ms, delay_feedback, delay_mix
master_gain, limiter_threshold
```

## Deployment objetivo

- Servicio systemd `micfx`: arranque automático, restart automático si falla.
- SSH disponible durante el desarrollo. Sin desktop.

```text
POWER ON → Linux boot → ALSA → micfx service → Audio processing active
```

## Prioridades (en orden)

1. Latencia 2. Estabilidad / sin clicks ni xruns 3. Calidad de voz
4. Bajo consumo CPU 5. Arranque sencillo 6. Tamaño mínimo del sistema

No optimizar prematuramente ni añadir software innecesario.

## Forma de trabajar acordada

- Incremental. Antes de cambios importantes: inspeccionar estado, explicar hallazgo, decir qué/cambiar por qué.
- Después de cada etapa: comando exacto de prueba, resultado esperado, medición CPU/xruns/latencia, y forma de rollback.

## Fase 1 (COMPLETADA 2026-09-05): plataforma de baja latencia + qué audio detecta la Pi

Resultado (salida completa verificada por SSH directo):
- Pi 3 Model B Rev 1.2, Debian 13 trixie, kernel 6.18.34+rpt-rpi-v8 aarch64,
  `SMP PREEMPT` (no RT, no `/sys/kernel/realtime`).
- Solo playback: card 0 `bcm2835 Headphones` (8 subdev), card 1 `vc4-hdmi`.
  **Cero dispositivos de captura** (`arecord -l` vacío).
- `lsusb`: solo hub + Ethernet. Ningún interfaz USB/I2S de audio conectado.
- `dtparam=audio=on`, `dtparam=i2s=on` comentado.

## Fase 2 (EN CURSO): plataforma base + hardware de captura

Hecho 2026-09-05 (todo por SSH directo, sin sudo en la Pi):
- Repo: `src/tone.c` (test playback), `src/passthrough.c` (etapa 1 DSP),
  `src/Makefile`, `config/micfx.conf`, `systemd/micfx.service`, `tools/xrun_test.sh`.
- Acceso sudo en la Pi: el usuario dio `NOPASSWD` para
  `/usr/bin/apt, /usr/bin/apt-get, /usr/bin/dpkg` (`/etc/sudoers.d/`).
  Instalado `libasound2-dev` vía apt (antes: headers extraídos sin root
  a `~/micfx/sysroot`, ya innecesario).
- Build en la Pi (`~/micfx/src`, `make` a secas). Cero warnings con
  `-Wall -Wextra`.
- Línea base playback (`hw:0,0` Headphones, 48 kHz S16 estéreo, buffer 20 ms,
  periodo real 480): **10 s, 0 xruns, CPU 2.4 %**. Con buffer de 5 ms:
  499 underruns (esperable; el gate es 0 xruns con buffer ≥20 ms).
- Etapa 3 (HPF) implementada sin hardware: `src/dsp.h` (biquad RBJ),
  integrado por canal en `micfx` (`hpf_frequency`, 0 = bypass).
  Verificado offline en la Pi con `src/test_hpf.c`: fc=100 Hz →
  50 Hz a −12.3 dB (teoría −12.0), 1 kHz a 0.0 dB. `HPF_OK`.
  De paso cazó un bug real: el parser de config se atascaba en los
  comentarios (`fscanf`); ahora lee por líneas con `fgets`+`sscanf`.
- Etapa 4 (compresor) implementada sin hardware: `comp_t` en `dsp.h`
  (seguidor de pico + attack/release exponenciales, curva umbral/ratio en
  lineal, estado independiente por canal, ratio ≤1 = bypass).
  Verificado offline con `src/test_comp.c`: entrada 0.5 con umbral 0.25 y
  ratio 4:1 → 0.330 (teoría 0.3125, tolerancia ±10 %); bajo umbral intacto;
  bypass bit-exacto. `COMP_OK`. Integrado en `micfx` tras el HPF, trabaja
  en [−1,1] (normalizado desde S16).
- Etapa 5 (EQ) implementada sin hardware: `eq3_t` en `dsp.h`
  (low-shelf 250 Hz, pico 1 kHz Q=1, high-shelf 4 kHz, RBJ).
  Verificado con `src/test_eq.c`: flat 0.5000 exacto, boosts +6 dB
  (≈×2) solo en su banda, resto intacto. `EQ_OK`. Cadena actual:
  HPF → comp → EQ → gains. Commits: `aff5b4d` (etapas 1–4), `420ba95` (etapa 5).
- Pendiente: el usuario conectará su interfaz M-Audio por USB; entonces
  reescaneo (`arecord -l`), elección de `hw:X,Y` y test full-duplex.

Pendiente: que el usuario pegue la salida de este bloque ejecutado **en la Pi por SSH** (solo lectura; no instala nada):

```bash
echo "=== 1. Modelo / sistema ==="
cat /proc/device-tree/model; echo
uname -a; cat /etc/os-release | head -5
cat /proc/cmdline; nproc; free -h | head -3
echo "=== 2. ALSA cards ==="
cat /proc/asound/cards; echo
cat /proc/asound/devices; echo
command -v aplay >/dev/null && { aplay -l; echo; arecord -l; } || echo "MISSING: alsa-utils"
echo "=== 3. PCM info por subdispositivo ==="
for f in /proc/asound/card*/pcm*/sub*/info; do echo "--- $f"; cat "$f"; echo; done
echo "=== 4. Kernel RT / audio ==="
uname -v; ls /sys/kernel/realtime 2>&1
lsmod | grep -E '^snd' || echo "no snd modules"
dmesg | grep -i -E 'snd|audio|usb.*audio|i2s|bcm2835|UAC' | head -30; echo
lsusb; echo
cat /boot/firmware/config.txt 2>/dev/null | grep -i -E 'audio|i2s|dac|dtoverlay' || cat /boot/config.txt 2>/dev/null | grep -i -E 'audio|i2s|dac|dtoverlay' || echo "no config.txt match"
```

- Si falta `aplay`/`arecord`, única dependencia de esta fase: `sudo apt update && sudo apt install -y alsa-utils` (rollback: `sudo apt remove alsa-utils`).
- Hipótesis a confirmar: con Pi 3 sin hardware USB/I2S enchufado, lo normal es ver solo `bcm2835 Headphones/HDMI` y **ningún dispositivo de captura**.
- Siguiente paso tras recibir la salida: elegir candidato `hw:X,Y` y test de loopback `arecord | aplay` para xruns/latencia.

## Estructura prevista del repo (aún no creada)

```text
micfx/
  README.md        # este fichero
  src/             # DSP en C/C++ (passthrough primero)
  config/          # fichero de parámetros
  systemd/         # unidad micfx.service
  tools/           # scripts de medición (xruns, latencia, CPU)
```
