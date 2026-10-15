#define PHOTO_CLOCK_ARITHMETIC_ONLY
#include "components/smart_display/photo_clock.h"
#include <cassert>
#include <iostream>
#include <utility>
int main(){int count=0;for(auto shape:{std::pair{1280,800},std::pair{800,1280},std::pair{240,320},std::pair{320,240}}) {
 for(auto anchor:{"top_left","top_center","top_right","center","bottom_left","bottom_center","bottom_right"})for(int pct=4;pct<=40;pct++){
 ui::configure(shape.first<400?143:149,shape.first<400?"compact":"standard");
 auto p=photo_clock::plan(shape.first,shape.second,pct,anchor,{49,128},{21,300});
 for(auto b:{p.time,p.date}){assert(b.x>=0&&b.y>=0);assert(b.x+b.w<=shape.first);assert(b.y+b.h<=shape.second);}
 assert(p.date.y>=p.time.y+p.time.h);assert(p.requested==shape.second*pct/100);count++;
 }}
 std::cout<<"PASS "<<count<<" clock size/anchor bounds cases\n";}
