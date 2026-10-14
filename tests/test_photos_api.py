import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
from types import SimpleNamespace
from aiohttp.test_utils import TestClient,TestServer
from PIL import Image
import test_portal
import photos
import catalogue
from server import create_app

class PhotosApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        self.path=Path(temp.name)/'screens.json'
        self.manager=test_portal.ManagerTests.setup_manager(self,self.path)
        self.manager.save('text.screen',{'title':'Test','tiles':[],'pages':1})
        self.folder=Path(temp.name)/'photos';self.folder.mkdir();Image.new('RGB',(32,64),'red').save(self.folder/'a.png')
        self.manager.photo_shows={'text.screen':photos.PhotoShow(str(self.folder))}
        self.client=TestClient(TestServer(create_app(self.manager,True)));await self.client.start_server();self.addAsyncCleanup(self.client.close)
        inv=await(await self.client.get('/api/inventory')).json();self.headers={'X-Screen-CSRF':inv['csrf']}
    async def test_preview_and_status_do_not_advance_or_change_layout(self):
        before=self.path.read_bytes()
        status=await self.client.get('/api/screens/text.screen/photos?fit=pair&mb=1');self.assertEqual(status.status,200);self.assertEqual((await status.json())['portraits'],1)
        preview=await self.client.get('/api/screens/text.screen/photos?preview=1&fit=pair&mb=1');self.assertEqual(preview.status,200);self.assertEqual(preview.content_type,'image/jpeg')
        self.assertIsNone(self.manager.photo_shows['text.screen'].since);self.assertEqual(self.path.read_bytes(),before);self.assertEqual(self.manager.ha.messages,[])
        self.assertEqual((await self.client.get('/api/screens/absent/photos')).status,404)
        self.assertEqual((await self.client.get('/api/screens/text.screen/photos?fit=bad')).status,400)
    async def test_old_firmware_effect_gate_and_new_firmware_save(self):
        screen=self.manager.screen('text.screen');screen['device_id']='device'
        sender=self.manager.page_sender('text.screen',screen);sender.features=set()
        choice={'show':True,'photo_transition':'fade','photo_clock':True}
        self.addCleanup(patch.stopall)
        patch.object(self.manager,'screen',return_value=screen).start()
        old=await self.client.put('/api/screens/text.screen/screensaver',json={'screensaver':choice},headers=self.headers);self.assertEqual(old.status,400)
        sender.features.add(catalogue.PHOTOS['feature'])
        new=await self.client.put('/api/screens/text.screen/screensaver',json={'screensaver':choice},headers=self.headers);self.assertEqual(new.status,200,await new.text())
        self.assertEqual((await new.json())['screensaver']['photo_transition'],'fade');self.assertEqual(self.manager.ha.messages,[])
