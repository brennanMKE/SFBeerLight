# MQTT message API

The complete pub/sub interface. [home-assistant-control.md](home-assistant-control.md)
is the reasoning for having it; the short version is that MQTT is the only
option where **Home Assistant needs nothing done to it** — no YAML, no restart,
no filesystem access to the Home Assistant host.

MQTT is optional and off by default. A light with no broker configured never
connects and is fully usable over [REST](rest-api.md), which stays the interface
for `sfbeerlight.py`, identify, and OTA.

## Enabling

Settings page → **Home Assistant** → broker address. Or:

```sh
./scripts/sfbeerlight.py broker 192.168.1.10 --host castor-light.local
curl -X POST 'http://castor-light.local/api/broker?host=192.168.1.10&port=1883'
curl -X POST 'http://castor-light.local/api/broker?host='        # turns MQTT off
```

Takes effect immediately, no reboot. The stored password is never echoed back in
`/api/status`, and sending an empty one leaves it alone, so the broker address
can be changed without knowing the password.

The light appears in Home Assistant within a second or two as `light.<name>`.

## Connection

| | |
|---|---|
| Client ID | `sfbeerlight_<mac>`, e.g. `sfbeerlight_44b176051860` |
| Auth | Optional username/password; anonymous when no username is set |
| TLS | **None.** Plain MQTT on the LAN, like the REST API |
| QoS | 0, publish and subscribe |
| Keepalive | PubSubClient's default, 15 s |
| Reconnect | One attempt every 5 s, never blocking the LED loop |
| Max packet | 1024 bytes, set on every connect |

A light reconnects on its own after a broker restart and republishes discovery,
availability and state each time — so a broker without persistence, which loses
its retained messages on restart, recovers without anyone touching the lights.

## Topics

`<hostname>` is the light's mDNS hostname, e.g. `castor-light`. `<mac>` is its MAC
with no separators.

| Topic | Direction | Retained | Payload |
|---|---|---|---|
| `homeassistant/light/sfbeerlight_<mac>/config` | publish | **yes** | Discovery config |
| `sfbeerlight/<hostname>/state` | publish | **yes** | Current state |
| `sfbeerlight/<hostname>/availability` | publish | **yes** | `online` / `offline` |
| `sfbeerlight/<hostname>/set` | **subscribe** | **never** | Command |
| `sfbeerlight/<hostname>/alert` | **subscribe** | **never** | Fire or clear an alert |
| `sfbeerlight/all/alert` | **subscribe** | **never** | The same, to every light at once |
| `sfbeerlight/<hostname>/alert/state` | publish | no | `on` / `off` while an alert runs |
| `sfbeerlight/<hostname>/chime` | **subscribe** | **never** | Quarter-hour chime |
| `sfbeerlight/all/chime` | **subscribe** | **never** | The same, to every light at once |

**Never retain a command.** A retained command replays on every reconnect, which
pins the light to whatever was last sent and makes it impossible to control from
anywhere else. This is not hypothetical — it is the stale-brightness bug already
diagnosed on the GlowKitchen strips, where retained messages kept resetting the
fleet. Discovery, state and availability are retained on purpose, so Home
Assistant knows the light the moment it restarts.

## Subscribe: `sfbeerlight/<hostname>/set`

Home Assistant's JSON light schema. Any subset of the fields, in one message.

```json
{"state": "ON", "brightness": 200, "color": {"r": 255, "g": 0, "b": 0}, "effect": "neon"}
```

| Field | Type | Meaning |
|---|---|---|
| `state` | `"ON"` / `"OFF"` | Power. Persisted, so it survives a power cut. |
| `brightness` | 0–255 | Persisted. Independent of power. |
| `effect` | `blend`, `loop`, `flicker`, `neon`, `drift`, `scan`, `pour`, `ignite`, `sparkle`, `breathe` | Keeps the current palette unless `colors` or `color` is also sent. |
| `colors` | array of 1–5 `"#rrggbb"` strings | **Replaces** the palette. Same format as `/api/effect`; the `#` is optional. Wins over `color`. |
| `color` | `{"r":,"g":,"b":}` | **Replaces** the palette with this one color. What Home Assistant's color picker sends. |

```sh
mosquitto_pub -h broker -t sfbeerlight/castor-light/set -m '{"state":"OFF"}'
mosquitto_pub -h broker -t sfbeerlight/castor-light/set -m '{"state":"ON","brightness":200}'
mosquitto_pub -h broker -t sfbeerlight/castor-light/set -m '{"effect":"flicker"}'
mosquitto_pub -h broker -t sfbeerlight/castor-light/set -m '{"color":{"r":0,"g":0,"b":255}}'
mosquitto_pub -h broker -t sfbeerlight/castor-light/set -m '{"effect":"pour","colors":["#ff9000","#fff0c8"]}'
```

Ordering within a message: `brightness` first, then `colors`, `color` and `effect`
together, then `state`. So "turn on and set a color" in one message ends up on,
whichever order the fields were written in.

A state publish follows every accepted command, so a caller never has to ask
what happened.

### Validation

Every field is validated independently, and anything unusable is ignored rather
than partially applied. A bad field never prevents the good fields in the same
message from landing.

| Sent | Result |
|---|---|
| Malformed JSON | Whole message ignored, logged |
| `{}` | Nothing changes |
| `{"state": true}` | **Ignored** — see below |
| `{"effect": "chase"}` | Effect unchanged; other fields still apply |
| `{"brightness": 999}` | Brightness unchanged; other fields still apply |
| `{"colors": ["#ff9000", "nothex"]}` | Palette unchanged (no half-applied palette), logged; other fields still apply |
| `{"colors": [six colors]}` | First five used, the rest dropped — as over REST |

`state` must be the **word** `"ON"` or `"OFF"`, not a boolean. `{"state": true}`
is accepted silently and never turns the light on, which is a miserable thing to
debug — it is the single most common mistake with the JSON light schema.

### Not supported

`transition`, `flash`, `color_temp`, `white`, `hs`/`xy` color, effect
parameters (speed, intensity), and an expiry: an effect set over MQTT runs until
the next one or a reboot, never reverting on its own the way `/api/effect` does.

`colors` is outside Home Assistant's light schema. The entity's color picker
still sends one `color`; send `colors` from an automation with `mqtt.publish`.

## Subscribe: `sfbeerlight/<hostname>/alert` and `sfbeerlight/all/alert`

A red pulse loud enough to be noticed from across a room, for firing at
something that needs a person: a door left open, a cycle finished, a sensor gone
quiet.

```sh
mosquitto_pub -h broker -t sfbeerlight/all/alert -m '{"seconds": 30}'
mosquitto_pub -h broker -t sfbeerlight/all/alert -m '60'        # a bare number works
mosquitto_pub -h broker -t sfbeerlight/all/alert -m ''          # empty: the 30s default
mosquitto_pub -h broker -t sfbeerlight/all/alert -m 'off'       # clear it now
```

The payload is deliberately forgiving. An alert is fired from an automation in a
hurry, and the difference between `{"seconds":30}` and an empty message should
not decide whether anyone is warned.

**`sfbeerlight/all/alert` reaches every light with one publish.** Firing an alert
almost never means "that light" — it means "whoever is in the building" — and a
broadcast keeps an automation from naming each light and being edited when a
third one arrives.

### What an alert overrides

An alert is an overlay, not an effect. It overrides:

* the running **effect**,
* the **brightness** setting — it runs at full, because an alert nobody can see
  at brightness 5 is not an alert,
* the **power state** — a light switched off is exactly the one an alert needs to
  reach.

When it ends, all three go back to exactly what they were, including being off.

**It always expires.** 30 seconds by default, one hour at the ceiling. An alert
that stays on forever stops being an alert, and a light stuck red because a
broker went down is worse than no alert at all.

Identify wins over an alert, because someone standing at the light pressing
identify can see the alert on it either way.

### From Home Assistant

`mqtt.publish` already exists wherever the MQTT integration is set up, so an
alert needs no `rest_command`, no YAML-only integration, and no restart:

```yaml
script:
  alert_lamps:
    alias: Flash the lights red
    sequence:
      - action: mqtt.publish
        data:
          topic: sfbeerlight/all/alert
          payload: '{"seconds": 30}'

automation:
  - alias: Warn when the back door is left open
    triggers:
      - trigger: state
        entity_id: binary_sensor.back_door
        to: "on"
        for: "00:05:00"
    actions:
      - action: mqtt.publish
        data:
          topic: sfbeerlight/all/alert
          payload: '{"seconds": 60}'
```

To stop one early — the door was closed, the thing was dealt with:

```yaml
      - action: mqtt.publish
        data:
          topic: sfbeerlight/all/alert
          payload: "off"
```

### Testing it

From any machine that can reach the broker:

```sh
mosquitto_pub -h <broker> -u <user> -P <pass> -t sfbeerlight/all/alert -m '{"seconds": 15}'
```

Both lights should pulse red within a second, at full brightness, whatever they
were showing and **even if they are switched off**. They return to exactly what
they were doing when it expires.

If nothing happens, check in this order:

1. `curl http://castor-light.local/api/status` — is `mqtt.connected` true? If not,
   `mqtt.error` says why.
2. `mosquitto_sub -h <broker> -u <user> -P <pass> -t 'sfbeerlight/#' -v` in another
   terminal — does the publish appear at all?
3. Fire it over REST instead: `curl -X POST 'http://castor-light.local/api/alert?seconds=15'`.
   If that pulses and the MQTT publish does not, the light is fine and the broker
   path is the problem.

### Reading alert state

`sfbeerlight/<hostname>/alert/state` carries `on` or `off`, for a binary sensor:

```yaml
mqtt:
  binary_sensor:
    - name: Castor alerting
      state_topic: sfbeerlight/castor-light/alert/state
      payload_on: "on"
      payload_off: "off"
```

It is **not retained**: a retained `on` would come back after a broker restart
and describe an alert that finished hours ago.

## Subscribe: `sfbeerlight/all/chime` and `sfbeerlight/<hostname>/chime`

The quarter-hour chime. Home Assistant publishes one message **on the hour and at
15, 30 and 45 past**, and every light surges **25% brighter for 15 seconds**, then
settles back. The running effect and its colors carry on underneath untouched —
only brightness moves.

```sh
mosquitto_pub -h broker -t sfbeerlight/all/chime -m '{"minute": 0}'    # on the hour
mosquitto_pub -h broker -t sfbeerlight/all/chime -m '{"minute": 15}'   # quarter past
mosquitto_pub -h broker -t sfbeerlight/all/chime -m '{"minute": 30}'   # half past
mosquitto_pub -h broker -t sfbeerlight/all/chime -m '{"minute": 45}'   # quarter to
mosquitto_pub -h broker -t sfbeerlight/all/chime -m '30'               # a bare number works
mosquitto_pub -h broker -t sfbeerlight/all/chime -m ''                 # so does nothing
```

### Payload

| Payload | Result |
|---|---|
| `{"minute": 0}` / `15` / `30` / `45` | Chime, recorded as that mark |
| A bare number, e.g. `15` | The same |
| Empty, or anything unreadable | Chime anyway, with no mark (`minute: -1`) |

`minute` is optional and does not change the surge today — every mark gets the
same response. It is logged (`chime for :15` in `/api/log`) and reported in
`/api/status` as `chime.minute`, and it is there so the marks can be given
different looks later without anything changing on the Home Assistant side.
**Send it**, so that later firmware can tell the marks apart.

Any value 0–59 is accepted; outside that range it counts as no mark.

### The surge

| | |
|---|---|
| Length | 15 s |
| Peak | Configured brightness + 25%, capped at 255 |
| Shape | Eases up over 1.2 s, holds, eases down over the last 2.5 s |

25% is relative to the light's own brightness: at 150 it peaks at 188, at 64 it
peaks at 80. A light at 255 is already at the ceiling and shows no surge; a light
at 0 stays at 0.

* **It respects power.** A light switched off stays off — a chime is ambient, not
  a summons. (Contrast the alert, which overrides power.)
* **It never changes the brightness setting.** Nothing is persisted and the state
  topic does not change; Home Assistant sees no brightness change.
* **A second chime restarts it** rather than stacking, so two automations firing on
  the same mark look like one chime.
* **Alert and identify win.** A chime that lands during either is hidden by it,
  and resumes for whatever is left of its 15 seconds if they end first.

Messages must **not be retained**. A retained chime would replay every time a
light reconnects to the broker, surging at a random time.

### From Home Assistant

One automation, a time-pattern trigger on minutes divisible by 15:

```yaml
automation:
  - alias: SF Beer Light quarter-hour chime
    mode: single
    triggers:
      - trigger: time_pattern
        minutes: "/15"
    actions:
      - action: mqtt.publish
        data:
          topic: sfbeerlight/all/chime
          payload: '{"minute": {{ now().minute }}}'
          retain: false
```

`time_pattern` with `minutes: "/15"` fires at :00, :15, :30 and :45. Add a
condition (e.g. a time window) if the chime should only run during opening hours.

### Testing it

```sh
mosquitto_pub -h <broker> -u <user> -P <pass> -t sfbeerlight/all/chime -m '{"minute": 15}'
curl -X POST 'http://sf-beer-light.local/api/chime?minute=15'   # no broker needed
curl -s http://sf-beer-light.local/api/status | grep -A3 chime  # "active": true for 15 s
curl -s http://sf-beer-light.local/api/log | grep chime
```

## Publish: `sfbeerlight/<hostname>/state`

```json
{"state":"ON","brightness":90,"color_mode":"rgb","color":{"r":255,"g":32,"b":0},"effect":"flicker","colors":["#ff2000","#ff6000","#ffa000"]}
```

| Field | Meaning |
|---|---|
| `state` | `"ON"` or `"OFF"` |
| `brightness` | 0–255 |
| `color_mode` | Always `"rgb"` |
| `color` | The palette's **first** color, not the live one |
| `effect` | The running effect |
| `colors` | The whole palette as `"#rrggbb"` strings — what a `colors` command takes, so it can be sent back unchanged. Outside HA's schema; HA ignores it. |

Published on connect, after every accepted command, and whenever power,
brightness, effect or the palette changes — **not per frame**. The
live color moves continuously under `blend`, so publishing it would be a message
every 16 ms for a value nothing can act on. The first palette entry is stable,
and is the color someone actually picked.

Changes made from the web UI or the REST API publish too, so Home Assistant
follows along when a light is changed at the light.

## Publish: `sfbeerlight/<hostname>/availability`

`online` on connect, `offline` otherwise. Registered as the connection's last
will, so a power cut or a dropped link marks the entity unavailable rather than
leaving it frozen on its last known state.

A last will only fires on an **ungraceful** disconnect. Turning MQTT off, or
moving a light to another broker, is a clean disconnect and suppresses it — so
the light publishes `offline` itself before hanging up. Without that, changing
brokers left a permanently "available" ghost entity behind.

## Publish: `homeassistant/light/sfbeerlight_<mac>/config`

Published retained on every connect. This is what creates the entity; nothing
needs to be added to `configuration.yaml`.

```json
{
  "schema": "json",
  "name": "Castor",
  "unique_id": "sfbeerlight_44b176051860",
  "state_topic": "sfbeerlight/castor-light/state",
  "command_topic": "sfbeerlight/castor-light/set",
  "availability_topic": "sfbeerlight/castor-light/availability",
  "payload_available": "online",
  "payload_not_available": "offline",
  "brightness": true,
  "supported_color_modes": ["rgb"],
  "effect": true,
  "effect_list": ["blend", "loop", "flicker", "neon", "drift", "scan", "pour", "ignite", "sparkle", "breathe"],
  "device": {
    "identifiers": ["sfbeerlight_44b176051860"],
    "name": "Castor",
    "manufacturer": "SF Beer Light",
    "model": "14-LED edge-lit sign",
    "sw_version": "0.0.5",
    "configuration_url": "http://castor-light.local/"
  }
}
```

`unique_id` is the MAC, not the name, so renaming a light **updates** the existing
entity rather than creating a second one. `configuration_url` puts a link to the
light's own settings page in Home Assistant's device panel.

### Renaming

The command and state topics are built from the hostname, so changing it moves
them. The light republishes discovery pointing at the new topics, and Home
Assistant follows — but the **old retained state topic is left behind** holding a
final stale message. Nothing reads it, and it costs a few bytes on the broker.
To clear it:

```sh
mosquitto_pub -h broker -r -n -t sfbeerlight/old-name/state
mosquitto_pub -h broker -r -n -t sfbeerlight/old-name/availability
```

Removing a light from Home Assistant entirely means clearing its discovery topic
the same way:

```sh
mosquitto_pub -h broker -r -n -t homeassistant/light/sfbeerlight_44b176051860/config
```

## MQTT and REST together

Both interfaces drive the same light and both publish state, so they never
disagree. One deliberate difference:

**Nothing set over MQTT expires.** An effect set over REST reverts to the default
after five minutes unless told otherwise; one set over MQTT does not. Home
Assistant is a controller, not someone trying something out, and an effect that
reverted on its own would leave the entity showing a state the light no longer
has.

## Watching and troubleshooting

```sh
mosquitto_sub -h broker -v -t 'sfbeerlight/#' -t 'homeassistant/light/sfbeerlight_+/config'
```

| Symptom | Cause |
|---|---|
| Entity never appears | No retained discovery — check the light shows `connected` on its settings page |
| Entity is "unavailable" | Availability says `offline`: light is down, or on another broker |
| Commands do nothing | Check `state` is the word `"ON"`, not `true` |
| Light reverts after a reconnect | A **retained** command on the `set` topic. Clear it: `mosquitto_pub -r -n -t sfbeerlight/<host>/set` |
| Nothing publishes at all | A payload over the 1024-byte buffer is dropped silently by PubSubClient; the light logs an error at ERROR level, visible in the release build |
