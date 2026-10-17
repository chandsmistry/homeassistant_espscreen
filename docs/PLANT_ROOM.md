# The plant room card

A tile (`screen.plant_room`) for a heating and hot-water plant room: a boiler, two zone valves, radiators, a cylinder and
the mains water. It has two forms. On a small tile three tabs (Heating, Hot water, Cold) show a headline and rows of
readings under the boiler's state. On a large tile (room for the picture, about 66 by 50 mm) it draws a schematic of the
same state.

## Who works out what

The add-on does (`screen_manager/app/plant_room.py`); the screen only draws what it is told (`plant_card.h` places it,
`plant_view.cpp` paints it). The add-on reads a preset of 30 sensors (`PRESET`), each replaceable by the tile's
`plant_room` option, and works out: the boiler (offline, standby, firing, fault), the heating and hot-water calls, the zone
valves, stale temperatures, the water pressure, a condensing return, mains and immersion activity and the cylinder's
thermocline. The numbers of its rules are at the top of that file.

The payload keeps the order of the heating tab's rows (flow, flow target, return, pressure, gas, outside, house), the hot
water tab's first row (cylinder) and the cold tab's first row (mains), because the schematic opens their sensors by place.

## Taps

A tab opens. A reading opens that sensor's history. The four thermostat buttons (radiators off or on, preset normal or
eco) ask for a second tap within four seconds before they call `climate.set_hvac_mode` or `climate.set_preset_mode`: they
change the heating. Only those two services ever leave the card.

## Limits

The words on the card are English: the add-on sends them. Firmware before this one shows a plain tile.
