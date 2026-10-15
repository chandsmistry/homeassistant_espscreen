"""Negotiated full-resolution JPEG frames and legacy BMP delivery."""
import asyncio
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import test_photos
import photos
import camera_feed
from PIL import Image
from aiohttp.test_utils import TestClient, TestServer

class JpegPhotosTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.path = self.folder / 'a.jpg'
        Image.new('RGB', (128, 80), 'red').save(self.path, 'JPEG', quality=97, subsampling=0)
        self.show = photos.PhotoShow(str(self.folder))

    def test_exact_baseline_is_unchanged_without_bmp_encoding(self):
        original = self.path.read_bytes()
        with patch('tile_art.bmp', side_effect=AssertionError('BMP encoding should not run')):
            wire, preview = self.show.render((str(self.path),), (128, 80), 'fit', jpeg=True)
        self.assertEqual(wire, original)
        self.assertEqual(preview, original)
        self.assertEqual(self.path.read_bytes(), original)

    def test_geometry_pair_and_cache_encoding_isolation(self):
        b = self.folder / 'b.png'
        Image.new('RGB', (40, 90), 'blue').save(b)
        for mode, paths in [('fit', (str(b),)), ('fill', (str(b),)), ('pair', (str(b), str(b)))]:
            wire, preview = self.show.render(paths, (128, 80), mode, jpeg=True)
            with Image.open(io.BytesIO(wire)) as image:
                self.assertEqual(image.size, (128, 80))
                self.assertFalse(image.info.get('progressive'))
                self.assertEqual(image.mode, 'RGB')
            self.assertLessEqual(len(wire), photos.MAX_JPEG_BYTES)
            legacy, _ = self.show.render(paths, (128, 80), mode, rle=True)
            self.assertTrue(legacy.startswith(b'BM'))
            self.assertEqual(self.show.render(paths, (128, 80), mode, jpeg=True)[0], wire)
        fit = Image.open(io.BytesIO(self.show.render((str(b),), (128, 80), 'fit', jpeg=True)[0]))
        self.assertLess(max(fit.getpixel((0, 0))), 5)

    def test_progressive_reencoded_baseline_and_bound_falls_back_same_size(self):
        Image.new('RGB', (128, 80), 'green').save(self.path, 'JPEG', progressive=True)
        wire, _ = self.show.render((str(self.path),), (128, 80), 'fill', jpeg=True)
        self.assertFalse(Image.open(io.BytesIO(wire)).info.get('progressive'))
        self.show.frames.clear()
        with patch.object(photos, 'MAX_JPEG_BYTES', 10):
            wire, preview = self.show.render((str(self.path),), (128, 80), 'fill', jpeg=True)
        self.assertTrue(wire.startswith(b'BM'))
        self.assertEqual(Image.open(io.BytesIO(wire)).size, (128, 80))
        self.assertTrue(preview.startswith(b'\xff\xd8'))

    def test_unaligned_logical_frame_is_padded_not_resized(self):
        Image.new('RGB', (1278,799), (230,30,40)).save(self.path,'JPEG',quality=97)
        wire,_ = self.show.render((str(self.path),),(1278,799),'fit',jpeg=True)
        self.assertTrue(photos.baseline_jpeg(wire,(1278,799)))
        self.assertFalse(photos.baseline_jpeg(wire,(1280,800)))
        self.assertEqual(wire[2:6],b'\xff\xef\x00\x0e')
        self.assertEqual(wire[6:14],b'MNETCROP')
        self.assertEqual(int.from_bytes(wire[14:16],'big'),1278)
        self.assertEqual(int.from_bytes(wire[16:18],'big'),799)
        with Image.open(io.BytesIO(wire)) as decoded:
            self.assertEqual(decoded.size,(1280,800))
            self.assertEqual(decoded.getpixel((1277,798)),decoded.getpixel((1279,799)))
        legacy,_ = self.show.render((str(self.path),),(1278,799),'fit')
        self.assertEqual(Image.open(io.BytesIO(legacy)).size,(1278,799))

    def test_rgb_encoded_jpeg_is_reencoded_as_ycbcr(self):
        Image.new('RGB',(128,80),'red').save(self.path,'JPEG',keep_rgb=True)
        original=self.path.read_bytes()
        self.assertFalse(photos.baseline_jpeg(original))
        wire,_=self.show.render((str(self.path),),(128,80),'fit',jpeg=True)
        self.assertNotEqual(wire,original)
        self.assertTrue(photos.baseline_jpeg(wire))
        self.assertGreater(Image.open(io.BytesIO(wire)).getpixel((30,30))[0],240)

class JpegHttpTests(unittest.IsolatedAsyncioTestCase):
    async def test_stable_jpeg_link_refresh_etag_and_bmp_fallback(self):
        feed = camera_feed.CameraFeed(lambda entity: None)
        frames = [('"one"', b'\xff\xd8jpeg'), ('"two"', b'BMfallback')]
        current = [0]
        async def refresh(): return frames[current[0]]
        token = feed.link(photos.PHOTOS, (128,80), prepared=frames[0], refresh=refresh, content_type='image/jpeg')
        async with TestClient(TestServer(camera_feed.web_app(feed))) as client:
            response = await client.get('/camera/' + token + '.jpg')
            self.assertEqual(response.status, 200)
            self.assertEqual(response.content_type, 'image/jpeg')
            self.assertEqual(await response.read(), frames[0][1])
            unchanged = await client.get('/camera/' + token + '.jpg', headers={'If-None-Match': '"one"'})
            self.assertEqual(unchanged.status, 304)
            current[0] = 1
            response = await client.get('/camera/' + token + '.jpg', headers={'If-None-Match': '"one"'})
            self.assertEqual(response.status, 200)
            self.assertEqual(response.content_type, camera_feed.CONTENT_TYPE)
            self.assertEqual(response.headers['ETag'], '"two"')
            self.assertEqual(await response.read(), frames[1][1])

class JpegPlaybackTests(unittest.IsolatedAsyncioTestCase):
    async def test_warm_preview_and_future_bmp_do_not_reset_cursor(self):
        with tempfile.TemporaryDirectory() as folder:
            for name, colour in [('a.png', 'red'), ('b.png', 'blue')]:
                Image.new('RGB', (128,80), colour).save(Path(folder)/name)
            clock = test_photos.Clock()
            show = photos.PhotoShow(folder, clock=clock)
            settings = {'photo_fit':'fit','photo_order':'filename','photo_seconds':5}
            await show.warm(settings, (128,80), jpeg=True)
            self.assertIsNone(show.since)
            await show.frame(settings, (64,40), preview=True)
            self.assertIsNone(show.since)
            first, _ = await show.frame(settings, (128,80), jpeg=True)
            self.assertTrue(first.startswith(b'\xff\xd8'))
            await show.prefetch
            self.assertEqual(Path(show.current_paths[0]).name,'a.png')
            clock.now += 5
            show.frames.clear()
            with patch.object(photos, 'MAX_JPEG_BYTES', 10):
                second, _ = await show.frame(settings, (128,80), jpeg=True)
            self.assertTrue(second.startswith(b'BM'))
            self.assertEqual(Path(show.current_paths[0]).name,'b.png')
            self.assertTrue(show.settings[-1])
            if show.prefetch: await show.prefetch

class JpegEnvelopeTests(unittest.TestCase):
    def test_trailing_data_and_multiscan_are_not_passed_through(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'a.jpg'
            Image.new('RGB',(128,80),'red').save(path,'JPEG')
            original=path.read_bytes()
            self.assertTrue(photos.baseline_jpeg(original))
            self.assertFalse(photos.baseline_jpeg(original+b'trailer'))
            sos=original.index(b'\xff\xda')
            length=int.from_bytes(original[sos+2:sos+4],'big')
            invalid_selector=bytearray(original)
            invalid_selector[sos+6]=0x40
            self.assertFalse(photos.baseline_jpeg(invalid_selector))
            second_scan=original[sos:sos+2+length]
            self.assertFalse(photos.baseline_jpeg(original[:-2]+second_scan+b'\xff\xd9'))
            for mode in ('L', 'CMYK'):
                out = io.BytesIO()
                Image.new(mode, (128,80)).save(out, 'JPEG')
                self.assertFalse(photos.baseline_jpeg(out.getvalue()))
            path.write_bytes(original+b'trailer')
            wire,_=photos.PhotoShow(folder).render((str(path),),(128,80),'fit',jpeg=True)
            self.assertNotEqual(wire,path.read_bytes())
            self.assertTrue(photos.baseline_jpeg(wire))

class ManagerNegotiationTests(unittest.IsolatedAsyncioTestCase):
    async def test_answer_and_warm_follow_negotiated_encoding(self):
        import test_portal
        import catalogue
        from types import SimpleNamespace
        from unittest.mock import AsyncMock
        with tempfile.TemporaryDirectory() as folder:
            manager=test_portal.ManagerTests.setup_manager(self,Path(folder)/'screens.json')
            manager.save('text.screen',{'title':'Test','tiles':[],'pages':1})
            screen=manager.screen('text.screen');screen.update(device_id='device',online=True)
            album=Path(folder)/'photos';album.mkdir()
            Image.new('RGB',(128,80),'red').save(album/'a.jpg','JPEG')
            show=photos.PhotoShow(str(album));manager.photo_shows={'text.screen':show}
            manager.savers.set('device',{'show':True,'off':['media','camera','clock'],'photo_fit':'fit'})
            manager.send_auxiliary=AsyncMock()
            manager.ha.request=AsyncMock()
            with patch.object(manager,'saver_entities',return_value={photos.PHOTOS}), patch.object(manager,'transport',return_value='action'), patch.object(camera_feed,'box',return_value=(128,80)), patch.object(camera_feed,'base_url',new=AsyncMock(return_value='http://test')):
                for feature in [set(),{catalogue.PHOTOS['packed_feature']},{catalogue.PHOTOS['jpeg_feature']}]:
                    manager.page_senders['text.screen']=SimpleNamespace(features=feature)
                    await manager.answer_saver('text.screen',screen,{'saver':'camera','entity':photos.PHOTOS})
                    message=manager.send_auxiliary.call_args.args[1]
                    jpeg=catalogue.PHOTOS['jpeg_feature'] in feature
                    self.assertTrue(message['u'].endswith('.jpg' if jpeg else '.bmp'))
                    token=message['u'].rsplit('/',1)[1].split('.')[0]
                    status,body,_=await manager.camera.serve(token)
                    self.assertEqual(status,200)
                    self.assertTrue(body.startswith(b'\xff\xd8' if jpeg else b'BM'))
                    self.assertEqual(show.settings[-1],jpeg)
                    if show.prefetch: await show.prefetch
                sender=SimpleNamespace(features={catalogue.PHOTOS['jpeg_feature'],'screensaver'},protocol=2,confirmed='revision',session='session',auxiliary=AsyncMock(return_value=True))
                manager.page_senders['text.screen']=sender
                with patch.object(manager,'saver_message',return_value={'e':photos.PHOTOS,'k':'camera'}),patch.object(show,'warm',new=AsyncMock()) as warm:
                    await manager.sync_saver('text.screen',screen)
                    await manager.photo_warming['text.screen'][1]
                    self.assertTrue(warm.call_args.kwargs['jpeg'])
