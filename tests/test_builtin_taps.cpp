// Exercise the production Map/Energy callback branches, not a duplicate tap predicate.
// The standalone suite has no LVGL; isolate these branches with satisfied external guards.
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static int run(const char *program, char *const args[]) {
  pid_t child; int status=0;
  assert(posix_spawnp(&child,program,nullptr,nullptr,args,environ)==0);
  assert(waitpid(child,&status,0)==child);
  return WIFEXITED(status)?WEXITSTATUS(status):1;
}
int main() {
  std::ifstream input("components/smart_display/runtime_tiles.h");assert(input);
  const std::string source{std::istreambuf_iterator<char>(input),{}};
  auto branch=[&](const char *start,const char *end) {
    auto a=source.find(start),b=source.find(end,a);assert(a!=std::string::npos && b!=std::string::npos);
    return source.substr(a,b-a);
  };
  char folder[]="/tmp/tessera-builtin-taps-XXXXXX";assert(mkdtemp(folder));
  std::string cpp=std::string(folder)+"/test.cpp",binary=std::string(folder)+"/test";
  std::ofstream out(cpp);
  out << R"CPP(
#include <cassert>
#include <string>
constexpr int LV_EVENT_SHORT_CLICKED=1,TILE_TOUCH=1;
struct lv_point_t {int x=0,y=0;};
struct Card {std::string tap,entity="screen.example",name="Example";bool is_map()const{return true;}bool is_energy()const{return true;}};
struct Widgets {unsigned index=0;};
namespace ui {int mm(int n){return n;}}
namespace esphome {unsigned millis(){return 1000;}}
namespace screen_input {struct Guard {int distance(){return 0;}} touch_guard;}
bool allowed(unsigned,int,const std::string&){return true;}
bool camera_supported(){return true;}
bool finger_at(lv_point_t&){return true;}
bool opened=false;
void camera_open(const std::string&,const std::string&,int){opened=true;}
namespace energy_view {void tap(Widgets&,Card&,lv_point_t&){opened=true;}}
void map_route(Card &card,int code=LV_EVENT_SHORT_CLICKED){Widgets w;
)CPP";
  out << branch("    if (card.is_map()) {","    // The energy card:") << "}\nvoid energy_route(Card &card,int code=LV_EVENT_SHORT_CLICKED){Widgets w;\n";
  out << branch("    if (card.is_energy()) {","    if (!card.is_settings()") << R"CPP(
}
int main(){
 for(auto route:{map_route,energy_route}) {
  Card card;card.tap="auto";opened=false;route(card,LV_EVENT_SHORT_CLICKED);assert(opened);
  card.tap="none";opened=false;route(card,LV_EVENT_SHORT_CLICKED);assert(!opened);
  card.tap="auto";opened=false;route(card,0);assert(!opened);
 }
}
)CPP";
  out.close();
  const char *compiler=std::getenv("CXX");if(!compiler)compiler="clang++";
  char *args[]={const_cast<char*>(compiler),const_cast<char*>("-std=c++17"),const_cast<char*>("-Wall"),const_cast<char*>("-Wextra"),const_cast<char*>("-Werror"),cpp.data(),const_cast<char*>("-o"),binary.data(),nullptr};
  int result=run(compiler,args);
  if(!result){char *execute[]={binary.data(),nullptr};result=run(binary.c_str(),execute);}
  std::filesystem::remove_all(folder);
  assert(result==0);
}
