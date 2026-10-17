#include "game/content.h"
#include "game/content_names.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

using namespace vyruz;
static void check(bool ok, const std::string& message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static TextDocument parse(const std::string& s, const std::string& name="COMMON/OBJECT.3DC") {
    TextDocument d; std::string e;
    check(parseContentText(s,name,d,e),e); return d;
}
int main() {
    check(contentNameEqual("Saving-Plot","saving-plot") && !contentNameEqual("ammo","ammunition"),"allocation-free content key comparison");
    check(contentNameIs("FX","fx") && !contentNameIs("FX","FX"),"attribute token comparison retains lowercase-token convention");
    std::vector<TextNode> duplicate(2);duplicate[0].key="Energy";duplicate[1].key="ENERGY";
    check(contentProperty(duplicate,"energy")==&duplicate[1],"last authored property still wins");
    const std::string classes = R"(1.3
DEFAULT-PROPERTY [
 Behaviour = Fixed
 Shield [
  Initial = 1
  Max = 100
 ]
 Managed = 1;
]
OBJECT-CLASS [
 Name = Base
 GAF-name = OBJECT\test sprite.GAF
 GAF-index = 3
 TYPE Explosion [ Damage = 10 ]
 TYPE Explosion [ Damage = 20 ]
 Future [ Unknown = 7 ]
]
OBJECT-CLASS [
 GAF-index = 99
 Derived-From = Base
 Name = Child
 Shield [ Initial = 20 ]
 Behaviour = Player
 Over-water = 0
]
OBJECT-CLASS [
 Derived-From = Child
 Name = Cleared
 Override-Attributes
 TYPE Patrol [ Path-ID = -1 ]
]
DEFAULT-PROPERTY [ Managed = 0 ]
OBJECT-CLASS [ Name = Late ]
)";
    std::vector<ObjectClass> catalog; std::string error;
    auto doc = parse(classes);
    check(buildClassCatalog({doc,parse("1.3\nOBJECT-CLASS [ Name = Fresh ]","WORLD/ITEM.3DC")},catalog,error),error);
    check(catalog.size()==5,"class count");
    const auto& child=catalog[1];
    check(child.parent==std::optional<ClassId>(ClassId{0}),"typed parent ID");
    check(child.sprite.path=="COMMON/DATA/OBJECT/test sprite.GAF" && child.sprite.sequence.value==3,
          "inheritance must reset earlier override and retain resource provenance");
    check(child.behaviours.size()==1 && child.behaviours[0]=="Player","valid Player behavior remains inherited");
    auto shield=contentProperty(child.properties,"SHIELD");
    check(shield && contentProperty(shield->children,"initial")->values[0]=="20" &&
          contentProperty(shield->children,"max")->values[0]=="100","nested field inheritance");
    int explosions=0;
    for(const auto& p:child.properties) if(p.key=="TYPE") ++explosions;
    check(explosions==2 && contentProperty(child.properties,"Future"),"repeated and unknown attributes retained");
    check(catalog[2].properties.size()>0 && contentProperty(catalog[2].properties,"TYPE")->arguments[0]=="Patrol",
          "attribute override retains new attribute");
    check(child.flags.managed==true && catalog[3].flags.managed==false && !catalog[4].flags.managed,
          "default snapshots and per-file reset");
    const auto rejects=[&](const std::string& body){
        auto size=catalog.size();
        check(!buildClassCatalog({parse("1.3\n"+body)},catalog,error) && !error.empty() && catalog.size()==size,
              "malformed catalog must fail atomically");
    };
    rejects("OBJECT-CLASS [\n Derived-From = Missing\n Name = Bad\n]");
    rejects("OBJECT-CLASS [ Name = Same ]\nOBJECT-CLASS [ Name = same ]");
    rejects("OBJECT-CLASS [\n Name = Bad\n GAF-index = -1\n]");
    rejects("OBJECT-CLASS [\n Name = Bad\n Invisible = 9\n]");
    rejects("OBJECT-CLASS [\n Name = Bad\n TYPE Patrol\n]");
    rejects("OBJECT-CLASS [\n Name = Bad\n TYPE [ Path-ID = 7 ]\n]");
    rejects("OBJECT-CLASS [\n Name = Bad\n Override-Attributes\n]");
    auto disabled=parse("1.3\nOBJECT-CLASS [\nName = Hidden\nGAF-index = 0\nShadow-index = 0\n]\n");
    std::vector<ObjectClass> invisible;
    check(buildClassCatalog({disabled},invisible,error) && invisible[0].sprite.sequence.value==0 &&
          invisible[0].shadow.sequence.value==0,"zero disables animation channels");

    std::unordered_map<std::string,std::string> files;
    for(const char* name:{"DIFFICULTY","THING","FX","AMMO","WEAPON","RESOURCE","ITEM","OBJECT","SPECIFIC","DECOR","COMMON-DECOR"})
        files[std::string("COMMON/")+name+".3DC"]="1.3\n";
    files["COMMON/OBJECT.3DC"]=classes;
    // Existing empty local file must win over the otherwise invalid Common file.
    files["WORLD/DECOR.3DC"]="1.3\n"; files["COMMON/DECOR.3DC"]="not valid";
    files["WORLD/TEST.3DO"]=R"(1.3
LEVEL-NAME [ Test level ]
LEVEL-ID [ 1.1 ]
OBJECT-COUNT [ 2 ]
3DG-FILENAME [ TEST.3DG ]
CREATE-OBJECT [
 Class-name = child
 Position = 10.5, -20
 Shield = 5
 TYPE Patrol [
  Path-ID = 7
  Point-ID = -1
 ]
 Mystery [ Extra = retained ]
]
CREATE-OBJECT [
 Class-name = Base
 Position = 0, 0
 TYPE Patrol [ Path-ID = 99 ]
]
)";
    files["WORLD/TEST.3DP"]=R"(1.3
CREATE-PATH [
 Id = 7
 Count = 2
 Point = 1, 2
 Point = 3, 4
]
)";
    ContentReader read=[&](const auto& name,auto& bytes) {
        auto it=files.find(name); if(it==files.end())return false;
        bytes.assign(it->second.begin(),it->second.end());return true;
    };
    SceneContent scene;
    check(loadSceneContent(read,"WORLD/TEST.3DG",scene,error),error);
    check(scene.objects.size()==2 && scene.paths.size()==1 && scene.player->value==0,"typed scene counts/player");
    check(!scene.objects[0].owner && scene.objects[0].position.x==10.5f,"ownership and fractional position");
    check(scene.objects[0].patrols[0].resolved && !scene.objects[1].patrols[0].resolved && scene.diagnostics.size()==1,
          "valid path and retained unresolved path diagnostic");
    check(contentProperty(scene.objects[0].properties,"shield")->kind==TextKind::Assignment &&
          contentProperty(scene.objects[0].properties,"mystery"),"placement scalar and unknown overrides");
    const auto old=files["WORLD/TEST.3DO"];
    auto withoutPlayer=old;
    withoutPlayer.insert(withoutPlayer.find(" Position = 10.5")," Behaviour = Fixed\n");
    files["WORLD/TEST.3DO"]=withoutPlayer;
    check(loadSceneContent(read,"WORLD/TEST.3DG",scene,error) && !scene.player,
          "resolved placement behavior controls player identification");
    files["WORLD/TEST.3DO"]=old;
    // A placement updates an existing patrol's fields, retaining untouched defaults.
    auto classWithPatrol=classes;
    const auto future=classWithPatrol.find(" Future [");
    classWithPatrol.insert(future," TYPE Patrol [\n Path-ID = 0\n Closed = 1\n ]\n");
    files["COMMON/OBJECT.3DC"]=classWithPatrol;
    check(loadSceneContent(read,"WORLD/TEST.3DG",scene,error),error);
    check(scene.objects[0].patrols.size()==1 && scene.objects[0].patrols[0].path.value==7,
          "placement patrol must update inherited attribute, not duplicate it");
    for(const auto& p:scene.objects[0].properties) if(p.key=="TYPE" && p.arguments[0]=="Patrol")
        check(contentProperty(p.children,"closed")->values[0]=="1","partial patrol override retains defaults");
    // Native 413d80/4158be claims attributes by tag and ordinal, not Name.
    auto ammoClasses=classes;
    ammoClasses.insert(ammoClasses.find(" Future ["),
        " ADD Ammo [\n Name = Gatling\n Count = 11\n Max = 100\n ]\n"
        " ADD Ammo [\n Name = Rocket\n Count = 22\n Max = 200\n ]\n");
    files["COMMON/OBJECT.3DC"]=ammoClasses;
    auto ammoPlacement=old;
    ammoPlacement.insert(ammoPlacement.find(" Position = 10.5"),
        " ADD Ammo [\n Name = Mine\n Count = 3\n ]\n"
        " ADD Ammo [\n Name = Mine\n Count = 4\n ]\n");
    files["WORLD/TEST.3DO"]=ammoPlacement;
    check(loadSceneContent(read,"WORLD/TEST.3DG",scene,error),error);
    size_t ammoIndex=0;
    for(const auto& p:scene.objects[0].properties)if(p.key=="ADD") {
        check(contentProperty(p.children,"Name")->values[0]=="Mine","placement renames claimed ammo slot");
        check(contentProperty(p.children,"Count")->values[0]==(ammoIndex?"4":"3"),"each slot claimed once");
        check(contentProperty(p.children,"Max")->values[0]==(ammoIndex?"200":"100"),"partial override retains slot cap");
        ++ammoIndex;
    }
    check(ammoIndex==2,"replacement does not append or merge by name");
    files["WORLD/TEST.3DO"]=old;
    files["COMMON/OBJECT.3DC"]=classes;
    for(const auto& bad:{std::string("Position = nan, 0"),std::string("Class-name = Missing"),
                        std::string("Override-Objects")}) {
        files["WORLD/TEST.3DO"]="1.3\nLEVEL-NAME [Bad]\nLEVEL-ID [1]\nOBJECT-COUNT [1]\n3DG-FILENAME [TEST.3DG]\nCREATE-OBJECT [\nClass-name = Base\nPosition = 1, 2\n"+bad+"\n]\n";
        check(!loadSceneContent(read,"WORLD/TEST.3DG",scene,error) && scene.objects.size()==2,
              "invalid placement fails without replacing previous scene");
    }
    files["WORLD/TEST.3DO"]=old;
    files["WORLD/TEST.3DO"].replace(files["WORLD/TEST.3DO"].find("Point-ID = -1"),13,"Point-ID = 10");
    check(!loadSceneContent(read,"WORLD/TEST.3DG",scene,error),"patrol point outside path");
    files["WORLD/TEST.3DO"]=old;
    files["WORLD/TEST.3DP"]="1.3\nCREATE-PATH [\nId = 7\nCount = 3\nPoint = 1, 2\n]\n";
    check(!loadSceneContent(read,"WORLD/TEST.3DG",scene,error),"path count mismatch");
    files.erase("COMMON/FX.3DC");
    check(!loadSceneContent(read,"WORLD/TEST.3DG",scene,error),"missing required catalog");
}
