#include "render/native_render_cells.h"
#include <stdexcept>
#include <vector>
#include <iostream>
using namespace vyruz;
void need(bool a){if(!a)throw std::runtime_error("native render cells");}
int main(){
 auto w=nativeRenderWindow(-640,-320,640,480,122,122);need(w.left==9&&w.top==8&&w.right==22&&w.bottom==28);
 w=nativeRenderWindow(50,20,640,480,8,8);need(w.left==0&&w.top==0&&w.right==8&&w.bottom==8);
 // Retained433b4f..433bcd boundary cases: camera near origin/map edge,
 // plus a wider viewport. Right/bottom remain exclusive and map-clamped.
 w=nativeRenderWindow(-63,-31,640,480,122,122);need(w.left==0&&w.top==0&&w.right==13&&w.bottom==20);
 w=nativeRenderWindow(-128,-96,640,480,122,122);need(w.left==1&&w.top==1&&w.right==14&&w.bottom==21);
 w=nativeRenderWindow(-7744,-3872,640,480,122,122);need(w.left==120&&w.top==119&&w.right==122&&w.bottom==122);
 w=nativeRenderWindow(0,0,1280,720,122,122);need(w.left==0&&w.top==0&&w.right==23&&w.bottom==27);
 NativeActorCells c;c.reset(40,40);c.relocate(0,c.coordinate(640,320));c.relocate(1,c.coordinate(640,320));c.relocate(2,c.coordinate(704,320));c.relocate(3,c.coordinate(640,352));c.relocate(4,c.coordinate(2300,1000));
 std::vector<size_t> a;visitNativeRenderCells(c,nativeRenderWindow(-640,-320,640,480,40,40),[&](size_t id){a.push_back(id);});need(a==std::vector<size_t>({1,0,2,3}));
 c.relocate(0,c.coordinate(642,321));a.clear();visitNativeRenderCells(c,nativeRenderWindow(-640,-320,640,480,40,40),[&](size_t id){a.push_back(id);});need(a==std::vector<size_t>({1,0,2,3}));
 c.erase(1);c.relocate(1,c.coordinate(704,320));a.clear();visitNativeRenderCells(c,nativeRenderWindow(-640,-320,640,480,40,40),[&](size_t id){a.push_back(id);});need(a==std::vector<size_t>({0,1,2,3}));
 std::cout<<"Native padded window, Y/X/head ordering, same-cell stability and re-entry passed\n";
}
