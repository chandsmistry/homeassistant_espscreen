#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include "lvgl.h"

namespace photo_transition {
// One extra retained frame, only while an effect needs it. Allocation uses the existing PSRAM picture allocator.
inline lv_image_dsc_t before{};
inline void *buffer = nullptr;
inline lv_obj_t *overlay = nullptr;
inline const lv_image_dsc_t *after = nullptr;
inline std::function<void *(size_t)> allocate;
inline std::function<void(void *)> release;
inline std::string effect;
inline int progress = 0, width = 0, height = 0;
inline uint32_t random_state = 0;
inline void clear() {
  if (overlay) { lv_anim_delete(overlay, nullptr); lv_obj_delete(overlay); overlay = nullptr; }
  if (buffer && release) release(buffer);
  buffer = nullptr; before = {}; after = nullptr;
}
inline bool retain(const lv_image_dsc_t *src) {
  clear();
  if (!src || !src->data || !src->data_size || src->data_size > 8 * 1024 * 1024 || !allocate || !release) return false;
  buffer = allocate(src->data_size);
  if (!buffer) return false;
  std::memcpy(buffer, src->data, src->data_size);
  before = *src; before.data = static_cast<const uint8_t *>(buffer);
  return true;
}
inline void picture(lv_layer_t *layer, const lv_image_dsc_t *src, lv_area_t clip, int x=0, int y=0, int scale=256, int opacity=255) {
  if (!src || !src->data) return;
  const lv_area_t saved = layer->_clip_area;
  lv_area_t intersection{std::max(saved.x1,clip.x1),std::max(saved.y1,clip.y1),std::min(saved.x2,clip.x2),std::min(saved.y2,clip.y2)};
  if (intersection.x1 > intersection.x2 || intersection.y1 > intersection.y2) return;
  layer->_clip_area = intersection;
  lv_draw_image_dsc_t d; lv_draw_image_dsc_init(&d);
  d.src = src; d.opa = opacity;
  d.scale_x = scale * width / std::max(1, int(src->header.w));
  d.scale_y = scale * height / std::max(1, int(src->header.h));
  lv_area_t area{x, y, x + int(src->header.w) - 1, y + int(src->header.h) - 1};
  lv_draw_image(layer, &d, &area);
  layer->_clip_area = saved;
}
inline void draw(lv_event_t *event) {
  auto *layer = lv_event_get_layer(event);
  const int p = std::clamp(progress, 0, 1000);
  const lv_area_t full{0,0,width-1,height-1};
  const bool motion = effect == "zoom_in" || effect == "zoom_out" || effect == "pan_left" || effect == "pan_right";
  if (motion) {
    int scale = effect == "zoom_out" ? 282 - p*26/1000 : 256 + p*26/1000;
    if (effect == "pan_left" || effect == "pan_right") scale = 282;
    int x = -(width * (scale-256)/256)/2, y = -(height * (scale-256)/256)/2;
    if (effect == "pan_left") x = -width*p/10000;
    if (effect == "pan_right") x = -width*(1000-p)/10000;
    picture(layer, after, full, x,y,scale);
    return;
  }
  picture(layer,&before,full);
  if (effect == "fade") { picture(layer,after,full,0,0,256,p*255/1000); return; }
  if (effect.rfind("slide_",0) == 0) {
    int x=0,y=0;
    if (effect == "slide_left") x=-width*(1000-p)/1000;
    if (effect == "slide_right") x=width*(1000-p)/1000;
    if (effect == "slide_up") y=-height*(1000-p)/1000;
    if (effect == "slide_down") y=height*(1000-p)/1000;
    picture(layer,after,full,x,y); return;
  }
  if (effect.rfind("wipe_",0) == 0) {
    lv_area_t r=full;
    if (effect == "wipe_left") r.x2=width*p/1000-1;
    if (effect == "wipe_right") r.x1=width-width*p/1000;
    if (effect == "wipe_up") r.y2=height*p/1000-1;
    if (effect == "wipe_down") r.y1=height-height*p/1000;
    if (r.x2>=r.x1 && r.y2>=r.y1) picture(layer,after,r);
    return;
  }
  // Geometry and blinds use clipped image strips; no full-size intermediate render layer.
  if (effect == "diamond") {
    for (int row=0;row<32;row++) {
      int y1=height*row/32,y2=height*(row+1)/32-1;
      int reach=std::max(0,width*p/1000-std::abs((y1+y2)/2-height/2)*width/std::max(1,height));
      if (reach) picture(layer,after,{std::max(0,width/2-reach),y1,std::min(width-1,width/2+reach),y2});
    }
    return;
  }
  if (effect == "blinds") {
    for(int row=0;row<16;row++) {
      int top=height*row/16, h=(height*(row+1)/16-top)*p/1000;
      if(h) picture(layer,after,{0,top,width-1,top+h-1});
    }
    return;
  }
  for(int row=0;row<8;row++) for(int col=0;col<8;col++) {
    int rank=row*8+col;
    if(effect == "blocks") rank=(rank*37+17)%64;
    if(effect == "checker") rank=(row+col)%2*32+(row*8+col)/2;
    if(effect == "spiral") {
      // Clockwise order from outer ring toward the centre.
      int ring=std::min({row,col,7-row,7-col}),side=8-2*ring,offset=0;
      for(int r=0;r<ring;r++) offset+=4*(8-2*r)-4;
      int step=row==ring?col-ring:col==7-ring?(side-1)+(row-ring):row==7-ring?2*(side-1)+(7-ring-col):3*(side-1)+(7-ring-row);
      rank=offset+step;
    }
    if(p*64/1000>rank) picture(layer,after,{width*col/8,height*row/8,width*(col+1)/8-1,height*(row+1)/8-1});
  }
}
inline void start(lv_obj_t *parent, const lv_image_dsc_t *src, const std::string &name, int w, int h, int seconds) {
  if(name == "none" || !src || !buffer) {clear();return;}
  effect=name; after=src; width=w; height=h; progress=0;
  if(name == "random") {
    static const char *choices[]={"fade","wipe_left","wipe_right","wipe_up","wipe_down","slide_left","slide_right","slide_up","slide_down","zoom_in","zoom_out","pan_left","pan_right","diamond","checker","spiral","blocks","blinds"};
    if (!random_state) random_state = lv_tick_get() | 1u;
    random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5;
    effect=choices[random_state%18];
  }
  overlay=lv_obj_create(parent);lv_obj_remove_style_all(overlay);
  lv_obj_remove_flag(overlay,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(overlay,w,h);lv_obj_set_pos(overlay,0,0);
  lv_obj_add_event_cb(overlay,draw,LV_EVENT_DRAW_MAIN,nullptr);
  lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,overlay);lv_anim_set_values(&a,0,1000);
  bool motion=effect=="zoom_in"||effect=="zoom_out"||effect=="pan_left"||effect=="pan_right";
  lv_anim_set_duration(&a,motion?std::clamp(seconds,15,3600)*1000:900);
  lv_anim_set_exec_cb(&a,[](void *obj,int32_t value){progress=value;lv_obj_invalidate(static_cast<lv_obj_t *>(obj));});
  lv_anim_set_completed_cb(&a,[](lv_anim_t *){
    // Keep the final zoom/pan position until the next photo, avoiding a snap back to the unzoomed image.
    if (effect != "zoom_in" && effect != "zoom_out" && effect != "pan_left" && effect != "pan_right") clear();
  });lv_anim_start(&a);
}
} // namespace photo_transition
