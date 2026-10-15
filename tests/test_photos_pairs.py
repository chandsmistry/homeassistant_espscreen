"""Portrait pairs preserve every album entry and the playback order."""
import asyncio
import io
import random
import tempfile
import unittest
from pathlib import Path
import test_photos
import photos
from PIL import Image


class PortraitPairsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.show = photos.PhotoShow(str(self.folder), rng=random.Random(7))
        self.settings = {'photo_fit': 'pair', 'photo_order': 'filename'}

    def album(self, shapes):
        paths = []
        for i, shape in enumerate(shapes):
            path = self.folder / f'{i:02}.jpg'
            Image.new('RGB', (40, 80) if shape == 'P' else (128, 80), 'red').save(path)
            paths.append(str(path))
        return paths

    def round(self, count):
        groups, seen = [], []
        while len(seen) < count:
            group = self.show.next_paths(self.settings)
            groups.append(group)
            seen.extend(group)
        return groups, seen

    def test_portraits_pair_in_equal_frames(self):
        paths = self.album('PPPP')
        groups, seen = self.round(4)
        self.assertEqual(groups, [tuple(paths[:2]), tuple(paths[2:])])
        self.assertEqual(seen, paths)
        _, data = self.show.render(groups[0], (128, 80), 'pair', jpeg=True)
        with Image.open(io.BytesIO(data)) as image:
            # Both portrait images retain 40 x 80 geometry, centred in equal 63px frames.
            self.assertGreater(image.getpixel((31, 40))[0], 200)
            self.assertGreater(image.getpixel((96, 40))[0], 200)
            self.assertLess(max(image.getpixel((0, 40))), 20)
            self.assertLess(max(image.getpixel((127, 40))), 20)

    def test_landscapes_remain_single_full_frames(self):
        paths = self.album('LLL')
        groups, seen = self.round(3)
        self.assertEqual(groups, [(p,) for p in paths])
        _, data = self.show.render(groups[0], (128, 80), 'pair', jpeg=True)
        with Image.open(io.BytesIO(data)) as image:
            self.assertGreater(image.getpixel((0, 40))[0], 200)
            self.assertGreater(image.getpixel((127, 40))[0], 200)
        self.assertEqual(seen, paths)

    def test_mixed_filename_order_is_not_reordered_or_filtered(self):
        paths = self.album('PPLPLPP')
        groups, seen = self.round(7)
        self.assertEqual(groups, [tuple(paths[:2]), (paths[2],), (paths[3],), (paths[4],), tuple(paths[5:])])
        self.assertEqual(seen, paths)

    def test_odd_portrait_never_pairs_across_rounds(self):
        paths = self.album('PPP')
        groups, seen = self.round(3)
        self.assertEqual(groups, [tuple(paths[:2]), (paths[2],)])
        self.assertEqual(seen, paths)
        self.assertEqual(self.show.next_paths(self.settings), tuple(paths[:2]))

    def test_single_portrait_is_alone_centred(self):
        paths = self.album('P')
        self.assertEqual(self.show.next_paths(self.settings), tuple(paths))
        _, data = self.show.render(tuple(paths), (128, 80), 'pair', jpeg=True)
        with Image.open(io.BytesIO(data)) as image:
            self.assertGreater(image.getpixel((64, 40))[0], 200)
            self.assertLess(max(image.getpixel((0, 40))), 20)
            self.assertLess(max(image.getpixel((127, 40))), 20)

    def test_shuffle_has_no_duplicates_or_dropped_files_in_each_round(self):
        paths = self.album('PPLPLPP')
        self.settings['photo_order'] = 'shuffle'
        for _ in range(4):
            groups, seen = self.round(len(paths))
            self.assertCountEqual(seen, paths)
            self.assertEqual(len(set(seen)), len(paths))
            for group in groups:
                if len(group) == 2:
                    self.assertTrue(all(self.show.metadata[p][2] for p in group))

    def test_exif_orientation_determines_portrait_pairing(self):
        paths = self.album('LP')
        with Image.open(paths[0]) as source:
            source.load()
            exif = source.getexif()
            exif[274] = 6
            source.save(paths[0], exif=exif)
        self.assertEqual(self.show.next_paths(self.settings), tuple(paths))
        self.assertTrue(self.show.metadata[paths[0]][2])

    def test_preview_and_live_share_grouping_without_advancing_cursor(self):
        paths = self.album('LPP')
        self.assertEqual(self.show.preview_paths(self.settings), (paths[0],))
        self.assertEqual(self.show.album.order, [])
        self.assertEqual(self.show.next_paths(self.settings), (paths[0],))
        remaining = list(self.show.album.order)
        rng = self.show.album.rng.getstate()
        asyncio.run(self.show.frame(self.settings, (128, 80), preview=True))
        self.assertEqual(self.show.album.order, remaining)
        self.assertEqual(self.show.album.rng.getstate(), rng)
        self.assertEqual(self.show.next_paths(self.settings), tuple(paths[1:]))

    def test_non_pair_modes_keep_single_photo_selection(self):
        paths = self.album('PP')
        for fit in ('fit', 'fill'):
            self.show.album.order = []
            self.settings['photo_fit'] = fit
            self.assertEqual(self.show.next_paths(self.settings), (paths[0],))

    def test_warm_prefetch_and_manual_next_keep_mixed_round_order(self):
        paths = self.album('PPLP')
        self.settings['photo_seconds'] = 30

        async def playback():
            await self.show.warm(self.settings, (128, 80), jpeg=True)
            self.assertEqual(self.show.ahead, tuple(paths[:2]))
            self.assertEqual(self.show.current_paths, ())
            await self.show.frame(self.settings, (128, 80), jpeg=True)
            self.assertEqual(self.show.current_paths, tuple(paths[:2]))
            self.assertEqual(self.show.ahead, (paths[2],))
            self.assertTrue(await self.show.advance(('test', 1)))
            await self.show.frame(self.settings, (128, 80), jpeg=True)
            self.assertEqual(self.show.current_paths, (paths[2],))
            self.assertEqual(self.show.ahead, (paths[3],))
            self.assertFalse(await self.show.advance(('test', 1)))
            self.assertTrue(await self.show.advance(('test', 2)))
            await self.show.frame(self.settings, (128, 80), jpeg=True)
            self.assertEqual(self.show.current_paths, (paths[3],))
            if self.show.prefetch:
                await self.show.prefetch

        asyncio.run(playback())
