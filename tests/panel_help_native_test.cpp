#include "render/panel_help_native.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
static void need(bool v){if(!v){std::cerr<<"Panel Help layout mismatch\n";std::exit(1);}}
int main(){
 for(int language:{1,2})for(auto size:{std::array{640,480},std::array{853,480},std::array{1280,720},std::array{480,320}}){
  std::vector<std::string> labels;std::vector<std::array<int,2>> ends;int paths=0;
  vyruz::nativePanelHelpLayout(size[0],size[1],language,[&](std::string_view s,int x,int y){
   if(labels.empty())need(x==size[0]/2-72&&y==80);
   labels.emplace_back(s);return x+int(s.size())*8;
  },[&](const auto& points,int count,int color){need(color==250);need(count==3||count==4);ends.push_back(points[count-1]);++paths;});
  need(labels.size()==13&&paths==13&&ends.size()==13);
  need(labels[0]==(language==2?"ENERGIE":"ENERGY"));need(labels[1]==(language==2?"BOUCLIER":"SHIELD"));
  need(ends[0]==std::array{size[0]/2-80,30});need(ends[1]==std::array{size[0]/2-80,20});
  need(ends[8]==std::array{size[0]-10,size[1]-55});
  need(ends[12]==std::array{size[0]-120,size[1]-43});
 }
 std::cout<<"Panel Help native anchors and localized layout passed\n";
}
