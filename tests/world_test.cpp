#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/lift_access.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "assets/content_text.h"
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <limits>
using namespace vyruz;
void check(bool v,const char* why) {
#ifdef _WIN32
    // CTest retains these breadcrumbs when Windows terminates without a message.
    std::cerr<<"World checkpoint: "<<why<<std::endl;
#endif
    if (!v) {std::cerr<<why<<'\n';std::exit(1);} }
TextNode assignment(std::string name,std::string value) { TextNode n;n.kind=TextKind::Assignment;n.key=name;n.values={value};return n; }
TextNode block(std::string name,std::vector<TextNode> fields,std::string type={}) {
    TextNode n;n.kind=TextKind::Block;n.key=name;n.children=fields;if (!type.empty())n.arguments={type};return n;

}
//These fixtures model active authored classes, whose resolved originals carry
//Managed=1. Raw absent Managed is false; do not rely on the old all-actor loop.
static bool loadFixture(GameWorld& world,SceneContent& scene,const Level& level,std::string& error) {
    for(auto& cls:scene.classes)if(!contentProperty(cls.properties,"Managed"))
        cls.properties.push_back(assignment("Managed","1"));
    return world.load(scene,level,error);
}
int main() try {
    SceneContent scene;Level level;level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
    player.properties={assignment("Bounding-box-size","22"),block("Energy",{assignment("Initial","50"),assignment("Max","100")}),
        block("Shield",{assignment("Initial","20"),assignment("Max","100")}),
        block("ADD",{assignment("Name","Gun")},"Weapon"),block("ADD",{assignment("Name","Bullet"),assignment("Count","100"),assignment("Max","200")},"Ammo")};
    ObjectClass gun;gun.id={1};gun.name="Gun";gun.properties={block("TYPE",{assignment("Ammo","Bullet"),assignment("Delay","98"),assignment("Fire-Range","400"),assignment("Visible-gun-fire","1")},"Weapon")};
    ObjectClass bullet;bullet.id={2};bullet.name="Bullet";bullet.properties={assignment("Speed-max","300"),block("TYPE",{assignment("damage","10"),assignment("damage-zone","1")},"Ammo")};
    ObjectClass box;box.id={3};box.name="Box";box.behaviours={"Fixed"};
    TextNode have;have.kind=TextKind::Bare;have.key="HAVE";have.arguments={"Repair"};
    box.properties={assignment("Bounding-box-size","10"),block("Energy",{assignment("Initial","1")}),block("Shield",{assignment("Initial","10")}),have};
    ObjectClass repair;repair.id={4};repair.name="Repair";repair.properties={assignment("Bounding-box-size","12"),block("TYPE",{assignment("Only-for-player","1"),assignment("Add-NRJ","25")},"Item")};
    ObjectClass lift;lift.id={5};lift.name="Lift";lift.sprite.sequence={1};lift.properties={assignment("Bounding-box-size","64"),block("TYPE",{assignment("Active","1"),assignment("Present","1"),assignment("Stay-Up","1"),assignment("Delay","100"),assignment("Usage-Count","1"),assignment("Scenery","cave"),assignment("3do-file","t1c1")},"Lift")};
    scene.classes={player,gun,bullet,box,repair,lift};
    auto place=[&](size_t cls,float x,float y) {ObjectPlacement p;p.id={static_cast<uint32_t>(scene.objects.size())};p.classId={static_cast<uint32_t>(cls)};p.position={x,y};p.sprite=scene.classes[cls].sprite;p.properties=scene.classes[cls].properties;p.behaviours=scene.classes[cls].behaviours;scene.objects.push_back(p);};
    place(0,200,200);place(3,300,200);place(5,500,500);scene.player=ObjectId{0};
    // Shorelines block vehicles, but resolved Ammo Over-water must let both
    // player and enemy shots reach actors across the water. Exercise live World
    // and a disk continuation, not the --walk geometry probe.
    for(bool enemyShot:{false,true})for(bool water:{false,true})for(bool over:{false,true}) {
        auto fixture=scene;fixture.mapPath="TEST/WATER.3DG";fixture.objects.resize(2);
        fixture.objects[enemyShot?0:1].position={500,400};
        fixture.objects[enemyShot?1:0].position={200,200};
        fixture.classes[2].properties.push_back(assignment("Over-water",over?"1":"0"));
        auto terrain=level;terrain.collisionEdges={{450,0,450,800,water}};
        std::string error;GameWorld world;check(loadFixture(world,fixture,terrain,error),"water projectile fixture");
        auto state=world.snapshot();const size_t target=enemyShot?0:1;
        state.actors[target].vitals.energy=100;state.actors[target].vitals.shield=100;
        Projectile shot;shot.classId={2};shot.owner=enemyShot?1:0;shot.position={400,400};
        shot.radius=1;shot.directionX=.70710678f;shot.directionZ=-.70710678f;
        shot.damage=10;shot.damageRadius=1;shot.lifetimeMs=10000;shot.ageMs=600;
        shot.motionConfig.speedMax=1000;shot.motionConfig.inertia=true;
        shot.motionConfig.frictionX=shot.motionConfig.frictionZ=1;
        shot.motion.velocityX=10;shot.motion.velocityZ=-10;shot.motion.magnitude=14.142136f;
        vyruz_test::appendProjectile(state,fixture,shot);
        check(world.restoreSnapshot(state,error),"water projectile valid initial lifecycle");
        for(int tick=0;tick<2;++tick)world.tick(.014f,{});
        SaveGame save;save.currentLevel=fixture.mapPath;save.levels[save.currentLevel].world=world.snapshot();
        std::vector<uint8_t> bytes;SaveGame disk;
        check(encodeSave(save,bytes,error)&&decodeSave(bytes,disk,error),"water projectile disk checkpoint");
        auto copy=fixture;GameWorld resumed;check(loadFixture(resumed,copy,terrain,error)&&
            resumed.restoreSnapshot(disk.levels.at(save.currentLevel).world,error),"water projectile disk restore");
        for(int tick=0;tick<7;++tick){world.tick(.014f,{});resumed.tick(.014f,{});}
        const bool hit=world.actors()[target].vitals.shield<100;
        check(hit==(water&&over),"water flag passes only shorelines in both firing directions");
        check(resumed.actors()[target].vitals.shield==world.actors()[target].vitals.shield&&
            resumed.projectiles().size()==world.projectiles().size(),"water projectile disk continuation");
        check(!terrainMoveAllowed(terrain,400,400,500,400,22),"vehicle shoreline/wall still blocks");
    }
    // Slope expiry still selects the original water/ground product; crossing
    // a shoreline must not replace ground termination with a wall impact.
    for(bool water:{false,true}) {
        auto fixture=scene;fixture.objects.resize(1);
        for(const char* name:{"Water-Splash","Bullet-Ground-Impact"}) {
            ObjectClass fx;fx.id={static_cast<uint32_t>(fixture.classes.size())};fx.name=name;
            fx.behaviours={"Fixed"};fx.properties={assignment("Life-time","1000"),block("TYPE",{},"FX")};
            fixture.classes.push_back(fx);
        }
        auto terrain=level;terrain.collisionMask.resize(10000);terrain.collisionMask[12*100+6]=water?0x100:0x1e;
        GameWorld world;std::string error;check(loadFixture(world,fixture,terrain,error),"water ground-impact fixture");
        auto state=world.snapshot();Projectile shot;shot.classId={2};shot.owner=0;shot.position={400,400};
        shot.radius=1;shot.directionX=1;shot.slopeEnabled=true;shot.slopeAltitude=1;shot.slopeHeight=1;shot.slopeRadians=-.5f;
        vyruz_test::appendProjectile(state,fixture,shot);
        check(world.restoreSnapshot(state,error),"water ground-impact restore");world.tick(.014f,{});
        check(world.projectiles().empty(),"slope termination removes bullet over water and ground");
        check(std::any_of(fixture.objects.begin(),fixture.objects.end(),[&](const auto& o){return o.classId.value==(water?6u:7u);}),
            "original water splash versus ground impact product");
    }
    // Recycle only expired inert CREATE trails; keep projectile/emitter IDs,
    // emitted lifetime, per-slot generations and saved continuation intact.
    for(bool fx:{false,true}) {
        auto fixture=scene;fixture.objects.resize(1);
        fixture.classes[2].properties={assignment("Speed-max","0"),assignment("Life-time","2000"),
            block("TYPE",{assignment("Ignore-owner","1")},"Ammo"),
            block("CREATE",{assignment("Name","Smoke"),assignment("Repeat-timeout","1"),assignment("At-current-position","1")})};
        ObjectClass smoke;smoke.id={6};smoke.name="Smoke";smoke.behaviours={"Fixed"};
        smoke.properties={assignment("Life-time","50")};
        if(fx)smoke.properties.push_back(block("TYPE",{},"FX"));
        fixture.classes.push_back(smoke);const auto original=fixture;
        GameWorld pooled;std::string failure;check(loadFixture(pooled,fixture,level,failure),"trail pool fixture");
        WorldInput fire;fire.fire=true;pooled.tick(1.f/60,fire);
        for(int t=0;t<20;++t)pooled.tick(1.f/60,{});
        for(size_t id=0;id<fixture.objects.size();++id)if(fixture.objects[id].classId==smoke.id)
            check(!fixture.objects[id].owner,"projectile CREATE trails have no shooter ancestry");
        check(pooled.projectiles().size()==1 && pooled.projectiles()[0].visual &&
              fixture.objects[*pooled.projectiles()[0].visual].owner==ObjectId{0},
              "damaging projectile keeps shooter ancestry");
        check(pooled.actors().size()<=6,"expired trail slots bound retained storage");
        check(std::any_of(pooled.actors().begin(),pooled.actors().end(),[](const auto& a){return a.pooledTrail&&a.visualRevision>0;}),"trail reuse increments renderer identity");
        if(fx)for(size_t id=0;id<pooled.actors().size();++id)if(pooled.actors()[id].pooledTrail)pooled.expireVisual(id);
        SaveGame game;game.currentLevel="TEST/POOL.3DG";game.levels[game.currentLevel].world=pooled.snapshot();
        std::vector<uint8_t> bytes;SaveGame disk;
        check(encodeSave(game,bytes,failure)&&decodeSave(bytes,disk,failure),"pooled trail save roundtrip");
        // Older saves may have the incorrect owner, including pre-v11 actors
        // with no pooling marker. Repair only inert authored Ammo CREATE targets.
        auto old=disk.levels.at(game.currentLevel).world;
        for(size_t id=0;id<old.objects.size();++id)if(old.objects[id].classId==smoke.id) {
            old.objects[id].owner=ObjectId{0};old.actors[id].pooledTrail=false;
        }
        auto legacyScene=original;GameWorld legacy;
        check(loadFixture(legacy,legacyScene,level,failure)&&legacy.restoreSnapshot(old,failure),"legacy trail ancestry restore");
        for(const auto& object:legacyScene.objects)if(object.classId==smoke.id)
            check(!object.owner,"legacy trail ancestry normalized without pooling marker");
        check(legacyScene.objects[*legacy.projectiles()[0].visual].owner==ObjectId{0},
              "legacy normalization preserves damaging projectile ancestry");
        auto copy=original;GameWorld resumed;check(loadFixture(resumed,copy,level,failure)&&resumed.restoreSnapshot(disk.levels.at(game.currentLevel).world,failure),"pooled trail restore rebuilds free slots");
        for(int t=0;t<30;++t){
            pooled.tick(1.f/60,{});resumed.tick(1.f/60,{});
            check(pooled.actors().size()==resumed.actors().size(),"same slot choices after load");
            for(size_t id=0;id<pooled.actors().size();++id){const auto& a=pooled.actors()[id];const auto& b=resumed.actors()[id];
                check(a.alive==b.alive&&a.ageMs==b.ageMs&&a.visualRevision==b.visualRevision,"same reused lifetime/generation after load");}
        }
        auto bad=pooled.snapshot();bad.actors[0].pooledTrail=true;
        check(!pooled.restoreSnapshot(bad,failure),"reject recycling a gameplay actor");
        for(int cause=0;cause<5;++cause) {
            auto cutoffScene=original;
            //Same-pass newborns age25ms immediately; retain an independently
            //living75ms trail through the carrier removal at50ms.
            cutoffScene.classes[smoke.id.value].properties[0]=assignment("Life-time","75");
            cutoffScene.classes[2].properties[1]=assignment("Life-time",cause==0?"50":cause==2?"0":"2000");
            if(cause==0)cutoffScene.classes[2].properties[0]=assignment("Speed-max","300");
            if(cause==1||cause==4)cutoffScene.classes[2].properties[2].children.push_back(assignment("Damage-after","50"));
            GameWorld cutoff;check(loadFixture(cutoff,cutoffScene,level,failure),"CREATE cutoff fixture");
            cutoff.tick(.025f,fire);check(cutoff.projectiles().size()==1,"carrier alive before cutoff");
            const auto visual=*cutoff.projectiles()[0].visual;
            const auto positionBefore=cutoff.projectiles()[0].position;
            size_t trailsBefore=0;
            for(const auto& o:cutoffScene.objects)trailsBefore+=o.classId==smoke.id;
            check(trailsBefore==2,"constructor and first awake visit each emit due CREATE");
            if(cause==3) {
                auto removed=cutoff.snapshot();removed.actors[visual].alive=false;vyruz_test::releaseActor(removed,visual);
                check(cutoff.restoreSnapshot(removed,failure),"externally removed carrier checkpoint");
            }
            if(cause==4) {
                auto edge=cutoff.snapshot();auto& shot=edge.projectiles[0];
                shot.position.x=level.mapW*64.f-1;shot.motion.velocityX=100;shot.motion.velocityZ=-100;
                edge.objects[visual].position=shot.position;vyruz_test::synchronizeActor(edge,visual);
                check(cutoff.restoreSnapshot(edge,failure),"fuse at edge checkpoint");
            }
            cutoff.tick(.025f,{});
            size_t trailsAfter=0;
            for(const auto& o:cutoffScene.objects)if(o.classId==smoke.id) {
                ++trailsAfter;check(cutoff.actors()[o.id.value].alive,"detached trail survives carrier removal");
            }
            check(trailsAfter==trailsBefore+unsigned(cause==1||cause==2||cause==4),
                  "class lifetime/removal suppress CREATE; fuse/zero lifetime allow due CREATE");
            check(cutoff.projectiles().empty()==(cause!=2),"only zero-lifetime carrier remains alive");
            if(cause==0)check(cutoffScene.objects[visual].position.x==positionBefore.x &&
                             cutoffScene.objects[visual].position.y==positionBefore.y,
                             "class lifetime expires before further propulsion");
            for(int tick=0;tick<3&&cause!=2;++tick)cutoff.tick(.025f,{});
            if(cause!=2)for(const auto& o:cutoffScene.objects)if(o.classId==smoke.id)
                check(!cutoff.actors()[o.id.value].alive,"detached trails expire independently");
        }
    }
    // Native explicit Start-distance uses a scaled surface direction table,
    // then adds the barrel offset. Use stationary ammo to isolate launch from
    // the already tested movement solver and sweep collision.
    for(int direction=0;direction<24;++direction){
        auto fixture=scene;fixture.objects.resize(1);
        fixture.classes[1].properties[0].children.push_back(assignment("Start-distance","250"));
        fixture.classes[2].properties[0]=assignment("Speed-max","0");
        GameWorld launch;std::string failure;check(loadFixture(launch,fixture,level,failure),"launch placement fixture");
        launch.setPlayerDirection(direction);WorldInput shot;shot.fire=true;launch.tick(1.f/60,shot);
        check(launch.projectiles().size()==1,"explicit-offset shot remains live");
        const auto expected=projectileStartOffset(direction,250);const auto actual=launch.projectiles()[0].position;
        check(std::abs(actual.x-(200+expected.x))<.0001f&&std::abs(actual.y-(200+expected.y))<.0001f,
            "explicit Start-distance projects ahead of shooter for all24 headings");
    }
    for(int mode=0;mode<3;++mode) {
        auto fixture=scene;fixture.objects.resize(1);
        fixture.objects[0].properties.push_back(assignment("Altitude","24"));
        fixture.classes[2].properties[0]=assignment("Speed-max","0");
        fixture.classes[2].properties.push_back(assignment("Altitude","16"));fixture.classes[2].sprite.sequence={1};
        if(mode==1)fixture.classes[1].properties[0].children.push_back(assignment("Fire-altitude","37"));
        if(mode==2)fixture.classes[2].flags.inGround=true;
        GameWorld launch;std::string failure;check(loadFixture(launch,fixture,level,failure),"launch altitude fixture");
        WorldInput shot;shot.fire=true;launch.tick(1.f/60,shot);
        check(launch.projectiles().size()==1&&launch.projectiles()[0].visual.has_value(),"altitude projectile visual");
        const auto visual=*launch.projectiles()[0].visual;const float expected=mode==2?0:mode==1?37:24;
        check(launch.actors()[visual].projectileAltitude==expected,"launch uses shooter, explicit Fire-altitude or In-ground0");
        auto snapshot=launch.snapshot();check(launch.restoreSnapshot(snapshot,failure),"launch altitude restore");
        check(launch.actors()[visual].projectileAltitude==expected,"restore preserves launch altitude");
        snapshot.actors[visual].projectileAltitude.reset();check(launch.restoreSnapshot(snapshot,failure),"old launch altitude restore");
        check(launch.actors()[visual].projectileAltitude==expected,"old checkpoint derives launch altitude");
    }
    // Native vertical-bounce uses age and current horizontal magnitude; it
    // reflects dry ground and disposes only a negative integer over mask0x100.
    for(uint16_t mask:{uint16_t(0),uint16_t(0x100),uint16_t(0x101)}) {
        auto fixture=scene;fixture.objects.resize(1);
        auto& grenade=fixture.classes[2];grenade.sprite.sequence={25};
        grenade.properties.push_back(assignment("Vertical-Bounce","1"));
        grenade.properties.push_back(assignment("Altitude","24"));
        ObjectClass splash;splash.id={6};splash.name="Water-Splash";fixture.classes.push_back(splash);
        Level ground=level;ground.collisionMask.assign(ground.mapW*ground.mapH,mask);
        GameWorld bounce;std::string failure;check(loadFixture(bounce,fixture,ground,failure),"bounce fixture");
        bounce.setPlayerDirection(9);WorldInput fire;fire.fire=true;bounce.tick(.02f,fire);
        check(bounce.projectiles().size()==1&&bounce.projectiles()[0].visual.has_value(),"grenade body created");
        auto saved=bounce.snapshot();auto& p=saved.projectiles[0];const auto visual=*p.visual;
        p.ageMs=300;p.position={200,200};p.ignoreOwner=true;
        p.motion={9,0,9,0};p.motionConfig.inertia=true;p.motionConfig.launched=true;
        p.motionConfig.frictionX=p.motionConfig.frictionZ=1;
        p.motionConfig.thresholdX=p.motionConfig.thresholdZ=0;
        saved.actors[visual].projectileAltitude=999; // ignore transient checkpoint value
        check(bounce.restoreSnapshot(saved,failure),"bounce memory restore");
        check(bounce.actors()[visual].projectileAltitude==12,"restored altitude derives from native sine");
        SaveGame game;game.currentLevel="TEST/BOUNCE.3DG";game.levels[game.currentLevel].world=bounce.snapshot();
        std::vector<uint8_t> bytes;SaveGame decoded;
        check(encodeSave(game,bytes,failure)&&decodeSave(bytes,decoded,failure),"bounce disk round trip");
        check(bounce.restoreSnapshot(decoded.levels.at(game.currentLevel).world,failure),"bounce disk restore");
        check(bounce.actors()[visual].projectileAltitude==12,"disk restore has same body altitude before next tick");
        bounce.tick(0,{});check(bounce.projectiles()[0].ageMs==300,"paused grenade phase unchanged");
        bounce.tick(.02f,{});
        if(mask==0x100) {
            check(bounce.projectiles().empty()&&!bounce.actors()[visual].alive,"negative water halfwave removes grenade");
            check(bounce.actors().size()==saved.actors.size()+1&&fixture.objects.back().classId.value==6,
                "water removal spawns authored splash");
        } else {
            check(bounce.projectiles().size()==1&&bounce.actors()[visual].projectileAltitude==5,
                "dry and mixed masks reflect integer altitude");
        }
        // Native truncation happens before the sign test: fractional below-ground
        // results at a low speed remain zero, including water-only cells.
        p.ageMs=300;p.motion={.01f,0,.01f,0};
        check(bounce.restoreSnapshot(saved,failure),"low amplitude restore");bounce.tick(.02f,{});
        check(bounce.projectiles().size()==1&&bounce.actors()[visual].projectileAltitude==0,
            "negative fraction truncating to zero does not splash");
    }
    // A fast shot crossing both a wall and an actor in one tick must stop at
    // the earlier contact. Reverse their order to exercise the actor branch.
    for(bool wallFirst:{true,false}) {
        auto fixture=scene;fixture.objects.resize(2);
        fixture.objects[1].position={wallFirst?300.f:230.f,200};
        fixture.classes[2].properties.push_back(assignment("Speed-max","3000"));
        // Replace the original assignment rather than relying on duplicate keys.
        fixture.classes[2].properties.erase(fixture.classes[2].properties.begin());
        Level barrier=level;barrier.collisionEdges.push_back({250,0,250,400});
        GameWorld combat;std::string failure;
        check(loadFixture(combat,fixture,barrier,failure),"projectile ordering fixture");
        combat.setPlayerDirection(9);WorldInput shot;shot.fire=true;
        combat.tick(1.f/60,shot);
        check(combat.shotsFired==1,"ordering fixture fires one fast shot");
        check(combat.actors()[1].vitals.shield==(wallFirst?10.f:0.f),
              "projectile resolves earliest wall or actor along swept path");
        check(combat.projectiles().empty(),"impact removes projectile");
    }
    {
        auto fixture=scene;GameWorld debug;std::string failure;
        check(loadFixture(debug,fixture,level,failure),"debug fixture");
        check(debug.debugEnemyAI()&&!debug.debugGodMode(),"debug defaults");
        debug.setDebugGodMode(true);debug.damage(0,100000);
        check(debug.player().alive&&debug.player().vitals.shield==20,"god mode prevents shield/energy damage");
        auto emptyAmmo=debug.carry();emptyAmmo.ammo.clear();debug.restore(emptyAmmo);
        debug.debugFillAmmo();check(debug.player().ammo.at("bullet").count==200,"missing weapon ammo seeded from authored class maximum");
        debug.debugFillAmmo();check(debug.player().ammo.at("bullet").count==200,"repeat ammo does not overflow");
        auto limited=debug.carry();limited.ammo.at("bullet")={1,13};debug.restore(limited);debug.debugFillAmmo();
        check(debug.player().ammo.at("bullet").count==13&&debug.player().ammo.at("bullet").maximum==13&&debug.player().weapons[0].ammo=="bullet","existing instance maximum and matching variant preserved");
        auto disabled=debug.carry();disabled.ammo.at("bullet")={0,0};debug.restore(disabled);
        debug.debugFillAmmo();check(debug.player().ammo.at("bullet").count==200 && debug.player().ammo.at("bullet").maximum==200,"debug refill restores disabled stock from class capacity");
        const auto money=debug.player().vitals.money;
        check(debug.debugAddMoney()&&debug.debugAddMoney()&&debug.player().vitals.money==money+2000000,"exact repeat money increment");
        auto carry=debug.carry();carry.vitals.money=std::numeric_limits<int64_t>::max()-999999;debug.restore(carry);
        check(!debug.debugAddMoney()&&debug.player().vitals.money==carry.vitals.money,"money overflow rejects entire action");
        debug.setDebugEnemyAI(false);auto saved=debug.snapshot();
        check(debug.restoreSnapshot(saved,failure)&&debug.debugEnemyAI()&&!debug.debugGodMode(),"snapshot load resets transient toggles");
        debug.damage(0,100000);check(!debug.player().alive,"god mode off restores damage/death");
        debug.setDebugGodMode(true);debug.setDebugEnemyAI(false);
        check(loadFixture(debug,fixture,level,failure)&&debug.debugEnemyAI()&&!debug.debugGodMode(),"new level resets toggles");
    }
    {
        auto fixture=scene;GameWorld debug;std::string failure;
        fixture.classes[0].properties.back()=block("ADD",{assignment("Name","Bullet-II"),assignment("Count","0"),assignment("Max","17")},"Ammo");
        fixture.objects[0].properties=fixture.classes[0].properties;
        fixture.classes[1].properties.push_back(block("TYPE",{assignment("Ammo","Bullet-II"),assignment("Delay","77"),assignment("Fire-Range","123"),assignment("Weapons-gap","9"),assignment("Left-side","1")},"Weapon"));
        check(loadFixture(debug,fixture,level,failure),"multi-variant debug fixture");
        check(debug.player().weapons[0].ammo=="bullet-ii","normal initialization matches declared ammo variant");
        debug.debugFillAmmo();const auto& selected=debug.player().weapons[0];
        check(selected.ammo=="bullet-ii"&&selected.config.delayMs==77&&selected.range==123&&selected.config.weaponsGap==9&&selected.config.leftSide,"debug selects explicit authored variant with available player stock");
        check(debug.player().ammo.at("bullet-ii").count==17,"variant uses authored stock maximum");
        fixture.objects[0].properties.push_back(block("ADD",{assignment("Name","Bullet"),assignment("Count","2"),assignment("Max","200")},"Ammo"));
        check(loadFixture(debug,fixture,level,failure),"nonempty alternate stock fixture");
        check(debug.player().weapons[0].ammo=="bullet"&&debug.player().weapons[0].range==123,
            "native stock selection is independent of last authored firing metadata");
        auto stocked=debug.carry();stocked.ammo["bullet-ii"].count=1;debug.restore(stocked);
        check(debug.player().weapons[0].ammo=="bullet-ii","compatible stock selection follows actor ADD order, not map key order");
    }
    {
        auto fixture=scene;fixture.objects.resize(1);
        fixture.objects[0].properties.back()=block("ADD",{assignment("Name","Bullet"),assignment("Count","1"),assignment("Max","2")},"Ammo");
        fixture.objects[0].properties.push_back(block("ADD",{assignment("Name","Bullet"),assignment("Count","3"),assignment("Max","7")},"Ammo"));
        GameWorld duplicate;std::string failure;check(loadFixture(duplicate,fixture,level,failure),"duplicate ammo fixture");
        check(duplicate.player().ammo.size()==2 && duplicate.player().weapons[0].ammoSlot==0,"native duplicate records remain ordered");
        WorldInput fire;fire.fire=true;duplicate.tick(1.f/60,fire);
        check(duplicate.player().ammo.slots[0].second.count==0 && duplicate.player().ammo.slots[1].second.count==3,"first stock fires without consuming duplicate");
        for(int i=0;i<6;++i)duplicate.tick(1.f/60,{});
        duplicate.tick(1.f/60,fire);
        check(duplicate.player().weapons[0].ammoSlot==1 && duplicate.player().ammo.slots[1].second.count==2,"empty selected stock switches to next nonzero record");
        auto saved=duplicate.snapshot();saved.actors[0].ammo.slots[0].second.count=1;
        check(duplicate.restoreSnapshot(saved,failure),"selected duplicate snapshot restore");
        for(int i=0;i<6;++i)duplicate.tick(1.f/60,{});duplicate.tick(1.f/60,fire);
        check(duplicate.player().ammo.slots[0].second.count==1 && duplicate.player().ammo.slots[1].second.count==1,"valid selected slot persists when earlier stock refilled");
        ObjectClass pickup;pickup.id={uint32_t(fixture.classes.size())};pickup.name="AmmoPickup";
        pickup.properties={assignment("Bounding-box-size","12"),block("TYPE",{assignment("Only-for-player","1"),assignment("Ammo-Type","Bullet"),assignment("Add-Ammo","5")},"Item")};
        fixture.classes.push_back(pickup);
        duplicate.spawn(pickup.id,duplicate.playerPosition());duplicate.movePlayer(0,0);
        check(duplicate.player().ammo.slots[0].second.count==2 && duplicate.player().ammo.slots[1].second.count==1,"pickup fills first nonfull duplicate and discards excess");
        duplicate.spawn(pickup.id,duplicate.playerPosition());duplicate.movePlayer(0,0);
        check(duplicate.player().ammo.slots[0].second.count==2 && duplicate.player().ammo.slots[1].second.count==6,"pickup skips full first duplicate");
        // Native444540 requires a compatible carried weapon, not just stock.
        auto noWeapon=duplicate.snapshot();noWeapon.actors[0].weapons.clear();noWeapon.actors[0].selectedWeapon=0;
        noWeapon.actors[0].ammo.slots[0].second.count=0;
        check(duplicate.restoreSnapshot(noWeapon,failure),"orphan ammunition snapshot");
        const auto orphanPickup=duplicate.spawn(pickup.id,duplicate.playerPosition());duplicate.movePlayer(0,0);
        check(duplicate.actors()[orphanPickup].alive&&duplicate.player().ammo.slots[0].second.count==0,"ammo stock without compatible carried weapon refuses pickup");
        auto compatibleAgain=duplicate.snapshot();compatibleAgain.actors[0].weapons=saved.actors[0].weapons;
        check(duplicate.restoreSnapshot(compatibleAgain,failure),"compatible weapon restored");duplicate.movePlayer(0,0);
        check(!duplicate.actors()[orphanPickup].alive&&duplicate.player().ammo.slots[0].second.count==2,"refused ammo remains available after compatible weapon restored");
        auto carried=duplicate.carry();duplicate.restore(carried);
        check(duplicate.player().ammo.size()==2 && duplicate.player().ammo.slots[1].second.maximum==7,"level carry preserves duplicate stocks and caps");
        duplicate.debugFillAmmo();check(duplicate.player().ammo.slots[0].second.count==2 && duplicate.player().ammo.slots[1].second.count==7,"debug refill honors each duplicate cap");
    }
    {
        auto fixture=scene;fixture.objects.resize(1);const auto originalFixture=fixture;GameWorld pickupWorld;std::string failure;
        check(loadFixture(pickupWorld,fixture,level,failure),"pickup admission fixture");
        auto hidden=pickupWorld.spawn({4},pickupWorld.playerPosition());fixture.objects[hidden].flags.invisible=true;
        pickupWorld.movePlayer(0,0);check(pickupWorld.actors()[hidden].alive,"hidden pickup without bounds refused");
        fixture.objects[hidden].properties.push_back(assignment("Keep-bb-when-invisible","1"));
        pickupWorld.movePlayer(0,0);check(!pickupWorld.actors()[hidden].alive,"hidden pickup with retained bounds admitted");
        auto source=pickupWorld.spawn({3},pickupWorld.playerPosition());pickupWorld.damage(source,1000);
        auto drop=fixture.objects.size()-1;check(pickupWorld.actors()[drop].pickupDelayMs==0,"HAVE drop has no invented collection delay");
        pickupWorld.movePlayer(0,0);check(!pickupWorld.actors()[drop].alive,"fresh death drop can collect before500ms");
        auto state=pickupWorld.snapshot();state.actors[0].vitals.energy=1;check(pickupWorld.restoreSnapshot(state,failure),"deplete pickup fixture");
        auto plain=pickupWorld.spawn({4},pickupWorld.playerPosition());
        for(auto& n:fixture.objects[plain].properties)if(n.key=="TYPE")n.children.push_back(assignment("Reactivate-after","1000"));
        pickupWorld.movePlayer(0,0);check(!pickupWorld.actors()[plain].alive&&pickupWorld.actors()[plain].reactivateMs<0,"non-Carry pickup cannot reactivate");
        auto held=pickupWorld.spawn({4},pickupWorld.playerPosition());fixture.objects[held].properties.push_back(assignment("Carry","1"));
        for(auto& n:fixture.objects[held].properties)if(n.key=="TYPE")n.children.push_back(assignment("Reactivate-after","1000"));
        fixture.objects[held].properties.push_back(assignment("Bounding-box-size","77"));
        const auto replacement=fixture.objects.size();
        pickupWorld.movePlayer(0,0);
        check(!pickupWorld.actors()[held].alive&&pickupWorld.actors()[held].reactivateMs<0,"held original is not scheduled for revival");
        check(fixture.objects.size()==replacement+1 && !pickupWorld.actors()[replacement].alive && pickupWorld.actors()[replacement].reactivateMs==1000,"Carry schedules fresh replacement with instance delay");
        check(fixture.objects[replacement].classId==fixture.objects[held].classId &&
              fixture.objects[replacement].position.x==fixture.objects[held].position.x &&
              fixture.objects[replacement].position.y==fixture.objects[held].position.y,"replacement retains class and pickup position");
        check(!contentProperty(fixture.objects[replacement].properties,"Carry") &&
              pickupWorld.actors()[replacement].radius!=77,"replacement discards placement overrides");
        pickupWorld.setPlayerPosition(2000,2000);
        auto waiting=pickupWorld.snapshot();SaveGame game;game.currentLevel="TEST/CARRY.3DG";game.levels[game.currentLevel].world=waiting;
        std::vector<uint8_t> bytes;SaveGame disk;
        check(encodeSave(game,bytes,failure)&&decodeSave(bytes,disk,failure),"replacement serialized by existing v11 layout");
        auto copy=originalFixture;GameWorld resumed;check(loadFixture(resumed,copy,level,failure),"replacement fixture loads");
        if(!resumed.restoreSnapshot(disk.levels.at(game.currentLevel).world,failure)){std::cerr<<failure<<std::endl;check(false,"replacement checkpoint restores");}
        for(int tick=0;tick<9;++tick){pickupWorld.tick(.1f,{});resumed.tick(.1f,{});}
        check(!pickupWorld.actors()[replacement].alive&&!resumed.actors()[replacement].alive,"fresh replacement waits full delay");
        pickupWorld.tick(.1f,{});resumed.tick(.1f,{});
        check(pickupWorld.actors()[replacement].alive&&resumed.actors()[replacement].alive&&!pickupWorld.actors()[held].alive,"only replacement reactivates, including after save/load");
        check(std::abs(resumed.actors()[replacement].ageMs-1000)<.01f,"replacement age includes disabled interval");
        waiting.actors[replacement].lifeMs=500;
        check(pickupWorld.restoreSnapshot(waiting,failure),"finite-lifetime replacement fixture");
        for(int tick=0;tick<10;++tick)pickupWorld.tick(.1f,{});
        check(!pickupWorld.actors()[replacement].alive && pickupWorld.actors()[replacement].reactivateMs<0,
              "class lifetime can expire while replacement is disabled");
    }
    GameWorld world;std::string error;check(loadFixture(world,scene,level,error),"world load");world.setPlayerDirection(9);
    world.movePlayer(100,0);check(world.playerPosition().x<270,"intact box blocks player");
    world.setPlayerPosition(200,200);
    WorldInput input;input.fire=true;
    for (int i=0;i<60;++i)world.tick(1.f/60,input);
    const auto shotAudio=world.takeAudioEvents();
    check(std::any_of(shotAudio.begin(),shotAudio.end(),[](const auto& e){return e.kind==AudioEventKind::WeaponFire&&e.actor==0&&e.name=="Gun";}),"successful shots emit authored weapon events");
    check(world.takeAudioEvents().empty(),"audio events drain exactly once");
    check(!world.actors()[1].alive && std::count_if(scene.objects.begin(),scene.objects.end(),[](const auto& object){return object.classId.value==4;})==1,"shoot box once and spawn authored loot");
    check(world.player().fireRevision>1,"successful visible fire signals authored channel");
    const auto revision=world.player().fireRevision;world.tick(1.f/60,{});
    check(world.player().fireRevision==revision,"release must retain pending fire without new flash");
    auto dry=world.carry();dry.ammo["bullet"].count=0;world.restore(dry);world.tick(1.f/60,input);
    check(world.player().fireRevision==revision,"dry fire cannot produce muzzle streaks");
    check(world.shotsFired>1 && world.player().ammo.at("bullet").count<100,"firing consumes ammo");
    world.setPlayerPosition(300,200);world.tick(1.f/60,{});
    check(world.pickupsTaken==1 && world.player().vitals.energy==75,"spawned repair collects");
    check(std::any_of(world.notifications().entries().begin(),world.notifications().entries().end(),
        [](const auto& n){return n.kind==NoticeKind::Pickup && n.text=="Energy +25" && n.color==0x80ff20;}),"pickup uses original font message and tint");
    world.tick(1.f/60,{});check(world.pickupsTaken==1,"pickup cannot repeat");
    world.damage(0,25);check(world.player().vitals.shield==0 && world.player().vitals.energy==70,"shield overflow damage");
    world.finishGateAnimation(2,1);world.setPlayerPosition(500,500);world.tick(.1f,{});
    check(world.transition() && world.transition()->path=="cave/t1c1.3DG","authored lift destination");
    auto carry=world.carry();world.rejectTransition("missing destination");world.tick(1.f/60,{});
    check(!world.transition() && world.player().vitals.energy==carry.vitals.energy,"failed transition retains state");
    world.damage(0,100);check(!world.player().alive,"death disables player");
    const auto deathAudio=world.takeAudioEvents();
    check(std::any_of(deathAudio.begin(),deathAudio.end(),[](const auto& e){return e.kind==AudioEventKind::Death&&e.actor==0;}),"death emits one presentation event");
    auto shots=world.shotsFired;world.tick(1.f/60,input);check(shots==world.shotsFired,"dead player cannot fire");
    SceneContent missing=scene;missing.player.reset();GameWorld invalid;
    check(!loadFixture(invalid,missing,level,error),"missing player must fail");
    missing=scene;missing.objects[0].classId={99999};
    check(!loadFixture(invalid,missing,level,error),"invalid class must fail before indexing");
    scene.objects.resize(3);
    scene.objects[1].properties.push_back(assignment("Shield","3"));
    // Replacement scalar as the content loader produces it.
    auto& props=scene.objects[1].properties;
    props.erase(props.begin()+2); // inherited Shield block
    scene.objects[1].authored.children={assignment("Life-time","14")};
    scene.classes[3].properties.push_back(assignment("Life-time","0"));
    check(loadFixture(world,scene,level,error),"reload fixture");
    world.setPlayerPosition(200,200);
    check(world.actors()[1].vitals.shield==3 && world.actors()[1].vitals.shieldMax==10,
          "placement current shield must preserve class limits");
    for(int i=0;i<10;++i)world.tick(.02f,{});
    check(world.actors()[1].alive && world.actors()[1].ageMs>=214,
          "placement elapsed age and unlimited class lifespan must remain distinct");
    world.setPlayerPosition(500,500);
    scene.objects[2].properties.back().children.push_back(assignment("Id","7"));
    check(loadFixture(world,scene,level,error),"reload keyed Lift fixture");
    world.finishGateAnimation(2,1);world.setPlayerPosition(500,500);
    world.tick(.1f,{});check(!world.transition(),"missing key rejects exit");
    auto withKey=world.carry();withKey.keys.insert(7);world.restore(withKey);
    world.tick(.1f,{});check(world.transition().has_value(),"held key permits eligible exit");

    // Lift authorization runs while nearby, before the countdown or containment.
    {
        auto fixture=scene;fixture.objects.resize(1);
        ObjectPlacement platform;platform.id={1};platform.classId={5};platform.position={300,200};platform.sprite.sequence={1};
        platform.properties={assignment("Bounding-box-size","64"),block("TYPE",{
            assignment("Active","1"),assignment("Present","1"),assignment("Stay-Up","1"),
            assignment("Delay","1000"),assignment("Id","9"),assignment("Needs-code","1"),
            assignment("Destroy-key","1"),assignment("Usage-Count","1"),assignment("Scenery","cave"),assignment("3do-file","t1c1")},"Lift")};
        fixture.objects.push_back(platform);GameWorld access;
        check(loadFixture(access,fixture,level,error),"nearby Lift code fixture");access.setPlayerPosition(210,200);
        auto parts=access.carry();parts.heldKeys={{9,true,1,2}};access.restore(parts);
        access.finishGateAnimation(1,1);access.tick(.1f,{});
        check(access.actors()[1].liftState.keyId==9&&access.player().heldKeys.size()==1&&!access.transition(),"partial nearby code cannot authorize Lift");
        parts.heldKeys.push_back({9,true,2,2});access.restore(parts);access.tick(.1f,{});
        check(std::any_of(access.notifications().entries().begin(),access.notifications().entries().end(),[](const auto& n){return n.kind==NoticeKind::KeyUsed&&n.text=="Using Code #9 !";}),"Lift code consumption emits native centered notice");
        check(access.player().heldKeys.empty()&&access.actors()[1].liftState.keyId==0&&access.actors()[1].liftState.remainingMs==800&&!access.transition(),"complete nearby code consumed before containment and countdown expiry");
        auto continuation=access.snapshot();access.tick(.1f,{});
        check(access.restoreSnapshot(continuation,error)&&access.actors()[1].liftState.remainingMs==800,"Lift snapshot resumes remaining delay");
        auto corrupt=continuation;corrupt.actors[1].liftState.remainingMs=-1;
        check(!access.restoreSnapshot(corrupt,error)&&access.actors()[1].liftState.remainingMs==800,"invalid Lift snapshot rejected atomically");
        auto legacy=continuation;legacy.actors[1].lift.reset();legacy.actors[1].gateRevision=0;legacy.actors[1].gateAnimationFinished=false;
        check(access.restoreSnapshot(legacy,error)&&access.actors()[1].lift&&access.actors()[1].liftState.remainingMs==1000,"legacy Lift resumes authored startup without invented cycle history");
        auto lowered=access.snapshot();lowered.actors[1].liftState.flags&=~(LiftPresent|LiftGoingUp);lowered.actors[1].liftState.passable=false;
        check(access.restoreSnapshot(lowered,error),"lowered keyed Lift");access.setPlayerPosition(210,200);access.movePlayer(100,0);
        check(access.notifications().entries().size()==1&&access.notifications().entries()[0].text=="Lift needs full code !"&&access.notifications().entries()[0].x==100&&access.notifications().entries()[0].y==168,"lowered Lift missing code notice uses original object anchor");
        lowered.actors[1].liftState.keyId=0;check(access.restoreSnapshot(lowered,error),"lowered authorized Lift");access.setPlayerPosition(210,200);access.movePlayer(100,0);
        check(access.notifications().entries().size()==1&&access.notifications().entries()[0].text=="Wait, lift will come."&&access.notifications().entries()[0].x==150,"lowered authorized Lift displays original waiting text");
        lowered.objects[lowered.player].flags.invisible=true;check(access.restoreSnapshot(lowered,error),"invisible Lift contact");access.setPlayerPosition(210,200);access.movePlayer(100,0);
        check(access.notifications().entries().empty(),"native invisible contact bypass suppresses unreachable warning");
    }

    // Gates consume complete codes, remain solid through opening, and ignore
    // completion callbacks from an earlier animation revision.
    ObjectClass gate;gate.id={6};gate.name="Gate";gate.sprite.sequence={1};
    gate.properties={assignment("Bounding-box-size","32"),block("TYPE",{
        assignment("Active","1"),assignment("Sensor","1"),assignment("Id","9"),
        assignment("Destroy-key","1"),assignment("Delay","100")},"Gate")};
    scene.classes.push_back(gate);scene.objects.resize(1);place(6,300,200);
    scene.objects[1].sprite.sequence={1};
    check(loadFixture(world,scene,level,error),"gate fixture load");world.setPlayerPosition(200,200);
    world.finishGateAnimation(1,1);world.updateGates(20);
    check(world.actors()[1].solid && world.actors()[1].gateRevision==1,"unauthorized gate stays closed");
    world.movePlayer(100,0);
    check(world.notifications().entries().size()==1&&world.notifications().entries()[0].text=="Gate needs key #9 !"&&world.notifications().entries()[0].worldSpace,"blocked Gate emits native world-space key notice");
    world.movePlayer(100,0);check(world.notifications().entries().size()==1,"continued gate contact replaces existing notice");
    const auto keyGate=world.snapshot();auto codeGate=keyGate;codeGate.actors[1].gateState.flags|=GateNeedsCode;
    check(world.restoreSnapshot(codeGate,error),"code-labelled gate fixture");world.setPlayerPosition(200,200);world.movePlayer(100,0);
    check(world.notifications().entries().size()==1&&world.notifications().entries()[0].text=="Gate needs full code #9 !","blocked Gate code notice includes original ID");
    check(world.restoreSnapshot(keyGate,error),"restore key-labelled gate");
    world.setPlayerPosition(200,200);
    auto code=world.carry();code.heldKeys={{9,true,1,2}};world.restore(code);world.updateGates(20);
    check(world.actors()[1].gateRevision==1,"partial code cannot open gate");
    code.heldKeys.push_back({9,true,2,2});world.restore(code);world.updateGates(20);
    check(world.actors()[1].gateRevision==2 && world.actors()[1].solid && world.player().heldKeys.empty(),"complete code consumed at opening");
    check(std::any_of(world.notifications().entries().begin(),world.notifications().entries().end(),[](const auto& n){return n.kind==NoticeKind::KeyUsed&&n.text=="Using Key #9 !";}),"Gate consumption label follows Needs-code flag, not held-piece type");
    world.finishGateAnimation(1,1);world.updateGates(20);
    check(world.actors()[1].solid,"stale gate completion rejected");
    world.finishGateAnimation(1,2);world.updateGates(20);
    check(!world.actors()[1].solid,"opening completion enables passage");
    world.setPlayerPosition(600,500);world.updateGates(100);
    check(world.actors()[1].solid && world.actors()[1].gateRevision==3,"departing sensor closes after delay");
    // Paid map is world state: no opening debit, ticks charge, reload resets.
    auto rich=world.carry();rich.vitals.money=21000;world.restore(rich);
    world.toggleMap();check(world.mapOpen()&&world.player().vitals.money==21000,"map admission does not charge");
    world.tick(1.f/60,{});check(world.player().vitals.money==20970,"world tick bills map once");
    world.toggleMap();world.tick(1.f/60,{});
    check(!world.mapOpen()&&world.player().vitals.money==20970,"closing stops map billing");
    world.toggleMap();check(!world.mapOpen(),"reopening requires admission balance");
    world.restore(rich);world.toggleMap();
    world.damage(world.playerIndex(),1000000);
    check(!world.mapOpen(),"death closes map");
    check(loadFixture(world,scene,level,error)&&!world.mapOpen(),"level replacement resets map");

    ObjectClass hostile=box;hostile.id={static_cast<uint32_t>(scene.classes.size())};
    hostile.name="Guard";hostile.behaviours={"Seeker"};
    hostile.properties={assignment("Bounding-box-size","10"),block("Energy",{assignment("Initial","10")}),
        block("TYPE",{assignment("Target","Player")},"Seeker")};
    scene.classes.push_back(hostile);scene.objects.resize(1);
    for(int i=0;i<3;++i){place(hostile.id.value,1000+i*100,1000);scene.objects.back().sprite.path="OBJECT/COP/COP.GAF";}
    place(3,2000,2000);scene.objects.back().sprite.path="OBJECT/THING/BOX.GAF";
    check(loadFixture(world,scene,level,error),"HUD counter fixture");
    check(world.enemiesRemaining()==3&&world.playerKills()==0,"hostile target eligibility");
    world.damage(1,10000,0);world.damage(1,10000,0);
    check(world.playerKills()==1&&world.enemiesRemaining()==2,"player kill credited once");
    world.damage(2,10000,3);world.damage(4,10000,0);
    check(world.playerKills()==1&&world.enemiesRemaining()==1,"enemy-owned kill and scenery excluded from player credit");
    world.tick(.05f,{});const auto stats=world.carry();
    check(stats.kills==1&&stats.elapsedMs>49,"carry contains playtime and kills");
    check(loadFixture(world,scene,level,error)&&world.playerKills()==0&&world.elapsedMilliseconds()==0,"fresh game clears counters");
    world.restore(stats);check(world.playerKills()==1&&world.elapsedMilliseconds()==stats.elapsedMs,"transition restores cumulative counters");

    // Snapshots preserve active gameplay and reject corruption without altering it.
    const SceneContent fresh=scene;
    const auto saved=world.snapshot();
    world.damage(0,1000000);
    check(world.restoreSnapshot(saved,error)&&world.player().alive,"snapshot restores live player");
    auto broken=saved;broken.actors[0].selectedWeapon=9999;
    check(!world.restoreSnapshot(broken,error)&&world.player().alive,"bad weapon selection restore is atomic");
    broken=saved;broken.randomState="not a random state";
    check(!world.restoreSnapshot(broken,error),"bad RNG state rejected");
    SceneContent secondScene=fresh;GameWorld second;
    for(auto& object:secondScene.objects)for(auto* path:{&object.sprite.path,&object.shadow.path})
        for(auto& c:*path)if(c>='A'&&c<='Z')c+=32;else if(c=='/')c='\\';
    for(auto& cls:secondScene.classes)for(auto* path:{&cls.sprite.path,&cls.shadow.path})
        for(auto& c:*path)if(c>='A'&&c<='Z')c+=32;else if(c=='/')c='\\';
    check(loadFixture(second,secondScene,level,error)&&second.restoreSnapshot(saved,error),"snapshot restores into fresh content");
    world.tick(.05f,{});second.tick(.05f,{});
    check(world.elapsedMilliseconds()==second.elapsedMilliseconds() && world.playerPosition().x==second.playerPosition().x,
          "restored world continues simulation clock and motion");

    ObjectClass terminal;terminal.id={static_cast<uint32_t>(scene.classes.size())};terminal.name="Saving-Plot";
    terminal.properties={assignment("Friction-factor","0"),assignment("Indestructible","1"),assignment("Saving-plot","1"),assignment("Bounding-box-size","30"),assignment("Reward","-100000"),block("TYPE",{assignment("Only-for-player","0")},"Item")};
    scene.classes.push_back(terminal);
    ObjectClass area;area.id={static_cast<uint32_t>(scene.classes.size())};area.name="Saving-Plot-Area";
    area.properties={assignment("Saving-zone","1"),assignment("Bounding-box-size","64"),block("TYPE",{assignment("Only-for-player","1")},"Item")};
    scene.classes.push_back(area);scene.objects.resize(1);place(terminal.id.value,1000,1000);place(area.id.value,1000,1000);
    check(loadFixture(world,scene,level,error),"saving terminal fixture");
    check(world.notifications().savingPrices().size()==1 && world.notifications().savingPrices().front().notice.text=="Save your game for 100000 $",
          "terminal registers authored fee label");
    for(const auto cell: {std::pair{13,31},std::pair{17,31},std::pair{15,28},std::pair{15,34}}) {
        world.setPlayerPosition(cell.first*64+1,cell.second*32+1);world.tick(.01f,{});
        check(world.notifications().savingPrices().front().active,"price includes original two-column/three-row boundary");
        check(!world.saveRequest(),"price alone does not save or charge");
    }
    for(const auto cell: {std::pair{12,31},std::pair{18,31},std::pair{15,27},std::pair{15,35}}) {
        world.setPlayerPosition(cell.first*64+1,cell.second*32+1);world.tick(.01f,{});
        check(!world.notifications().savingPrices().front().active,"price excludes next cell beyond native range");
    }
    check(world.actors()[1].solid&&!world.actors()[1].pickup&&!world.actors()[2].solid&&!world.actors()[2].pickup,"visible saving tower is solid; only area is a nonpickup sensor");
    world.setPlayerPosition(900,1000);world.movePlayer(200,0);
    check(world.playerPosition().x>933 && world.playerPosition().x<935,"swept movement stops at tower plus player radii, not outer save area");
    // Passing beside the physical tower while inside its larger sensor is free.
    world.setPlayerPosition(900,1040);world.movePlayer(200,0);
    check(world.playerPosition().x==1100,"save trigger area does not block an offset pass");
    // Controller ticks exercise the normal indexed movement path as well.
    world.setPlayerPosition(900,1000);world.setPlayerDirection(9);
    WorldInput drive;drive.movement.forward=1;
    for(int i=0;i<120;++i){world.tick(1.f/60,drive);check(world.playerPosition().x<935,"normal driving cannot cross the tower");}
    check(scene.objects[1].position.x==1000 && scene.objects[1].position.y==1000,"authored zero-friction saving tower stays anchored");
    check(loadFixture(world,scene,level,error),"reset terminal dwell after driving probe");
    auto legacyTower=world.snapshot();legacyTower.actors[1].solid=false;
    legacyTower.actors[2].solid=true;legacyTower.actors[2].pickup=true;
    check(world.restoreSnapshot(legacyTower,error)&&world.actors()[1].solid&&!world.actors()[2].solid&&!world.actors()[2].pickup,
          "legacy checkpoint repairs tower solidity and sensor flags");
    auto deadTower=legacyTower;deadTower.actors[1].alive=false;vyruz_test::synchronizeActor(deadTower,1);
    check(world.restoreSnapshot(deadTower,error)&&!world.actors()[1].solid,"dead saved tower is not made solid");
    check(world.restoreSnapshot(legacyTower,error),"restore live terminal after migration probes");
    auto funds=world.carry();funds.vitals.money=200000;world.restore(funds);
    // 100 screen pixels horizontally is native distance 79, beyond area64
    // but within combined area64 + player22 radius.
    world.setPlayerPosition(1100,1000);world.tick(.1f,{});
    for(int i=0;i<54;++i)world.tick(.1f,{});
    check(!world.saveRequest(),"terminal must wait full 5500ms after entry");
    const auto& notices=world.notifications().entries();
    check(notices.size()==1&&notices.front().kind==NoticeKind::Saving&&notices.front().text=="Saving in 0 seconds",
          "countdown refreshes one visible notification at native integer boundary");
    check(notices.front().worldSpace&&!notices.front().reveal&&notices.front().font==6&&notices.front().mode==7&&
          notices.front().x==850&&notices.front().y==950&&notices.front().lifetimeMs==500&&world.message().empty(),
          "countdown uses native world-space tower anchor and font, not window status");
    world.tick(.1f,{});
    check(world.saveRequest()&&world.saveRequest()->cost==100000&&world.player().vitals.money==200000,"eligible terminal requests write before debit");
    auto paid=world.snapshot();paid.actors[0].vitals.money-=world.saveRequest()->cost;
    world.finishSave(true);
    check(world.player().vitals.money==100000&&!world.saveRequest(),"successful write debits once");
    check(world.notifications().entries().size()==1&&world.notifications().entries().front().text=="Saving Completed."&&
          world.notifications().entries().front().lifetimeMs==3000&&world.message().empty(),"successful save replaces countdown with visible completion");
    check(world.restoreSnapshot(paid,error),"paid checkpoint restores");
    check(world.takeAudioEvents().empty(),"checkpoint restore never replays transient audio");
    for(int i=0;i<60;++i)world.tick(.1f,{});
    check(!world.saveRequest()&&world.player().vitals.money==100000,"loaded checkpoint cannot recharge during same stay");
    world.setPlayerPosition(1300,1000);world.tick(.1f,{});world.setPlayerPosition(1100,1000);world.tick(.1f,{});
    for(int i=0;i<55;++i)world.tick(.1f,{});
    check(world.saveRequest().has_value(),"leave and reenter rearms terminal");
    world.finishSave(false,"disk failure");
    check(world.player().vitals.money==100000,"failed write preserves credits");
    check(world.notifications().entries().back().text=="Cannot save: disk failure"&&world.message().empty(),
          "disk failure stays visible in game without charging");
    for(int i=0;i<60;++i)world.tick(.1f,{});
    check(!world.saveRequest(),"failed attempt remains consumed until departure");
    world.setPlayerPosition(1300,1000);world.tick(.1f,{});world.setPlayerPosition(1100,1000);world.tick(.1f,{});
    funds=world.carry();funds.vitals.money=99999;world.restore(funds);
    for(int i=0;i<55;++i)world.tick(.1f,{});
    check(!world.saveRequest()&&world.snapshot().saveAttempted,"unaffordable stay consumes its attempt");
    check(world.notifications().entries().back().text.find("Not enough Cash")!=std::string::npos&&
          world.notifications().entries().back().lifetimeMs==5000,"unaffordable save displays original-style in-game refusal");
    funds.vitals.money=100000;world.restore(funds);world.tick(.1f,{});
    check(!world.saveRequest(),"funds arriving during attempted stay cannot retrigger");
    world.setPlayerPosition(1300,1000);world.tick(.1f,{});world.setPlayerPosition(1100,1000);world.tick(.1f,{});
    for(int i=0;i<20;++i)world.tick(.1f,{});
    const auto dwelling=world.snapshot();
    check(world.restoreSnapshot(dwelling,error),"partial terminal dwell restores");
    for(int i=0;i<34;++i)world.tick(.1f,{});
    check(!world.saveRequest(),"partial dwell restore preserves remaining time");
    world.tick(.1f,{});check(world.saveRequest().has_value(),"exactly sufficient funds permit save");

    // Native reward credit is independent of killer ownership, once per death;
    // noneligible scenery, nonpositive Reward and the player do not pay.
    SceneContent rewards;rewards.classes={player,box};rewards.classes[1].id={1};
    rewards.classes[1].properties={block("Energy",{assignment("Initial","10")}),
        assignment("Sub-type","Cop"),assignment("Name","Police"),assignment("Reward","1000"),assignment("Kill-Count","7777")};
    for(size_t i=0;i<2;++i){ObjectPlacement o;o.id={static_cast<uint32_t>(i)};
        o.classId={static_cast<uint32_t>(i)};o.position={float(i*100),0};
        o.properties=rewards.classes[i].properties;rewards.objects.push_back(o);}
    rewards.player=ObjectId{0};GameWorld bountyWorld;
    check(loadFixture(bountyWorld,rewards,level,error),"reward fixture");
    bountyWorld.damage(1,100);check(bountyWorld.player().vitals.money==1000 && bountyWorld.player().vitals.lifetimeMoney==1000,"native unowned death pays wallet and lifetime");
    check(bountyWorld.playerKills()==0 && bountyWorld.notifications().entries().size()==2,"cash/name events independent of kill statistic");
    check(bountyWorld.notifications().entries()[0].text=="1000 $" && bountyWorld.notifications().entries()[1].text=="Police","authored reward and object name");
    bountyWorld.damage(1,100,0);check(bountyWorld.player().vitals.money==1000,"dead actor cannot pay twice");
    const auto credited=bountyWorld.snapshot();check(bountyWorld.restoreSnapshot(credited,error),"reward snapshot restores");
    check(bountyWorld.notifications().entries().empty(),"transient messages do not replay on load");
    bountyWorld.damage(1,100);check(bountyWorld.player().vitals.money==1000,"saved dead actor cannot pay again");
    bountyWorld.toggleMap();check(!bountyWorld.mapOpen() && bountyWorld.message().empty() && bountyWorld.notifications().entries().back().kind==NoticeKind::MapDenied,"denial uses transient presentation");
    check(loadFixture(bountyWorld,rewards,level,error),"owned reward fixture");bountyWorld.damage(1,100,0);
    check(bountyWorld.player().vitals.money==1000 && bountyWorld.playerKills()==1,"owned kill credits Reward, not Kill-Count, and one kill");
    rewards.objects[1].properties[1]=assignment("Sub-type","Thing");
    check(loadFixture(bountyWorld,rewards,level,error),"nonactor reward fixture");bountyWorld.damage(1,100,0);
    check(bountyWorld.player().vitals.money==0 && bountyWorld.notifications().entries().empty(),"scenery cannot pay actor bounty");
    rewards.objects[1].properties[1]=assignment("Sub-type","Cop");rewards.objects[1].properties[3]=assignment("Reward","-1000");
    check(loadFixture(bountyWorld,rewards,level,error),"negative reward fixture");bountyWorld.damage(1,100,0);
    check(bountyWorld.player().vitals.money==0 && bountyWorld.notifications().entries().empty(),"negative reward not paid");
    // Restored gameplay keeps save and launched-ammunition compatibility.
    SceneContent motionScene;motionScene.classes={player,gun,bullet};motionScene.player=ObjectId{0};
    ObjectPlacement pilot;pilot.id={0};pilot.classId={0};pilot.position={200,200};pilot.properties=player.properties;pilot.behaviours={"Player"};
    motionScene.objects={pilot};
    Level motionLevel;motionLevel.mapW=100;motionLevel.mapH=100;motionLevel.hasTerrainCollision=true;
    GameWorld motionWorld;check(loadFixture(motionWorld,motionScene,motionLevel,error),"motion fixture");motionWorld.setPlayerDirection(9);
    WorldInput thrust;thrust.movement.forward=1;
    for(int i=0;i<60;++i)motionWorld.tick(1.f/60,thrust);
    const auto heldMotion=motionWorld.snapshot();const auto speedBefore=motionWorld.playerSurfaceSpeed();
    motionWorld.tick(1.f/60,{});
    check(motionWorld.playerPosition().x>heldMotion.objects[0].position.x&&motionWorld.playerSurfaceSpeed()>0&&motionWorld.playerSurfaceSpeed()<speedBefore,"restored release coasts and brakes");
    auto coast=motionWorld.snapshot();SceneContent resumedScene=motionScene;resumedScene.objects[0].position={200,200};GameWorld resumed;
    check(loadFixture(resumed,resumedScene,motionLevel,error)&&resumed.restoreSnapshot(coast,error),"restore coasting state");
    for(int i=0;i<60;++i){motionWorld.tick(1.f/60,{});resumed.tick(1.f/60,{});}
    check(motionWorld.playerPosition().x==resumed.playerPosition().x&&motionWorld.snapshot().motion.thrust==resumed.snapshot().motion.thrust,"coast restore continues identically");
    // At large map coordinates the terrain solver rounds positions more than
    // .0001. That is not a collision and must never feed back into momentum.
    motionLevel.mapW=motionLevel.mapH=1024;
    check(loadFixture(motionWorld,motionScene,motionLevel,error),"large-coordinate motion fixture");
    motionWorld.setPlayerPosition(10000,10000);motionWorld.setPlayerDirection(9);
    PlayerMotion reference;reference.direction=9;
    for(int i=0;i<600;++i) {
        WorldInput input;if(i<300)input.movement.forward=1;
        advancePlayerMotion(reference,input.movement,1.f/60);
        motionWorld.tick(1.f/60,input);
        const auto actual=motionWorld.snapshot().motion;
        check(actual.velocityX==reference.velocityX&&actual.velocityZ==reference.velocityZ,
              "unobstructed world retains solver momentum despite rounded positions");
    }
    check(motionWorld.playerNativeSpeed()==0,"released momentum eventually stops at large coordinates");
    motionLevel.collisionEdges={{400,0,400,1000}};
    motionScene.objects[0].position={200,200};
    check(loadFixture(motionWorld,motionScene,motionLevel,error),"restored wall fixture");motionWorld.setPlayerPosition(200,200);motionWorld.setPlayerDirection(9);
    bool bounced=false;
    for(int i=0;i<200;++i){
        motionWorld.tick(1.f/60,thrust);check(motionWorld.playerPosition().x<400,"swept movement does not cross wall");
        const auto m=motionWorld.snapshot().motion;
        bounced|=nativeToSurface(m.velocityX,m.velocityZ).x<-.1f;
    }
    check(bounced,"authored elastic wall contact reflects player momentum");
    motionLevel.collisionEdges.clear();
    motionScene.classes[2].properties.push_back(assignment("Use-inertia","1"));
    motionScene.classes[2].properties.push_back(assignment("Launched","1"));
    motionScene.classes[2].properties.push_back(block("Thrust",{assignment("Initial","50")}));
    for(auto& node:motionScene.classes[2].properties)if(node.key=="TYPE")node.children.push_back(assignment("Ignore-owner","1"));
    check(loadFixture(motionWorld,motionScene,motionLevel,error),"launched motion fixture");motionWorld.setPlayerPosition(200,200);motionWorld.setPlayerDirection(9);
    for(int i=0;i<40;++i)motionWorld.tick(1.f/60,thrust);
    WorldInput firing;firing.fire=true;motionWorld.tick(1.f/60,firing);
    check(motionWorld.projectiles().size()==1,"launch fixture fires one projectile");
    const auto& launched=motionWorld.projectiles()[0];const auto native=motionWorld.snapshot().motion;
    check(std::abs(launched.motion.velocityX-(launched.directionX*5+native.velocityX)*.97f)<.00001f&&
          std::abs(launched.motion.velocityZ-(launched.directionZ*5+native.velocityZ)*.97f)<.00001f,"launched ammo inherits native velocity without double conversion");

    // Native43ca30 health never sets Hit. Contact/radial producers and actual
    // player-render consumption are exercised by hit_latch_world instead.
    // Presentation-only engine/light state must remain independent of motion.
    motionScene.objects[0].position={200,200};
    check(loadFixture(motionWorld,motionScene,motionLevel,error),"feedback fixture");
    const auto initialPosition=motionWorld.playerPosition();
    const auto initialShield=motionWorld.player().vitals.shield;
    motionWorld.damage(0,1);
    check(!motionWorld.player().nativeHit&&motionWorld.player().hitFlashMs==0&&motionWorld.player().vitals.shield==initialShield-1,"shield-only health does not synthesize Hit or flash");
    motionWorld.tick(.1f,{});check(!motionWorld.player().nativeHit&&motionWorld.player().hitFlashMs==0,"health alone remains unlatched after simulation");
    motionWorld.damage(0,1);check(!motionWorld.player().nativeHit&&motionWorld.player().hitFlashMs==0,"repeated direct health is not a Hit producer");
    motionWorld.damage(0,0);motionWorld.damage(0,-1);
    check(!motionWorld.player().nativeHit&&motionWorld.player().hitFlashMs==0,"invalid damage does not synthesize feedback");
    for(int i=0;i<5;++i)motionWorld.tick(.1f,{});
    check(!motionWorld.player().nativeHit&&motionWorld.player().hitFlashMs==0,"idle health fixture remains unlatched");
    motionWorld.damage(0,0);motionWorld.damage(0,-1);motionWorld.damage(0,std::numeric_limits<float>::quiet_NaN());
    check(motionWorld.player().hitFlashMs==0,"nonpositive and nonfinite attempts do not start feedback");
    check(motionWorld.playerPosition().x==initialPosition.x&&motionWorld.playerPosition().y==initialPosition.y,"feedback alone never moves player");
    motionScene.objects[0].properties.push_back(assignment("Indestructible","1"));
    check(loadFixture(motionWorld,motionScene,motionLevel,error),"indestructible feedback fixture");
    const auto protectedVitals=motionWorld.player().vitals;motionWorld.damage(0,10);
    check(motionWorld.player().vitals.energy==protectedVitals.energy&&motionWorld.player().vitals.shield==protectedVitals.shield&&!motionWorld.player().nativeHit&&motionWorld.player().hitFlashMs==0,"authored indestructible blocks direct health without inventing Hit");
    thrust.movement.forward=1;for(int i=0;i<45;++i)motionWorld.tick(1.f/60,thrust);
    check(motionWorld.player().visualThrust==45&&motionWorld.player().engineGlowMs==1000,"forward builds independent authored engine brightness");
    motionWorld.tick(.1f,{});
    check(motionWorld.player().visualThrust==44.5f&&motionWorld.player().engineGlowMs==900,"release decays visual thrust and engine latch");
    thrust.movement.forward=-1;for(int i=0;i<40;++i)motionWorld.tick(1.f/60,thrust);
    check(motionWorld.player().visualThrust<0,"reverse has no positive engine brightness");
    auto feedbackSave=motionWorld.snapshot();
    check(motionWorld.restoreSnapshot(feedbackSave,error)&&motionWorld.player().visualThrust==0&&motionWorld.player().hitFlashMs==0&&motionWorld.player().engineGlowMs==0,"transient feedback restarts on checkpoint restore");
    // Original T1C1 omits Scenery for its same-scenery exit.
    const auto localLift=block("TYPE",{assignment("3do-file","t1c2")},"Lift");
    check(liftDestination(localLift.children,"CAVE/T1C1.3DG")=="CAVE/t1c2.3DG","blank Lift scenery preserves current scenery");
    check(liftDestination(localLift.children,"CAVE\\T1C1.3DG")=="CAVE/t1c2.3DG","native path separator accepted");
    auto external=localLift;external.children.push_back(assignment("Scenery","CPU"));
    check(liftDestination(external.children,"CAVE/T1C1.3DG")=="CPU/t1c2.3DG","explicit Lift scenery still overrides");
    external.children.push_back(assignment("3do-file","../t1c2"));
    check(!liftDestination(external.children,"CAVE/T1C1.3DG"),"same-scenery fallback does not permit path traversal");
    ObjectPlacement liveLift;liveLift.properties={block("TYPE",{assignment("Usage-Count","1")},"Lift")};
    auto spent=liveLift;check(consumeLiftUse(spent),"staged Lift use decrements");
    check(propertyNumber(attribute(spent.properties,"lift")->children,"Usage-Count")==0&&!consumeLiftUse(spent),"exhausted Lift cannot spend another use");
    check(propertyNumber(attribute(liveLift.properties,"lift")->children,"Usage-Count")==1,"staging does not mutate live source Lift");

} catch(const std::exception& error) {
    std::cerr<<"World test exception: "<<error.what()<<std::endl;
    return 1;
}
