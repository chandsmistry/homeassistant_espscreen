# Power Distribution cards

A screen whose hello reports `pd_native` receives live text and numbers. Its native LVGL canvas draws the header and battery panels locally, keeping the canvas between updates. Small frozen emoji/symbol decorations are compiled into firmware; no full-card bitmap is downloaded. The PDC has no internal outline and uses its entire content area with equal padding. Fonts come from the board's existing Roboto set, so weights and sizes can differ slightly from the web dashboard.

Older firmware continues to receive map-compatible picture messages. That compatibility classification is added on the wire only, not stored in PD layout data. The old picture path below remains supported.

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

Built-in PD cards use a sensor snapshot to render and encode once before issuing a download link. The link pins those bytes for its normal two-minute lifetime, so later sensor changes cannot trigger rendering during GET. Logs beginning `pd_cards` report preparation cache hits, render/encode time and HTTP response timing. `pd_cards GET` also says how old the link was when the screen used it and how many PD sensor changes happened since it was issued; `pd_cards HTTP` says whether the add-on handed over every byte (`sent`) or the screen dropped the connection (`connection_reset`). HTTP completion measures server delivery, not panel decoding. The old picture path paces telemetry at 45 seconds, except confirmed mode changes; the negotiated native path has no picture mark or throttle.

PD cards present a Power Distribution inspector, with Controls → On tap and an Action section instead of map controls. Perform action → Open selector chooses any select or input_select entity. Tapping opens its native selector; choosing a row sends that entity's select_option action. The confirmed Home Assistant value updates the selection. Opening the selector sends no mode change. Older layouts migrate their implicit selector into explicit action configuration; older firmware keeps the original target until updated.

Holds and consumed edge swipes do not open the selector. The user confirmed edge page swipes and selector choices on the panel. The earlier picture path opened the map's full-screen camera view on a short click; the reported “edit mode” was not independently identified. Native firmware rejects a short click after an internal drag of more than 3 mm. The WASM touch test covers this; physical internal drags still need confirmation.

Native card data lives in its own shared block, following Energy; removing the per-tile mode string more than pays for its shared pointer in each Extra. Catalogue memory allowances conservatively reserve 6 KB per PD tile. Small tiles use a battery-power summary when all four panels cannot fit; the existing larger layout retains all rows.

`tools/generate_pd_decorations.py` regenerates the 24-pixel decorations from Noto Color Emoji and DejaVu symbols using the recorded font source hashes. They occupy 64,512 bytes of pixel data in flash. See `fonts/pd-decorations-licenses.txt` for licenses. Shared edge navigation is unchanged.
