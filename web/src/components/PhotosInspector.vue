<script setup lang="ts">
import { computed, ref, watch, onBeforeUnmount } from 'vue';
import { getJson } from '../api';
import catalogue from '../model/catalogue.json';
import PhotoClockPreview from './PhotoClockPreview.vue';
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
const clockSize = computed(() => saver.saver?.photo_clock_size ?? catalogue.domains.screen.photos.clock_size_default);
const shape = computed(() => scr.currentScreen?.shape);
function sizeChanged(event: Event) { const n = Number((event.target as HTMLInputElement).value); if (Number.isInteger(n) && n >= catalogue.domains.screen.photos.clock_size_min && n <= catalogue.domains.screen.photos.clock_size_max) saver.changeSaver({photo_clock_size:n}); }
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
    <PhotoClockPreview v-if="saver.saver?.photo_clock" :width="shape?.width ?? 1280" :height="shape?.height ?? 800" :dpi="shape?.dpi ?? 170" :percent="clockSize" :position="saver.saver?.photo_clock_position" :time="saver.clockPreview.photoTime" :date="saver.clockPreview.photoDate" />
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
    <fieldset class="clock-size" :disabled="!saver.saver?.photo_sizes">
      <legend>{{ T('photo_clock_size') }}</legend>
      <input id="photo-clock-size" type="range" :min="catalogue.domains.screen.photos.clock_size_min" :max="catalogue.domains.screen.photos.clock_size_max" step="1" :value="clockSize" :aria-label="T('photo_clock_size')" @change="sizeChanged" />
      <input id="photo-clock-size-number" type="number" :min="catalogue.domains.screen.photos.clock_size_min" :max="catalogue.domains.screen.photos.clock_size_max" step="1" :value="clockSize" :aria-label="T('photo_clock_size')" @change="sizeChanged" /> %
      <button type="button" class="ghost-btn" @click="saver.changeSaver({photo_clock_size:catalogue.domains.screen.photos.clock_size_default})">{{ T('photo_clock_reset') }}</button>
      <small class="help">{{ T('photo_clock_size_hint') }}</small>
    </fieldset>
  </template>
  <label for="photo-transition">{{ T('photo_transition') }}</label>
  <UiSelect id="photo-transition" :model-value="transition" :options="transitions" :disabled="!saver.saver?.photo_effects" @update:model-value="(value: string) => saver.changeSaver({photo_transition: value})" />
  <small v-if="!saver.saver?.photo_effects" class="help">{{ T('photo_update') }}</small>
  <small class="help">{{ T('photos_hint') }}</small>
</template>
<style scoped>
.photo-preview { width:100%; background:#10151b; border-radius:12px; overflow:hidden; position:relative; container-type:inline-size; display:grid; place-items:center; color:#fff; }
.photo-preview img {width:100%;height:100%;object-fit:contain;}
.clock-size {margin-top:12px;border:0;padding:0;} .clock-size input[type=range]{width:65%;} .clock-size input[type=number]{width:64px;}
.photo-skipped {max-height:180px;overflow:auto;padding-left:20px;font-size:12px;overflow-wrap:anywhere;}
label {display:block;margin-top:12px;}
</style>
