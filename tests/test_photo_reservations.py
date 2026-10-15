"""Prepared Photos have immutable bytes and an acknowledged, once-only cursor."""
import asyncio
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch, AsyncMock
from test_photos import make, photos


class Reservations(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        for name, colour in [('a.jpg', (220, 0, 0)), ('b.jpg', (0, 220, 0)), ('c.jpg', (0, 0, 220))]:
            make(self.tmp.name, name, colour=colour)
        self.show = photos.PhotoShow(self.tmp.name)
        self.settings = {'photo_fit': 'fit', 'photo_order': 'filename', 'photo_mb': 8, 'photo_seconds': 5}

    async def reserve(self, commit='', session='one', settings=None):
        return await self.show.reserve(settings or self.settings, (128, 80), session, commit, jpeg=True)

    async def test_prepare_does_not_advance_visible_cursor(self):
        first = await self.reserve()
        self.assertEqual(self.show.current_paths, ())
        self.assertEqual(await self.reserve(), first)
        self.assertEqual(self.show.current_paths, ())

    async def test_acknowledgement_advances_once_and_retry_returns_same_next(self):
        first, _ = await self.reserve()
        second = await self.reserve(first)
        self.assertEqual(Path(self.show.current_paths[0]).name, 'a.jpg')
        self.assertEqual(await self.reserve(first), second)
        third = await self.reserve(second[0])
        self.assertEqual(Path(self.show.current_paths[0]).name, 'b.jpg')
        self.assertEqual(await self.reserve(first), third)

    async def test_concurrent_duplicate_commit_consumes_one(self):
        first, _ = await self.reserve()
        results = await asyncio.gather(*(self.reserve(first) for _ in range(5)))
        self.assertTrue(all(result == results[0] for result in results))
        self.assertEqual(Path(self.show.current_paths[0]).name, 'a.jpg')

    async def test_new_session_retires_old_session(self):
        await self.reserve()
        await self.reserve(session='two')
        with self.assertRaisesRegex(ValueError, 'retired'):
            await self.reserve()

    async def test_settings_change_invalidates_old_frame(self):
        first, _ = await self.reserve()
        changed = dict(self.settings, photo_fit='fill')
        second, _ = await self.reserve(first, settings=changed)
        self.assertNotEqual(first, second)
        self.assertEqual(self.show.current_paths, ())

    async def test_changed_file_invalidates_old_frame(self):
        first, _ = await self.reserve()
        make(self.tmp.name, 'a.jpg', colour=(1, 2, 3))
        second, _ = await self.reserve(first)
        self.assertNotEqual(first, second)
        self.assertEqual(self.show.current_paths, ())

    async def test_failed_render_preserves_order_and_can_retry(self):
        first, _ = await self.reserve()
        with patch.object(self.show, 'render', side_effect=ValueError('test failure')):
            with self.assertRaises(ValueError):
                await self.reserve(first)
        second, _ = await self.reserve(first)
        self.assertEqual(Path(self.show.reservation['paths'][0]).name, 'b.jpg')
        self.assertNotEqual(first, second)

    async def test_cancelled_render_preserves_next_selection(self):
        first, _ = await self.reserve()
        with patch.object(self.show, 'render', side_effect=asyncio.CancelledError):
            with self.assertRaises(asyncio.CancelledError):
                await self.reserve(first)
        await self.reserve(first)
        self.assertEqual(Path(self.show.reservation['paths'][0]).name, 'b.jpg')

    async def test_cancel_during_selection_waits_worker_before_restoring(self):
        import threading
        first, _ = await self.reserve()
        started, release = threading.Event(), threading.Event()
        original = self.show.next_paths
        def delayed(settings):
            paths = original(settings)
            started.set()
            release.wait(2)
            return paths
        with patch.object(self.show, 'next_paths', side_effect=delayed):
            task = asyncio.create_task(self.reserve(first))
            await asyncio.to_thread(started.wait, 2)
            task.cancel()
            release.set()
            with self.assertRaises(asyncio.CancelledError):
                await task
        await self.reserve(first)
        self.assertEqual(Path(self.show.reservation['paths'][0]).name, 'b.jpg')

    async def test_warming_does_not_replace_reserved_next(self):
        first = await self.reserve()
        await self.show.warm(self.settings, (128, 80), jpeg=True)
        self.assertEqual(await self.reserve(), first)

    async def test_unknown_ack_rejected_without_consuming(self):
        first = await self.reserve()
        with self.assertRaisesRegex(ValueError, 'unknown'):
            await self.reserve('unknown')
        self.assertEqual(await self.reserve(), first)

    async def test_immutable_link_get_never_calls_playback(self):
        import camera_feed
        frame_id, image = await self.reserve()
        feed = camera_feed.CameraFeed(AsyncMock())
        token = feed.link(photos.PHOTOS, (128, 80), prepared=(frame_id, image), content_type='image/jpeg')
        for _ in range(3):
            status, body, _ = await feed.serve(token)
            self.assertEqual((status, body), (200, image))
        self.assertEqual(self.show.current_paths, ())
        self.assertIsNone(feed.links[token].refresh)

class ManagerReservations(unittest.IsolatedAsyncioTestCase):
    async def test_protocol_is_capability_gated_and_get_is_immutable(self):
        import test_portal
        import camera_feed
        import catalogue
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as folder:
            manager = test_portal.ManagerTests.setup_manager(self, Path(folder) / 'screens.json')
            manager.save('text.screen', {'title': 'Test', 'tiles': [], 'pages': 1})
            screen = manager.screen('text.screen')
            screen.update(device_id='device', online=True)
            album = Path(folder) / 'photos'
            album.mkdir()
            make(str(album), 'a.jpg')
            make(str(album), 'b.jpg', colour=(0, 20, 200))
            show = photos.PhotoShow(str(album))
            manager.photo_shows = {'text.screen': show}
            manager.savers.set('device', {'show': True, 'off': ['media', 'camera', 'clock'], 'photo_fit': 'fit'})
            manager.send_auxiliary = AsyncMock()
            manager.ha.request = AsyncMock()
            request = {'saver': 'camera', 'entity': photos.PHOTOS, 'photo_prepare': '1', 'photo_session': 'test'}
            with patch.object(manager, 'saver_entities', return_value={photos.PHOTOS}), patch.object(manager, 'transport', return_value='action'), patch.object(camera_feed, 'box', return_value=(128, 80)), patch.object(camera_feed, 'base_url', new=AsyncMock(return_value='http://test')):
                manager.page_senders['text.screen'] = SimpleNamespace(features={catalogue.PHOTOS['jpeg_feature']})
                await manager.answer_saver('text.screen', screen, request)
                self.assertNotIn('pf', manager.send_auxiliary.call_args.args[1])
                if show.prefetch:
                    await show.prefetch
                manager.page_senders['text.screen'].features.add(catalogue.PHOTOS['prepared_feature'])
                await manager.answer_saver('text.screen', screen, request)
                message = manager.send_auxiliary.call_args.args[1]
                self.assertTrue(message['pf'])
                token = message['u'].rsplit('/', 1)[1].split('.')[0]
                self.assertIsNone(manager.camera.links[token].refresh)
                body = (await manager.camera.serve(token))[1]
                await manager.answer_saver('text.screen', screen, dict(request, photo_commit=message['pf']))
                second = manager.send_auxiliary.call_args.args[1]
                self.assertNotEqual(message['pf'], second['pf'])
                self.assertEqual((await manager.camera.serve(token))[1], body)
                await manager.answer_saver('text.screen', screen, dict(request, photo_commit=message['pf']))
                self.assertEqual(manager.send_auxiliary.call_args.args[1]['pf'], second['pf'])
