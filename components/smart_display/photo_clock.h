#pragma once
// One clock layout and renderer for the glass and the native browser preview.
#include <algorithm>
#include <cmath>
#include <string>
#include "ui_scale.h"
#include "tile_catalogue.h"
#ifndef PHOTO_CLOCK_ARITHMETIC_ONLY
#include "lvgl.h"
#endif
namespace photo_clock {
struct Face { int line = 1, width = 1; };
struct Box { int x=0,y=0,w=1,h=1; };
struct Plan { Box time,date; int requested=0, actual=0, time_scale=256,date_scale=256; bool clamped=false; };
inline int scaled(int value,int scale) { return std::max(1,(value*scale+128)/256); }
inline Plan plan(int width,int height,int percent,const std::string &anchor,Face time,Face date) {
  Plan p;
  const int margin=std::min(ui::px(ui::large()?28:12),std::max(0,(std::min(width,height)-2)/4));
  const int room_w=std::max(1,width-2*margin),room_h=std::max(1,height-2*margin);
  p.requested=std::max(1,height*std::clamp(percent,tile_catalogue::photos::CLOCK_SIZE_MIN,tile_catalogue::photos::CLOCK_SIZE_MAX)/100);
  p.time_scale=std::max(1,p.requested*256/std::max(1,time.line));
  p.date_scale=std::max(1,p.requested*tile_catalogue::photos::DATE_RATIO*256/(100*std::max(1,date.line)));
  const int gap=ui::px(ui::large()?8:4);
  auto dimensions=[&] { p.time.w=scaled(time.width,p.time_scale); p.time.h=scaled(time.line,p.time_scale);
    p.date.w=scaled(date.width,p.date_scale); p.date.h=scaled(date.line,p.date_scale); };
  dimensions();
  const double fit=std::min({1.0,double(room_w)/std::max(p.time.w,p.date.w),double(std::max(1,room_h-gap))/(p.time.h+p.date.h)});
  if(fit<1) { p.clamped=true; p.time_scale=std::max(1,int(p.time_scale*fit));p.date_scale=std::max(1,int(p.date_scale*fit));dimensions(); }
  auto x=[&](int w){if(anchor.find("right")!=std::string::npos)return width-margin-w;
    if(anchor.find("center")!=std::string::npos)return (width-w)/2;
    return margin;};
  const int total=p.time.h+gap+p.date.h;
  int y=height-margin-total;
  if(anchor.rfind("top_",0)==0)y=margin;else if(anchor=="center")y=(height-total)/2;
  y=std::clamp(y,0,std::max(0,height-total));
  p.time.x=std::clamp(x(p.time.w),0,std::max(0,width-p.time.w));p.time.y=y;
  p.date.x=std::clamp(x(p.date.w),0,std::max(0,width-p.date.w));p.date.y=y+p.time.h+gap;
  p.actual=p.time.h;return p;
}
#ifndef PHOTO_CLOCK_ARITHMETIC_ONLY
struct Result { Plan layout; const lv_font_t *time_font=nullptr,*date_font=nullptr; };
inline bool supports(const lv_font_t *font,const std::string &text) {
  for(size_t i=0;i<text.size();) {
    const auto first=static_cast<unsigned char>(text[i++]);uint32_t cp=first;
    int remaining=0;
    if((first&0xE0)==0xC0){cp=first&0x1F;remaining=1;}
    else if((first&0xF0)==0xE0){cp=first&0x0F;remaining=2;}
    else if((first&0xF8)==0xF0){cp=first&7;remaining=3;}
    while(remaining--){if(i>=text.size())return false;cp=(cp<<6)|(static_cast<unsigned char>(text[i++])&0x3F);}
    lv_font_glyph_dsc_t glyph{};
    if(!lv_font_get_glyph_dsc(font,&glyph,cp,0)||glyph.is_placeholder)return false;
  }
  return true;
}
inline const lv_font_t *choose(std::initializer_list<const lv_font_t*> fonts,int wanted,const std::string &text) {
  const lv_font_t *best=nullptr,*smallest=nullptr;
  for(auto *f:fonts)if(f&&supports(f,text)){const int h=lv_font_get_line_height(f);if(!smallest||h<lv_font_get_line_height(smallest))smallest=f;
    if(h<=wanted&&(!best||h>lv_font_get_line_height(best)))best=f;}
  return best?best:smallest;
}
inline Face measure(const std::string &text,const lv_font_t *font) {
  lv_point_t size{};lv_text_get_size(&size,text.c_str(),font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  return {int(lv_font_get_line_height(font)),std::max(1,int(size.x))};
}
inline Result render(lv_obj_t *parent,lv_obj_t *&time_label,lv_obj_t *&date_label,int width,int height,int percent,
 const std::string &anchor,const std::string &time,const std::string &date,lv_color_t ink,
 std::initializer_list<const lv_font_t*> time_fonts,std::initializer_list<const lv_font_t*> date_fonts) {
  Result r;const int wanted=std::max(1,height*std::clamp(percent,tile_catalogue::photos::CLOCK_SIZE_MIN,tile_catalogue::photos::CLOCK_SIZE_MAX)/100);
  r.time_font=choose(time_fonts,wanted,time);r.date_font=choose(date_fonts,std::max(1,wanted*tile_catalogue::photos::DATE_RATIO/100),date);
  if(!r.time_font||!r.date_font)return r;
  const auto tf=measure(time,r.time_font),df=measure(date,r.date_font);
  r.layout=plan(width,height,percent,anchor,tf,df);
  auto label=[&](lv_obj_t *&obj,const std::string &text,const lv_font_t *font,const Box &b,int scale,Face face){
    if(!obj){obj=lv_label_create(parent);lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_pad_all(obj,0,0);}
    if(text!=lv_label_get_text(obj))lv_label_set_text(obj,text.c_str());
    // LVGL's local style setters refresh even when the value is equal. In particular, transforms
    // invalidate the old area before assignment. A one-second clock tick must not repaint a photo.
    if(lv_obj_get_style_text_font(obj,LV_PART_MAIN)!=font)lv_obj_set_style_text_font(obj,font,0);
    if(!lv_color_eq(lv_obj_get_style_text_color(obj,LV_PART_MAIN),ink))lv_obj_set_style_text_color(obj,ink,0);
    if(lv_label_get_long_mode(obj)!=LV_LABEL_LONG_CLIP)lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
    if(lv_obj_get_style_transform_pivot_x(obj,LV_PART_MAIN)!=0)lv_obj_set_style_transform_pivot_x(obj,0,0);
    if(lv_obj_get_style_transform_pivot_y(obj,LV_PART_MAIN)!=0)lv_obj_set_style_transform_pivot_y(obj,0,0);
    if(lv_obj_get_style_transform_scale_x(obj,LV_PART_MAIN)!=scale)lv_obj_set_style_transform_scale_x(obj,scale,0);
    if(lv_obj_get_style_transform_scale_y(obj,LV_PART_MAIN)!=scale)lv_obj_set_style_transform_scale_y(obj,scale,0);
    lv_obj_set_pos(obj,b.x,b.y);lv_obj_set_size(obj,face.width,face.line);
  };
  label(time_label,time,r.time_font,r.layout.time,r.layout.time_scale,tf);
  label(date_label,date,r.date_font,r.layout.date,r.layout.date_scale,df);
  const auto children=lv_obj_get_child_count(parent);
  if(lv_obj_get_index(time_label)+2!=children||lv_obj_get_index(date_label)+1!=children){lv_obj_move_foreground(time_label);lv_obj_move_foreground(date_label);}
  return r;
}
#endif
}
