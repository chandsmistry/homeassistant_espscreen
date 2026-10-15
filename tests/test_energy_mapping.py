"""The energy card with sensors of its own (tile option `energy_sources`): the mapping builds the same diagram Home
Assistant's Energy settings would, an empty mapping changes nothing."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'screen_manager/app'))
import energy_flow  # noqa: E402


def st(value, unit='W'):
    return {'state': str(value), 'attributes': {'unit_of_measurement': unit}}


class Mapping(unittest.TestCase):
    PREFS = {'energy_sources': [{'type': 'grid', 'stat_rate': 'sensor.g'}], 'device_consumption': []}
    STATES = {'sensor.g': st(500)}

    def test_empty_mapping_is_byte_for_byte_today(self):
        base = energy_flow.payload(self.PREFS, self.STATES)
        for empty in (None, {}, {'devices': []}, {'grid': ''}):
            self.assertEqual(energy_flow.payload(self.PREFS, self.STATES, mapping=empty), base)

    def test_signed_sensors(self):
        x = energy_flow.payload({}, {'sensor.g': st(-300), 'sensor.s': st(1.5, 'kW')},
                                mapping={'grid': 'sensor.g', 'solar': 'sensor.s'})
        self.assertEqual(x['h'], 3)
        self.assertEqual(x['p'][0], 1500.0)
        self.assertEqual(x['p'][2], 300.0)
        self.assertEqual(x['p'][5], 1200.0)

    def test_battery_pair_is_netted(self):
        states = {'sensor.out': st(800), 'sensor.in': st(0), 'sensor.soc': st(80, '%'), 'sensor.g': st(200)}
        x = energy_flow.payload({}, states, mapping={'grid': 'sensor.g', 'battery_out': 'sensor.out', 'battery_in': 'sensor.in', 'soc': 'sensor.soc'})
        self.assertEqual(x['p'][3], 800.0)
        self.assertEqual(x['p'][5], 1000.0)
        self.assertEqual(x['c'], 80)
        states['sensor.out'], states['sensor.in'] = st(0), st(600)
        x = energy_flow.payload({}, states, mapping={'grid': 'sensor.g', 'battery_out': 'sensor.out', 'battery_in': 'sensor.in'})
        self.assertEqual(x['p'][4], 600.0)
        self.assertEqual(x['e'][2], 'sensor.out')

    def test_grid_pair(self):
        x = energy_flow.payload({}, {'sensor.i': st(0), 'sensor.x': st(900)}, mapping={'grid_import': 'sensor.i', 'grid_export': 'sensor.x'})
        self.assertEqual(x['p'][2], 900.0)

    def test_home_override(self):
        states = {'sensor.g': st(500), 'sensor.h': st(450)}
        x = energy_flow.payload({}, states, mapping={'grid': 'sensor.g', 'home': 'sensor.h'})
        self.assertEqual(x['p'][5], 450.0)

    def test_missing_sensor_counts_zero(self):
        x = energy_flow.payload({}, {}, mapping={'grid': 'sensor.gone', 'solar': 'sensor.gone2'})
        self.assertEqual(x['p'], [0.0] * 6)

    def test_devices_in_order(self):
        states = {'sensor.g': st(1000), 'sensor.a': st(100), 'sensor.b': st(300, 'W')}
        m = {'grid': 'sensor.g', 'devices': [{'entity': 'sensor.a', 'name': 'Office'}, {'entity': 'sensor.b'}]}
        d = energy_flow.payload({}, states, mapping=m)['d']
        self.assertEqual([i['n'] for i in d], ['Office', 'sensor.b'])

    def test_entities_watched(self):
        m = {'grid': 'sensor.g', 'devices': [{'entity': 'sensor.a'}], 'soc': 'sensor.s'}
        self.assertEqual(energy_flow.mapped_entities(m), ['sensor.g', 'sensor.s', 'sensor.a'])

    def test_clean(self):
        self.assertEqual(energy_flow.clean_mapping({}), {})
        self.assertEqual(energy_flow.clean_mapping({'grid': 'sensor.g', 'solar': ''}), {'grid': 'sensor.g'})
        for bad in ('x', {'grid': 'light.x'}, {'nope': 'sensor.x'}, {'devices': [{'entity': 'sensor.a'}] * 5},
                    {'devices': [{'entity': 'sensor.a', 'x': 1}]}):
            self.assertIsNone(energy_flow.clean_mapping(bad))


if __name__ == '__main__':
    unittest.main()
