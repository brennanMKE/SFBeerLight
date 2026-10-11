# Home Assistant

For what Home Assistant can do with a light before any of this is set up, what
each route costs, and why MQTT is recommended over both of the below, see
[home-assistant-control.md](home-assistant-control.md).

Two ways to wire these lights in. They are not exclusive — the REST switch is
what Home Assistant polls day to day, and the script is what finds the lights in
the first place and does the bulk work.

## 1. REST, one entity per light

No script, no dependencies. Home Assistant talks to the light directly.

`configuration.yaml`:

```yaml
switch:
  - platform: rest
    name: Living Room Light
    resource: http://sf-beer-light-051860.local/api/power
    state_resource: http://sf-beer-light-051860.local/api/status?pretty=0
    is_on_template: "{{ value_json.on }}"
    body_on: '{"on": true}'
    body_off: '{"on": false}'
    headers:
      Content-Type: application/json

light:
  - platform: template
    lights:
      living_room_lamp:
        friendly_name: Living Room Light
        value_template: "{{ is_state('switch.living_room_lamp', 'on') }}"
        level_template: >
          {{ state_attr('sensor.living_room_lamp_brightness', 'brightness') | int(0) }}
        turn_on:
          service: switch.turn_on
          target: { entity_id: switch.living_room_lamp }
        turn_off:
          service: switch.turn_off
          target: { entity_id: switch.living_room_lamp }
        set_level:
          service: rest_command.lamp_brightness
          data:
            host: sf-beer-light-051860.local
            value: "{{ brightness }}"

rest_command:
  lamp_brightness:
    url: "http://{{ host }}/api/brightness"
    method: POST
    content_type: application/json
    payload: '{"value": {{ value }}}'
  lamp_identify:
    url: "http://{{ host }}/api/identify"
    method: POST
    content_type: application/json
    payload: '{"seconds": 4}'
```

Address lights by their `.local` name and they survive a DHCP lease change. If
Home Assistant's container cannot resolve mDNS, use the IP and give the light a
reservation on the router.

## 2. The script, for discovery and bulk actions

`scripts/sfbeerlight.py` needs only the standard library. Copy it somewhere Home
Assistant can execute, typically `/config/scripts/`, and make it executable.

```yaml
shell_command:
  lamps_off: "/config/scripts/sfbeerlight.py off --all"
  lamps_on: "/config/scripts/sfbeerlight.py on --all"
  lamps_dim: "/config/scripts/sfbeerlight.py brightness 40 --all"
```

Then the thing this was built for — every light off with the rest of the house:

```yaml
automation:
  - alias: Everything off at night
    triggers:
      - trigger: state
        entity_id: input_boolean.house_asleep
        to: "on"
    actions:
      - action: shell_command.lamps_off
```

`--all` acts on every light discovery finds, so adding a third light needs no
config change. Discovery uses the `zeroconf` package that Home Assistant already
ships; without it the script falls back to `dns-sd` or `avahi-browse`.

A `shell_command` has no opinion about how long it takes. Discovery browses for
5 seconds by default, so `--timeout 2` is worth setting if an automation is
waiting on the result. Pointing the script at known addresses skips discovery
altogether:

```yaml
shell_command:
  lamp_living_off: "/config/scripts/sfbeerlight.py off --host sf-beer-light-051860.local"
```

## Nightly firmware updates

`POST /api/ota/update` makes the light check GitHub and install a new release
only if the tag differs from what it is running. A light with nothing to take
does nothing, so this is safe to fire at every light every night.

```yaml
rest_command:
  lamp_update:
    url: "http://{{ host }}/api/ota/update"
    method: POST

automation:
  - alias: Update the lights overnight
    triggers:
      - trigger: time
        at: "03:30:00"
    actions:
      - action: rest_command.lamp_update
        data:
          host: sf-beer-light-051860.local
      - action: rest_command.lamp_update
        data:
          host: sf-beer-light-fe00bc.local
```

Or let the script find them, so a new light needs no config change:

```yaml
shell_command:
  lamps_update: "/config/scripts/sfbeerlight.py update --all"
```

The light keeps its own daily check at 15:00 local regardless. Driving it from
Home Assistant instead is worth it for the timing: you pick the hour, and it is
the same hour for every light.

A light is unreachable for 10-30 s while it downloads, and reboots afterwards.
Overnight is a good time for that; it is also the reason the firmware's own
default is mid-afternoon, when someone is around to notice a bad one.

## As a module

```python
from sfbeerlight import discover, Light

for found in discover(timeout=3):
    print(found.label, found.url)
    Light(found.url).power(False)
```

Every mutating call returns the light's full state, so there is no second request
to learn what happened.

## Notes

- **No authentication.** These are LAN devices; anything that can reach one can
  control it. Keep them off any network you would not hand the light switch to.
- **mDNS does not cross VLANs**, so Home Assistant has to share a subnet with
  the lights for discovery to work. Fixed addresses work across a router; an
  mDNS browse does not.
- **A light is unavailable for 10–30 s during a firmware update** and then
  reboots. A REST switch will log a failed poll or two while that happens.
- **Power is persisted.** A light switched off stays off across a power cut,
  which is what you want from something Home Assistant turns off at night.
