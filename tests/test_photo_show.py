import asyncio
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'screen_manager/app'))
import photos
import screen_saver

class PhotoShowTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.now=100.0
        self.show=photos.PhotoShow(self.temp.name,clock=lambda:self.now)
    def make(self,name,size,color):
        path=Path(self.temp.name)/name;Image.new('RGB',size,color).save(path);return path
    async def test_pair_distinct_portraits_and_equal_gutter(self):
        self.make('a.png',(40,80),'red');self.make('b.png',(40,80),'blue');self.make('landscape.png',(80,40),'green')
        _,raw=await self.show.frame({'photo_fit':'pair','photo_order':'filename'},(160,80))
        im=Image.open(io.BytesIO(raw));self.assertGreater(im.getpixel((40,40))[0],200);self.assertGreater(im.getpixel((120,40))[2],200)
        self.assertLess(max(im.getpixel((79,40))),30);self.assertEqual(len(set(self.show.current_paths)),2)
    async def test_fit_keeps_full_image_and_fill_crops(self):
        p=self.make('p.png',(40,80),'red');stamp=p.stat().st_mtime_ns
        _,fit=await self.show.frame({'photo_fit':'fit'},(160,80),preview=True)
        _,fill=await self.show.frame({'photo_fit':'fill'},(160,80),preview=True)
        self.assertLess(max(Image.open(io.BytesIO(fit)).getpixel((0,40))),20)
        self.assertGreater(Image.open(io.BytesIO(fill)).getpixel((0,40))[0],200)
        self.assertEqual(p.stat().st_mtime_ns,stamp);self.assertIsNone(self.show.since)
    async def test_cache_and_content_invalidation(self):
        p=self.make('p.png',(40,80),'red')
        first=await self.show.frame({},(160,80));second=await self.show.frame({},(160,80));self.assertIs(first,second)
        self.make('p.png',(40,80),'blue');third=await self.show.frame({},(160,80));self.assertNotEqual(first,third)
    async def test_filename_round_and_independent_screens(self):
        for name in ('c.png','a.png','b.png'):self.make(name,(40,80),'red')
        await self.show.frame({'photo_order':'filename'},(160,80));self.assertTrue(self.show.current_paths[0].endswith('a.png'))
        self.now+=31;await self.show.frame({'photo_order':'filename'},(160,80));self.assertTrue(self.show.current_paths[0].endswith('b.png'))
        other=photos.PhotoShow(self.temp.name);await other.frame({'photo_order':'filename'},(160,80));self.assertTrue(other.current_paths[0].endswith('a.png'))
    async def test_status_size_unreadable_missing(self):
        self.make('good.png',(40,80),'red');(Path(self.temp.name)/'bad.jpg').write_bytes(b'broken')
        (Path(self.temp.name)/'huge.png').write_bytes(b'x'*(2*1024*1024))
        report=self.show.describe(1);self.assertEqual(report['usable'],1);self.assertEqual({e['reason'] for e in report['skipped']},{'size_limit','unreadable'})
        self.assertFalse(photos.PhotoShow(self.temp.name+'/missing').describe()['exists'])
    async def test_one_portrait_and_no_portrait_fallback(self):
        self.make('one.png',(40,80),'red');await self.show.frame({'photo_fit':'pair'},(160,80));self.assertEqual(len(self.show.current_paths),1)
        self.make('one.png',(80,40),'red');_,im=await self.show.frame({'photo_fit':'pair'},(160,80),preview=True);self.assertEqual(Image.open(io.BytesIO(im)).size,(160,80))
    async def test_prepared_photo_get_reuses_bytes_and_etag_without_rendering(self):
        from camera_feed import CameraFeed
        async def unexpected(_):
            raise AssertionError('GET must not fetch or render')
        feed=CameraFeed(unexpected)
        token=feed.link(photos.PHOTOS,(160,80),prepared=('"photo"',b'finished frame'))
        self.assertEqual(await feed.serve(token),(200,b'finished frame','"photo"'))
        self.assertEqual(await feed.serve(token,'"photo"'),(304,None,'"photo"'))
        pd=feed.link('screen.pd_header',(160,80),prepared=('"pd"',b'pd frame'))
        self.assertEqual(await feed.serve(pd,'"pd"'),(200,b'pd frame','"pd"'))
    async def test_slideshow_stable_url_advances_after_interval(self):
        from camera_feed import CameraFeed
        import hashlib
        self.make('a.png',(40,80),'red');self.make('b.png',(40,80),'blue')
        async def frame():
            image,_=await self.show.frame({'photo_order':'filename','photo_seconds':30},(160,80),rle=True)
            return hashlib.sha256(image).hexdigest(),image
        async def unexpected(_):
            raise AssertionError('must not fetch through Home Assistant')
        feed=CameraFeed(unexpected)
        prepared=await frame()
        token=feed.link(photos.PHOTOS,(160,80),prepared=prepared,refresh=frame)
        status,first,tag=await feed.serve(token)
        self.assertEqual(status,200)
        self.now+=29
        self.assertEqual(await feed.serve(token,tag),(304,None,tag))
        self.now+=2
        status,second,next_tag=await feed.serve(token,tag)
        self.assertEqual(status,200);self.assertNotEqual(first,second);self.assertNotEqual(tag,next_tag)
        self.assertEqual(await feed.serve(token,next_tag),(304,None,next_tag))

    async def test_finished_cache_stays_bounded(self):
        for i in range(7):self.make(f'{i}.png',(40,80),'red')
        for i in range(8):
            await self.show.frame({'photo_order':'filename'},(160,80));self.now+=31
        if self.show.prefetch:await self.show.prefetch
        self.assertLessEqual(len(self.show.frames),4)
        self.assertLessEqual(sum(len(a)+len(b) for a,b in self.show.frames.values()),16*1024*1024)
    async def test_warm_prepares_first_frame_without_starting_interval(self):
        self.make('a.png',(40,80),'red');self.make('b.png',(40,80),'blue')
        settings={'photo_order':'filename'}
        await self.show.warm(settings,(160,80),rle=True)
        self.assertIsNone(self.show.since);first=self.show.ahead
        cached=next(iter(self.show.frames.values()))
        shown=await self.show.frame(settings,(160,80),rle=True)
        self.assertEqual(self.show.current_paths,first);self.assertIs(shown,cached)
    async def test_rle_is_smaller_and_preserves_all_pixels(self):
        path=self.make('a.png',(40,80),'red')
        plain=self.show.render((str(path),),(160,80),'fit')[0]
        packed=self.show.render((str(path),),(160,80),'fit',True)[0]
        self.assertLess(len(packed),len(plain))
        self.assertEqual(Image.open(io.BytesIO(plain)).convert('RGB').tobytes(),Image.open(io.BytesIO(packed)).convert('RGB').tobytes())
    def test_validation_and_old_defaults(self):
        self.assertNotIn('photo_transition',screen_saver.validate({}))
        for name in ('none','fade','wipe_left','slide_up','zoom_in','pan_right','diamond','checker','spiral','blocks','blinds','random'):
            self.assertEqual(screen_saver.validate({'photo_transition':name})['photo_transition'],name)
        for data in ({'photo_fit':'bad'},{'photo_clock':1},{'photo_transition':'video'},{'photo_clock_position':'wrong'}):
            with self.assertRaises(ValueError):screen_saver.validate(data)
