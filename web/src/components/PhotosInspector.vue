<script setup lang="ts">
import { computed, ref, watch, onBeforeUnmount } from 'vue';
import { getJson } from '../api';
import catalogue from '../model/catalogue.json';
import { photoClockMetrics } from '../model/ui-scale';
import { t } from '../i18n';
import { useScreenStore } from '../stores/screen';
import { useScreensaverStore } from '../stores/screensaver';
import UiSelect from './ui/UiSelect.vue';
import SwitchRow from './ui/SwitchRow.vue';
const scr = useScreenStore(), saver = useScreensaverStore();
const T = (key: string, values: Record<string, unknown> = {}) => t(`editor.screen_settings.screensaver.${key}`, values);
const report = ref<{exists: boolean; total: number; usable: number; portraits: number; skipped: {name: string; reason: string}[]} | null>(null);
const preview = ref(''), error = ref(''), loading = ref(false);
let epoch = 0;
const fit = computed(() => saver.saver?.photo_fit ?? 'fill');
const clockStyle = computed(() => {
  const m = photoClockMetrics(scr.currentScreen?.shape || {});
  return { '--clock-font': `${100 * m.font / m.width}cqw`, '--clock-line': `${100 * m.line / m.width}cqw`,
    '--clock-margin': `${100 * m.margin / m.width}cqw` };
});
const order = computed(() => saver.saver?.photo_order ?? 'shuffle');
const transition = computed(() => saver.saver?.photo_transition ?? 'none');
const fits = computed(() => ['fit', 'fill', 'pair'].map(k => [k, T(`photo_modes.${k}`)] as const));
const orders = computed(() => ['shuffle', 'filename'].map(k => [k, T(`photo_orders.${k}`)] as const));
const qualityChoices = computed(() => ['native','fast','small'].map(k => [k,T(`photo_qualities.${k}`)] as const));
const clockPositions = computed(() => catalogue.domains.screen.photos.clock_positions.map(k => [k, T(`photo_positions.${k}`)] as const));
const transitions = computed(() => catalogue.domains.screen.photos.transitions.map(k => [k, T(`photo_transitions.${k}`)] as const));
async function refresh() {
  const id = scr.selected;
  if (!id) return;
  const mine = ++epoch;
  loading.value = true; error.value = '';
  try {
    const query = `fit=${fit.value}&mb=${saver.photoMb}`;
    const status = await getJson(`screens/${encodeURIComponent(id)}/photos?${query}`);
    if (mine !== epoch) return;
    report.value = status;
    if (!status.usable) { preview.value = ''; return; }
    // Same-origin URL obeys img-src 'self' and downloads the preview only once.
    preview.value = `api/screens/${encodeURIComponent(id)}/photos?preview=1&${query}&v=${mine}`;
  } catch (e) { if (mine === epoch) error.value = e instanceof Error ? e.message : T('photo_unavailable'); }
  finally { if (mine === epoch) loading.value = false; }
}
watch(() => [scr.selected, fit.value, order.value, saver.photoMb], refresh, {immediate: true});
onBeforeUnmount(() => { epoch++; });
</script>
<template>
  <div class="photo-preview" :style="{aspectRatio: saver.glassRatio}">
    <img v-if="preview" :src="preview" :alt="T('photo_preview')" @error="error = T('photo_unavailable'); preview = ''" />
    <span v-else>{{ loading ? T('photo_loading') : T('photo_empty') }}</span>
    <span v-if="saver.saver?.photo_clock" class="photo-clock" :style="clockStyle" :class="`clock-${saver.saver?.photo_clock_position ?? 'bottom_left'}`">{{ saver.clockPreview.time }}</span>
  </div>
  <p v-if="report" class="help">{{ T('photo_count', {usable: report.usable, total: report.total}) }}</p>
  <p v-if="report && !report.exists" class="help">{{ T('photo_folder_missing') }}</p>
  <p v-if="fit === 'pair' && report && report.portraits < 2" class="help">{{ T('photo_pair_fallback') }}</p>
  <details v-if="report?.skipped.length"><summary>{{ T('photo_skipped', {n: report.skipped.length}) }}</summary>
    <ul class="photo-skipped"><li v-for="file in report.skipped" :key="file.name">{{ file.name }}: {{ T(`photo_reasons.${file.reason}`) }}</li></ul>
  </details>
  <p v-if="error" role="status" class="help">{{ error }}</p>
  <button type="button" class="ghost-btn" :disabled="loading" @click="refresh">{{ T('photo_refresh') }}</button>
  <label for="photo-fit">{{ T('photo_display') }}</label>
  <UiSelect id="photo-fit" :model-value="fit" :options="fits" @update:model-value="(value: string) => saver.changeSaver({photo_fit: value as 'fit' | 'fill' | 'pair'})" />
  <label for="photo-order">{{ T('photo_order') }}</label>
  <UiSelect id="photo-order" :model-value="order" :options="orders" @update:model-value="(value: string) => saver.changeSaver({photo_order: value as 'shuffle' | 'filename'})" />
  <label for="photo-quality">{{ T('photo_quality') }}</label>
  <UiSelect id="photo-quality" :model-value="saver.saver?.photo_quality ?? 'native'" :options="qualityChoices" @update:model-value="(value: string) => saver.changeSaver({photo_quality:value})" />
  <small class="help">{{ T('photo_quality_hint') }}</small>
  <label for="saver-photo-seconds">{{ T('photo_seconds') }}</label>
  <UiSelect id="saver-photo-seconds" :model-value="saver.photoSeconds" :options="saver.secondsChoices" @update:model-value="(value: number) => saver.changeSaver({photo_seconds: value})" />
  <label for="saver-photo-mb">{{ T('photo_mb') }}</label>
  <UiSelect id="saver-photo-mb" :model-value="saver.photoMb" :options="saver.mbChoices" @update:model-value="(value: number) => saver.changeSaver({photo_mb: value})" />
  <SwitchRow :label="T('photo_clock')" :model-value="saver.saver?.photo_clock ?? false" :disabled="!saver.saver?.photo_effects" @update:model-value="(value: boolean) => saver.changeSaver({photo_clock: value})" />
  <template v-if="saver.saver?.photo_clock">
    <label for="photo-clock-position">{{ T('photo_clock_position') }}</label>
    <UiSelect id="photo-clock-position" :model-value="saver.saver.photo_clock_position ?? 'bottom_left'" :options="clockPositions" :disabled="!saver.saver?.photo_positions" @update:model-value="(value: string) => saver.changeSaver({photo_clock_position: value})" />
  </template>
  <label for="photo-transition">{{ T('photo_transition') }}</label>
  <UiSelect id="photo-transition" :model-value="transition" :options="transitions" :disabled="!saver.saver?.photo_effects" @update:model-value="(value: string) => saver.changeSaver({photo_transition: value})" />
  <small v-if="!saver.saver?.photo_effects" class="help">{{ T('photo_update') }}</small>
  <small class="help">{{ T('photos_hint') }}</small>
</template>
<style scoped>
.photo-preview { width:100%; background:#10151b; border-radius:12px; overflow:hidden; position:relative; container-type:inline-size; display:grid; place-items:center; color:#fff; }
.photo-preview img {width:100%;height:100%;object-fit:contain;}
.photo-clock {position:absolute;color:white;font-family:"Bar Roboto",sans-serif;font-weight:500;font-size:var(--clock-font);line-height:var(--clock-line);white-space:nowrap;}
.clock-top_left {top:var(--clock-margin);left:var(--clock-margin);} .clock-top_center {top:var(--clock-margin);left:50%;transform:translateX(-50%);} .clock-top_right {top:var(--clock-margin);right:var(--clock-margin);}
.clock-center {top:50%;left:50%;transform:translate(-50%,-50%);}
.clock-bottom_left {bottom:var(--clock-margin);left:var(--clock-margin);} .clock-bottom_center {bottom:var(--clock-margin);left:50%;transform:translateX(-50%);} .clock-bottom_right {bottom:var(--clock-margin);right:var(--clock-margin);}
.photo-skipped {max-height:180px;overflow:auto;padding-left:20px;font-size:12px;overflow-wrap:anywhere;}
label {display:block;margin-top:12px;}
</style>
