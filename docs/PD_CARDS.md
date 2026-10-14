# Power Distribution cards

## Native drawing

Firmware that reports `pd_native` receives live text and numbers and draws the header, individual batteries and combined Battery Allocations locally. One persistent LVGL draw object is retained between updates. No full-card bitmap or picture refresh throttle is used on this path. A compact summary is used when all battery panels cannot fit.

Small embedded decorations provide emoji and symbols, including the header hexagram. `tools/generate_pd_decorations.py` generates 29 decorations with 66,816 bytes of pixel data from Noto Color Emoji and symbol fonts. Licenses are in `fonts/pd-decorations-licenses.txt`. `tools/generate_pd_fonts.py` generates PD-only Roboto glyphs from the existing licensed picture-renderer fonts: regular labels, bold values and heavier names and badges. Other cards keep their existing typography. The header has no internal outline. Allocations use the tile padding once, four panel outlines, eligibility and connection badges, three bar tracks and active glow.

The header and combined allocations appearance at revision `58765a31` was visually accepted on a physical panel. This acceptance does not establish native touch behaviour, internal-drag behaviour, speed or other-board appearance. Earlier firmware stages had separate user confirmations for selector opening, edge swipes and faster picture refresh.

## Layout and controls

PD tiles are saved without a map display. The Power Distribution inspector offers Controls → On tap and an Action section, rather than map settings. The choices and selector action come from `catalogue/screen.yaml` and its generated add-on, editor and firmware data. Perform action → Open selector configures a select or input_select target. A tap opens the native selector; choosing an option sends the target's select_option action. Home Assistant confirmation updates the selected state. Opening the selector alone changes no mode. Nothing sends no action. Legacy implicit selector configuration is migrated to explicit action data.

The native short-click handler rejects an internal drag exceeding 3 mm. Holds and consumed edge swipes do not open the selector. Shared edge navigation is unchanged. Automated WASM tests cover native updates, the selector round trip and internal-drag rejection; final native physical interaction and speed remain untested. The earlier picture path used map full-screen behaviour on a short click; the reported edit-mode view was not independently identified.

## Older firmware

Without `pd_native`, the add-on retains the picture path. The map-compatible display classification is added on the wire only, not saved in the PD layout. Older firmware retains the original selector target until updated.

The add-on prepares and encodes a sensor snapshot before issuing a download link. The link pins those bytes for its normal two-minute lifetime, so changes cannot trigger rendering during GET. Preparation and HTTP logs report cache hits, render/encode time, link age and delivery completion. Server delivery does not measure panel decoding. Picture telemetry is paced at 45 seconds, except confirmed mode changes. RLE8 is negotiated with supported firmware; other firmware receives plain BMP.

## Optional legacy export

Built-in PD tiles do not read exported PNG files. Export is disabled by default. Set `SCREEN_PD_PNG_EXPORT=1` only for a separate local-file camera use case. The add-on then writes changed header and battery pictures under `/config/www/pd_cards/`. A local-file camera can read a generated picture, for example:

```yaml
camera:
  - platform: local_file
    name: Distribution header
    file_path: /config/www/pd_cards/header.png
```

Native card data uses a shared block. Catalogue memory allowances conservatively reserve 6 KB per PD tile. Physical acceptance on one panel does not establish a flash budget or visual result for another board.
