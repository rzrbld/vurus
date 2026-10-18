#include "render/texture_residency.h"
#include <cassert>
#include <vector>
int main(){
 vyruz::TextureResidency cache;unsigned a=1,b=2,c=3;std::vector<unsigned> removed;
 auto erase=[&](unsigned id){removed.push_back(id);};
 cache.beginDraw();cache.touch(a,10);cache.touch(b,20);cache.trim(0,0,erase);
 assert(removed.empty()&&cache.bytes()==30); // current draw is pinned
 cache.beginDraw();cache.touch(a,10);cache.touch(c,30);cache.trim(40,2,erase);
 assert((removed==std::vector<unsigned>{2})&&b==0&&a==1&&c==3&&cache.bytes()==40);
 cache.beginDraw();cache.trim(0,0,erase);
 assert(a==0&&c==0&&cache.count()==0&&cache.bytes()==0);
 a=4;cache.beginDraw();cache.touch(a,10);cache.touch(a,10);assert(cache.bytes()==10);
 cache.touch(a,25);assert(cache.bytes()==25&&cache.count()==1);
 cache.touch(a,5);assert(cache.bytes()==5&&cache.count()==1);
 cache.touch(a,10);
 vyruz::TextureResidency moved=std::move(cache);moved.beginDraw();moved.touch(a,10);
 moved.trim(10,1,erase);assert(moved.count()==1);
 moved.clear();assert(a==4&&moved.count()==0); // owner performs full teardown
}
