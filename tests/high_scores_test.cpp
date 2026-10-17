#include "game/high_scores.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include "host_temp.h"
using namespace vyruz;
void check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
 auto scores=defaultHighScores(123);check(scores[0].name=="Guillaume Portes"&&scores[0].values[2]==3&&scores[0].values[3]==11,"Native top default name and progress");
 check(scores[19].values[2]==1&&scores[19].values[3]==1&&scores[19].values[6]==3,"Native last default progress and hit ratio");
 auto bytes=encodeHighScores(scores);check(bytes.size()==1440&&bytes[8]==3&&bytes[52]=='G',"Original72-byte row offsets");
 HighScores decoded;std::string error;check(decodeHighScores(bytes,decoded,error)&&encodeHighScores(decoded)==bytes,"Native score file exact roundtrip");
 auto before=decoded;bytes.pop_back();check(!decodeHighScores(bytes,decoded,error)&&decoded[0].name==before[0].name,"Truncation rejected transactionally");
 auto current=makeHighScore("Player",3,12,100,54321,3661000,7,3);check(current.values[6]==70&&current.values[7]==10&&current.values[8]==1&&current.values[9]==1&&current.values[10]==1,"Native result stats and time");
 auto large=makeHighScore("Player",1,1,0,0,0,UINT32_MAX,UINT32_MAX);check(large.values[6]==50&&large.values[7]==UINT32_MAX,"Large contact totals cannot overflow accuracy or Ammo");
 check(insertHighScore(scores,current)==1&&scores[0].name=="Player","Higher level outranks other stats");current.name="Tie";check(insertHighScore(scores,current)==1&&scores[1].name=="Player","New equal entry inserted ahead of old tie");
 HighScoreEntry poor;poor.name="poor";check(insertHighScore(scores,poor)==0,"Outside top20 rejected");
 HighScores tied{};for(auto& e:tied)e=makeHighScore("old",1,1,10,100,0,1,1);
 auto better=tied[0];better.name="new";better.values[4]=11;better.values[5]=0;check(insertHighScore(tied,better)==1,"Kills sort ahead of money");
 const auto path=(std::filesystem::temp_directory_path()/("vyruz-high-score-"+std::to_string(getpid())+".sco")).string();
 check(writeHighScores(path,scores,error)&&readHighScores(path,decoded,error)&&decoded[0].name=="Tie","Persistent original score format");std::filesystem::remove(path);
}
