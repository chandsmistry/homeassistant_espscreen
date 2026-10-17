<script setup lang="ts">
// The plant room card's sensors: a preset of the whole plant room that each role may replace (add-on: plant_room.py
// `clean_mapping`). A role left empty keeps the preset. The add-on works every state out; the screen only draws.
import { computed } from "vue";
import { t } from "../../i18n";
import type { Tile } from "../../types";
import PropRow from "../ui/PropRow.vue";
import Section from "../ui/Section.vue";
import PlantEntityField from "./PlantEntityField.vue";
import { setTileOption } from "../../editor/tiles";
import { useInventoryStore } from "../../stores/inventory";
import { PLANT_PRESET, PLANT_ROLES } from "../../model/plant-room";

const inv = useInventoryStore();
const props = defineProps<{ tile: Tile }>();
const mapping = computed<Record<string, string>>(() => (props.tile.options?.plant_room as Record<string, string> | undefined) ?? {});
const ids = computed(() => (inv.inventory.entities || []).map((item) => item.id));
function pick(role: string, value: string) {
  const next = { ...mapping.value, [role]: value };
  for (const key of Object.keys(next)) if (!next[key] || next[key] === PLANT_PRESET[key]) delete next[key];
  setTileOption(props.tile, "plant_room", next);
}
const used = computed(() => Object.keys(mapping.value).length > 0);
const reset = () => setTileOption(props.tile, "plant_room", {});
</script>

<template>
  <Section :title="t('editor.tile.plant.title')">
    <p class="hint">{{ t(used ? "editor.tile.plant.hint_own" : "editor.tile.plant.hint") }}</p>
    <PropRow v-for="role in PLANT_ROLES" :key="role" :label="t(`editor.tile.plant.roles.${role}`)" icon="flash">
      <PlantEntityField :value="mapping[role] ?? ''" :entities="ids" :label="t(`editor.tile.plant.roles.${role}`)" :preset="PLANT_PRESET[role]" @commit="(v) => pick(role, v)" />
    </PropRow>
    <button v-if="used" type="button" class="energy-reset" @click="reset">{{ t("editor.tile.plant.reset") }}</button>
  </Section>
</template>
