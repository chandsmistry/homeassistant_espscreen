<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import type { Tile, TileOptions, HeaderItem } from '../types';
import { t } from '../i18n';
import { useInventoryStore } from '../stores/inventory';
import { setPdBlockAction } from '../editor/tiles';
import ActionPicker from './ActionPicker.vue';
import ChoiceField from './ChoiceField.vue';
import EntityItemPicker from './EntityItemPicker.vue';
const props=defineProps<{tile:Tile; block:string; label:string}>();
const inventory=useInventoryStore();
const config=computed(()=>props.tile.options?.pd_actions?.[props.block]);
const mode=ref(config.value===null?'none':config.value?'action':'default');
const entity=ref(config.value?.entity || '');
const choosingEntity=ref(!entity.value);
const entityName=computed(()=>inventory.inventory.entities.find(e=>e.id===entity.value)?.name || entity.value);
watch(()=>JSON.stringify(config.value),()=>{const value=config.value;mode.value=value===null?'none':value?'action':'default';entity.value=value?.entity || '';choosingEntity.value=!entity.value;});
const entities=computed(()=>inventory.inventory.entities.filter(e=>!e.id.startsWith('screen.')).map(e=>e.id));
const validEntity=computed(()=>entities.value.includes(entity.value));
const choices=computed(()=>[['default',t('editor.tile.pd.use_card_action')],['action',t('editor.tile.tap.action')],['none',t('editor.tile.tap.none')]] as [string,string][]);
function pickMode(value:string){mode.value=value;if(value==='default')setPdBlockAction(props.tile,props.block,undefined);if(value==='none')setPdBlockAction(props.tile,props.block,null);}
function chooseEntity(item:HeaderItem){if(item.entity){entity.value=item.entity;choosingEntity.value=false;}}
function accepts(item:HeaderItem){return item.type==='entity' && !!item.entity && !item.entity.startsWith('screen.');}
function action(value:TileOptions['action']){if(value && validEntity.value)setPdBlockAction(props.tile,props.block,{entity:entity.value,action:value});}
</script>
<template>
  <section class="pd-block-action">
    <h4>{{ label }}</h4>
    <ChoiceField :choices="choices" :value="mode" :aria-label="label" @pick="pickMode" />
    <template v-if="mode==='action'">
      <label class="f-label">{{ t('editor.tile.pd.action_entity') }}</label>
      <button type="button" class="row" @click="choosingEntity=!choosingEntity">{{ entityName || t('editor.action.choose') }}</button>
      <EntityItemPicker v-if="choosingEntity" :id="`pd-action-${tile.id}-${block}`" :taken="()=>false" :accepts="accepts" any-domain @pick="chooseEntity" />
      <ActionPicker v-if="validEntity" :tile="tile" :action-entity="entity" :action-config="config && config.entity===entity ? config.action : undefined" controlled @change="action" />
    </template>
  </section>
</template>
<style scoped>
.pd-block-action { padding: 16px 0; border-top: 1px solid var(--line); }
h4 { margin: 0 0 10px; }
.f-label { display: block; margin: 12px 0 6px; }
</style>
