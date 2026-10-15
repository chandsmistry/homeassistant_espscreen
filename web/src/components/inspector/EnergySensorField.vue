<script setup lang="ts">
// One sensor of the energy card: a suggest field that keeps what is typed to itself and saves only when a sensor is
// picked or the field is left, so the layout is not rewritten on every key. A value that is not a sensor id is put back.
import { ref, watch } from "vue";
import UiSuggest from "../ui/UiSuggest.vue";

const props = defineProps<{ value: string; sensors: readonly string[]; label: string }>();
const emit = defineEmits<{ commit: [value: string] }>();
const draft = ref(props.value);
watch(() => props.value, (value) => { draft.value = value; });
function commit(value: string) {
  const next = value.trim();
  if (next === props.value) return;
  if (next === "" || next.startsWith("sensor.")) emit("commit", next);
  else draft.value = props.value;
}
</script>

<template>
  <UiSuggest :model-value="draft" :suggestions="sensors" :aria-label="label" :max="40" placeholder="sensor.…"
    @update:model-value="(v: string) => (draft = v)" @pick="(v: string) => { draft = v; commit(v); }" @blur="commit(draft)" />
</template>
