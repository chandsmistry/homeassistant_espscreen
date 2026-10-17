<script setup lang="ts">
// One entity of the plant room card: a suggest field that keeps what is typed to itself and saves only when an entity is
// picked or the field is left. A value that is not an entity id is put back; empty goes back to the card's preset.
import { ref, watch } from "vue";
import UiSuggest from "../ui/UiSuggest.vue";

const props = defineProps<{ value: string; entities: readonly string[]; label: string; preset: string }>();
const emit = defineEmits<{ commit: [value: string] }>();
const draft = ref(props.value);
watch(() => props.value, (value) => { draft.value = value; });
function commit(value: string) {
  const next = value.trim();
  if (next === props.value) return;
  if (next === "" || /^[a-z_]+\.[a-z0-9_]+$/.test(next)) emit("commit", next);
  else draft.value = props.value;
}
</script>

<template>
  <UiSuggest :model-value="draft" :suggestions="entities" :aria-label="label" :max="40" :placeholder="preset"
    @update:model-value="(v: string) => (draft = v)" @pick="(v: string) => { draft = v; commit(v); }" @blur="commit(draft)" />
</template>
