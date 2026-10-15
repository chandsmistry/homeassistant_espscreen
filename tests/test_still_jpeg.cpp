// clang++ -std=c++17 -Wall -Wextra -Werror tests/test_still_jpeg.cpp -o /tmp/test_still_jpeg
#define MAX_INPUT 255  // POSIX limits.h macro must not collide with the JPEG byte bound
#include "../components/smart_display/still_jpeg.h"
#undef MAX_INPUT
#include "../components/smart_display/picture_fetch.h"
#include <cassert>
#include <vector>
using namespace still_jpeg;
static std::vector<uint8_t> frame(unsigned w = 24, unsigned h = 16) {
  return {255,216,255,192,0,17,8,uint8_t(h>>8),uint8_t(h),uint8_t(w>>8),uint8_t(w),3,
    1,0x22,0,2,0x11,1,3,0x11,1,255,218,0,12,3,1,0,2,0x11,3,0x11,0,63,0,
    1,2,255,0,3,255,208,4,255,217};
}
int main() {
  auto f = frame(); auto i = inspect(f.data(), f.size());
  assert(i.ok && i.width == 24 && i.height == 16);
  auto crop = std::vector<uint8_t>{255,239,0,14,'M','N','E','T','C','R','O','P',0,17,0,9};
  f.insert(f.begin()+2,crop.begin(),crop.end());
  i=inspect(f.data(),f.size());
  assert(i.ok && i.width==17 && i.height==9 && i.encoded_width==24 && i.encoded_height==16);
  auto bad=f; bad[14]=0; bad[15]=25; assert(!inspect(bad.data(),bad.size()).ok);
  bad=f; bad.insert(bad.begin()+2,crop.begin(),crop.end()); assert(!inspect(bad.data(),bad.size()).ok);
  f=frame(1278,799); assert(!inspect(f.data(),f.size()).ok); // IDF encoded area must divide by eight
  f=frame(1280,800); crop[12]=4; crop[13]=254; crop[14]=3; crop[15]=31;
  f.insert(f.begin()+2,crop.begin(),crop.end()); i=inspect(f.data(),f.size());
  assert(i.ok && i.width==1278 && i.height==799 && i.encoded_width==1280 && i.encoded_height==800);
  f=frame(); f[12]='R'; assert(!inspect(f.data(),f.size()).ok);
  f=frame(); f[13]=0x12; assert(!inspect(f.data(),f.size()).ok);
  const std::vector<uint8_t> adobe={255,238,0,14,'A','d','o','b','e',0,100,0,0,0,0,0};
  f=frame(); f.insert(f.begin()+2,adobe.begin(),adobe.end()); assert(!inspect(f.data(),f.size()).ok);
  f=frame();
  for (size_t n = 0; n < f.size(); ++n) assert(!inspect(f.data(), n).ok);
  f[3] = 194; assert(!inspect(f.data(), f.size()).ok); // progressive
  f = frame(); f[11] = 1; assert(!inspect(f.data(), f.size()).ok); // grayscale
  f = frame(0,9); assert(!inspect(f.data(), f.size()).ok);
  f = frame(4097,9); assert(!inspect(f.data(), f.size()).ok);
  f = frame(4096,4096); assert(!inspect(f.data(), f.size()).ok);
  f = frame(); f.push_back(0); assert(!inspect(f.data(), f.size()).ok);
  f = frame(); f[5] = 255; assert(!inspect(f.data(), f.size()).ok); // segment exceeds body
  f.assign(MAX_JPEG_BYTES + 1, 0); assert(!inspect(f.data(), f.size()).ok);
  assert(!inspect(nullptr, 50).ok);
  uint16_t padded[] = {1,2,99,99,3,4,99,99}, packed[4] = {};
  assert(copy_rows(packed,4,padded,8,2,2,4));
  assert(packed[0]==1 && packed[1]==2 && packed[2]==3 && packed[3]==4);
  assert(!copy_rows(packed,3,padded,8,2,2,4));
  assert(!copy_rows(packed,4,padded,7,2,2,4));
  assert(!copy_rows(packed,4,padded,8,2,2,1));
  const std::string h="HTTP/1.1 200 OK\r\nContent-Type: image/jpeg; charset=binary\r\nContent-Length: 40\r\n\r\n";
  assert(picture_fetch::parse_head(h.data(),h.size()).jpeg);
  const std::string overflow="HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999999999999999999999\r\n\r\n";
  assert(!picture_fetch::parse_head(overflow.data(),overflow.size()).has_length);
  const std::string bmp="HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\n\r\n";
  assert(!picture_fetch::parse_head(bmp.data(),bmp.size()).jpeg);
  f=frame(); f[26]=4; assert(!inspect(f.data(),f.size()).ok); // mismatched scan component
  f=frame(); f[27]=0x40; assert(!inspect(f.data(),f.size()).ok); // invalid Huffman selector
  f=frame(); f[33]=1; assert(!inspect(f.data(),f.size()).ok); // non-baseline spectral scan

}
