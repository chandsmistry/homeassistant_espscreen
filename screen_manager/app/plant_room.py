"""The plant room card (tile `screen.plant_room`): a heating and hot-water plant room in three tabs (Heating, Hot water,
Cold) and a small schematic of the same state.

The add-on works every derived state out, the screen only draws. The rules are those of the Lovelace plant-room card the
tile is made from: a boiler is online when its eBUS signal and running sensors are both on, firing above 0.5 kW of gas,
a heating call is the thermostat's `hvac_action`, a hot-water call is the boiler active with a hot-water schedule or a
boost, a temperature older than two hours is stale, the water pressure is normal from 1.0 to 2.7 bar, a return under
54 degrees condenses, mains water runs from 0.75 L/min.

`PRESET` is the default entity of each role (the owner's plant room); a tile's `plant_room` option replaces any of them.
Text is English: the screen has no words for these roles yet.
"""
from __future__ import annotations

import re
from datetime import datetime, timezone

PRESET = {
    'house_temp': 'sensor.whole_home_average_temperature',
    'thermostat': 'climate.hallway_thermostat',
    'heaters': 'group.all_heaters',
    'hot_water': 'water_heater.hallway_hot_water',
    'signal': 'binary_sensor.ebusd_global_signal',
    'running': 'binary_sensor.ebusd_global_running',
    'trouble': 'binary_sensor.vaillant_boiler_trouble_codes',
    'power': 'sensor.boiler_3a_spur_current_power',
    'gas': 'sensor.ebusd_bai_boiler_live_gas',
    'flow_temp': 'sensor.ebusd_bai_status01_temp',
    'flow_target': 'sensor.ebusd_bai_setmode_flowtempdesired',
    'flow_min': 'number.ebusd_ctlx0_hc1minflowtempdesired',
    'flow_max': 'number.ebusd_ctlx0_hc1maxflowtempdesired',
    'primary_flow': 'sensor.ebusd_bai_primarycircuitflowrate',
    'return_temp': 'sensor.ebusd_bai_returntemp_temp',
    'pressure': 'sensor.ebusd_bai_waterpressure_press',
    'outside': 'sensor.ebusd_broadcast_outsidetemp',
    'heat_curve': 'number.ebusd_ctlx0_hc1heatcurve',
    'hc_mode': 'sensor.ebusd_bai_setmode_hcmode',
    'pump': 'sensor.ebusd_bai_status01_pumpstate',
    'z1_mode': 'select.ebusd_ctlx0_z1opmode',
    'z1_desired': 'number.ebusd_ctlx0_z1actualroomtempdesired',
    'megaflo_intake': 'sensor.salus_ps600_megaflo_primary_intake_temperature',
    'cylinder': 'sensor.estimated_hot_water_cylinder_temperature',
    'draw_off': 'sensor.ps600_dhw_draw_off_temperature_last_known',
    'hot_draw': 'binary_sensor.hot_draw_active',
    'immersion': 'switch.water_tank_spur',
    'immersion_power': 'sensor.water_tank_spur_power',
    'mains_flow': 'sensor.aquaro_sensor_82464185790730_water_flow',
    'salt_low': 'binary_sensor.water_softener_moisture',
}
ROLES = tuple(PRESET)
ENTITY = re.compile(r'^[a-z_]+\.[a-z0-9_]+$')

FIRING_KW = 0.5
STALE_S = 7200
PRESSURE = (1.0, 2.7)
CONDENSING_C = 54
MAINS_ACTIVE_LPM = 0.75
PRIMARY_ACTIVE_LPM = 0.5
IMMERSION_W = 100
PUMP_ON = ('on', 'running', 'heating')

# Colours the screen maps to theme roles.
IDLE, OK, HEAT, HOT, COLD, WARN, BAD, IMMERSION = range(8)
BOILER_OFFLINE, BOILER_STANDBY, BOILER_FIRING, BOILER_FAULT = range(4)


def clean_mapping(value):
    """The tile's `plant_room` option: role to entity id. Unknown roles and defaults drop; None when malformed."""
    if value is None:
        return {}
    if not isinstance(value, dict):
        return None
    out = {}
    for role, entity in value.items():
        if role not in PRESET or entity in (None, ''):
            continue
        if not isinstance(entity, str) or not ENTITY.match(entity):
            return None
        if entity != PRESET[role]:
            out[role] = entity
    return out


def entity_of(mapping: dict | None, role: str) -> str:
    return (mapping or {}).get(role) or PRESET[role]


def mapped_entities(mapping: dict | None) -> list[str]:
    out = []
    for role in ROLES:
        e = entity_of(mapping, role)
        if e not in out:
            out.append(e)
    return out


def _num(state):
    try:
        v = float((state or {}).get('state'))
    except (TypeError, ValueError):
        return None
    return None if v != v else v


def _is(state, *values):
    return str((state or {}).get('state', '')).lower() in values


def _epoch(text):
    try:
        return datetime.fromisoformat(str(text).replace('Z', '+00:00')).timestamp()
    except ValueError:
        return None


def _stale(state, now):
    moment = _epoch((state or {}).get('last_updated') or (state or {}).get('last_changed'))
    return moment is not None and now - moment > STALE_S


def _attr(state, name):
    return ((state or {}).get('attributes') or {}).get(name)


def probe(t, hot_outlet=False):
    """A temperature probe's colour: grey when invalid, blue under 30, orange to the limit, red from it."""
    if t is None or t < 1 or t > 100:
        return IDLE
    if t < 30:
        return COLD
    limit = 42 if hot_outlet else 50
    return HOT if t < limit else HEAT


def _fmt(v, unit, digits=1):
    if v is None:
        return '–'
    text = f'{v:.{digits}f}'
    if digits:
        text = text.rstrip('0').rstrip('.')
    return f'{text} {unit}'


def mains_lpm(state):
    """The mains flow in L/min, from the sensor's own unit (ml/s, L/s or L/min)."""
    v = _num(state)
    if v is None:
        return None
    unit = str(_attr(state, 'unit_of_measurement') or '').lower().replace(' ', '')
    if unit in ('ml/s', 'ml/sec'):
        return v * 0.06
    if unit in ('l/s', 'l/sec'):
        return v * 60
    return v


def facts(states: dict, mapping: dict | None = None, now: float | None = None) -> dict:
    """Every derived state of the plant room."""
    if now is None:
        now = datetime.now(timezone.utc).timestamp()
    s = {role: states.get(entity_of(mapping, role)) for role in ROLES}
    f = {}
    online = _is(s['signal'], 'on') and _is(s['running'], 'on')
    gas = _num(s['gas'])
    trouble = _is(s['trouble'], 'on')
    firing = online and gas is not None and gas > FIRING_KW
    f['online'], f['trouble'], f['firing'] = online, trouble, firing
    f['standby'] = online and not firing and not trouble
    f['boiler'] = BOILER_FAULT if trouble else BOILER_FIRING if firing else BOILER_STANDBY if online else BOILER_OFFLINE
    primary = _num(s['primary_flow'])
    f['active'] = online and (_is(s['pump'], *PUMP_ON) or (primary is not None and primary > PRIMARY_ACTIVE_LPM))
    f['heat_call'] = _attr(s['thermostat'], 'hvac_action') == 'heating'
    hw = s['hot_water']
    schedule = bool(_attr(hw, 'hot_water_schedule_active'))
    boost = str(_attr(hw, 'operation_mode') or (hw or {}).get('state') or '').lower() == 'boost'
    f['dhw_call'] = f['active'] and (schedule or boost)
    f['zv1'], f['zv2'] = f['heat_call'], f['dhw_call']
    f['gas'], f['primary'] = gas, primary
    f['flow'], f['return'] = _num(s['flow_temp']), _num(s['return_temp'])
    f['flow_target'] = _num(s['flow_target'])
    f['pressure'] = _num(s['pressure'])
    f['pressure_ok'] = f['pressure'] is not None and PRESSURE[0] <= f['pressure'] <= PRESSURE[1]
    f['condensing'] = f['return'] is not None and f['return'] < CONDENSING_C
    f['outside'], f['house'] = _num(s['outside']), _num(s['house_temp'])
    f['power'] = _num(s['power'])
    f['cylinder'], f['intake'], f['draw'] = _num(s['cylinder']), _num(s['megaflo_intake']), _num(s['draw_off'])
    f['stale_intake'], f['stale_draw'] = _stale(s['megaflo_intake'], now), _stale(s['draw_off'], now)
    f['hot_draw'] = _is(s['hot_draw'], 'on')
    mains = mains_lpm(s['mains_flow'])
    f['mains'], f['mains_active'] = mains, mains is not None and mains >= MAINS_ACTIVE_LPM
    ip = _num(s['immersion_power'])
    f['immersion'] = _is(s['immersion'], 'on') or (ip is not None and ip > IMMERSION_W)
    f['salt_low'] = _is(s['salt_low'], 'on')
    f['mode'] = (s['thermostat'] or {}).get('state')
    f['preset'] = _attr(s['thermostat'], 'preset_mode')
    f['schedule'], f['boost'] = schedule, boost
    return f


def _row(label, value, colour, entity=''):
    return [label, value, colour, entity]


def thermocline(f):
    """Where the hot water ends in the cylinder, 8 to 92 percent from the bottom: 20 to 60 degrees clamped, and a cold
    draw lifts the cold region by 20 percent."""
    t = f['cylinder']
    if t is None:
        return 8
    pct = 8 + (min(60.0, max(20.0, t)) - 20) / 40 * 84
    if f['mains_active'] and not f['hot_draw']:
        pct -= 20
    return round(min(92, max(8, pct)))


def controls(f):
    """The thermostat's buttons: radiators off or on, the preset none or eco. A button is shown only for a state the
    thermostat has; the screen asks for a second tap before it calls the service."""
    out = []
    if f['mode'] in ('off', 'heat'):
        out += [['Radiators off', 'climate.set_hvac_mode', 'hvac_mode', 'off', f['mode'] == 'off'],
                ['Radiators on', 'climate.set_hvac_mode', 'hvac_mode', 'heat', f['mode'] == 'heat']]
    if f['preset'] in ('none', 'eco'):
        out += [['Preset normal', 'climate.set_preset_mode', 'preset_mode', 'none', f['preset'] == 'none'],
                ['Preset eco', 'climate.set_preset_mode', 'preset_mode', 'eco', f['preset'] == 'eco']]
    return out


def payload(states: dict, mapping: dict | None = None, now: float | None = None) -> dict:
    f = facts(states, mapping, now)
    e = lambda role: entity_of(mapping, role)
    boiler_text = 'Fault' if f['trouble'] else 'Firing' if f['firing'] else 'Standby' if f['standby'] else 'Offline'
    boiler_colour = {BOILER_FAULT: BAD, BOILER_FIRING: HEAT, BOILER_STANDBY: OK, BOILER_OFFLINE: IDLE}[f['boiler']]

    heating = {'k': 'heat', 'h': 'Calling for heat' if f['heat_call'] else 'No heat call' if f['online'] else 'Boiler offline',
               'c': HEAT if f['heat_call'] else IDLE, 'r': [
        _row('Flow', _fmt(f['flow'], '°C'), probe(f['flow']), e('flow_temp')),
        _row('Flow target', _fmt(f['flow_target'], '°C'), IDLE, e('flow_target')),
        _row('Return', _fmt(f['return'], '°C'), COLD if f['condensing'] else WARN if f['return'] is not None else IDLE, e('return_temp')),
        _row('Pressure', _fmt(f['pressure'], 'bar', 2), OK if f['pressure_ok'] else WARN if f['pressure'] is not None else IDLE, e('pressure')),
        _row('Gas', _fmt(f['gas'], 'kW'), HEAT if f['firing'] else IDLE, e('gas')),
        _row('Outside', _fmt(f['outside'], '°C'), IDLE, e('outside')),
        _row('House', _fmt(f['house'], '°C'), IDLE, e('house_temp')),
    ]}
    busy = f['hot_draw'] or f['dhw_call'] or f['immersion']
    hot = {'k': 'hot', 'h': 'Hot draw' if f['hot_draw'] else 'Heating cylinder' if f['dhw_call'] else 'Immersion on' if f['immersion'] else 'Standby',
           'c': HOT if busy else IDLE, 'r': [
        _row('Cylinder (est.)', _fmt(f['cylinder'], '°C'), probe(f['cylinder'], True), e('cylinder')),
        _row('Megaflo intake', _fmt(f['intake'], '°C') + (' stale' if f['stale_intake'] else ''),
             IDLE if f['stale_intake'] else probe(f['intake']), e('megaflo_intake')),
        _row('Last draw-off', _fmt(f['draw'], '°C') + (' stale' if f['stale_draw'] else ''),
             IDLE if f['stale_draw'] else probe(f['draw'], True), e('draw_off')),
        _row('Immersion', 'On' if f['immersion'] else 'Off', IMMERSION if f['immersion'] else IDLE, e('immersion')),
        _row('Schedule', 'Active' if f['schedule'] else 'Off', HOT if f['schedule'] else IDLE, e('hot_water')),
    ]}
    cold = {'k': 'cold', 'h': 'Mains running' if f['mains_active'] else 'Mains idle', 'c': COLD if f['mains_active'] else IDLE, 'r': [
        _row('Mains flow', _fmt(f['mains'], 'L/min'), COLD if f['mains_active'] else IDLE, e('mains_flow')),
        _row('Softener salt', 'Low' if f['salt_low'] else 'OK', WARN if f['salt_low'] else OK, e('salt_low')),
    ]}
    return {
        'b': [f['boiler'], boiler_text, boiler_colour],
        'tabs': [heating, hot, cold],
        'm': {'flow': f['flow'], 'ret': f['return'], 'cyl': f['cylinder'], 'draw': f['draw'], 'mains': f['mains'],
              'bar': f['pressure'], 'tc': thermocline(f), 'fl': int(f['firing']), 'z1': int(f['zv1']), 'z2': int(f['zv2']),
              'im': int(f['immersion']), 'hd': int(f['hot_draw']), 'ma': int(f['mains_active']), 'co': int(f['condensing'])},
        'a': {'e': e('thermostat'), 'c': controls(f), 'cf': 'Tap again to confirm'},
    }
