# Power Distribution cards as pictures

A screen cannot draw emoji, glow or custom colours, so the add-on draws these dashboard cards itself and the screen
shows them as picture tiles.

- `screen_manager/app/pd_cards.py` draws `header`, `dp3`, `ultra`, `ac` and `max` from the PD sensors.
- When one of those sensors changes, the add-on redraws and writes `/config/www/pd_cards/<name>.png`.
- In Home Assistant, a `camera` entity of the `local_file` platform per picture makes each one available to a camera tile.

```yaml
camera:
  - platform: local_file
    name: PD header
    file_path: /config/www/pd_cards/header.png
```

Repeat for `dp3`, `ultra`, `ac` and `max`. Only pictures whose bytes changed are written.
