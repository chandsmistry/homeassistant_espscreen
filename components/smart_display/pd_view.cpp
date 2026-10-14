// Native PD cards: one persistent LVGL canvas, numeric/text updates, no picture fetch.
#include "runtime_tiles.h"
#include "pd_decorations.h"
#include "pd_fonts.h"
#include "pd_view.h"
namespace pd_view {
namespace rt=runtime_tiles;
using pd_card::Data;
using pd_card::Battery;
static int line_height(const lv_font_t *font) { return static_cast<int>(lv_font_get_line_height(font)); }
struct View {std::shared_ptr<const Data> data; int width=0,height=0; bool header=false;};
struct Run {std::string text; const pd_decorations::Asset *asset=nullptr;};
static std::vector<Run> runs(const std::string &text) {
  std::vector<Run> result;
  for(size_t i=0;i<text.size();) {
    size_t start=i; unsigned c=static_cast<unsigned char>(text[i++]);
    int more=c>=240?3:c>=224?2:c>=192?1:0;
    if(more){c &= (1u<<(6-more))-1; while(more-- && i<text.size())c=(c<<6)|(static_cast<unsigned char>(text[i++])&63);}
    if(c==0xFE0F || c==0x200D)continue;
    const pd_decorations::Asset *asset=nullptr;
    for(const auto &a:pd_decorations::ASSETS)if(a.codepoint==c){asset=&a;break;}
    if(asset)result.push_back({{},asset});
    else {
      if(result.empty() || result.back().asset)result.push_back({{},nullptr});
      result.back().text+=text.substr(start,i-start);
    }
  }
  return result;
}
static int asset_width(const pd_decorations::Asset &a,int h) {
  if(a.codepoint==0x25CF || a.codepoint==0x25CB)return std::max(3,h/3);
  if(a.codepoint==0x2713 || a.codepoint==0x2717)return h*3/4;
  return h;
}
static int rich_width(const std::string &text,const lv_font_t *font) {
  int width=0, h=line_height(font);
  for(const auto &r:runs(text))width+=r.asset?asset_width(*r.asset,h):rt::text_width(r.text,font);
  return width;
}
static const lv_font_t *face(const std::string &text,int width,int height,bool title=false) {
  const lv_font_t *fonts[]={title?rt::label_font:rt::control_font,rt::small_font,nullptr};
  for(auto *font:fonts)if(font && line_height(font)<=height && rich_width(text,font)<=width)return font;
  return rt::small_font?rt::small_font:rt::label_font;
}
static void text(lv_layer_t *layer,int x,int y,int width,const std::string &value,const lv_font_t *font,lv_color_t ink,bool center=true) {
  if(!font || width<=0)return;
  const int h=line_height(font),total=rich_width(value,font);
  int cursor=x+(center?std::max(0,(width-total)/2):0);
  for(const auto &r:runs(value)) {
    const int w=r.asset?asset_width(*r.asset,h):rt::text_width(r.text,font);
    if(r.asset && (r.asset->codepoint==0x25CF || r.asset->codepoint==0x25CB)) {
      lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=LV_RADIUS_CIRCLE;d.bg_color=ink;
      d.bg_opa=r.asset->codepoint==0x25CF?LV_OPA_COVER:LV_OPA_TRANSP;
      d.border_width=r.asset->codepoint==0x25CB?1:0;d.border_color=ink;
      const int by=y+(h-w)/2;lv_area_t box{cursor,by,cursor+w-1,by+w-1};lv_draw_rect(layer,&d,&box);
    } else if(r.asset) {
      static const auto images=[] {
        std::array<lv_image_dsc_t,std::size(pd_decorations::ASSETS)> result{};
        for(size_t i=0;i<result.size();++i) {
          auto &image=result[i];image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_ARGB8888;
          image.header.w=image.header.h=pd_decorations::SIDE;image.header.stride=pd_decorations::SIDE*4;
          image.data=pd_decorations::ASSETS[i].pixels;image.data_size=pd_decorations::SIDE*pd_decorations::SIDE*4;
        }
        return result;
      }();
      lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=&images[r.asset-pd_decorations::ASSETS];d.scale_x=d.scale_y=w*256/pd_decorations::SIDE;
      d.pivot={0,0};if(r.asset->ink){d.recolor=ink;d.recolor_opa=LV_OPA_COVER;}
      const int ay=y+(h-w)/2;
      lv_area_t box{cursor,ay,cursor+pd_decorations::SIDE-1,ay+pd_decorations::SIDE-1};
      lv_draw_image(layer,&d,&box);
    } else {
      lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=font;d.color=ink;d.text=r.text.c_str();d.text_local=1;
      lv_area_t box{cursor,y,cursor+w-1,y+h-1};lv_draw_label(layer,&d,&box);
    }
    cursor+=w;
  }
}
static void rect(lv_layer_t *layer,int x,int y,int w,int h,int radius,lv_color_t fill,int border=0,lv_color_t edge=theme::color(theme::LINE),int glow=0) {
  if(w<=0 || h<=0)return;
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=fill;d.radius=radius;
  d.border_width=border;d.border_color=edge;d.border_opa=border?LV_OPA_COVER:LV_OPA_TRANSP;
  if(glow){d.shadow_width=glow;d.shadow_color=edge;d.shadow_opa=LV_OPA_40;}
  lv_area_t a{x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);
}
static std::string fixed(float v,int decimals=0){return screen_text::decimal(v,decimals);}
static void pill(lv_layer_t *layer,int x,int y,int width,int height,const std::string &value,lv_color_t bg,lv_color_t ink,const lv_font_t *font=nullptr) {
  auto *f=font?font:face(value,width-12,height-6);
  rect(layer,x,y,width,height,height/2,bg,font?1:0,lv_color_mix(ink,bg,32));
  text(layer,x+6,y+(height-line_height(f))/2,width-12,value,f,ink);
}
static void header(lv_layer_t *layer,const View &view,int x,int y) {
  const Data &d=*view.data; const int width=view.width,height=view.height;
  const int pad=std::max(6,std::min(width,height)/24),inner=width-2*pad;
  const int rows=d.burst?6:5,row=(height-2*pad)/rows;
  const std::string lines[]={std::string(d.controller?"⚙️ ":"🔴 ")+"Power Distribution Console"+(d.controller?" ⚙️":" 🔴"),
    "⚡ Grid: "+fixed(d.power[0])+" W  |  🎯 Target: "+fixed(d.power[1])+" W  |  Σ "+fixed(d.power[2])+" W",
    d.mode,d.story,d.glance};
  for(int i=0;i<5;++i) {
    const auto *f=face(lines[i],inner,row-6,i==0 || i==2);const int fh=line_height(f);
    const int row_index=i+(d.burst && i>2?1:0),top=y+pad+row_index*row+(row-fh)/2;
    if(i==2) {
      const int pw=std::min(inner,rich_width(lines[i],f)+24),ph=std::min(row-4,fh+10);
      pill(layer,x+(width-pw)/2,top-(ph-fh)/2,pw,ph,lines[i],lv_color_hex(d.color),theme::color(theme::PD_PILL_INK));
    } else text(layer,x+pad,top,inner,lines[i],f,theme::color(theme::INK));
  }
  if(d.burst) {
    const std::string label="🚀 Burst Enabled 🚀";const auto *f=face(label,inner,row-6);
    const int pw=std::min(inner,rich_width(label,f)+24),ph=std::min(row-4,line_height(f)+10);
    pill(layer,x+(width-pw)/2,y+pad+3*row+(row-ph)/2,pw,ph,label,theme::color(theme::PD_BAD),theme::color(theme::PD_BAD_INK));
  }
}
static void battery(lv_layer_t *layer,const Battery &b,const Data &data,int x,int y,int width,int height) {
  // Match the approved 834 x 230 device panel, scaled into its tile.
  const int pad=std::max(6,int(std::lround(width*22.f/834))),gap=std::max(2,int(std::lround(height*20.f/230)));
  const auto ground=theme::color(theme::CARD);
  const bool charging=b.glow=="glow-charge",active=b.glow!="glow-off";
  const auto accent=charging?theme::color(theme::PD_SOC_2):lv_color_hex(data.color);
  rect(layer,x,y,width,height,std::max(6,int(width*18.f/834)),ground,active?2:1,active?accent:theme::color(theme::LINE),active?std::max(8,pad*2):0);
  const auto *body=pd_fonts::body;
  const int fh=line_height(body),ph=std::max(fh+4,int(height*42.f/230));
  const std::string eligibility=b.eligible?"✓ Eligible":"✗ Ineligible",connection=b.online?"● "+b.connection:"○ Offline";
  const int left=std::min(width/3,rich_width(eligibility,pd_fonts::badge)+16),right=std::min(width/4,rich_width(connection,pd_fonts::badge)+16);
  const int py=y+int(height*20.f/230);
  pill(layer,x+pad,py,left,ph,eligibility,theme::color(b.eligible?theme::PD_OK:theme::PD_BAD),theme::color(b.eligible?theme::PD_OK_INK:theme::PD_BAD_INK),pd_fonts::badge);
  pill(layer,x+width-pad-right,py,right,ph,connection,theme::color(b.online?theme::PD_OK:theme::PD_BAD),theme::color(b.online?theme::PD_OK_INK:theme::PD_BAD_INK),pd_fonts::badge);
  const auto *name=pd_fonts::name;
  text(layer,x+pad+left+pad,py+(ph-line_height(name))/2,width-left-right-4*pad,b.name,name,theme::color(theme::INK));
  using Segment=std::pair<std::string,bool>;
  const std::vector<Segment> detail={{"SoC ",false},{fixed(b.values[0],1)+"%",true},{" · Rem ",false},{fixed(b.values[1],2)+" kWh",true},{" · ",false},
    {b.values[2]>10?"⚡ Charging ":"Cap ",false},{fixed(b.values[2]>10?b.values[2]:b.values[3])+" W",true}};
  const std::vector<Segment> power={{"Set ",false},{fixed(b.values[4])+" W",true},{" · Act ",false},{fixed(b.values[5])+" W",true}};
  const int bw=width-2*pad,bh=std::max(4,int(std::lround(height*13.f/230))),bar_gap=std::max(3,int(std::lround(height*9.f/230)));
  const int bar_top=height-int(std::lround(height*29.f/230))-3*bh-2*bar_gap;
  const int first=py+ph+std::max(2,int(height*8.f/230)),second=first+fh;
  auto segments=[&](int by,const std::vector<Segment> &parts) {
    int total=0;for(const auto &part:parts)total+=rich_width(part.first,part.second?pd_fonts::bold:body);
    int cursor=x+(width-total)/2;
    for(const auto &part:parts){auto *font=part.second?pd_fonts::bold:body;const int w=rich_width(part.first,font);text(layer,cursor,by,w,part.first,font,theme::color(theme::INK),false);cursor+=w;}
  };
  segments(first,detail);segments(second,power);
  for(int k=0;k<3;++k) {
    const int by=y+bar_top+k*(bh+bar_gap);const auto color=theme::color(k==2?theme::PD_ACT:static_cast<theme::Role>(theme::PD_SOC_1+b.key));
    rect(layer,x+pad,by,bw,bh,bh/2,theme::color(theme::PD_TRACK),1,theme::color(theme::LINE));
    const int fill=std::lround(bw*std::clamp(b.values[6+k],0.f,100.f)/100);
    if(k==2 && b.solar) {
      const int solar=std::lround(bw*std::clamp(b.values[9],0.f,100.f)/100);
      const int battery=std::min(bw-solar,int(std::lround(bw*std::clamp(b.values[10],0.f,100.f)/100)));
      rect(layer,x+pad,by,solar,bh,bh/2,theme::color(theme::PD_SOLAR));
      rect(layer,x+pad+solar,by,battery,bh,bh/2,color);
    } else rect(layer,x+pad,by,fill,bh,bh/2,color);
  }
}
struct PanelLayout {int pad=0,gap=0,title=0,foot=0,height=0;bool compact=false;};
static PanelLayout panel_layout(const View &view) {
  const auto &d=*view.data;PanelLayout result;
  result.gap=std::max(5,view.width/40);
  result.foot=d.footer.empty()?0:line_height(rt::small_font)+result.gap;
  result.compact=view.height<int(d.batteries.size())*(2*line_height(rt::small_font)+18);
  result.height=(view.height-result.foot-(std::max(1,int(d.batteries.size()))-1)*result.gap)/std::max(1,int(d.batteries.size()));
  return result;
}
int action_key(const rt::Widgets &w,const lv_point_t &point) {
  auto *canvas=w.parts[0];if(!canvas)return -1;
  auto *view=static_cast<View *>(lv_obj_get_user_data(canvas));if(!view || !view->data || !view->data->available)return -1;
  lv_area_t at;lv_obj_get_coords(canvas,&at);
  const int x=point.x-at.x1,y=point.y-at.y1;
  if(x<0 || x>=view->width || y<0 || y>=view->height)return -1;
  if(view->header)return 0;
  const auto layout=panel_layout(*view);if(layout.compact)return -1;
  for(unsigned i=0;i<view->data->batteries.size();++i) {
    const int top=int(i)*(layout.height+layout.gap);
    if(y>=top && y<top+layout.height)return view->data->batteries[i].key+1;
  }
  return -1;
}
static void draw(lv_event_t *event) {
  auto *canvas=static_cast<lv_obj_t *>(lv_event_get_current_target(event));auto *view=static_cast<View *>(lv_obj_get_user_data(canvas));
  if(!view || !view->data)return;
  auto *layer=lv_event_get_layer(event);lv_area_t a;lv_obj_get_coords(canvas,&a);
  if(!view->data->available) {
    const std::string value="⏳ Waiting for controller data… ⏳";
    auto *f=face(value,view->width,view->height/3);text(layer,a.x1,a.y1+(view->height-line_height(f))/2,view->width,value,f,theme::color(theme::MUTED));return;
  }
  if(view->header){header(layer,*view,a.x1,a.y1);return;}
  // The tile already supplies equal outer padding. Do not add a second inset.
  const auto &d=*view->data;const auto layout=panel_layout(*view);const int pad=layout.pad,gap=layout.gap;
  const int title=layout.title; // No heading inside the approved allocations card.
  const int foot=layout.foot;
  if(title)text(layer,a.x1+pad,a.y1+pad,view->width-2*pad,"Battery Allocations",rt::small_font,theme::color(theme::INK),false);
  if(layout.compact) {
    float total=0;for(const auto &battery:d.batteries)total+=battery.values[5];
    const std::string summary=std::to_string(d.batteries.size())+" batteries · Act "+fixed(total)+" W";
    const auto *f=face(summary,view->width-2*pad,view->height/2);
    text(layer,a.x1+pad,a.y1+(view->height-line_height(f))/2,view->width-2*pad,summary,f,theme::color(theme::INK));return;
  }
  const int height=layout.height;
  for(int i=0;i<int(d.batteries.size());++i)battery(layer,d.batteries[i],d,a.x1+pad,a.y1+pad+title+i*(height+gap),view->width-2*pad,height);
  if(foot)text(layer,a.x1+pad,a.y1+view->height-pad-foot,view->width-2*pad,d.footer,face(d.footer,view->width-2*pad,foot),theme::color(theme::MUTED));
}
void render(rt::Widgets &w,const rt::Tile &tile,int width,int height) {
  rt::begin_extra(w,"pd",width,height);auto *&canvas=w.parts[0];
  if(!canvas) {
    canvas=lv_obj_create(w.extra);lv_obj_remove_style_all(canvas);
    lv_obj_remove_flag(canvas,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(canvas,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(canvas,new View());lv_obj_add_event_cb(canvas,draw,LV_EVENT_DRAW_MAIN,nullptr);
    lv_obj_add_event_cb(canvas,[](lv_event_t *e){delete static_cast<View *>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_current_target(e))));},LV_EVENT_DELETE,nullptr);
  }
  auto &view=*static_cast<View *>(lv_obj_get_user_data(canvas));
  view.data=tile.extra().pd;view.width=width;view.height=height;view.header=tile.entity=="screen.pd_header";
  lv_obj_set_pos(canvas,0,0);lv_obj_set_size(canvas,width,height);lv_obj_invalidate(canvas);
}
}  // namespace pd_view
