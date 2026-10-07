# SF Beer Light

A strip of 14 addressable LEDs that slowly blends between five vibrant colors,
driven by an ESP32-C3. It is [Glow Lamp](../GlowLamp)'s light engine with
everything networked taken out: no WiFi, no REST API, no MQTT, no OTA. Plug it
in and it glows.

The strip is one light, not fourteen pixels — every LED shows the same color —
and the only thing it does is cycle.

## Hardware

| Part | Detail |
|---|---|
| Board | ESP32-C3 (`esp32-c3-devkitm-1`) |
| Strip | WS2812B, 14 LEDs |
| Data pin | GPIO 4 |
| Power | 5 V, capped in firmware at 500 mA |

Wire the strip's data line to GPIO 4 and share ground between the strip's supply
and the ESP32-C3. At the default brightness of 64 the 14 LEDs are well inside
the 500 mA cap, so USB power is fine; run it near 255 and you will want a
separate 5 V supply.

## Behavior

The light holds no state and needs no network. It walks the palette in order,
easing in and out of each color so it lingers on the pure colors rather than
sweeping past them. One full cycle is `BLEND_MS × 5` — 30 seconds at the
default.

Colors blend in hue, the short way around the wheel, rather than in RGB. A
straight RGB blend between two distant colors passes through gray, which reads
as dirty white on a WS2812B; a hue blend stays vivid the whole way.

### Palette

The same five colors as Glow Lamp. Saturated and spaced 40–56 apart in hue on
purpose: WS2812B washes pastels out to near-white, and two neighboring hues read
as one slowly shifting color instead of as a cycle.

| Color | Hue (0–255) |
|---|---|
| Red | 0 |
| Gold | 40 |
| Green | 96 |
| Azure | 150 |
| Magenta | 200 |

### Effects

All four of Glow Lamp's effects are compiled in. With no network there is
nothing to switch between them at runtime, so the effect is chosen by
`EFFECT_MODE` in `src/main.cpp` and takes a reflash to change.

| Effect | What it does |
|---|---|
| `EFFECT_BLEND` | The whole strip holds one color and eases to the next. The default. |
| `EFFECT_LOOP` | Walks the palette steadily, never resting on a color. |
| `EFFECT_FLICKER` | Similar colors mixing and guttering, like a flame. The one effect that lights the strip several colors at once — give it reds and ambers, not the full palette. |
| `EFFECT_NEON` | One color at full brightness, held steady, switching cleanly to the next. |

## Tuning

The constants at the top of `src/main.cpp`:

| Constant | Default | Meaning |
|---|---|---|
| `DATA_PIN` | 4 | GPIO driving the strip |
| `NUM_LEDS` | 14 | LEDs in the strip (up to `MAX_LEDS`, 16, in `src/effects.h`) |
| `EFFECT_MODE` | `EFFECT_BLEND` | Which effect runs |
| `PALETTE` | 5 colors | What it cycles through, as hue and saturation (up to 5) |
| `BLEND_MS` | 6000 | Milliseconds to cross from one color to the next in blend |
| `LOOP_LEG_MS` | 2500 | One leg of loop |
| `NEON_LEG_MS` / `NEON_FADE_MS` | 7000 / 700 | Neon's hold-plus-fade, and the fade at its end |
| `FRAME_MS` | 16 | ~60 fps |
| `BRIGHTNESS` | 64 | 0–255 |

Everything is a compile-time constant; there are no settings stored on the
device.

## Building

```sh
pio run                      # build
pio run -t upload            # flash over USB
pio device monitor           # 115200
```

On boot the serial log prints the version and configuration:

```
=== SF Beer Light 0.0.1 ===
[I][main.cpp] setup(): [BEER] 14 LEDs on pin 4, effect blend, 5 colors
```
