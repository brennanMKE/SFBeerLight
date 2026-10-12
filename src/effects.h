#ifndef EFFECTS_H
#define EFFECTS_H

// The four renderers, and the palette they draw with.
//
// Copied from GlowLamp, which adapted GlowKitchen's effect engine for a ring
// of 8 behind a diffuser. SF Beer Light runs it on 14 LEDs.
//
// ===== Two kinds of effect =====
//
// GlowLamp's ring sits behind a diffuser, where several colors an inch apart
// mix into a muddy white. So its renderers -- blend, loop and neon -- paint
// every LED the same color on every frame, and differ only in how that one
// color moves over TIME. They are kept here unchanged.
//
// SF Beer Light is not behind a diffuser. Its 14 LEDs sit in a row along the
// bottom edge of a sheet of acrylic, and each one throws a fan of light up into
// the sheet that mostly lights the engraved letters in front of it. Neighbors
// overlap softly, so a gradient reads as a gradient rather than as dots, but
// the left of the sign and the right of it can genuinely be different colors.
// That puts position back on the table: drift, scan and pour all lay their
// color out across the sheet, which is exactly what GlowLamp had to give up.
//
// Those spatial renderers draw in LOGICAL order, index 0 at the left of the
// sign. main.cpp maps that onto the physical strip, which may run either way
// depending on how it was glued in -- see setLampReversed() in lamp.h.
//
// FLICKER lights LEDs several colors at once too, and on this sign the
// per-LED unevenness shows up as movement across the letters instead of being
// blurred into one guttering color. Give it reds and ambers; hand it five
// colors from opposite sides of the wheel and it will look like confetti.
//
// GlowKitchen's version of this header deliberately does not include
// <FastLED.h>, so its renderers can be compiled against a stub and diffed
// frame-for-frame by a native test. That contract costs every other includer a
// forced include order, and this project has no native test to pay it for --
// lamp.h needs MAX_COLORS, and settings.cpp needs lamp.h, neither of which has
// any business knowing about FastLED. So this header includes what it uses.
//
// ===== Why the palette is hue + saturation, not RGB =====
//
// 0.0.4 blended in RGB, and a blend from red (255,0,40) to azure (0,120,255)
// passes through (127,60,147) on the way -- a washed-out mauve that reads as
// dirty white on a WS2812B. Every RGB interpolation between two colors that
// are far apart on the wheel crosses the gray axis, because that is what a
// straight line through the middle of the color cube does.
//
// Interpolating hue instead, the short way around the wheel, never leaves full
// saturation: red to azure now travels through orange, yellow and green, all
// of them vivid. Saturation is carried per color and interpolated too, so a
// deliberately pale color stays pale, but nothing becomes pale by accident.
//
// Value is not stored at all. It belongs to the brightness setting, and a
// palette that also carried value would be fighting it.

#include <FastLED.h>
#include <stdint.h>

#include "every_n_millis.h"  // timeReached()

// Five is the ceiling the UI offers and the API enforces. Drift lays two legs
// of the palette across the sign, so more colors would only mean more of them
// off screen at any moment.
#define MAX_COLORS 5

// Sized above the 14 LEDs here so flicker's per-LED timers have room to spare,
// and do not have to be resized alongside a longer strip.
#define MAX_LEDS 16

struct PaletteColor {
    uint8_t h;
    uint8_t s;
};

enum EffectMode {
    EFFECT_BLEND = 0,    // holds on a color, eases to the next
    EFFECT_LOOP = 1,     // walks the palette steadily, never resting
    EFFECT_FLICKER = 2,  // several similar colors mixing, guttering like a flame
    EFFECT_NEON = 3,     // one color, full brightness, switching cleanly to the next
    EFFECT_DRIFT = 4,    // the palette laid across the sign, sliding sideways
    EFFECT_SCAN = 5,     // a bright band sweeping back and forth over a dim base
    EFFECT_POUR = 6,     // fills with the first color under a head of the second
    EFFECT_IGNITE = 7,   // neon that stutters to life on every change of color
    EFFECT_SPARKLE = 8,  // a slow blend with LEDs glinting toward white
    EFFECT_BREATHE = 9,  // one color, slowly pulsing, changing at the bottom
    EFFECT_COUNT = 10
};

// The wire form, and what the UI shows. Index matches EffectMode; appended,
// never inserted, because these values are what an API caller sends.
static const char *const EFFECT_NAMES[EFFECT_COUNT] = {
    "blend", "loop", "flicker", "neon", "drift", "scan", "pour", "ignite", "sparkle", "breathe"};

// Everything a renderer needs. One instance lives in main.cpp.
struct EffectState {
    CRGB *leds = nullptr;
    uint8_t numLeds = 0;

    PaletteColor colors[MAX_COLORS];
    uint8_t colorCount = 0;

    // BLEND and LOOP: milliseconds into the cycle, accumulated from elapsed
    // time rather than taken from millis() directly. The modulo form jumps at
    // the millis() rollover -- a visible color snap once every 49.7 days --
    // because the wrap point is not a multiple of the cycle.
    uint32_t phase = 0;

    // Flicker only: when each LED next picks a new brightness, and which palette
    // entry the sign is drifting through. A timer per LED is what puts them
    // deliberately out of step; no other effect varies brightness at all.
    uint32_t timeouts[MAX_LEDS];
    uint32_t hueTimeout = 0;
    uint8_t hueIndex = 0;

    // Ignite: which LEDs are lit while a tube strikes, and when each one stops
    // stuttering and holds. Reuses `timeouts` for the stutter itself, which is
    // safe because only one effect renders at a time and applyPalette() resets
    // them on every change.
    uint16_t litMask = 0;
    uint32_t settleAt[MAX_LEDS];
    uint8_t igniteLeg = 0xFF;  // the leg the strike was scheduled for
    uint32_t igniteInto = 0;   // how far into it the last frame was

    // Sparkle: how bright each LED's glint is, decaying a little every frame.
    uint8_t spark[MAX_LEDS] = {0};
};

// ===== Shared helpers =====

// Interpolate hue the short way around the wheel. Going the short way is what
// keeps a blend saturated: the long way around from red to azure would cross
// magenta, which is fine, but doing it in RGB crosses gray, which is not.
inline uint8_t blendHue(uint8_t from, uint8_t to, uint8_t frac) {
    uint8_t forward = (uint8_t)(to - from);
    uint8_t backward = (uint8_t)(from - to);
    if (forward <= backward) return (uint8_t)(from + (uint8_t)(((uint16_t)forward * frac) / 255));
    return (uint8_t)(from - (uint8_t)(((uint16_t)backward * frac) / 255));
}

inline CHSV blendColor(const PaletteColor &from, const PaletteColor &to, uint8_t frac) {
    return CHSV(blendHue(from.h, to.h, frac), lerp8by8(from.s, to.s, frac), 255);
}

// ===== BLEND =====
//
// The whole sign holds one color, eases to the next, and holds again. This is
// what the lamp has always done and what it comes back to; the only change is
// that the path between two colors now stays vivid.
//
// ease8InOutCubic is what makes it linger on the palette colors instead of
// sweeping past them -- a linear blend spends as little time on the pure
// colors as on the transition.
inline void renderBlend(EffectState &s, uint32_t blendMs) {
    uint8_t from = (s.phase / blendMs) % s.colorCount;
    uint8_t to = (uint8_t)((from + 1) % s.colorCount);

    // The multiply happens before the divide and in 32 bits, so a 6-second leg
    // does not lose resolution to integer truncation.
    uint8_t frac = (uint8_t)(((s.phase % blendMs) * 255UL) / blendMs);

    CHSV color = blendColor(s.colors[from], s.colors[to], ease8InOutCubic(frac));
    for (uint8_t i = 0; i < s.numLeds; i++) s.leds[i] = color;
}

// ===== LOOP =====
//
// The same palette as blend, walked at a steady rate and never resting. Blend
// eases, so it spends most of its time sitting on a palette color; loop is
// always mid-transition, so the sign is never quite the color you would name.
// That is the whole difference between them, and on a single-color lamp it is
// the only difference available: no easing, and a shorter leg.
inline void renderLoop(EffectState &s, uint32_t legMs) {
    uint8_t from = (s.phase / legMs) % s.colorCount;
    uint8_t to = (uint8_t)((from + 1) % s.colorCount);

    // Linear, deliberately. ease8InOutCubic() here would make this blend.
    uint8_t frac = (uint8_t)(((s.phase % legMs) * 255UL) / legMs);

    CHSV color = blendColor(s.colors[from], s.colors[to], frac);
    for (uint8_t i = 0; i < s.numLeds; i++) s.leds[i] = color;
}

// ===== FLICKER =====
//
// A flame. Each LED carries its own palette color and its own brightness, and
// picks a new brightness every 80-220 ms on its own schedule; the assignment
// drifts around the sign every second and a half, so no LED stays the one that
// is always red.
//
// This is the one renderer that lights the sign several colors at once, and it
// is built to be given similar ones. The unevenness IS the flame: LEDs in
// lockstep read as a lamp being dimmed, not as something burning.
inline void renderFlicker(EffectState &s, uint32_t now) {
    for (uint8_t i = 0; i < s.numLeds; i++) {
        // timeReached() rather than a plain compare: an absolute deadline test
        // strands the LED for 49.7 days if the loop blocks across the rollover.
        if (timeReached(now, s.timeouts[i])) {
            s.timeouts[i] = now + random16(80, 220);
            const PaletteColor &c = s.colors[(i + s.hueIndex) % s.colorCount];
            s.leds[i] = CHSV(c.h, c.s, (uint8_t)random16(130, 255));
        }
    }

    // Drifts which LED shows which color, so the sign does not settle into a
    // fixed pattern of stripes.
    if (timeReached(now, s.hueTimeout)) {
        s.hueTimeout = now + 1500;
        s.hueIndex = (uint8_t)((s.hueIndex + 1) % s.colorCount);
    }
}

// ===== NEON =====
//
// A lit tube: full brightness, holding a color, then a quick fade to the next.
// No brightness variation at all.
//
// This effect shipped twice with a flicker in it, on the theory that a stutter
// is what makes neon read as neon. On a strip of 240 LEDs that is true. On 8
// diffused LEDs it is the whole lamp going dim at once, which is not a tube
// struggling to strike -- it is a light with a fault. Softening it helped and
// did not fix it, because the problem was the idea, not the depth.
//
// What distinguishes it from blend is now the SHAPE of the change rather than
// any texture: blend is always drifting, easing through the whole leg and never
// quite still, while neon sits rock steady on a color and then switches. A neon
// sign does not fade up and down. It is on, in one color, until it is another.
inline void renderNeon(EffectState &s, uint32_t legMs, uint32_t fadeMs) {
    uint8_t from = (s.phase / legMs) % s.colorCount;
    uint8_t to = (uint8_t)((from + 1) % s.colorCount);

    // A leg is mostly hold, with the cross-fade at the end of it.
    uint32_t into = s.phase % legMs;
    uint32_t holdMs = legMs > fadeMs ? legMs - fadeMs : 0;
    uint8_t frac = into >= holdMs ? (uint8_t)(((into - holdMs) * 255UL) / fadeMs) : 0;

    CHSV color = blendColor(s.colors[from], s.colors[to], ease8InOutCubic(frac));
    for (uint8_t i = 0; i < s.numLeds; i++) s.leds[i] = color;
}

// ===== Spatial helpers =====

// The palette as one continuous loop of `legMs` per color, sampled at `u`
// milliseconds along it. Eased, so each color holds over a stretch of the sign
// and the hand-offs between them are short -- a word sits IN a color rather than
// always between two.
inline CHSV paletteAt(const EffectState &s, uint32_t u, uint32_t legMs) {
    uint8_t from = (u / legMs) % s.colorCount;
    uint8_t to = (uint8_t)((from + 1) % s.colorCount);
    uint8_t frac = (uint8_t)(((u % legMs) * 255UL) / legMs);
    return blendColor(s.colors[from], s.colors[to], ease8InOutCubic(frac));
}

// An LED's position across the sign in 1/256ths of an LED. The center, not
// the left edge, so a band or a fill line at position x lights the LED under it.
inline uint32_t ledCenter(uint8_t i) { return (uint32_t)i * 256 + 128; }

// ===== DRIFT =====
//
// Two legs of the palette laid across the width of the sign -- three colors in
// view, eased so each holds over a few letters -- and the whole thing sliding
// sideways by one leg every `legMs`. On blend the sign is one color that
// changes; on drift it is several, moving.
//
// Two legs and not the whole palette: five colors across 14 LEDs is under
// three LEDs each, which the acrylic smears into each other.
inline void renderDrift(EffectState &s, uint32_t legMs) {
    uint32_t span = legMs * 2;
    uint32_t cycle = legMs * s.colorCount;
    for (uint8_t i = 0; i < s.numLeds; i++) {
        // Higher index sits further BACK in the cycle, so colors enter at the
        // left and travel right.
        uint32_t offset = s.numLeds > 1 ? (span * i) / (s.numLeds - 1) : 0;
        uint32_t u = (s.phase + cycle - (offset % cycle)) % cycle;
        s.leds[i] = paletteAt(s, u, legMs);
    }
}

// ===== SCAN =====
//
// A band of light sweeping left to right and back over a dim wash of color,
// catching the letters in turn like a spotlight passing over them.
//
// The base is one palette color and the band the next, and both advance one
// color per round trip -- so with one color it is a brighter band of the same,
// and with two it is a bright accent over a dark ground, which is what it is
// for.
// Value as handed to CHSV, which FastLED's rainbow conversion squares on the
// way to RGB -- 60 came out as 14, a base too dark to read the letters by.
// 110 lands at about 47, a fifth of the band's brightness.
static const uint8_t SCAN_BASE_V = 110;
static const uint16_t SCAN_HALF_WIDTH = 640;  // 2.5 LEDs either side of center

inline void renderScan(EffectState &s, uint32_t passMs) {
    uint32_t trip = passMs * 2;
    uint8_t k = (s.phase / trip) % s.colorCount;
    const PaletteColor &base = s.colors[k];
    const PaletteColor &band = s.colors[(k + 1) % s.colorCount];

    // Eased at each end so the band slows and turns rather than bouncing.
    uint32_t into = s.phase % trip;
    uint32_t leg = into < passMs ? into : trip - into;
    uint8_t t = ease8InOutCubic((uint8_t)((leg * 255UL) / passMs));
    uint32_t width = (uint32_t)s.numLeds * 256;
    uint32_t center = ((width - 256) * t) / 255 + 128;

    for (uint8_t i = 0; i < s.numLeds; i++) {
        uint32_t x = ledCenter(i);
        uint32_t d = x > center ? x - center : center - x;
        // A smooth falloff, not a hard edge: 255 at the center, 0 at the
        // half-width, eased so the band has a body rather than a point.
        uint8_t w = d >= SCAN_HALF_WIDTH ? 0 : 255 - (uint8_t)((d * 255) / SCAN_HALF_WIDTH);
        w = ease8InOutCubic(w);
        s.leds[i] = CHSV(blendHue(base.h, band.h, w), lerp8by8(base.s, band.s, w),
                         lerp8by8(SCAN_BASE_V, 255, w));
    }
}

// ===== POUR =====
//
// The sign fills from the left with the first color under a head of the second
// -- amber and foam, given the Beer preset -- holds full, drains, and pours
// again. The head rides the leading edge as it fills and sits at the right end
// while it holds.
//
// With one color the head is a pale version of it, so pour still reads as a
// pour on someone else's palette.
static const uint32_t POUR_FILL_MS = 4000;
static const uint32_t POUR_HOLD_MS = 5000;
static const uint32_t POUR_DRAIN_MS = 2000;
static const uint32_t POUR_EMPTY_MS = 800;
static const uint32_t POUR_CYCLE_MS = POUR_FILL_MS + POUR_HOLD_MS + POUR_DRAIN_MS + POUR_EMPTY_MS;
static const uint16_t POUR_HEAD = 512;  // two LEDs of foam

inline void renderPour(EffectState &s) {
    uint32_t t = s.phase % POUR_CYCLE_MS;
    uint32_t width = (uint32_t)s.numLeds * 256;

    // How far across the liquid reaches, in 1/256ths of an LED, head included.
    uint32_t level;
    if (t < POUR_FILL_MS) {
        // Eased out: a pour starts fast and slows as the glass fills.
        uint8_t f = (uint8_t)((t * 255UL) / POUR_FILL_MS);
        level = (width * (255 - scale8(255 - f, 255 - f))) / 255;
    } else if (t < POUR_FILL_MS + POUR_HOLD_MS) {
        level = width;
    } else if (t < POUR_FILL_MS + POUR_HOLD_MS + POUR_DRAIN_MS) {
        uint32_t into = t - POUR_FILL_MS - POUR_HOLD_MS;
        level = width - (width * into) / POUR_DRAIN_MS;
    } else {
        level = 0;
    }

    const PaletteColor &beer = s.colors[0];
    PaletteColor head = s.colorCount > 1 ? s.colors[1] : PaletteColor{beer.h, (uint8_t)(beer.s / 4)};

    for (uint8_t i = 0; i < s.numLeds; i++) {
        uint32_t left = (uint32_t)i * 256;
        // How much of this LED's width is filled, so the line moves smoothly
        // through an LED rather than jumping from one to the next.
        uint32_t cover = level <= left ? 0 : (level - left >= 256 ? 256 : level - left);
        if (cover == 0) {
            s.leds[i] = CRGB::Black;
            continue;
        }
        uint8_t v = (uint8_t)((cover * 255) / 256);
        // Distance back from the surface decides head versus beer, blended
        // over half an LED so the two do not meet at a hard seam.
        uint32_t x = ledCenter(i);
        uint32_t depth = level > x ? level - x : 0;
        uint8_t h;
        if (depth <= POUR_HEAD) h = 255;
        else if (depth >= POUR_HEAD + 128) h = 0;
        else h = (uint8_t)(255 - ((depth - POUR_HEAD) * 255) / 128);
        s.leds[i] = CHSV(blendHue(beer.h, head.h, h), lerp8by8(beer.s, head.s, h), v);
    }
}

// ===== IGNITE =====
//
// Neon, as it actually behaves when it switches on: each new color does not
// fade in, it strikes. For the first part of a leg the tube buzzes -- LEDs
// cutting in and out on their own schedules -- and each one catches at its own
// moment and then holds, so the letters come on in a ragged order rather than
// all at once. Then it sits rock steady, like neon, until the next color.
//
// This is the stutter GlowLamp's neon had to drop: on a diffused ring it read
// as a fault. Across a sign it reads as a tube catching, because different
// letters stutter at different times.
static const uint32_t IGNITE_STRIKE_MS = 1400;

inline void renderIgnite(EffectState &s, uint32_t legMs, uint32_t now) {
    uint8_t leg = (s.phase / legMs) % s.colorCount;
    uint32_t into = s.phase % legMs;

    // A new leg: the old color is out, and every LED gets a moment to catch,
    // spread across the strike so the last few letters keep everyone waiting.
    // Going backwards within a leg counts too -- with one color the leg index
    // never changes, and the tube should still strike every time round.
    bool newLeg = leg != s.igniteLeg || into < s.igniteInto;
    s.igniteInto = into;
    if (newLeg) {
        s.igniteLeg = leg;
        s.litMask = 0;
        for (uint8_t i = 0; i < s.numLeds; i++) {
            s.settleAt[i] = now + random16(250, IGNITE_STRIKE_MS);
            s.timeouts[i] = now + random16(20, 120);
        }
    }

    const PaletteColor &c = s.colors[leg];
    for (uint8_t i = 0; i < s.numLeds; i++) {
        bool settled = into >= IGNITE_STRIKE_MS || timeReached(now, s.settleAt[i]);
        if (settled) {
            s.litMask |= (1u << i);
        } else if (timeReached(now, s.timeouts[i])) {
            // Short, irregular on/off bursts. Weighted toward off early so the
            // tube visibly struggles before it holds.
            s.timeouts[i] = now + random16(30, 140);
            if (random8() < 110) s.litMask |= (1u << i);
            else s.litMask &= ~(1u << i);
        }
        bool lit = s.litMask & (1u << i);
        s.leds[i] = lit ? CRGB(CHSV(c.h, c.s, settled ? 255 : 200)) : CRGB::Black;
    }
}

// ===== SPARKLE =====
//
// Blend underneath, a little dimmer, with single LEDs glinting toward white
// and fading back -- carbonation catching the light. Rare and brief on
// purpose: a sign that is mostly sparkle is a sign that is mostly noise.
static const uint8_t SPARKLE_BASE_V = 190;
static const uint8_t SPARKLE_CHANCE = 6;  // per frame, out of 255, across the strip
static const uint8_t SPARKLE_DECAY = 14;  // per frame

inline void renderSparkle(EffectState &s, uint32_t blendMs) {
    uint8_t from = (s.phase / blendMs) % s.colorCount;
    uint8_t to = (uint8_t)((from + 1) % s.colorCount);
    uint8_t frac = (uint8_t)(((s.phase % blendMs) * 255UL) / blendMs);
    CHSV base = blendColor(s.colors[from], s.colors[to], ease8InOutCubic(frac));

    if (random8() < SPARKLE_CHANCE) s.spark[random8(s.numLeds)] = 255;

    for (uint8_t i = 0; i < s.numLeds; i++) {
        uint8_t g = s.spark[i];
        s.spark[i] = g > SPARKLE_DECAY ? g - SPARKLE_DECAY : 0;
        // Desaturated and brightened together: a glint is white light, not a
        // brighter version of the color it lands on.
        s.leds[i] = CHSV(base.h, scale8(base.s, 255 - g), lerp8by8(SPARKLE_BASE_V, 255, g));
    }
}

// ===== BREATHE =====
//
// The whole sign one color, swelling and fading slowly, moving to the next
// color at the bottom of each breath where the change is least visible. Quiet
// enough to leave running all night.
//
// Never fully dark: a floor keeps the letters legible at the bottom of the
// breath, and stops a status check from reporting the sign as dark every few
// seconds.
//
// 110 rather than the 40 it shipped with. CHSV value is squared on the way to
// RGB, so 40 reached the strip as about 6 -- sampling /api/frame showed the
// sign bottoming out at 4 of 255, which is off in all but name. 110 squares
// to about 47.
static const uint8_t BREATHE_FLOOR = 110;

inline void renderBreathe(EffectState &s, uint32_t breathMs) {
    uint8_t k = (s.phase / breathMs) % s.colorCount;
    uint8_t t = (uint8_t)(((s.phase % breathMs) * 255UL) / breathMs);
    // cubicwave8 is 0 at both ends of the breath and 255 in the middle, so the
    // color change at the leg boundary always happens at the floor.
    uint8_t v = lerp8by8(BREATHE_FLOOR, 255, cubicwave8(t));
    const PaletteColor &c = s.colors[k];
    fill_solid(s.leds, s.numLeds, CHSV(c.h, c.s, v));
}

#endif  // EFFECTS_H
