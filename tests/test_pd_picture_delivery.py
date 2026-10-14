"""PD download URLs pin bytes even when HA changes after the URL is issued."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'screen_manager/app'))
import camera_feed
from PIL import Image

class PdPictures(unittest.IsolatedAsyncioTestCase):
    async def test_link_never_renders_again_after_sensor_change(self):
        async def fetch(entity):
            raise AssertionError('PD cards do not fetch a camera')
        feed = camera_feed.CameraFeed(fetch)
        state, draws = ['first'], []
        async def draw(width, height):
            draws.append(state[0])
            return Image.new('RGB', (width, height), 'black' if state[0] == 'first' else 'white')
        options = dict(atlas=(40, 40, ((0, 0, 40, 40, 0, 0),)), compact=True, rle=True,
                       renders=[(lambda: state[0], draw)])
        first = await feed.live(['screen.pd_batteries'], 54, [0], [15], **options)
        token = feed.link('screen.pd_batteries', (40, 40), prepared=first[:2])
        state[0] = 'second'
        status, body, tag = await feed.serve(token)
        self.assertEqual((status, body, tag), (200, first[1], first[0]))
        self.assertEqual(draws, ['first'])
        second = await feed.live(['screen.pd_batteries'], 54, [0], [15], **options)
        self.assertNotEqual(first[1], second[1])
        await feed.live(['screen.pd_batteries'], 54, [0], [15], **options)
        self.assertEqual(draws, ['first', 'second'])
        self.assertEqual((await feed.serve(token))[1], first[1])

    async def test_pinned_links_use_normal_expiry(self):
        async def fetch(entity): return None
        clock = [0]
        feed = camera_feed.CameraFeed(fetch, clock=lambda: clock[0])
        token = feed.link('screen.pd_header', (40, 40), prepared=('tag', b'pixels'))
        self.assertEqual(feed.links[token].lifetime, camera_feed.LINK_SECONDS)
        clock[0] = camera_feed.LINK_SECONDS + 1
        self.assertEqual((await feed.serve(token))[0], 404)

if __name__ == '__main__': unittest.main()
