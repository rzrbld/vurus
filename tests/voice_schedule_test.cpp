#include "audio/voice_schedule.h"
#include <cstdlib>
#include <iostream>
using vyruz::VoiceSchedule;
static void need(bool ok,const char*why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
 VoiceSchedule q;
 need(q.enqueue("map",100,20,1000,5000),"enqueue");
 need(!q.enqueue("map",110,0,0,0),"duplicate before launch suppressed");
 need(q.advance(119).empty(),"not before delay");
 need(q.advance(120)==std::vector<std::string>{"map"},"launch at delay boundary");
 need(q.nextAllowed()==1120,"hold measured from actual launch time");
 need(!q.enqueue("map",1120,0,0,0),"duplicate suppressed beyond refractory");
 need(q.enqueue("blocked",121,0,10,2000),"enqueue cooldown collision");
 need(q.advance(121).empty(),"refractory rejects attempt");
 need(q.advance(1120).empty(),"rejected attempt is not retried after hold");
 need(!q.enqueue("blocked",1120,0,0,0),"failed attempt still deduplicated");
 need(q.enqueue("other",1120,0,30,0),"different sample accepted");
 need(q.advance(1120)==std::vector<std::string>{"other"},"inclusive cooldown boundary");
 q.advance(6120);need(q.size()==0,"expiry inclusive");
 need(q.enqueue("map",6120,0,0,0),"expired sample may enqueue again");
 need(q.advance(6120)==std::vector<std::string>{"map"}&&q.size()==0,"attempt precedes same-tick expiry");
 VoiceSchedule late;late.enqueue("late",10,10,100,10);
 need(late.advance(999)==std::vector<std::string>{"late"}&&late.size()==0&&late.nextAllowed()==1099,"expired unattempted event launches on late update");
 VoiceSchedule order;order.enqueue("a",0,0,0,100);order.enqueue("b",0,0,0,100);order.enqueue("c",0,0,20,100);order.enqueue("d",0,0,0,100);
 need(order.advance(0)==std::vector<std::string>({"a","b","c"}),"FIFO and hold-zero multiple launches");
 order.clear();need(order.size()==0&&order.nextAllowed()==20,"clear retains native global refractory");
 order.enqueue("e",1,0,0,100);need(order.advance(1).empty(),"clear does not bypass cooldown");
 need(order.advance(20).empty(),"blocked postclear event also oneattempt");
 std::cout<<"voice schedule tests passed\n";
}
