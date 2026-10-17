// Black-box content admission regressions from native409490's validated grammar.
#include "game/content.h"
#include "game/brain.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
using namespace vyruz;
static void check(bool value,const std::string& why){if(!value){std::cerr<<why<<'\n';std::exit(1);}}
static TextDocument document(const std::string& text,const std::string& source="COMMON/OBJECT.3DC"){
 TextDocument doc;std::string error;check(parseContentText(text,source,doc,error),error);return doc;
}
static std::string definition(const std::string& behavior,const std::string& name="Subject"){
 return "OBJECT-CLASS [\nName = "+name+"\nBehaviour = \""+behavior+"\"\n]\n";
}
int main(){
 std::string error;std::vector<ObjectClass> catalog;
 // The complete canonical language is empty, Player, or one base optionally
 // followed by one Seeker/Tracker and then Guardian:22 combinations in total.
 size_t valid=0;
 auto accept=[&](const std::string& text){check(buildClassCatalog({document("1.3\n"+definition(text))},catalog,error),"native-valid behavior rejected: "+text+": "+error);++valid;};
 accept("");accept("Player");
 for(const std::string base:{"Random","Fixed","Patrol","Straight"}){accept(base);for(const std::string target:{"Seeker","Tracker"}){accept(base+" "+target);accept(base+" "+target+" Guardian");}}
 check(valid==22,"canonical grammar coverage");
 accept(" \tfIxEd\t sEeKeR gUaRdIaN  ");
 check(catalog[0].behaviours.size()==3&&catalog[0].behaviours[0]=="fIxEd"&&catalog[0].behaviours[1]=="sEeKeR"&&catalog[0].behaviours[2]=="gUaRdIaN","valid authored spelling survives for catalog/save compatibility");
 // Native lookup405bb0 hashes case-folded bytes, rather than comparing token
 // strings.16'a' has hash0; the following prefixes are real accepted aliases.
 accept("aaaaaaaaaaaaaaaaFixed aaaaaaaaaaaaaaaaSeeker aaaaaaaaaaaaaaaaGuardian");
 ObjectPlacement alias;alias.behaviours=catalog[0].behaviours;SceneContent context;context.classes=catalog;
 const auto brain=makeBrainConfig(alias,context);check(brain.base==BrainBase::Fixed&&brain.seeker&&brain.guardian,"native hash aliases reach existing controllers");
 auto reject=[&](const std::string& text){const auto old=catalog;check(!buildClassCatalog({document("1.3\n"+definition("Fixed","Earlier")+definition(text))},catalog,error)&&!error.empty(),"native-invalid behavior accepted: "+text);check(catalog.size()==old.size()&&catalog[0].name==old[0].name&&catalog[0].behaviours==old[0].behaviours,"failed catalog validation publishes nothing");};
 for(const std::string text:{"Bogus","Seeker","Tracker","Guardian","Fixed Fixed","Fixed Random","Patrol Straight","Straight Player","Player Player","Player Fixed","Player Seeker","Player Tracker","Player Guardian","Fixed Seeker Seeker","Fixed Tracker Tracker","Fixed Seeker Tracker","Fixed Tracker Seeker","Fixed Guardian","Fixed Guardian Seeker","Fixed Seeker Guardian Guardian","Fixed Seeker Guardian Tracker","Fixed Seeker Guardian Patrol","Fixed=Seeker","=Fixed","Fixed,Seeker"})reject(text);
 // Exercise the actual object-override resolution site independently of valid
 // inherited classes. A failed placement must not replace the caller's scene.
 std::unordered_map<std::string,std::string> files;
 for(const char* name:{"DIFFICULTY","THING","FX","AMMO","WEAPON","RESOURCE","ITEM","OBJECT","SPECIFIC","DECOR","COMMON-DECOR"})files[std::string("COMMON/")+name+".3DC"]="1.3\n";
 files["COMMON/OBJECT.3DC"]="1.3\n"+definition("Player","Pilot")+definition("Fixed Seeker","Guard");files["WORLD/TEST.3DP"]="1.3\n";
 const std::string mapHeader="1.3\nLEVEL-NAME [ Behavior audit ]\nLEVEL-ID [ 1.1 ]\n3DG-FILENAME [ TEST.3DG ]\n";
 auto objects=[&](const std::string& text){return mapHeader+"OBJECT-COUNT [ 2 ]\nCREATE-OBJECT [\nClass-name = Pilot\nPosition = 64, 32\n]\nCREATE-OBJECT [\nClass-name = Guard\nPosition = 128, 64\nBehaviour = \""+text+"\"\n]\n";};
 files["WORLD/TEST.3DO"]=objects("Patrol Tracker Guardian");ContentReader read=[&](const auto& path,auto& bytes){const auto it=files.find(path);if(it==files.end())return false;bytes.assign(it->second.begin(),it->second.end());return true;};
 SceneContent scene;check(loadSceneContent(read,"WORLD/TEST.3DG",scene,error),error);check(scene.player==ObjectId{0}&&scene.objects[1].behaviours==std::vector<std::string>({"Patrol","Tracker","Guardian"}),"valid placement override preserved");
 const auto originalMap=scene.mapPath;const auto originalObjects=scene.objects.size();for(const std::string text:{"Player Tracker","Fixed Guardian","Patrol Seeker Tracker","Bogus"}){files["WORLD/TEST.3DO"]=objects(text);check(!loadSceneContent(read,"WORLD/TEST.3DG",scene,error)&&!error.empty(),"invalid placement override rejected");check(scene.mapPath==originalMap&&scene.objects.size()==originalObjects&&scene.objects[1].behaviours[0]=="Patrol","failed placement validation is transactional");}
 files["WORLD/TEST.3DO"]=mapHeader+"OBJECT-COUNT [ 1 ]\nCREATE-OBJECT [\nClass-name = Guard\nPosition = 64, 32\nBehaviour = aaaaaaaaaaaaaaaaPlayer\n]\n";
 check(loadSceneContent(read,"WORLD/TEST.3DG",scene,error)&&scene.player==ObjectId{0},"native Player hash alias selects controlled actor");
 std::cout<<"PASS native behavior grammar, spelling/hash aliases, inheritance/placement admission and transactional errors\n";
}
