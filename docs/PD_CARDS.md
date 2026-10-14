# Power Distribution cards as pictures

A screen cannot draw emoji, glow or custom colours, so the add-on draws these dashboard cards itself and the screen
shows them as picture tiles.

- `screen_manager/app/pd_cards.py` draws `header`, `dp3`, `ultra`, `ac` and `max` from the PD sensors.
- Optional legacy PNG export: set `SCREEN_PD_PNG_EXPORT=1` in the add-on environment only if you use the local-file camera integration below. Export is off by default; built-in PD picture tiles do not read these files.
- With export enabled, when one of those sensors changes, the add-on redraws and writes `/config/www/pd_cards/<name>.png`.
- In Home Assistant, a `camera` entity of the `local_file` platform per picture makes each one available to a camera tile.

```yaml
camera:
  - platform: local_file
    name: PD header
    file_path: /config/www/pd_cards/header.png
```

Repeat for `dp3`, `ultra`, `ac` and `max`. Only pictures whose bytes changed are written.

Built-in PD cards use a sensor snapshot to render and encode once before issuing a download link. The link pins those bytes for its normal two-minute lifetime, so later sensor changes cannot trigger rendering during GET. Logs beginning `pd_cards` report preparation cache hits, render/encode time and HTTP response timing. HTTP completion measures server delivery, not panel decoding. The 45-second screen-mark throttle remains unchanged.
