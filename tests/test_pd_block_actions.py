"""Independent PD actions: validation, page persistence and negotiated delivery."""
import copy
import json
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'screen_manager/app'))
import catalogue
from core import Grid, extras, validate_layout, validate_pd_actions
from page_layout import compile_tiles
from layout_migrations import migrate_legacy
STATES=json.loads((ROOT/'tests/fixtures/pd_cards_states.json').read_text())
ACTIONS={key:{'entity':f'input_boolean.example_{key}','action':{'action':'homeassistant.toggle'}} for key in ['dp3','ultra','ac','max']}
class PdBlockActions(unittest.TestCase):
    def test_four_independent_targets_round_trip_without_geometry_changes(self):
        layout=validate_layout({'title':'PD','tiles':[{'entity':'screen.pd_batteries','name':'','slot':0,'options':{'size':'2x4','pd_actions':ACTIONS}}]},grid=Grid(6,6))
        doc=migrate_legacy(layout,Grid(6,6))['layout']
        tile=compile_tiles(doc,Grid(6,6))[0]
        self.assertEqual(tile['options']['pd_actions'],ACTIONS)
        self.assertEqual(tile['slot'],0)
        self.assertEqual(tile['options']['size'],'2x4')
        self.assertNotIn('display',tile['options'])
    def test_disabled_and_unset_blocks_are_distinct(self):
        value=validate_pd_actions('screen.pd_batteries',{'dp3':None,'max':ACTIONS['max']})
        self.assertIsNone(value['dp3']);self.assertNotIn('ultra',value)
    def test_bad_targets_fields_and_wrong_blocks_are_rejected(self):
        invalid=[{}, {'header':None}, {'dp3':{'entity':'screen.pd_header','action':{'action':'homeassistant.toggle'}}},
                 {'dp3':{'entity':'input_boolean.example','action':{'action':'homeassistant.toggle','data':{'entity_id':'switch.other'}}}},
                 {'dp3':{'entity':'input_boolean.example','action':{'action':'screen.open_selector','data':{'entity':'input_select.example'}}}}]
        for value in invalid:
            with self.subTest(value=value),self.assertRaises(ValueError):validate_pd_actions('screen.pd_batteries',value)
        with self.assertRaises(ValueError):validate_pd_actions('light.example',{'dp3':None})
        with self.assertRaises(ValueError):validate_pd_actions('screen.pd_header',{'dp3':None})
    def test_native_delivery_negotiates_actions_and_never_mutates_states(self):
        tile={'entity':'screen.pd_batteries','options':{'pd_actions':ACTIONS}}
        before=copy.deepcopy(STATES)
        old=extras(tile,STATES,native_pd=True)
        self.assertNotIn('actions',old['pd'])
        sent=extras(tile,STATES,native_pd=True,pd_actions=True)['pd']['actions']
        self.assertEqual([item['k'] for item in sent],[1,2,3,4])
        self.assertEqual([item['e'] for item in sent],[ACTIONS[k]['entity'] for k in ACTIONS])
        self.assertTrue(all(item['a']['s']=='homeassistant.toggle' for item in sent))
        self.assertEqual(STATES,before)
        tile['options']['pd_actions']={'dp3':None}
        self.assertEqual(extras(tile,STATES,native_pd=True,pd_actions=True)['pd']['actions'],[{'k':1,'n':True}])
    def test_header_has_its_own_generic_action(self):
        cfg={'header':{'entity':'script.example','action':{'action':'script.turn_on'}}}
        self.assertEqual(validate_pd_actions('screen.pd_header',cfg),cfg)
class PdFeatureGate(unittest.IsolatedAsyncioTestCase):
    async def test_old_firmware_is_refused_even_without_ha_action_metadata(self):
        from types import SimpleNamespace
        from server import Manager
        manager=SimpleNamespace(ha=SimpleNamespace(),aliases={},page_senders={'test':SimpleNamespace(features={'pd_native','pd_selector'})})
        layout={'title':'PD','tiles':[{'entity':'screen.pd_batteries','options':{'pd_actions':{'dp3':None}}}]}
        with self.assertRaisesRegex(ValueError,'Update this screen firmware'):
            await Manager.check_supported(manager,'test',layout)
        manager.page_senders['test'].features.add(catalogue.PD['actions_feature'])
        await Manager.check_supported(manager,'test',layout)
if __name__=='__main__':unittest.main()
