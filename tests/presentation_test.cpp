#include "game/presentation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
 using namespace vyruz;
 for(double dt:{0.,1000./60,1000./90,1000./144,2000.}){
  Presentation p;p.show(PresentationPage::Mission,"\nA \t\nB\r\n");
  assert(p.revealing()&&p.visibleText().empty());
  assert(p.advance(dt)&&p.visibleText()=="\nA");
  p.suspended=true;assert(!p.advance(dt)&&p.visibleText()=="\nA");p.suspended=false;
  assert(!p.advance(dt)&&p.visibleText()=="\nA");
  assert(!p.advance(dt)&&p.visibleText()=="\nA ");assert(!p.advance(dt));
  assert(!p.advance(dt)&&p.visibleText()=="\nA \t");assert(!p.advance(dt));
  assert(p.advance(dt)&&p.visibleText()=="\nA \t\nB");assert(!p.advance(dt));
  assert(p.revealing());assert(!p.advance(dt)&&p.revealing());assert(!p.advance(dt)&&!p.revealing());
  assert(p.visibleText()=="\nA \t\nB\n");assert(!p.timedOut());
  p.show(PresentationPage::Mission,"ABC");p.advance(dt);p.requestDismiss();assert(!p.dismissalReady());p.advance(dt);assert(p.dismissalReady());p.close();assert(!p.dismissalReady());
 }
 Presentation p;p.show(PresentationPage::Story,"ABC");assert(p.visibleText()=="ABC");p.advance(29999);assert(!p.timedOut());p.advance(1);assert(p.timedOut());
 std::cout<<"PASS byte/two-present cadence, whitespace, LF, final buffers, pause, skip boundary and static story\n";
}
