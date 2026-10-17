#include "game/hud_stats.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>
using namespace vyruz;
void check(bool b){if(!b){std::cerr<<"HUD counter classification regression\n";std::exit(1);}}
TextNode field(std::string key,std::vector<std::string> values){TextNode n;n.key=key;n.values=values;return n;}
TextNode type(std::string kind,std::vector<TextNode> children={}){TextNode n;n.key="TYPE";n.arguments={kind};n.children=children;return n;}
int main(){
    check(nativeSpeedReadout(0)==0&&nativeSpeedReadout(1.25f)==125&&nativeSpeedReadout(4.5f)==450);
    check(nativeSpeedReadout(9.5f)==950&&nativeSpeedReadout(10)==0&&nativeSpeedReadout(10.25f)==25);
    check(nativeSpeedReadout(-1.25f)==-125&&nativeSpeedReadout(-10.25f)==-25);
    check(nativeSpeedReadout(999.5f)==950&&nativeSpeedReadout(1000)==900&&nativeSpeedReadout(1001)==900);
    check(nativeSpeedReadout(-999.5f)==-950&&nativeSpeedReadout(-1000)==-900&&nativeSpeedReadout(-1001)==-900);
    check(nativeSpeedReadout(std::numeric_limits<float>::infinity())==0&&nativeSpeedReadout(std::numeric_limits<float>::quiet_NaN())==0);
    // No float32 product rounding before truncation (native x87 keeps precision).
    check(nativeSpeedReadout(1.01f)==100&&nativeSpeedReadout(std::nextafter(1.01f,0.f))==100);
    // Same surface pixels/sec has different native magnitude by projection axis.
    check(nativeSpeedReadout(.014f*200*std::sqrt(.625f))==221);
    check(nativeSpeedReadout(.014f*200*std::sqrt(2.5f))==442);

    SceneContent scene;ObjectClass player;player.name="Player vehicle";player.behaviours={"Player"};scene.classes.push_back(player);
    ObjectPlacement cop;cop.behaviours={"Random","Seeker"};cop.sprite.path="OBJECT\\COP\\COP.GAF";
    cop.properties={type("Seeker",{field("Target",{"Player","vehicle"})}),field("Kill-Count",{"20000"})};
    check(countsAsEnemy(cop,scene)&&killCreditEligible(cop));
    ObjectPlacement box;box.behaviours={"Fixed"};box.properties=cop.properties;box.sprite.path="object/thing/box.gaf";
    check(!countsAsEnemy(box,scene)&&!killCreditEligible(box));
    auto patrol=cop;patrol.behaviours={"Patrol"};check(!countsAsEnemy(patrol,scene)&&killCreditEligible(patrol));
    auto neutral=cop;neutral.properties={type("Seeker",{field("Target",{"Missing"})})};check(!countsAsEnemy(neutral,scene));
    auto tracker=cop;tracker.behaviours={"Tracker"};tracker.properties={type("Tracker",{field("Target",{"Player","vehicle"})})};check(countsAsEnemy(tracker,scene));
    auto thing=cop;thing.properties.push_back(field("Sub-type",{"Thing"}));check(!countsAsEnemy(thing,scene)&&!killCreditEligible(thing));
    ObjectClass product;product.name="Guard robot";product.behaviours=cop.behaviours;product.properties=cop.properties;scene.classes.push_back(product);
    ObjectPlacement gen;gen.behaviours={"Fixed"};gen.sprite.path="object/staticgen/generator.gaf";gen.properties={type("Generator",{field("Product",{"Guard","robot"}),field("Max-births",{"0"})})};
    check(countsAsEnemy(gen,scene)&&killCreditEligible(gen));
    scene.classes[1].behaviours={"Fixed"};check(!countsAsEnemy(gen,scene));
    auto self=cop;self.behaviours={"Player","Seeker"};check(!countsAsEnemy(self,scene));
}
