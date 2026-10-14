"""The PD console cards are drawn as pictures: the right sizes, and only changed pictures are written."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'screen_manager/app'))
import pd_cards  # noqa: E402

STATES = json.loads((ROOT / 'tests/fixtures/pd_cards_states.json').read_text())


class PdCards(unittest.TestCase):
    def test_control_mode_and_choices_travel_without_changing_any_entity(self):
        import copy
        from core import extras
        states = copy.deepcopy(STATES)
        states['input_select.pd_control_mode'] = {'state': 'Stream Weighted', 'attributes': {'options': ['Master Distribution', 'Stream Weighted', 'Off']}}
        before = copy.deepcopy(states)
        for entity in ('screen.pd_header', 'screen.pd_batteries'):
            value = extras({'entity': entity, 'options': {'display': 'map'}}, states)
            self.assertEqual(value['pm'], 'Stream Weighted')
            self.assertEqual(value['po'], ['Master Distribution', 'Stream Weighted', 'Off'])
        self.assertEqual(states, before)
        del states['input_select.pd_control_mode']
        value = extras({'entity': 'screen.pd_header', 'options': {'display': 'map'}}, states)
        self.assertEqual((value['pm'], value['po']), ('unavailable', []))

    def test_selector_action_is_configurable_and_rejects_non_select_targets(self):
        import catalogue
        from core import validate_layout
        tile = {'entity': 'screen.pd_header', 'name': '', 'slot': 0, 'options': {'display': 'map',
                'tap': 'action', 'action': {'action': catalogue.PD['action'], 'data': {'entity': 'select.example'}}}}
        checked = validate_layout({'title': '', 'tiles': [tile]})
        self.assertEqual(catalogue.selector_target(checked['tiles'][0]), 'select.example')
        tile['options']['action']['data']['entity'] = 'input_boolean.example'
        with self.assertRaises(ValueError):
            validate_layout({'title': '', 'tiles': [tile]})

    def test_legacy_selector_migration_preserves_nothing_and_explicit_actions(self):
        import catalogue
        self.assertEqual(catalogue.pd_options({'tap': 'none'}), {'tap': 'none'})
        original = {'tap': 'action', 'action': {'action': catalogue.PD['action'], 'data': {'entity': 'select.example'}}}
        self.assertEqual(catalogue.pd_options(original), original)
        self.assertEqual(catalogue.selector_target({'entity': 'screen.pd_header', 'options': {}}), catalogue.PD['legacy_target'])

    def test_composite_card_follows_each_battery(self):
        import copy
        initial = pd_cards.render('batteries', STATES)
        for key in ('dp3', 'ultra', 'ac', 'max'):
            changed = copy.deepcopy(STATES)
            batteries = json.loads(changed[pd_cards.BATTERIES]['attributes']['batteries'])
            batteries[key]['soc'] = 12.3
            changed[pd_cards.BATTERIES]['attributes']['batteries'] = json.dumps(batteries)
            self.assertNotEqual(pd_cards.render('batteries', changed), initial, key)
            self.assertNotEqual(pd_cards.fingerprint({'entity': 'screen.pd_batteries'}, changed),
                                pd_cards.fingerprint({'entity': 'screen.pd_batteries'}, STATES), key)

    def test_screen_mark_waits_for_download_then_follows_latest_data(self):
        import copy
        tile, cache = {'entity': 'screen.pd_header'}, {}
        initial = pd_cards.screen_fingerprint(tile, STATES, now=0, cache=cache)
        changed = copy.deepcopy(STATES)
        changed[pd_cards.HEADER]['attributes']['grid_w'] = 12345
        self.assertEqual(pd_cards.screen_fingerprint(tile, changed, now=1, cache=cache), initial)
        self.assertNotEqual(pd_cards.screen_fingerprint(tile, changed, now=pd_cards.SCREEN_REFRESH_SECONDS, cache=cache), initial)
        self.assertNotEqual(pd_cards.fingerprint(tile, changed), initial)

    def test_confirmed_mode_refreshes_both_cards_before_telemetry_interval(self):
        import copy
        changed = copy.deepcopy(STATES)
        changed[pd_cards.HEADER]['attributes']['mode'] = 'New confirmed mode'
        for entity in ('screen.pd_header', 'screen.pd_batteries'):
            tile, cache = {'entity': entity}, {}
            initial = pd_cards.screen_fingerprint(tile, STATES, now=0, cache=cache)
            latest = pd_cards.screen_fingerprint(tile, changed, now=1, cache=cache)
            self.assertNotEqual(latest, initial, entity)
            telemetry = copy.deepcopy(changed)
            telemetry[pd_cards.HEADER]['attributes']['grid_w'] = 12345
            self.assertEqual(pd_cards.screen_fingerprint(tile, telemetry, now=2, cache=cache), latest)

    def test_selector_change_waits_for_rendered_sensor_confirmation(self):
        import copy
        tile, cache = {'entity': 'screen.pd_header'}, {}
        initial = pd_cards.screen_fingerprint(tile, STATES, now=0, cache=cache)
        changed = copy.deepcopy(STATES)
        changed['input_select.pd_control_mode'] = {'state': 'New requested mode', 'attributes': {}}
        self.assertEqual(pd_cards.screen_fingerprint(tile, changed, now=1, cache=cache), initial)

    def test_header_collapses_template_whitespace_like_html(self):
        import copy
        multiline = copy.deepcopy(STATES)
        multiline[pd_cards.HEADER]['attributes']['story'] = '👑 DP3 is Primary · → UX in ~\n  6h 55m\n (17:31) 👑'
        singleline = copy.deepcopy(multiline)
        singleline[pd_cards.HEADER]['attributes']['story'] = pd_cards.inline_text(
            multiline[pd_cards.HEADER]['attributes']['story'])
        self.assertEqual(pd_cards.render('header', multiline), pd_cards.render('header', singleline))

    def test_hexagram_uses_symbol_font(self):
        self.assertEqual(pd_cards.runs('䷡'), [('symbol', '䷡')])

    def test_every_card_is_a_png(self):
        for name in pd_cards.NAMES:
            self.assertTrue(pd_cards.render(name, STATES, 'light', 1).startswith(b'\x89PNG'), name)

    def test_header_has_the_cards_proportions(self):
        from PIL import Image
        import io
        image = Image.open(io.BytesIO(pd_cards.render('header', STATES, 'light', 1)))
        self.assertEqual(image.size, pd_cards.HEADER_SIZE)

    def test_only_changed_pictures_are_written(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertEqual(pd_cards.write_all(STATES, folder, scale=1), list(pd_cards.NAMES))
            self.assertEqual(pd_cards.write_all(STATES, folder, scale=1), [])

    def test_library_cards_draw_at_the_size_asked(self):
        for entity in pd_cards.TILES:
            image = pd_cards.render_tile({'entity': entity}, STATES, (400, 200), dark=True)
            self.assertEqual(image.size, (400, 200), entity)
        self.assertTrue(pd_cards.fingerprint({'entity': 'screen.pd_dp3'}, STATES))

    def test_fingerprint_follows_the_card_state(self):
        before = pd_cards.fingerprint({'entity': 'screen.pd_dp3'}, STATES)
        changed = {**STATES, pd_cards.ELIGIBILITY['dp3']: {'state': 'off', 'attributes': {}}}
        self.assertNotEqual(before, pd_cards.fingerprint({'entity': 'screen.pd_dp3'}, changed))


if __name__ == '__main__':
    unittest.main()
