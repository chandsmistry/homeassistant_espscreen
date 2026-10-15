import unittest
import catalogue
import screen_saver
from core import short, media_extras

class PhotoClockSettingTests(unittest.TestCase):
    def test_optional_setting_keeps_old_document_unchanged(self):
        choice=screen_saver.validate({'show':True})
        self.assertNotIn('photo_clock_size',choice)
    def test_integer_range(self):
        for n in (4,12,40):
            self.assertEqual(screen_saver.validate({'photo_clock_size':n})['photo_clock_size'],n)
        for n in (3,41,True,12.5,'12',None):
            with self.assertRaises(ValueError):screen_saver.validate({'photo_clock_size':n})
    def test_wire_key_only_when_capability_negotiated(self):
        choice=screen_saver.validate({'show':True,'order':['photos','media','camera','clock'],'photo_clock_size':25})
        states={'image.screensaver_photos':{'state':'idle','attributes':{}}}
        before=screen_saver.message(choice,states,True,short,media_extras)
        self.assertNotIn(catalogue.PHOTOS['clock_size_key'],before)
        after=screen_saver.message(choice,states,True,short,media_extras,photo_size=True)
        self.assertEqual(after[catalogue.PHOTOS['clock_size_key']],25)
        del choice['photo_clock_size']
        self.assertEqual(screen_saver.message(choice,states,True,short,media_extras,photo_size=True)[catalogue.PHOTOS['clock_size_key']],12)
