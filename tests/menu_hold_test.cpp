#include "game/menu_hold.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(){
    vyruz::MenuAdjustmentHold h;
    h.context(101);h.press(false,1,100);
    assert(h.repeat(249)==0&&h.repeat(250)==1&&h.repeat(250)==0);
    assert(h.repeat(10000)==1&&h.repeat(10001)==0); // no catch-up
    h.press(true,-1,10010);assert(h.repeat(10160)==-1); // left priority across sources
    h.release(true,-1);assert(h.repeat(10310)==1);
    h.release(false,1);assert(h.repeat(11000)==0);
    h.press(true,1,12000);h.context(102);assert(h.repeat(13000)==0);
    h.press(false,-1,14000);h.reset();assert(h.repeat(15000)==0);
}
