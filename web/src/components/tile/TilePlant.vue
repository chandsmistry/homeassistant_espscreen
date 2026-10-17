<script setup lang="ts">
// The plant room card (app 0.4.88): the screen draws tabs of readings, or from a large size a schematic of boiler, valves,
// radiators and cylinder, live from the sensors; the mockup shows the schematic's shape.
import { glyph } from "../../model/topbar";
import type { TileCardView } from "../../composables/useTileCard";

const props = defineProps<{ card: TileCardView }>();
const card = props.card;
const ink = card.energy.ink;
</script>

<template>
  <svg class="energy-diagram" viewBox="0 0 200 100" aria-hidden="true">
    <g fill="none" stroke-width="1.5">
      <path d="M62 30 H125" stroke="#ef5350" />
      <path d="M62 70 H125" stroke="#ffa726" />
    </g>
    <g fill="none" :stroke="ink" stroke-width="1.5">
      <rect x="10" y="18" width="52" height="64" rx="4" />
      <rect x="125" y="8" width="65" height="30" rx="4" stroke="#ef5350" />
      <rect x="125" y="48" width="65" height="46" rx="4" />
    </g>
    <rect x="127" y="75" width="61" height="17" rx="3" fill="#4dd0e1" opacity="0.5" />
    <rect x="127" y="50" width="61" height="25" rx="3" fill="#ffa726" opacity="0.5" />
    <g fill="#66bb6a"><circle cx="93" cy="30" r="4" /><circle cx="93" cy="70" r="4" /></g>
    <g class="mdi" text-anchor="middle" dominant-baseline="central" font-size="14" :fill="ink">
      <text x="36" y="50">{{ glyph("F0238") }}</text>
    </g>
  </svg>
</template>
