"""The plant room card's derived states (screen_manager/app/plant_room.py)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'screen_manager/app'))
import plant_room as pr  # noqa: E402

NOW = 1_800_000_000.0


def st(value, unit=None, **attrs):
    a = dict(attrs)
    if unit:
        a['unit_of_measurement'] = unit
    return {'state': str(value), 'attributes': a, 'last_updated': '2027-01-15T08:00:00+00:00'}


def base():
    P = pr.PRESET
    return {
        P['signal']: st('on'), P['running']: st('on'), P['trouble']: st('off'),
        P['gas']: st(0, 'kW'), P['pump']: st('off'), P['primary_flow']: st(0),
        P['thermostat']: st('heat', hvac_action='idle', preset_mode='none'),
        P['hot_water']: st('eco', hot_water_schedule_active=False, operation_mode='eco'),
        P['flow_temp']: st(45), P['return_temp']: st(40), P['pressure']: st(1.5),
        P['cylinder']: st(55), P['mains_flow']: st(0, 'ml/s'), P['hot_draw']: st('off'),
    }


class Rules(unittest.TestCase):
    def test_offline_needs_signal_and_running(self):
        s = base(); s[pr.PRESET['running']] = st('off')
        self.assertEqual(pr.facts(s, now=NOW)['boiler'], pr.BOILER_OFFLINE)

    def test_standby_firing_fault(self):
        s = base()
        self.assertEqual(pr.facts(s, now=NOW)['boiler'], pr.BOILER_STANDBY)
        s[pr.PRESET['gas']] = st(0.5, 'kW')
        self.assertEqual(pr.facts(s, now=NOW)['boiler'], pr.BOILER_STANDBY)  # not above 0.5
        s[pr.PRESET['gas']] = st(4.2, 'kW')
        self.assertEqual(pr.facts(s, now=NOW)['boiler'], pr.BOILER_FIRING)
        s[pr.PRESET['trouble']] = st('on')
        self.assertEqual(pr.facts(s, now=NOW)['boiler'], pr.BOILER_FAULT)

    def test_calls_and_valves(self):
        s = base()
        f = pr.facts(s, now=NOW)
        self.assertFalse(f['heat_call'] or f['dhw_call'])
        s[pr.PRESET['thermostat']] = st('heat', hvac_action='heating')
        self.assertTrue(pr.facts(s, now=NOW)['zv1'])
        s[pr.PRESET['pump']] = st('running')
        s[pr.PRESET['hot_water']] = st('eco', hot_water_schedule_active=True)
        f = pr.facts(s, now=NOW)
        self.assertTrue(f['dhw_call'] and f['zv2'])
        s[pr.PRESET['pump']] = st('off'); s[pr.PRESET['primary_flow']] = st(0.6)
        self.assertTrue(pr.facts(s, now=NOW)['dhw_call'])  # primary flow alone makes the boiler active
        s[pr.PRESET['primary_flow']] = st(0.5)
        self.assertFalse(pr.facts(s, now=NOW)['dhw_call'])

    def test_mains_units(self):
        self.assertAlmostEqual(pr.mains_lpm(st(25, 'ml/s')), 1.5)
        self.assertAlmostEqual(pr.mains_lpm(st(0.05, 'L/s')), 3.0)
        self.assertAlmostEqual(pr.mains_lpm(st(2, 'L/min')), 2.0)
        s = base(); s[pr.PRESET['mains_flow']] = st(12.5, 'ml/s')  # 0.75 L/min
        self.assertTrue(pr.facts(s, now=NOW)['mains_active'])

    def test_pressure_and_condensing(self):
        s = base()
        for value, ok in ((0.9, False), (1.0, True), (2.7, True), (2.8, False)):
            s[pr.PRESET['pressure']] = st(value)
            self.assertEqual(pr.facts(s, now=NOW)['pressure_ok'], ok)
        s[pr.PRESET['return_temp']] = st(54)
        self.assertFalse(pr.facts(s, now=NOW)['condensing'])

    def test_stale(self):
        s = base()
        s[pr.PRESET['megaflo_intake']] = {'state': '40', 'attributes': {}, 'last_updated': '2027-01-15T07:00:00+00:00'}
        old = 1_800_000_000.0
        from datetime import datetime
        moment = datetime.fromisoformat('2027-01-15T07:00:00+00:00').timestamp()
        self.assertFalse(pr.facts(s, now=moment + 7200)['stale_intake'])
        self.assertTrue(pr.facts(s, now=moment + 7201)['stale_intake'])

    def test_probe_colours(self):
        self.assertEqual(pr.probe(None), pr.IDLE)
        self.assertEqual(pr.probe(0.5), pr.IDLE)
        self.assertEqual(pr.probe(101), pr.IDLE)
        self.assertEqual(pr.probe(29), pr.COLD)
        self.assertEqual(pr.probe(30), pr.HOT)
        self.assertEqual(pr.probe(49), pr.HOT)
        self.assertEqual(pr.probe(50), pr.HEAT)
        self.assertEqual(pr.probe(41, True), pr.HOT)
        self.assertEqual(pr.probe(42, True), pr.HEAT)

    def test_immersion(self):
        s = base()
        s[pr.PRESET['immersion_power']] = st(150, 'W')
        self.assertTrue(pr.facts(s, now=NOW)['immersion'])
        s[pr.PRESET['immersion_power']] = st(100, 'W')
        self.assertFalse(pr.facts(s, now=NOW)['immersion'])
        s[pr.PRESET['immersion']] = st('on')
        self.assertTrue(pr.facts(s, now=NOW)['immersion'])

    def test_thermocline(self):
        s = base()
        self.assertEqual(pr.thermocline(pr.facts(s, now=NOW)), round(8 + 35 / 40 * 84))
        s[pr.PRESET['cylinder']] = st(10)
        self.assertEqual(pr.thermocline(pr.facts(s, now=NOW)), 8)
        s[pr.PRESET['cylinder']] = st(90)
        self.assertEqual(pr.thermocline(pr.facts(s, now=NOW)), 92)
        s[pr.PRESET['mains_flow']] = st(2, 'L/min')
        self.assertEqual(pr.thermocline(pr.facts(s, now=NOW)), 72)  # cold draw lifts the cold region by 20


class Payload(unittest.TestCase):
    def test_shape_with_nothing(self):
        x = pr.payload({}, now=NOW)
        self.assertEqual(x['b'][0], pr.BOILER_OFFLINE)
        self.assertEqual([t['k'] for t in x['tabs']], ['heat', 'hot', 'cold'])
        self.assertEqual(x['tabs'][0]['r'][0][1], '–')
        self.assertIsNone(x['a']['hv'])

    def test_actions_and_entities(self):
        x = pr.payload(base(), now=NOW)
        self.assertEqual(x['a'], {'e': 'climate.hallway_thermostat', 'hv': 'heat', 'pm': 'none'})
        self.assertEqual(x['tabs'][0]['r'][0][3], pr.PRESET['flow_temp'])

    def test_mapping_overrides_one_role(self):
        m = pr.clean_mapping({'flow_temp': 'sensor.other', 'gas': pr.PRESET['gas'], 'bogus': 'sensor.x'})
        self.assertEqual(m, {'flow_temp': 'sensor.other'})
        self.assertIn('sensor.other', pr.mapped_entities(m))
        self.assertNotIn(pr.PRESET['flow_temp'], pr.mapped_entities(m))
        self.assertIsNone(pr.clean_mapping({'gas': 'not an entity'}))
        self.assertIsNone(pr.clean_mapping([]))
        self.assertEqual(pr.clean_mapping(None), {})

    def test_wire_is_small(self):
        import json
        self.assertLess(len(json.dumps(pr.payload(base(), now=NOW))), 2400)


class Layout(unittest.TestCase):
    def test_tile_in_a_layout(self):
        import core
        make = lambda options: core.validate_layout({'title': 'Plant', 'tiles': [
            {'entity': 'screen.plant_room', 'name': '', 'options': {'size': 'square', 'display': 'standard', **options}}]})
        layout = make({'plant_room': {'flow_temp': 'sensor.other', 'gas': pr.PRESET['gas']}})
        self.assertEqual(layout['tiles'][0]['options'], {'size': 'square', 'plant_room': {'flow_temp': 'sensor.other'}})
        self.assertEqual(core.min_firmware(layout), core.PLANT_ROOM_MIN_FIRMWARE)
        self.assertEqual(core.builtin_name('screen.plant_room', core.english), 'Plant room')
        with self.assertRaises(ValueError):
            make({'plant_room': {'gas': 'not an entity'}})
        other = core.validate_layout({'title': 'x', 'tiles': [{'entity': 'screen.clock', 'name': '', 'options': {'size': 'square', 'plant_room': {}}}]})
        self.assertNotIn('plant_room', other['tiles'][0]['options'])

    def test_message_carries_the_plant_room(self):
        import core
        tile = {'entity': 'screen.plant_room', 'name': '', 'options': {'size': 'square'}}
        message = core.state_message(0, tile, base(), core.extras(tile, base()))
        self.assertEqual(message['x']['b'][0], pr.BOILER_STANDBY)
        self.assertEqual(message['name'], 'Plant room')


if __name__ == '__main__':
    unittest.main()
