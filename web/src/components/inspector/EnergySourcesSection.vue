<script setup lang="ts">
// The energy card's own sensors: the way power-flow-card-plus is configured, a sensor for each circle. With none chosen
// the card follows Home Assistant's Energy settings, as it always did. The add-on checks and uses them
// (screen_manager/app/energy_flow.py `clean_mapping`, `mapped`).
import { computed } from "vue";
import { t } from "../../i18n";
import type { EnergySources, Tile } from "../../types";
import PropRow from "../ui/PropRow.vue";
import Section from "../ui/Section.vue";
import UiSelect from "../ui/UiSelect.vue";
import { setTileOption } from "../../editor/tiles";
import { useEntitiesStore } from "../../stores/entities";
import { useInventoryStore } from "../../stores/inventory";

const MAX_DEVICES = 4;
const entities = useEntitiesStore();
const inv = useInventoryStore();
const props = defineProps<{ tile: Tile }>();

const mapping = computed<EnergySources>(() => (props.tile.options?.energy_sources as EnergySources | undefined) ?? {});
const sensors = computed(() => (inv.inventory.entities || []).filter((item) => item.id.startsWith("sensor."))
  .map((item) => [item.id, item.name || item.id] as [string, string]).sort((a, b) => a[1].localeCompare(b[1])));
const options = (current?: string) => [["", t("editor.tile.energy.sources.none")] as [string, string],
  ...(current && !sensors.value.some(([id]) => id === current) ? [[current, current] as [string, string]] : []), ...sensors.value];
const KEYS = ["grid", "grid_import", "grid_export", "solar", "battery", "battery_out", "battery_in", "soc", "home"] as const;
type Key = (typeof KEYS)[number];

function save(next: EnergySources) {
  const clean: EnergySources = { ...next };
  for (const key of KEYS) if (!clean[key]) delete clean[key];
  if (clean.devices) clean.devices = clean.devices.filter((item) => item.entity);
  if (!clean.devices?.length) delete clean.devices;
  setTileOption(props.tile, "energy_sources", clean);
}
const pick = (key: Key, value: string) => save({ ...mapping.value, [key]: value });
const devices = computed(() => mapping.value.devices ?? []);
function device(index: number, entity: string) {
  const list = [...devices.value];
  list[index] = { ...(list[index] ?? {}), entity };
  save({ ...mapping.value, devices: list });
}
function deviceName(index: number, name: string) {
  const list = [...devices.value];
  if (!list[index]) return;
  list[index] = { entity: list[index].entity, ...(name.trim() ? { name: name.trim() } : {}) };
  save({ ...mapping.value, devices: list });
}
const used = computed(() => Object.keys(mapping.value).length > 0);
const reset = () => save({});
</script>

<template>
  <Section :title="t('editor.tile.energy.sources.title')">
    <p class="hint">{{ t(used ? "editor.tile.energy.sources.hint_own" : "editor.tile.energy.sources.hint") }}</p>
    <PropRow v-for="key in KEYS" :key="key" :label="t(`editor.tile.energy.sources.${key}.label`)" icon="flash" :hint="t(`editor.tile.energy.sources.${key}.hint`)">
      <UiSelect :model-value="mapping[key] ?? ''" :options="options(mapping[key])" :aria-label="t(`editor.tile.energy.sources.${key}.label`)"
        @update:model-value="(v: string) => pick(key, v)" />
    </PropRow>
    <PropRow v-for="index in Math.min(devices.length + 1, MAX_DEVICES)" :key="`d${index}`" :label="t('editor.tile.energy.sources.device', { n: index })" icon="flash"
      :hint="index === 1 ? t('editor.tile.energy.sources.device_hint') : undefined">
      <UiSelect :model-value="devices[index - 1]?.entity ?? ''" :options="options(devices[index - 1]?.entity)" :aria-label="t('editor.tile.energy.sources.device', { n: index })"
        @update:model-value="(v: string) => device(index - 1, v)" />
      <input v-if="devices[index - 1]?.entity" class="energy-device-name" type="text" maxlength="48" :value="devices[index - 1]?.name ?? ''"
        :placeholder="entities.entityName(devices[index - 1].entity)" :aria-label="t('editor.tile.energy.sources.device_name')"
        @change="(e) => deviceName(index - 1, (e.target as HTMLInputElement).value)" />
    </PropRow>
    <button v-if="used" type="button" class="energy-reset" @click="reset">{{ t("editor.tile.energy.sources.reset") }}</button>
  </Section>
</template>

<style scoped>
.energy-device-name { margin-top: 6px; width: 100%; }
</style>
