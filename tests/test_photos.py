"""The Photos folder of the screensaver (screen_manager/app/photos.py): a shuffled round of the pictures in a folder, each
for a while, small files first in speed: a file over the size limit is skipped and a big photo is made small."""
import asyncio
import io
import os
import random
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'screen_manager/app'))
import photos  # noqa: E402
import screen_saver  # noqa: E402
from PIL import Image  # noqa: E402


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


def make(folder, name, size=(64, 48), colour=(200, 30, 30), noise=False):
    path = os.path.join(folder, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image = Image.new('RGB', size, colour)
    if noise:
        image.putdata([(random.randrange(256), random.randrange(256), random.randrange(256)) for _ in range(size[0] * size[1])])
    image.save(path, 'JPEG', quality=95)
    return path


class PhotosTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.folder = self.dir.name
        self.clock = Clock()
        self.photos = photos.Photos(self.folder, self.clock, random.Random(1))

    def tearDown(self):
        self.dir.cleanup()

    def test_empty_folder_is_unavailable(self):
        self.assertEqual(self.photos.state()['state'], 'unavailable')
        self.assertEqual(self.photos.current(), '')

    def test_missing_folder_is_unavailable(self):
        self.assertEqual(photos.Photos(os.path.join(self.folder, 'nope'), self.clock).state()['state'], 'unavailable')

    def test_a_round_shows_every_photo_once_then_starts_again(self):
        for name in ('a.jpg', 'b.jpg', 'c.JPG', 'sub/d.png'):
            make(self.folder, name)
        (Path(self.folder) / 'notes.txt').write_text('x')
        (Path(self.folder) / '.hidden.jpg').write_bytes(b'x')
        seen = []
        for _ in range(4):
            seen.append(os.path.basename(self.photos.current(30, 8)))
            self.clock.now += 31
        self.assertEqual(sorted(seen), ['a.jpg', 'b.jpg', 'c.JPG', 'd.png'])
        self.assertTrue(self.photos.current(30, 8))

    def test_a_photo_stays_for_its_seconds(self):
        make(self.folder, 'a.jpg')
        make(self.folder, 'b.jpg')
        first = self.photos.current(30, 8)
        self.clock.now += 29
        self.assertEqual(self.photos.current(30, 8), first)
        self.clock.now += 2
        self.assertNotEqual(self.photos.current(30, 8), first)

    def test_a_file_over_the_limit_is_skipped(self):
        big = make(self.folder, 'big.jpg', (1200, 1000), noise=True)
        make(self.folder, 'small.jpg')
        self.assertGreater(os.path.getsize(big), 1024 * 1024)
        for _ in range(6):
            self.assertEqual(os.path.basename(self.photos.current(15, 1)), 'small.jpg')
            self.clock.now += 16

    def test_prepare_cuts_to_the_edge_and_keeps_it_upright(self):
        path = make(self.folder, 'wide.jpg', (4000, 3000))
        data = self.photos.prepare(path)
        image = Image.open(io.BytesIO(data))
        self.assertEqual(max(image.size), photos.MAX_EDGE)
        self.assertEqual(image.format, 'JPEG')

    def test_an_unreadable_file_is_skipped_and_the_next_one_comes(self):
        (Path(self.folder) / 'broken.jpg').write_bytes(b'not a picture')
        make(self.folder, 'good.jpg')
        data = asyncio.run(self.photos.image({'seconds': 15, 'mb': 8}))
        self.assertEqual(Image.open(io.BytesIO(data)).format, 'JPEG')

    def test_image_raises_without_photos(self):
        with self.assertRaises(FileNotFoundError):
            asyncio.run(self.photos.image({}))


class ChoiceTest(unittest.TestCase):
    def test_photo_settings_are_kept_and_checked(self):
        choice = screen_saver.validate({'show': True, 'photo_seconds': 45, 'photo_mb': 4})
        self.assertEqual((choice['photo_seconds'], choice['photo_mb']), (45, 4))
        for bad in ({'photo_seconds': 5}, {'photo_seconds': 'x'}, {'photo_mb': 0}, {'photo_mb': 99}, {'photo_mb': True}):
            with self.assertRaises(ValueError):
                screen_saver.validate(bad)

    def test_photos_is_a_step_after_the_camera_and_an_old_order_still_reads(self):
        self.assertEqual(screen_saver.validate({'show': True})['order'], ['media', 'camera', 'photos', 'clock'])
        old = screen_saver.validate({'order': ['clock', 'media', 'camera']})['order']
        self.assertEqual(old, ['clock', 'media', 'camera', 'photos'])
        self.assertEqual(screen_saver.validate({'order': ['camera', 'photos', 'media', 'clock']})['order'],
                         ['camera', 'photos', 'media', 'clock'])
        with self.assertRaises(ValueError):
            screen_saver.validate({'order': ['media', 'photos', 'clock']})

    def test_photos_step_shows_as_the_camera_the_screen_knows(self):
        choice = screen_saver.validate({'show': True, 'order': ['photos', 'clock']}) if False else \
            screen_saver.validate({'show': True, 'order': ['photos', 'media', 'camera', 'clock']})
        here = {photos.PHOTOS: {'state': 'idle', 'attributes': {'friendly_name': 'Photos'}}}
        gone = {photos.PHOTOS: {'state': 'unavailable', 'attributes': {}}}
        self.assertEqual(screen_saver.pick(choice, here, True), 'photos')
        self.assertEqual(screen_saver.pick(choice, gone, True), 'clock')
        self.assertEqual(screen_saver.pick(choice, here, False), 'clock')  # a board without pictures
        message = screen_saver.message(choice, here, True, lambda text, n: text, lambda attrs: {})
        self.assertEqual(message, {'op': 'saver', 'k': 'camera', 'e': photos.PHOTOS, 'n': 'Photos'})
        self.assertIn(photos.PHOTOS, screen_saver.entities(choice))
        off = screen_saver.validate({'show': True, 'off': ['photos']})
        self.assertNotIn(photos.PHOTOS, screen_saver.entities(off))
        self.assertEqual(screen_saver.pick({**choice, 'off': ['photos']}, here, True), 'clock')


if __name__ == '__main__':
    unittest.main()
