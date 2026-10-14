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

Built-in PD cards use a sensor snapshot to render and encode once before issuing a download link. The link pins those bytes for its normal two-minute lifetime, so later sensor changes cannot trigger rendering during GET. Logs beginning `pd_cards` report preparation cache hits, render/encode time and HTTP response timing. `pd_cards GET` also says how old the link was when the screen used it and how many PD sensor changes happened since it was issued; `pd_cards HTTP` says whether the add-on handed over every byte (`sent`) or the screen dropped the connection (`connection_reset`). HTTP completion measures server delivery, not panel decoding. The 45-second screen-mark throttle remains unchanged.

PD cards present a Power Distribution inspector, with Controls → On tap and an Action section instead of map controls. Perform action → Open selector chooses any select or input_select entity. Tapping opens its native selector; choosing a row sends that entity's select_option action. The confirmed Home Assistant value updates the selection. Opening the selector sends no mode change. Older layouts migrate their implicit selector into explicit action configuration; older firmware keeps the original target until updated.

Holds and consumed edge swipes do not open the selector. The user confirmed edge page swipes and selector choices on the panel. The earlier picture path opened the map's full-screen camera view on a short click; the reported “edit mode” was not independently identified. Non-edge drag behavior requires separate testing.
