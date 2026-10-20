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


def _countdown(end, now):
    """The Lovelace card's compact countdown, rounded up from an absolute endpoint."""
    end_ts = _epoch(end)
    if end_ts is None or end_ts <= now:
        return ''
    seconds = end_ts - now
    if seconds < 60:
        return '<1m'
    minutes = int((seconds + 59) // 60)
    hours, part = divmod(minutes, 60)
    return f'{hours}h {part}m' if hours and part else f'{hours}h' if hours else f'{minutes}m'


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
    mode = str(_attr(hw, 'operation_mode') or (hw or {}).get('state') or '').lower()
    operations = _attr(hw, 'operation_list') or []
    boost = mode in ('boost_30m', 'boost_1h', 'boost_2h') and mode in operations
    schedule_selected = mode == 'schedule' and mode in operations
    boiler_active = bool(_attr(hw, 'boiler_active'))
    f['dhw_call'] = boiler_active and ((schedule_selected and schedule) or boost)
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
    f['immersion_heating'] = ip is not None and ip > IMMERSION_W
    f['immersion_active'] = _is(s['immersion'], 'on') or f['immersion_heating']
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


# The scene: the Lovelace card's 1000 x 620 schematic, drawn by the screen from these states. Colour roles of the wire.
S_DIM, S_TXT, S_FLOW, S_COLD, S_DHW, S_ELEC, S_OK, S_CTRL, S_GAS, S_SOFT, S_PROBE = range(11)
# Flag bits of `sc.f`.
F_HEAT_CALL, F_HW_CALL, F_HEAT_PATH, F_DHW_PATH, F_ANY_PRIMARY, F_ACTIVE, F_DHW_FLOW, F_MAINS, F_TANK_COLD, \
    F_HOUSE_COLD, F_WATER, F_HOT_DRAW, F_FIRING, F_IMMERSION, F_CYL, F_STANDBY, F_CONDENSING, F_NEST_OFF, \
    F_NEST_HEAT, F_PRESET_NONE, F_PRESET_ECO, F_COIL_FLOW, F_COIL_RETURN, F_BURNING_ONLINE = range(24)
# The text slots of `sc.x`, kept in lock-step with plant_scene.h.
T_SPUR, T_GAS, T_FLOW, T_FLOW_TAG, T_RET, T_RET_TAG, T_DELTA, T_PRESSURE, T_OUTDOOR, T_CURVE, T_LIMITS, T_STATUS, \
    T_COND, T_COND2, T_RET_PROBE, T_FLOW_PROBE, T_RATE, T_DRAW_PROBE, T_INTAKE, T_CYL, T_CYL_TAG, T_CIRCUIT, \
    T_MANAGER, T_IMM_STATE, T_IMM_W, T_TARGET, T_HOME, T_SPACE, T_HOT, T_DRAW, T_MAINS, T_AQUARO, T_SALT, \
    T_CYL_SUB, T_COUNT = range(35)
# The entities a tap on the scene opens, in the order of `sc.e`.
SCENE_ENTITIES = ('return_temp', 'flow_temp', 'draw_off', 'gas', 'z1_mode', 'power', 'primary_flow', 'pressure', 'outside',
                  'heat_curve', 'signal', 'megaflo_intake', 'cylinder', 'hc_mode', 'pump', 'immersion_power', 'heaters',
                  'thermostat', 'hot_water', 'mains_flow', 'salt_low')
UNAVAILABLE = ('unknown', 'unavailable', 'none', '')


def _raw(state):
    return str((state or {}).get('state', 'unavailable')).strip()


def _have(state):
    return _raw(state).lower() not in UNAVAILABLE


def _fl(state):
    try:
        v = float(_raw(state))
    except ValueError:
        return None
    return None if v != v else v


def _r(v, digits):
    """What the card's `round(digits)` prints: 55.0, 1.6, 20.45."""
    return str(round(v, digits))


def _probe_colour(t, available, hot_outlet=False):
    if not available or t < 1 or t > 100:
        return S_DIM
    if t < 30:
        return S_COLD
    return S_DHW if t < (42 if hot_outlet else 50) else S_FLOW


def _model(states, mapping, now):
    """Every variable of the Lovelace card's template, by the card's own rules."""
    s = {role: states.get(entity_of(mapping, role)) for role in ROLES}
    m = {}
    th = s['thermostat']
    m['nest_state'] = _raw(th).lower() if th else 'unavailable'
    preset = _attr(th, 'preset_mode')
    m['preset'] = str(preset if preset not in (None, '') else 'none').lower()
    action = _attr(th, 'hvac_action')
    m['heating_call'] = str(action if action not in (None, '') else 'off').lower() == 'heating'
    m['nest_target'] = _attr(th, 'temperature')
    m['home'] = _fl(s['house_temp'])
    m['home_ok'] = _have(s['house_temp'])
    gas = _fl(s['gas']) if _have(s['gas']) else None
    m['gas_ok'], m['gas_kw'] = gas is not None or _have(s['gas']), gas or 0.0
    m['firing'] = _have(s['gas']) and m['gas_kw'] > FIRING_KW
    hw = s['hot_water']
    m['hw_ok'] = _have(hw)
    mode = _attr(hw, 'operation_mode')
    mode = str(mode if mode not in (None, '') else _raw(hw)).lower()
    ops = _attr(hw, 'operation_list') or []
    sched_active = bool(_attr(hw, 'hot_water_schedule_active'))
    hw_boiler = bool(_attr(hw, 'boiler_active'))
    m['sched_sel'] = mode == 'schedule' and 'schedule' in ops
    m['boost_sel'] = mode in ('boost_30m', 'boost_1h', 'boost_2h') and mode in ops
    m['sched_active'] = sched_active
    m['hw_enabled'] = m['hw_ok'] and mode != 'off'
    m['hw_call'] = m['hw_ok'] and hw_boiler and ((m['sched_sel'] and sched_active) or m['boost_sel'])
    m['hw_state'] = _raw(hw).lower()
    m['online'] = _is(s['signal'], 'on') and _is(s['running'], 'on')
    rate = _fl(s['primary_flow'])
    m['rate'] = rate or 0.0
    m['rate_ok'] = _have(s['primary_flow']) and rate is not None
    m['pump_ok'] = _have(s['pump'])
    m['pump_state'] = _raw(s['pump'])
    m['pump_run'] = m['pump_ok'] and _raw(s['pump']).upper() in ('ON', 'RUNNING', 'HEATING')
    m['active'] = m['online'] and (m['pump_run'] or (m['rate_ok'] and m['rate'] > PRIMARY_ACTIVE_LPM))
    for key, role in (('flow', 'flow_temp'), ('ret', 'return_temp')):
        v = _fl(s[role])
        m[key + '_ok'] = _have(s[role])
        m[key] = v if v is not None else 0.0
        m[key + '_stale'] = m[key + '_ok'] and _stale(s[role], now)
    draw = _fl(s['draw_off'])
    m['draw_ok'] = _have(s['draw_off']) and draw is not None
    m['draw'] = draw or 0.0
    m['draw_stale'] = m['draw_ok'] and _stale(s['draw_off'], now)
    cyl = _fl(s['cylinder'])
    m['cyl_ok'] = _have(s['cylinder']) and cyl is not None
    m['cyl'] = cyl or 0.0
    intake = _fl(s['megaflo_intake'])
    m['intake_ok'] = _have(s['megaflo_intake'])
    m['intake'] = intake or 0.0
    m['target_ok'] = _have(s['flow_target'])
    m['target'] = _fl(s['flow_target']) or 0.0
    m['pressure_ok'] = _have(s['pressure']) and _fl(s['pressure']) is not None
    m['pressure'] = _fl(s['pressure']) or 0.0
    m['outside_ok'] = _have(s['outside'])
    m['outside'] = _fl(s['outside']) or 0.0
    m['curve_ok'] = _have(s['heat_curve'])
    m['curve'] = _fl(s['heat_curve']) or 0.0
    m['spur_ok'] = _have(s['power'])
    m['spur'] = _fl(s['power']) or 0.0
    m['circuit_ok'] = _have(s['hc_mode'])
    m['circuit'] = _raw(s['hc_mode'])
    lo, hi = _fl(s['flow_min']), _fl(s['flow_max'])
    m['limits_ok'] = _have(s['flow_min']) and _have(s['flow_max']) and lo is not None and hi is not None
    m['limits'] = (lo or 0.0, hi or 0.0)
    m['trouble_known'] = _have(s['trouble'])
    m['trouble'] = m['trouble_known'] and _raw(s['trouble']).lower() in ('on', 'true', '1')
    m['hot_draw'] = _is(s['hot_draw'], 'on')
    m['hot_draw_known'] = _have(s['hot_draw'])
    m['delta_ok'] = m['flow_ok'] and m['ret_ok']
    m['delta'] = m['flow'] - m['ret']
    m['in_range'] = m['ret_ok'] and m['ret'] < CONDENSING_C
    mains = mains_lpm(s['mains_flow']) if _have(s['mains_flow']) else None
    m['mains_ok'] = _have(s['mains_flow'])
    raw = mains if mains is not None else 0.0
    m['mains'] = raw if raw >= MAINS_ACTIVE_LPM else 0.0
    m['mains_active'] = m['mains_ok'] and m['mains'] > 0
    m['dhw_flow'] = m['hot_draw'] and m['mains_active']
    m['tank_cold'] = m['mains_active'] and m['hot_draw']
    m['house_cold'] = m['mains_active']
    m['water'] = m['mains_active'] or m['hot_draw']
    m['salt_ok'] = _have(s['salt_low'])
    m['salt_low'] = m['salt_ok'] and _raw(s['salt_low']).lower() == 'on'
    ip = _fl(s['immersion_power'])
    m['imm_ok'] = _have(s['immersion_power'])
    m['imm_w'] = ip or 0.0
    m['immersion_heating'] = m['imm_ok'] and m['imm_w'] > IMMERSION_W
    m['immersion_active'] = _is(s['immersion'], 'on') or m['immersion_heating']
    useful = _attr(s['cylinder'], 'usable_hot_water_temperature_c')
    useful_age = _attr(s['cylinder'], 'usable_hot_water_age_minutes')
    try:
        m['useful'] = float(useful)
    except (TypeError, ValueError):
        m['useful'] = None
    try:
        m['useful_age'] = float(useful_age)
    except (TypeError, ValueError):
        m['useful_age'] = None
    m['useful_confidence'] = str(_attr(s['cylinder'], 'usable_hot_water_confidence') or 'Not observed')
    m['useful_ok'] = m['useful'] is not None
    m['useful_recent'] = m['useful_ok'] and m['useful_age'] is not None and m['useful_age'] <= 180
    end = _attr(hw, 'boost_timer_end') if m['boost_sel'] else _attr(hw, 'next_transition_time') if m['sched_sel'] else None
    m['hw_countdown'] = _countdown(end, now) if m['hw_call'] else ''
    ok = not m['flow_stale'] and not m['ret_stale']
    m['heat_path'] = m['heating_call'] and m['active'] and ok
    m['dhw_path'] = m['hw_call'] and m['active'] and ok
    m['any_primary'] = m['heat_path'] or m['dhw_path']
    m['condensing'] = m['active'] and m['in_range'] and not m['ret_stale']
    m['status_known'] = _have(s['signal'])
    zone = _raw(s['z1_mode'])
    m['zone_known'] = _have(s['z1_mode'])
    master_wanted = (m['nest_state'] == 'heat' and m['heating_call']) or m['hw_state'] in ('boost_30m', 'boost_1h', 'boost_2h') \
        or (m['hw_state'] == 'schedule' and (hw_boiler or sched_active))
    inputs_known = m['nest_state'] not in UNAVAILABLE and m['hw_ok']
    target = _fl(s['z1_desired'])
    mismatch = m['zone_known'] and inputs_known and (zone != ('day' if master_wanted else 'off') or
                                                      (master_wanted and target is not None and target != 30))
    m['mismatch'] = mismatch
    m['interlock_unknown'] = (not m['zone_known']) or (not inputs_known) or (master_wanted and target is None)
    m['safe'] = (m['online'] and m['trouble_known'] and not m['trouble'] and m['pressure_ok']
                 and PRESSURE[0] <= m['pressure'] <= PRESSURE[1] and not mismatch)
    return m


def _status(m):
    if not m['status_known']:
        return 'Status unavailable'
    if not m['online']:
        return 'Offline'
    if not m['trouble_known']:
        return 'Fault status unavailable'
    if m['trouble']:
        return 'Fault code'
    if not m['pressure_ok']:
        return 'Pressure unavailable'
    if m['pressure'] < PRESSURE[0]:
        return 'Low pressure'
    if m['pressure'] > PRESSURE[1]:
        return 'High pressure'
    demand = m['heating_call'] or m['hw_call']
    if m['active'] and not demand:
        return 'Active · no demand'
    if m['mismatch']:
        return 'Master differs'
    if m['interlock_unknown']:
        return 'Interlock unavailable'
    if demand and not m['active']:
        return 'Demand · waiting'
    return 'Online · no faults'


def _title(text):
    return text.replace('_', ' ').title()


def scene(states: dict, mapping: dict | None = None, now: float | None = None) -> dict:
    """The data the screen draws the Lovelace schematic from: flags, colours, texts and the entities a tap opens."""
    if now is None:
        now = datetime.now(timezone.utc).timestamp()
    m = _model(states, mapping, now)
    flags = 0
    for bit, on in (
            (F_HEAT_CALL, m['heating_call']), (F_HW_CALL, m['hw_call']), (F_HEAT_PATH, m['heat_path']),
            (F_DHW_PATH, m['dhw_path']), (F_ANY_PRIMARY, m['any_primary']), (F_ACTIVE, m['active']),
            (F_DHW_FLOW, m['dhw_flow']), (F_MAINS, m['mains_active']), (F_TANK_COLD, m['tank_cold']),
            (F_HOUSE_COLD, m['house_cold']), (F_WATER, m['water']), (F_HOT_DRAW, m['hot_draw']),
            (F_FIRING, m['firing']), (F_IMMERSION, m['immersion_heating']), (F_CYL, m['cyl_ok']),
            (F_STANDBY, m['online'] and not m['firing'] and not m['trouble']), (F_CONDENSING, m['condensing']),
            (F_NEST_OFF, m['nest_state'] == 'off'), (F_NEST_HEAT, m['nest_state'] == 'heat'),
            (F_PRESET_NONE, m['preset'] == 'none'), (F_PRESET_ECO, m['preset'] == 'eco')):
        if on:
            flags |= 1 << bit
    # The cylinder's mix level and the coil's colour stops.
    frac_raw = (m['cyl'] - 20) / 40 if m['cyl_ok'] else 0.5
    frac = min(0.92, max(0.08, frac_raw))
    pos = max(0.05, frac - (0.2 if m['tank_cold'] else 0))
    mix_y = round(124 + pos * 320, 1)
    coil_y = mix_y if m['cyl_ok'] else 284
    off = min(1.0, max(0.0, (coil_y - 194) / 226))
    if off <= 0:
        flags |= 1 << F_COIL_RETURN
    elif off >= 1 or (m['cyl_ok'] and frac_raw >= 1 and not m['tank_cold']):
        flags |= 1 << F_COIL_FLOW
    nest_status = S_ELEC if m['heating_call'] and m['hw_call'] else S_FLOW if m['heating_call'] else S_DHW if m['hw_call'] else S_CTRL
    imm_colour = _probe_colour(m['cyl'], m['cyl_ok'])
    if not m['immersion_heating']:
        imm_colour = S_DIM
    probes = [_probe_colour(m['flow'], m['flow_ok']), _probe_colour(m['ret'], m['ret_ok']),
              _probe_colour(m['draw'], m['draw_ok'], True), _probe_colour(m['intake'], m['intake_ok']), imm_colour, nest_status]
    hue = None
    if m['nest_state'] == 'heat' or m['preset'] == 'eco':
        if m['home_ok'] and m['home'] is not None:
            t = min(30.0, max(15.0, m['home']))
            hue = int(round(210 - ((t - 15) / 15) * 210))

    def deg(v, ok, bad='n/a', unit=' °C', digits=1):
        return _r(v, digits) + unit if ok else bad

    flow_tag = 'FLOW' + (' · UNAVAILABLE' if not m['flow_ok'] else ' · STALE' if m['flow_stale'] else
                         f" · TARGET {_r(m['target'], 1)}" if m['target_ok'] else '')
    ret_tag = 'RETURN · UNAVAILABLE' if not m['ret_ok'] else 'RETURN · STALE' if m['ret_stale'] else 'BOILER RETURN'
    pr = m['pressure']
    pr_colour = S_DHW if not m['pressure_ok'] else S_OK if PRESSURE[0] <= pr <= PRESSURE[1] else S_FLOW
    stale_bad = lambda ok, stale: not ok or stale
    ret_probe = (_r(m['ret'], 1) + ' °C' + (' · STALE' if m['ret_stale'] else '')) if m['ret_ok'] else 'UNAVAILABLE'
    draw_probe = (_r(m['draw'], 1) + ' °C' + (' · STALE' if m['draw_stale'] else '')) if m['draw_ok'] else 'UNAVAILABLE'
    if not m['draw_ok']:
        draw_line = 'n/a'
    elif m['draw_stale']:
        draw_line = _r(m['draw'], 1) + ' °C · Stale'
    elif not m['hot_draw_known']:
        draw_line = _r(m['draw'], 1) + ' °C'
    else:
        draw_line = _r(m['draw'], 1) + ' °C · ' + ('Flow mismatch' if m['hot_draw'] and m['mains_ok'] and not m['mains_active']
                                                    else 'Drawing' if m['hot_draw'] else 'No draw')
    if not m['hw_ok']:
        hw_text, hw_colour = 'Unavailable', S_DHW
    elif m['hw_call']:
        hw_text = 'Calling' + (' · ' + m['hw_countdown'] if m['hw_countdown'] else '')
        hw_colour = S_DHW
    else:
        hw_text = ('Schedule active' if m['sched_sel'] and m['sched_active'] else 'Scheduled' if m['sched_sel'] else
                   'Boost ready' if m['boost_sel'] else 'Ready' if m['hw_enabled'] else 'Off')
        hw_colour = S_DIM
    limits = (f"Flow limits · {int(round(m['limits'][0]))}–{int(round(m['limits'][1]))} °C" if m['limits_ok']
              else 'Flow limits · n/a')
    cond_tag = ('RETURN UNAVAILABLE' if not m['ret_ok'] else 'CONDENSING RANGE' if m['condensing'] else
                'READY TO CONDENSE' if m['in_range'] else 'RETURN TOO WARM')
    cond_colour = S_DHW if not m['ret_ok'] else S_OK if (m['condensing'] or m['in_range']) else S_FLOW
    cond2 = f"RETURN {_r(m['ret'], 1)} °C" if m['ret_ok'] else 'CHECK SENSOR'
    cond2_colour = S_OK if m['in_range'] else S_DHW if not m['ret_ok'] else S_DIM
    try:
        target_text = _r(float(m['nest_target']), 1) + '°' if m['nest_target'] is not None else '—'
    except (TypeError, ValueError):
        target_text = '—'
    if m['useful_recent']:
        cylinder_value = deg(m['useful'], True, '—')
        cylinder_tag = 'TOP · ' + ('LIVE' if m['hot_draw'] else
                                     f"{int(round(m['useful_age']))}M AGO" if m['useful_age'] < 60 else
                                     f"{_r(m['useful_age'] / 60, 1)}H AGO")
        cylinder_sub = 'BULK|' + deg(m['cyl'], m['cyl_ok'], '—')
    else:
        cylinder_value = deg(m['cyl'], m['cyl_ok'], '—')
        cylinder_tag = 'BULK ESTIMATE' if m['cyl_ok'] else 'ESTIMATE PENDING'
        cylinder_sub = ('STALE TOP|' + deg(m['useful'], True, '—', '°') if m['useful_ok'] else
                        'TOP|NOT OBSERVED' if m['cyl_ok'] else '—')
    texts = [
        [deg(m['spur'], m['spur_ok'], 'n/a', ' W'), S_TXT],
        [deg(m['gas_kw'], m['gas_ok'], 'n/a', ' kW'), S_GAS],
        [deg(m['flow'], m['flow_ok']), S_DHW if stale_bad(m['flow_ok'], m['flow_stale']) else S_FLOW],
        [flow_tag, S_DIM],
        [deg(m['ret'], m['ret_ok']), S_DHW if stale_bad(m['ret_ok'], m['ret_stale']) else S_COLD],
        [ret_tag, S_DIM],
        ['Δ ' + _r(m['delta'], 1) + ' K' if m['delta_ok'] else 'n/a', S_OK if m['condensing'] else S_TXT],
        [deg(pr, m['pressure_ok'], 'n/a', ' bar', 2), pr_colour],
        ['Outdoor · ' + deg(m['outside'], m['outside_ok']), S_DIM],
        ['Curve · ' + (_r(m['curve'], 2) if m['curve_ok'] else 'n/a'), S_DIM],
        [limits, S_DIM],
        [_status(m), S_OK if m['safe'] else S_FLOW],
        [cond_tag, cond_colour],
        [cond2, cond2_colour],
        [ret_probe, S_DHW if stale_bad(m['ret_ok'], m['ret_stale']) else S_DIM],
        [(_r(m['flow'], 1) + ' °C') if m['flow_ok'] else 'UNAVAILABLE', S_DHW if stale_bad(m['flow_ok'], m['flow_stale']) else S_DIM],
        [(_r(m['rate'], 1) if m['rate_ok'] else '—') + ' L/min', S_DIM],
        [draw_probe, S_DHW if stale_bad(m['draw_ok'], m['draw_stale']) else S_DIM],
        [deg(m['intake'], m['intake_ok']), S_TXT if m['intake_ok'] else S_DHW],
        [cylinder_value, S_TXT if (m['useful_recent'] or m['cyl_ok']) else S_DHW],
        [cylinder_tag, S_DIM],
        [_title(m['circuit']) if m['circuit_ok'] else 'n/a', S_TXT],
        [_title(m['pump_state']) if m['pump_ok'] else 'n/a', S_TXT],
        ['n/a' if not m['imm_ok'] else 'Active' if m['immersion_active'] else 'Off', S_ELEC if m['immersion_heating'] else S_DIM],
        [f"{int(round(m['imm_w']))} W" if m['imm_ok'] else '--', S_ELEC if m['immersion_heating'] else S_DIM],
        [target_text, S_TXT],
        [_r(m['home'], 1) if m['home_ok'] and m['home'] is not None else 'n/a', S_TXT],
        ['Calling' if m['heating_call'] else 'No demand', S_FLOW if m['heating_call'] else S_DIM],
        [hw_text, hw_colour],
        [draw_line, S_DHW if (not m['draw_ok'] or m['draw_stale'] or m['hot_draw']) else S_DIM],
        [(_r(m['mains'], 2) + ' L/min · ' + ('Drawing' if m['mains_active'] else 'No draw')) if m['mains_ok'] else 'n/a',
         S_COLD if m['mains_active'] else S_DIM],
        [(_r(m['mains'], 2) + ' L/min') if m['mains_ok'] else 'Unavailable', S_COLD if m['mains_active'] else S_DIM],
        ['Salt · unavailable' if not m['salt_ok'] else 'Salt low · top up' if m['salt_low'] else 'Salt level OK',
         S_DHW if not m['salt_ok'] else S_FLOW if m['salt_low'] else S_DIM],
        [cylinder_sub, S_DIM],
    ]
    return {'f': flags, 'p': probes, 'rh': -1 if hue is None else hue, 'my': int(round(mix_y * 10)),
            'co': [int(round(max(0.0, off - 0.06) * 1000)), int(round(min(1.0, off + 0.06) * 1000))],
            'x': texts, 'e': [entity_of(mapping, role) for role in SCENE_ENTITIES]}


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
    busy = f['hot_draw'] or f['dhw_call'] or f['immersion_heating']
    hot = {'k': 'hot', 'h': 'Hot draw' if f['hot_draw'] else 'Heating cylinder' if f['dhw_call'] else 'Immersion on' if f['immersion_active'] else 'Standby',
           'c': HOT if busy else IDLE, 'r': [
        _row('Cylinder (est.)', _fmt(f['cylinder'], '°C'), probe(f['cylinder'], True), e('cylinder')),
        _row('Megaflo intake', _fmt(f['intake'], '°C') + (' stale' if f['stale_intake'] else ''),
             IDLE if f['stale_intake'] else probe(f['intake']), e('megaflo_intake')),
        _row('Last draw-off', _fmt(f['draw'], '°C') + (' stale' if f['stale_draw'] else ''),
             IDLE if f['stale_draw'] else probe(f['draw'], True), e('draw_off')),
        _row('Immersion', 'On' if f['immersion_active'] else 'Off', IMMERSION if f['immersion_heating'] else IDLE, e('immersion')),
        _row('Schedule', 'Active' if f['schedule'] else 'Off', HOT if f['schedule'] else IDLE, e('hot_water')),
    ]}
    cold = {'k': 'cold', 'h': 'Mains running' if f['mains_active'] else 'Mains idle', 'c': COLD if f['mains_active'] else IDLE, 'r': [
        _row('Mains flow', _fmt(f['mains'], 'L/min'), COLD if f['mains_active'] else IDLE, e('mains_flow')),
        _row('Softener salt', 'Low' if f['salt_low'] else 'OK', WARN if f['salt_low'] else OK, e('salt_low')),
    ]}
    return {
        'b': [f['boiler'], boiler_text, boiler_colour],
        'tabs': [heating, hot, cold],
        'a': {'e': e('thermostat'), 'c': controls(f), 'cf': 'Tap again to confirm'},
        'sc': scene(states, mapping, now),
    }
