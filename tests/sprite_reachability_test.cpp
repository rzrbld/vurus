#include "render/sprite_reachability.h"
#include "render/sprite_sequences.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static TextNode ref(std::string key,std::string value){TextNode n;n.key=key;n.values={value};return n;}
int main(){
 GafAnimation bank;bank.sequences.resize(30);
 SpriteSequenceUse use;use.sequences={5};use.directionalBases={5};
 auto chosen=use.select(bank);
 for(size_t i=0;i<chosen.size();++i)if(chosen[i]!=(i>=4&&i<=27))return 2;
 bank.sequences.resize(27);chosen=use.select(bank);
 for(size_t i=0;i<chosen.size();++i)if(chosen[i]!=(i==4))return 3;
 SceneContent policy;ObjectClass fx;fx.sprite.path="COMMON\\FX.GAF";fx.sprite.sequence={5};
 TextNode subtype;subtype.key="Sub-type";subtype.values={"FX"};fx.properties={subtype};policy.classes.push_back(fx);
 auto rules=sceneSpriteSequenceUses(policy);
 if(rules.at("common/fx.gaf").all || !rules.at("common/fx.gaf").sequences.contains(5))return 4;
 fx.properties[0].values={"Walker"};policy.classes.push_back(fx);
 if(!sceneSpriteSequenceUses(policy).at("common/fx.gaf").all)return 5;
 SceneContent s;
 for(const char* name:{"Generator","Enemy","Death","Trail","Unused","CarriedWeapon","Ammo","Smoke","Override","Water-Splash"}){
  ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;s.classes.push_back(c);
 }
 TextNode generator;generator.key="TYPE";generator.arguments={"StaticGen"};generator.children={ref("Product","enemy")};
 s.classes[0].properties={generator};
 TextNode drop;drop.key="HAVE";drop.arguments={"Death"};s.classes[1].properties={drop};
 TextNode create;create.key="CREATE";create.children={ref("Name","Trail")};s.classes[2].properties={create};
 s.classes[3].properties={ref("Name","Death")}; // cycle must terminate
 TextNode weapon;weapon.key="TYPE";weapon.arguments={"Weapon"};weapon.children={ref("Ammo","Ammo"),ref("Smoke-fx","Smoke")};
 s.classes[5].properties={weapon};
 ObjectPlacement object;object.classId={0};object.properties={ref("Hit-fx","Override")};s.objects.push_back(object);
 const auto reached=reachableSpriteClasses(s);
 for(size_t i=0;i<reached.size();++i)if(reached[i]!=(i!=4)){std::cerr<<"wrong reachability "<<i<<'\n';return EXIT_FAILURE;}
}
