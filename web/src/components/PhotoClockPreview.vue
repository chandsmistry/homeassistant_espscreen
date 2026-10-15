<script setup lang="ts">
// This canvas is rendered by the same C++ helper, actual fonts and LVGL as the panel.
import {onBeforeUnmount,ref,watch} from 'vue';
import {loadFirmware} from '../wasm/load';
import catalogue from '../model/catalogue.json';
import type {FirmwarePreviewModule} from '../wasm/firmware_preview.js';
const props=withDefaults(defineProps<{width:number;height:number;dpi?:number;percent?:number;position?:string;time:string;date:string}>(),{dpi:170,percent:catalogue.domains.screen.photos.clock_size_default,position:'bottom_left'});
const canvas=ref<HTMLCanvasElement|null>(null);
const emit=defineEmits<{failed:[message:string]}>();
let module:FirmwarePreviewModule|null=null,generation=0,disposed=false,shape='';
async function draw(){
 const mine=++generation;
 try {
 const key=`${props.width}:${props.height}:${props.dpi}`;
 if(!module||key!==shape){const next=await loadFirmware();if(disposed||mine!==generation)return;
  if(!next._preview_init(props.width,props.height,props.dpi,1,1))return;module=next;shape=key;}
 if(disposed||mine!==generation||!canvas.value)return;
 module.ccall('preview_photo_clock','string',['number','string','string','string'],[props.percent,props.position,props.time,props.date]);
 const p=module._preview_frame(),bytes=new Uint8ClampedArray(module.HEAPU8.subarray(p,p+props.width*props.height*4));
 // Native clock ink is white on transparent black. Preserve LVGL's antialiased edge coverage.
 for(let i=0;i<bytes.length;i+=4){const alpha=Math.max(bytes[i]!,bytes[i+1]!,bytes[i+2]!);bytes[i]=255;bytes[i+1]=255;bytes[i+2]=255;bytes[i+3]=alpha;}
 canvas.value.width=props.width;canvas.value.height=props.height;
 canvas.value.getContext('2d')?.putImageData(new ImageData(bytes,props.width,props.height),0,0);
 } catch(error) {
  if(!disposed&&mine===generation){module=null;canvas.value?.getContext('2d')?.clearRect(0,0,props.width,props.height);emit('failed',error instanceof Error?error.message:String(error));}
 }
}
watch(()=>[props.width,props.height,props.dpi,props.percent,props.position,props.time,props.date,canvas.value],draw,{immediate:true,flush:'post'});
onBeforeUnmount(()=>{disposed=true;generation++;module=null;});
</script>
<template><canvas ref="canvas" aria-hidden="true" class="native-photo-clock" /></template>
<style scoped>.native-photo-clock{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;}</style>
