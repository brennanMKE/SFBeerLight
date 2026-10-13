# SF Beer Light

A strip of 14 addressable LEDs that slowly blends between five vibrant colors,
driven by an ESP32-C3. It runs [Glow Lamp](../GlowLamp)'s firmware under its own
name: WiFi is provisioned through a captive portal, it is controlled from a web
UI, a REST API and MQTT, and it updates itself from GitHub releases.

The strip is one light, not fourteen pixels — every LED shows the same color —
and it lights up on power-on with no network at all.

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

The light needs no network to light up. By default it walks the palette in
order, easing in and out of each color so it lingers on the pure colors rather
than sweeping past them. One full cycle is `BLEND_MS × 5` — 30 seconds at the
default.

Colors blend in hue, the short way around the wheel, rather than in RGB. A
straight RGB blend between two distant colors passes through gray, which reads
as dirty white on a WS2812B; a hue blend stays vivid the whole way.

### Palette

Saturated and spaced 40–56 apart in hue on purpose: WS2812B washes pastels out
to near-white, and two neighboring hues read as one slowly shifting color
instead of as a cycle.

| Color | Hue (0–255) |
|---|---|
| Red | 0 |
| Gold | 40 |
| Green | 96 |
| Azure | 150 |
| Magenta | 200 |

## Tuning

The constants at the top of `src/main.cpp`:

| Constant | Default | Meaning |
|---|---|---|
| `DATA_PIN` | 4 | GPIO driving the strip |
| `NUM_LEDS` | 14 | LEDs in the strip (up to `MAX_LEDS`, 16, in `src/effects.h`) |
| `DEFAULT_PALETTE` | 5 colors | The default effect's colors, as hue and saturation |
| `BLEND_MS` | 6000 | Milliseconds to cross from one color to the next |
| `FRAME_MS` | 16 | ~60 fps |
| `DEFAULT_BRIGHTNESS` | 64 | Until the Settings page saves one |

Brightness, power, the names and the MQTT broker live in NVS and are set from
the web UI or the REST API. Effects and colors are set the same way but are
temporary by design — see [Effects](#effects).

## WiFi setup

WiFi is handled by [EasyWiFi](https://github.com/brennanMKE/EasyWiFi).
Credentials are never compiled into the firmware, so a release binary is safe to
publish and any board can be re-provisioned in the field.

1. Power on an unprovisioned board. It raises an access point named
   `sf-beer-light-Setup-XXXXXX`.
2. Join it; the captive portal opens.
3. Pick the network, enter the password, save. The light reboots and joins.

Once joined it announces `_sfbeerlight._tcp` over mDNS:

```sh
./scripts/find_devices.sh
```

That prints the `.local` address and a direct link to the Settings page. The web
UI lives at:

| Path | What |
|---|---|
| `/` | Status, and the controls: on/off, brightness, identify |
| `/effects` | The everyday page: effect, colors, presets |
| `/settings` | Firmware check and update, names, MQTT broker, reboot |
| `/logs` | Diagnostics: why the strip is dark, reset reason, heap, and the device log |
| `/help` | The REST API, documented by the light itself |
| `/api` | The same reference as JSON |
| `/wifi` | EasyWiFi's own setup pages |

## Building

```sh
pio run                      # release, the build that ships
pio run -e esp32c3-debug     # verbose logging, USB only
pio run -t upload            # flash over USB
pio device monitor           # 115200
```

Copy `lib/Config/Config.h.sample` to `lib/Config/Config.h` before the first
build — it is gitignored and holds the device name and the OTA repo. There are
no secrets in it.

## Controlling a light

Everything the web UI does is a REST call, so a script can do the same:

```sh
curl http://sf-beer-light-051860.local/api/status
curl -X POST http://sf-beer-light-051860.local/api/power -d '{"on":false}'
curl -X POST http://sf-beer-light-051860.local/api/brightness -d '{"value":128}'
curl -X POST http://sf-beer-light-051860.local/api/identify -d '{"seconds":4}'
```

Values are accepted as a JSON body, a form field or a query parameter, and every
mutating call answers with the same object `GET /api/status` returns — the new
state never needs a second request. Anything that changes the light is POST only,
so nothing a browser can prefetch can switch a light off or reflash it. There is
no authentication: these are LAN devices, and anything that can reach one can
control it.

A JSON body is only read when the request sets
`Content-Type: application/json` — curl's default is form-encoded, and the HTTP
server discards a JSON document sent that way before the firmware sees it. Query
parameters never have that problem, which is why they lead here.

[docs/rest-api.md](docs/rest-api.md) is the full reference.
`http://<light>/help` is the same thing served by the light, so it always
describes the firmware actually running, and `/api` is that as JSON for an agent
that would rather not read HTML.

### Alerts

```sh
mosquitto_pub -h broker -t sfbeerlight/all/alert -m '{"seconds": 30}'
./scripts/sfbeerlight.py alert --all --seconds 30
curl -X POST 'http://castor-light.local/api/alert?seconds=30'
```

A red pulse to get attention, for Home Assistant to fire at something that needs
a person. It overrides the effect, the brightness and the power state, then puts
all three back, and always expires. See [docs/mqtt.md](docs/mqtt.md#subscribe-sfbeerlighthostnamealert-and-sfbeerlightallalert).

### Quarter-hour chime

```sh
mosquitto_pub -h broker -t sfbeerlight/all/chime -m '{"minute": 15}'
curl -X POST 'http://castor-light.local/api/chime?minute=15'
```

Home Assistant publishes this on the hour and at 15, 30 and 45 past. The light
swells brightness smoothly up to full and back about once a second for 15 seconds,
keeping its effect and colors. See [docs/mqtt.md](docs/mqtt.md#subscribe-sfbeerlightallchime-and-sfbeerlighthostnamechime).

### Effects

```sh
curl -X POST 'http://sf-beer-light-051860.local/api/effect?effect=neon&colors=ff0000,00ff00,0000ff'
curl -X POST http://sf-beer-light-051860.local/api/effect/reset
```

| Effect | What it does |
|---|---|
| `blend` | The whole strip holds one color and eases to the next. The default. |
| `loop` | Walks the palette steadily, never resting on a color. |
| `flicker` | Similar colors mixing and guttering, like a flame. Give it reds and ambers. |
| `neon` | One color at full brightness, held steady, switching cleanly to the next. |
| `drift` | The palette laid across the sign, three colors at a time, sliding left to right. |
| `scan` | A bright band of the next color sweeping back and forth over a dim base. Best with one or two colors. |
| `pour` | Fills from the left (the glass end) with the first color under a head of the second, holds, drains, pours again. Use the Beer preset: `ff9000,fff0c8`. |
| `ignite` | Neon that stutters to life letter by letter on every change of color, then holds steady. |
| `sparkle` | A slow blend with single LEDs glinting toward white, like carbonation. |
| `breathe` | One color slowly swelling and fading, changing color at the bottom of each breath. |

Up to 5 colors. An effect reverts to the default after 5 minutes unless you say
otherwise, is capped at 8 hours, and does not survive a reboot — see
[docs/rest-api.md](docs/rest-api.md#effects) for why that is the default.

`drift`, `scan` and `pour` run left to right across the sign. If the strip was
glued in running right to left, use **Show LED order** on the Settings page
(red is LED 0) and tick the box if red is on the right. From a script:
`POST /api/strip/test`, then `POST /api/strip -d 'reversed=1'`.

### Identify

`POST /api/identify` blinks the strip white for a few seconds at a floor
brightness, so a light dimmed to 5 in a bright room still announces itself. It
overrides power and restores whatever was showing, so identifying a light that is
switched off leaves it switched off.

### Names

A light has two:

| | |
|---|---|
| **Name** | What you call it — "Living Room". Free text, announced over mDNS, takes effect immediately. Nothing is addressed by it. |
| **Hostname** | The mDNS label. Lowercase, digits and hyphens. The light answers at `<hostname>-<mac>.local`, and its setup AP is named from it. Needs a reboot. |

## Home Assistant

Set an MQTT broker on the light and it appears as a light entity by itself — with
brightness, a color picker and the effect list — with nothing added to
`configuration.yaml` and no restart:

```sh
./scripts/sfbeerlight.py broker 192.168.1.10 --host castor-light.local
```

See [docs/mqtt.md](docs/mqtt.md) for the topics and commands, and
[docs/home-assistant-control.md](docs/home-assistant-control.md) for why MQTT
rather than REST from Home Assistant. MQTT is optional: a light with no broker
never connects and is fully usable over REST.

`scripts/sfbeerlight.py` finds the lights and drives them over REST, using only the
standard library so it runs under Home Assistant's Python:

```sh
./scripts/sfbeerlight.py discover
./scripts/sfbeerlight.py off --all
./scripts/sfbeerlight.py brightness 128 --name "Living Room"
./scripts/sfbeerlight.py identify --host sf-beer-light-051860.local
```

See [docs/home-assistant.md](docs/home-assistant.md) for the REST switch and
`shell_command` configuration, including turning every light off with the rest of
the house.

[docs/home-assistant-control.md](docs/home-assistant-control.md) covers what
Home Assistant can and cannot do with a light, what each way in costs, and why
MQTT is the recommended route.

## Finding the lights

```sh
./scripts/find_devices.sh            # scan and report
./scripts/find_devices.sh --check    # ask each light to check GitHub first
./scripts/find_devices.sh --update   # install on every light with an update
```

Each light announces `_sfbeerlight._tcp` over mDNS, so the scan finds them without
knowing an address. For each one it prints the URL, the resolved IP, the running
firmware, the latest release that light knows about, and whether an update is
waiting.

Names are resolved through the mDNS responder rather than left to curl. A cold
mDNS cache regularly takes longer to answer than curl's timeout, which made the
first scan after a reboot report every light as unreachable and the second one
work — indistinguishable from a flaky light, and not the light's fault.

An unprovisioned light is not on the network at all and will not appear here.
Look for its setup AP instead.

## OTA updates

A light can find out about a new release three ways:

| Trigger | What it does |
|---|---|
| 15 s after joining WiFi | Checks, and installs if the tag differs |
| Daily at 15:00 local | Same |
| `POST /api/ota/update` | Same, on someone else's schedule |
| The settings page, or `POST /api/ota/check` | Checks only, and reports |

The first two are unattended, so they install on their own — nobody is watching,
and there is no point holding an update back for an empty room. A check you
asked for never installs anything: it tells you what is available and offers a
button, so you can see what you are about to take.

Mid-afternoon is deliberate rather than overnight: an update that goes wrong
reboots the light, and 15:00 is when someone is around to notice. There is no
rollback, so the hour is the only safety margin.

Comparison is by tag equality, not ordering, so re-tagging an older release is a
supported way to roll back — and a light running firmware *newer* than the latest
release will install the older one. Cut the release before flashing a new
version by hand, or the next check quietly undoes it.

### From a script

Both endpoints answer JSON and take POST, never GET — a link a browser can
prefetch should not be able to reflash a light.

```sh
curl -X POST http://sf-beer-light-051860.local/api/ota/check    # ask GitHub
curl -s http://sf-beer-light-051860.local/api/status            # read the answer
curl -X POST http://sf-beer-light-051860.local/api/ota/install  # take it

curl -X POST http://sf-beer-light-051860.local/api/ota/update   # both, if there is one
```

The last one is the scheduler's endpoint: it checks, installs only if the
release differs, and does nothing at all on a light that is current. Home
Assistant can fire it at every light nightly — see
[docs/home-assistant.md](docs/home-assistant.md).

`status.json` carries the firmware block the scan and the status page both read:

```json
{"version":"0.0.2",
 "ota":{"state":"idle","latest":"0.0.3","available":true,"checked":12,"error":""}}
```

`state` is `idle`, `checking`, `installing` or `error`, and `checked` is seconds
since the last completed check (`-1` if none this boot).

While a light installs, it answers nothing — the download blocks its loop for
10–30 s and the strip holds its last color — and then it reboots on the new
version. The status page says so before it starts and recovers on its own.

### Cutting a release

```sh
# bump FIRMWARE_VERSION in src/version.h first, and commit
./scripts/release.sh 0.0.3
./scripts/find_devices.sh --update     # or let them find it themselves
```

The script refuses to tag if the version does not match `src/version.h`, if the
working tree is dirty, or if the binary would overflow the 1,966,080-byte OTA
slot — a build that overflows uploads fine over USB and then fails silently over
the air.

## Docs

| | |
|---|---|
| [docs/rest-api.md](docs/rest-api.md) | The REST API: endpoints, status fields, effects |
| [docs/mqtt.md](docs/mqtt.md) | The MQTT message API: topics, payloads, discovery |
| [docs/home-assistant.md](docs/home-assistant.md) | Home Assistant configuration |
| [docs/home-assistant-control.md](docs/home-assistant-control.md) | Why MQTT rather than REST from Home Assistant |

Each light also serves its own REST reference at `http://<light>/help`, and the
same thing as JSON at `/api` — those describe the firmware actually running.
