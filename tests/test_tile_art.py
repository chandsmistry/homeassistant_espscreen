"""Request bounds and prepared pixels for the shared tile image buffer."""
import io
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'screen_manager/app'))
import tile_art
from PIL import Image

class TileArt(unittest.TestCase):
    def test_reject_unbounded_malformed_and_overlapping_frames(self):
        for frames in (None, {}, [[0,0,481,480,8,170]], [[-1,0,40,40,4,0]],
                       [[0,0,40,40,21,0]], [[0,0,40,40,4,255]],
                       [[False,0,40,40,4,0]], [[0,0,40,40,4]],
                       [[0,0,40,40,4,0],[20,20,40,40,4,0]]):
            count=len(frames) if isinstance(frames,list) else 1
            self.assertIsNone(tile_art.parse(json.dumps(frames),(480,480),count))
        self.assertIsNone(tile_art.parse('[]',(480,480),0))
        self.assertIsNone(tile_art.parse('not json',(480,480),1))

    def test_native_dimensions_rounding_and_readability_scrim(self):
        frames=[[4,8,80,40,8,170],[90,8,30,70,4,0]]
        atlas=tile_art.parse(json.dumps(frames),(1280,800),2)
        self.assertEqual(atlas[:2],(120,78))
        raw=io.BytesIO();Image.new('RGB',(200,200),'white').save(raw,'PNG')
        result=tile_art.encode([raw.getvalue(),None],[0xEEEEEE,0x123456],atlas)
        with Image.open(io.BytesIO(result)) as im:
            self.assertEqual(im.size,(120,78))
            self.assertTrue(all(84 <= channel <= 85 for channel in im.getpixel((44,28))))
            self.assertEqual(im.getpixel((4,8)),(238,238,238))
            self.assertEqual(im.getpixel((105,30)),(18,52,86))

    def test_rle_lossless_and_smaller(self):
        image = Image.new('RGB', (600, 700), '#101419')
        from PIL import ImageDraw
        ImageDraw.Draw(image).rectangle((20,20,500,80), fill='#22cc66')
        plain = tile_art.bmp(image, compact=True)
        packed = tile_art.bmp(image, compact=True, rle=True)
        self.assertLess(len(packed), len(plain) // 10)
        with Image.open(io.BytesIO(plain)) as a, Image.open(io.BytesIO(packed)) as b:
            self.assertEqual(a.convert('RGB').tobytes(), b.convert('RGB').tobytes())
        # Literal runs, odd padding and 255-byte run boundaries.
        image = Image.new('RGB', (521, 9))
        image.putdata([(x % 256, x*7 % 256, x*19 % 256) for x in range(521*9)])
        with Image.open(io.BytesIO(tile_art.bmp(image, True))) as a, Image.open(io.BytesIO(tile_art.bmp(image, True, True))) as b:
            self.assertEqual(a.convert('RGB').tobytes(), b.convert('RGB').tobytes())

    def test_board_canvas_is_the_allocation_limit(self):
        self.assertIsNone(tile_art.parse('[[0,0,800,480,8,170]]',(480,800),1))
        self.assertIsNotNone(tile_art.parse('[[0,0,800,480,8,170]]',(800,480),1))
        self.assertIsNotNone(tile_art.parse('[[0,0,1280,800,8,170]]',(1280,800),1))

if __name__=='__main__':unittest.main()
