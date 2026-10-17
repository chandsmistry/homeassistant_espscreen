# The plant room card

A tile (`screen.plant_room`) for a heating and hot-water plant room: a boiler, two zone valves, radiators, a cylinder and
the mains water. It has two forms. On a small tile three tabs (Heating, Hot water, Cold) show a headline and rows of
readings under the boiler's state. On a large tile (room for the picture, about 100 by 62 mm) it draws the Lovelace card's 1000 x 620 schematic 1:1:
same positions, words and colours, from the add-on's `sc` scene (flags, probe colours, 33 words, 20 entities) by
`plant_scene.h` (pure geometry, tested on the PC) and `plant_view.cpp` (pixels). The glyphs come from
`tools/generate_plant_fonts.py` (`plant_fonts.h`).

## Who works out what

The add-on does (`screen_manager/app/plant_room.py`); the screen only draws what it is told (`plant_card.h` places it,
`plant_view.cpp` paints it). The add-on reads a preset of 30 sensors (`PRESET`), each replaceable by the tile's
`plant_room` option, and works out: the boiler (offline, standby, firing, fault), the heating and hot-water calls, the zone
valves, stale temperatures, the water pressure, a condensing return, mains and immersion activity and the cylinder's
thermocline. The numbers of its rules are at the top of that file.

The scene's rules are the Lovelace card's (`_model`): online, firing, boiler active, calls, stale temperatures, condensing,
mains deadband, immersion, probe colours and the cylinder's mix height. Entities in `sc.e` are what a tap opens.

## Taps

A tab opens. A reading opens that sensor's history. The four thermostat buttons (radiators off or on, preset normal or
eco) ask for a second tap within four seconds before they call `climate.set_hvac_mode` or `climate.set_preset_mode`: they
change the heating. Only those two services ever leave the card.

## Limits

The words on the card are English: the add-on sends them. Firmware before this one shows a plain tile.
