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
