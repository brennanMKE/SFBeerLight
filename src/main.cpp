#include <Arduino.h>
#include <FastLED.h>
#include <esp_log.h>

#include "every_n_millis.h"
#include "effects.h"

static const char *TAG = "BEER";

#define FIRMWARE_VERSION "0.0.1"

// ===== Strip layout =====
// 14 WS2812B LEDs. Like GlowLamp, the strip is one light, not 14 pixels: blend,
// loop and neon light every LED the same color at a time. See effects.h.
#define DATA_PIN 4
#define NUM_LEDS 14

// ===== Effect =====
// There is no network here, so nothing changes the effect at runtime. Pick one
// of EFFECT_BLEND, EFFECT_LOOP, EFFECT_FLICKER or EFFECT_NEON and rebuild.
static const uint8_t EFFECT_MODE = EFFECT_BLEND;

// ===== Palette =====
// GlowLamp's five defaults, as hue and saturation. Spaced 40-56 apart around
// the wheel so no two read as the same color, and deliberately no white, amber
// or pastel, which wash out on a WS2812B.
static const PaletteColor PALETTE[] = {
    {0, 255},    // red
    {40, 255},   // gold
    {96, 255},   // green
    {150, 255},  // azure
    {200, 255},  // magenta
};
static const uint8_t PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);

// ===== Timing =====
// Same values as GlowLamp. A full blend cycle is BLEND_MS * PALETTE_SIZE --
// 30 seconds on the five colors.
static const uint32_t BLEND_MS = 6000;
static const uint32_t LOOP_LEG_MS = 2500;
static const uint32_t NEON_LEG_MS = 7000;
static const uint32_t NEON_FADE_MS = 700;

// ~60 fps. Each show() disables interrupts for roughly 30 us per LED.
static const uint32_t FRAME_MS = 16;

// At 64 the 14 LEDs are well inside the 500 mA cap, so USB power is fine.
static const uint8_t BRIGHTNESS = 64;

CRGB leds[NUM_LEDS];

static EffectState fx;

static void loopLeds() {
    static uint32_t lastFrame = 0;

    uint32_t now = millis();
    uint32_t delta = now - lastFrame;
    if (delta < FRAME_MS) return;
    lastFrame = now;

    // Phase accumulates from elapsed time rather than millis() % cycle, which
    // would snap colors at the millis() rollover every 49.7 days.
    fx.phase += delta;

    switch (EFFECT_MODE) {
        case EFFECT_LOOP:
            fx.phase %= LOOP_LEG_MS * fx.colorCount;
            renderLoop(fx, LOOP_LEG_MS);
            break;
        case EFFECT_FLICKER:
            renderFlicker(fx, now);
            break;
        case EFFECT_NEON:
            fx.phase %= NEON_LEG_MS * fx.colorCount;
            renderNeon(fx, NEON_LEG_MS, NEON_FADE_MS);
            break;
        case EFFECT_BLEND:
        default:
            fx.phase %= BLEND_MS * fx.colorCount;
            renderBlend(fx, BLEND_MS);
            break;
    }

    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    delay(500);  // let USB CDC come up before the first log line

    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);  // volts, mA
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);

    fx.leds = leds;
    fx.numLeds = NUM_LEDS;
    fx.colorCount = PALETTE_SIZE;
    for (uint8_t i = 0; i < PALETTE_SIZE; i++) fx.colors[i] = PALETTE[i];
    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_LEDS; i++) fx.timeouts[i] = now;
    fx.hueTimeout = now + 3000;

    Serial.println("\n=== SF Beer Light " FIRMWARE_VERSION " ===");
    ESP_LOGI(TAG, "%d LEDs on pin %d, effect %s, %d colors", NUM_LEDS, DATA_PIN,
             EFFECT_NAMES[EFFECT_MODE], PALETTE_SIZE);
}

void loop() { loopLeds(); }
