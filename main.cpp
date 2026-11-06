#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <chrono>

// Self-contained, procedural side-scrolling shooter. No external art files.
constexpr int SW = 1280, SH = 720;
constexpr float GROUND = 575.0f;
constexpr float WORLD = 1000000000.0f;
constexpr float PI2 = 6.28318530718f;

float rf(float a, float b) { return a + (b-a)*(GetRandomValue(0,10000)/10000.0f); }
float clampf(float v,float lo,float hi) { return std::max(lo,std::min(v,hi)); }
Color alpha(Color c, unsigned char a) { c.a=a; return c; }
Vector2 vadd(Vector2 a,Vector2 b){return {a.x+b.x,a.y+b.y};}
Vector2 vmul(Vector2 a,float s){return {a.x*s,a.y*s};}
Vector2 dir(float a){return {cosf(a),sinf(a)};}
void line(Vector2 a,Vector2 b,float w,Color c){DrawLineEx(a,b,w,c);}
void rect(float x,float y,float w,float h,Color c){DrawRectangleRec({x,y,w,h},c);}
#include "tunnels.h"
#include "melee.h"

struct Particle { Vector2 p,v; float life,maxLife,size; Color color; int kind; };
struct Tracer { Vector2 a,b; float life; };
struct Soldier { Vector2 p{420,GROUND}; Vector2 vel{0,0}; bool grounded=true; float walk=0, recoil=0, fireCooldown=0; int facing=1; };
struct Zombie { Vector2 p; float health=100, phase=0, attack=0, fall=0; float vy=0, dodge=1, speed=150, bleed=0; unsigned missing=0; int kind=0, deathStyle=0; float vx=0, spin=0, frozen=0, stumble=0; bool ladderUsed=false,grounded=false; int climbing=-1; float drillTime=0,drillY=0; };
struct Quip { Vector2 p; float life; int text; };
constexpr const char* quips[]={"I MEANT TO DO THAT!","MY KNEES!","UNPAID SICK LEAVE","WHEEEEE!","10/10 LANDING","BRB. DEAD.","WHO PUT THAT THERE?"};
constexpr const char* zombieNames[]={"SHAMBLER","TRACK STAR","POGO CLOWN","BIG CHUNGUS","OFFICE INTERN","LADDER CREW","DRILLER"};
constexpr Color zombieColors[]={{73,83,58,255},{139,53,49,255},{135,69,146,255},{78,105,128,255},{89,89,99,255},{161,113,46,255},{214,146,45,255}};
struct Gib { Vector2 p,v; float angle,spin,life; int part; };
struct BloodPool { Vector2 p; float size,life; };
enum WeaponType { RIFLE, PISTOL, UZI, BOW, RPG, GRENADE, MACHINE_GUN, SHOTGUN, SNIPER, CROSSBOW, FREEZE, CONFETTI, WEAPON_COUNT };
struct Weapon { const char* name; int capacity; float interval,damage,length,reload; };
constexpr Weapon weapons[] = {
    {"ASSAULT RIFLE",30,.105f,40,57,2.4f}, {"PISTOL",12,.3f,50,27,1.5f},
    {"UZI",32,.065f,28,36,1.9f}, {"BOW / ARROW",1,.8f,110,42,.8f},
    {"RPG",1,1.1f,190,72,2.8f}, {"GRENADE LAUNCHER",6,.7f,150,45,2.5f},
    {"MACHINE GUN",80,.075f,45,76,3.2f}, {"SHOTGUN",8,.8f,23,60,2.3f},
    {"SNIPER",5,1.05f,150,88,2.6f}, {"CROSSBOW",1,.7f,145,45,1.1f},
    {"FREEZE BLASTER",24,.14f,22,43,1.8f}, {"CONFETTI CANNON",4,.7f,135,52,2.1f}
};
struct Projectile { Vector2 p,v; float life; int type; };
int randomWeapon(int current=-1){
    int choice=GetRandomValue(0,WEAPON_COUNT-(current>=0?2:1));
    return current>=0&&choice>=current?choice+1:choice;
}
std::uint32_t newLayoutSeed(){
    return (std::uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count()^std::uint32_t(GetRandomValue(1,2147483646));
}

int main(){
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SW,SH,"ASHFALL | Cinematic 2D Prototype");
    SetTargetFPS(60);
    RenderTexture2D scene=LoadRenderTexture(SW,SH);
    SetTextureFilter(scene.texture,TEXTURE_FILTER_BILINEAR);
    Soldier s;
    TunnelWorld tunnels(newLayoutSeed());
    Ladder personalLadder{};bool climbing=false;
    float cameraY=0,ladderNotice=0;
    float crowdTime=0,drillerCooldown=0,drillerNotice=0;Vector2 crowdSpot{};
    // Fixed ladders are deterministic; carrier ladders remain where deployed.
    std::unordered_set<std::uint64_t> generatedLadders;
    std::vector<Particle> particles;
    std::vector<Tracer> tracers;
    float time=0, cameraX=0, shake=0, flash=0;
    int rounds=0;
    std::vector<Zombie> zombies;
    std::vector<Gib> gibs;
    std::vector<BloodPool> pools;
    std::vector<Projectile> projectiles;
    std::vector<Quip> jokes;
    int weapon=randomWeapon();
    bool knifeEquipped=false;
    float knifeSwing=0,knifeCooldown=0;
    float switchTimer=rf(7,11), switchNotice=0;
    int ammo=weapons[weapon].capacity, kills=0;
    float health=100, reload=0, spawnTimer=0.8f, hurt=0;
    float reloadDuration=weapons[weapon].reload;

    auto spawn=[&](Vector2 p,Vector2 vel,float life,float size,Color color,int kind){
        if(particles.size()<2400) particles.push_back({p,vel,life,life,size,color,kind});
    };
    auto blood=[&](Vector2 p,Vector2 force,int count){
        for(int i=0;i<count;i++)spawn(p,{force.x+rf(-135,135),force.y+rf(-180,75)},rf(.6f,1.5f),rf(1.5f,4.5f),{(unsigned char)GetRandomValue(115,190),12,18,255},3);
    };
    auto detach=[&](Zombie &z,int part,Vector2 force){
        if(z.missing&(1u<<part))return;
        z.missing|=1u<<part;
        Vector2 p={z.p.x,z.p.y-(part==0?79.0f:part<3?52.0f:19.0f)};
        if(gibs.size()<160)gibs.push_back({p,{force.x+rf(-100,100),rf(-340,-190)},rf(0,PI2),rf(-12,12),14,part});
        blood(p,{force.x,-130},35); z.bleed=2.5f;
    };
    auto die=[&](Zombie &z,Vector2 force){
        kills++;z.deathStyle=GetRandomValue(0,2);z.vy=-rf(230,420);z.vx=clampf(force.x,-220,220);z.spin=rf(-12,12);
        if(jokes.size()<20)jokes.push_back({{z.p.x,z.p.y-110},2.3f,GetRandomValue(0,6)});
    };
    auto explode=[&](Vector2 p,int type){
        float radius=type==RPG?165.0f:125.0f;shake=17;
        for(int i=0;i<90;i++)spawn(p,vmul(dir(rf(0,PI2)),rf(70,570)),rf(.2f,.8f),rf(2,9),type==CONFETTI?ColorFromHSV(rf(0,360),.8f,1):i%3?Color{255,139,35,255}:Color{255,225,130,255},type==CONFETTI?4:0);
        for(int i=0;i<18;i++)spawn(p,{rf(-120,120),rf(-170,-30)},rf(.7f,1.6f),rf(12,26),{64,63,57,180},1);
        for(auto &z:zombies)if(z.health>0){
            Vector2 delta=Vector2Subtract({z.p.x,z.p.y-45},p);
            float distance=Vector2Length(delta);
            if(distance<radius){
                z.health-=weapons[type].damage*(1-distance/radius*.65f);
                Vector2 force=vmul(Vector2Normalize(delta),230);
                blood({z.p.x,z.p.y-45},force,40);detach(z,GetRandomValue(1,4),force);
                if(z.health<=0){die(z,force);detach(z,0,force);}
            }
        }
    };
    auto shoot=[&](Vector2 muzzle,float angle){
        const Weapon &w=weapons[weapon];
        s.recoil=1.0f; s.fireCooldown=w.interval; shake=weapon==BOW?0:weapon==RPG?9:4; flash=weapon==BOW?0:1; rounds++; ammo--;
        angle+=weapon==UZI?rf(-.045f,.045f):weapon==MACHINE_GUN?rf(-.025f,.025f):0;
        Vector2 d=dir(angle);
        if(weapon==BOW||weapon==RPG||weapon==GRENADE||weapon==CROSSBOW||weapon==CONFETTI){
            float speed=weapon==BOW?900:weapon==CROSSBOW?1150:weapon==RPG?700:470;
            projectiles.push_back({muzzle,vmul(d,speed),weapon==GRENADE?2.0f:4.0f,weapon});
            return;
        }
        int pellets=weapon==SHOTGUN?8:1;
        for(int pellet=0;pellet<pellets;pellet++){
        d=dir(angle+(pellets>1?rf(-.16f,.16f):0));
        Vector2 end=vadd(muzzle,vmul(d,1450));
        float nearest=1450; Zombie* hit=nullptr; int hitPart=1;
        for(float distance=0;distance<nearest;distance+=5)if(tunnels.solidPoint(vadd(muzzle,vmul(d,distance)))){nearest=distance;end=vadd(muzzle,vmul(d,distance));break;}
        for(auto &z:zombies)if(z.health>0){
            for(int part=0;part<5;part++){
                // Head, torso, outstretched arms, and both legs.
                if((part==0&&(z.missing&1))||(part>=2&&(z.missing&(1u<<(part==2?1:part)))))continue;
                float facing=s.p.x>z.p.x?1.0f:-1.0f;
                Vector2 center={z.p.x+(part==2?facing*29:part==3?-8.0f:part==4?8.0f:0),z.p.y-(part==0?79.0f:part==1?46.0f:part==2?56.0f:17.0f)};
                Vector2 offset=Vector2Subtract(center,muzzle);
                float along=Vector2DotProduct(offset,d);
                float radius=part==0?13.0f:part==1?20.0f:part==2?12.0f:10.0f;
                float perpendicular=Vector2LengthSqr(offset)-along*along;
                if(along>0 && perpendicular<radius*radius){
                    float entry=along-sqrtf(std::max(0.0f,radius*radius-perpendicular));
                    if(entry<nearest){nearest=entry;hit=&z;hitPart=part;}
                }
            }
        }
        if(hit){
            end=vadd(muzzle,vmul(d,nearest)); hit->health-=hitPart==0?w.damage*3:w.damage;
            hit->p.x=clampf(hit->p.x+d.x*7,25,WORLD-25);
            blood(end,vmul(d,210),45); hit->bleed=2;
            if(weapon==FREEZE){hit->frozen=2.5f;for(int i=0;i<12;i++)spawn(end,{rf(-80,80),rf(-130,10)},.4f,3,{115,226,255,255},4);}
            if(hitPart==0&&hit->health<=0)detach(*hit,0,vmul(d,200));
            else if(hitPart==2)detach(*hit,(hit->missing&2)?2:1,vmul(d,180));
            else if(hitPart>=3)detach(*hit,hitPart,vmul(d,180));
            else if(hit->health<=60)detach(*hit,(hit->missing&2)?2:1,vmul(d,140));
            if(hit->health<=0){die(*hit,vmul(d,180));detach(*hit,GetRandomValue(1,4),vmul(d,200));}
        }
        tracers.push_back({muzzle,end,0.095f});
        }
        for(int i=0;i<15;i++){
            float a=angle+rf(-0.38f,0.38f);
            spawn(muzzle,vmul(dir(a),rf(70,420)),rf(0.07f,0.26f),rf(1,4),
                  i%3?Color{255,177,62,255}:Color{255,245,182,255},0);
        }
        for(int i=0;i<6;i++){
            spawn(muzzle, {rf(-35,35),rf(-95,-10)},rf(0.38f,1.0f),rf(8,20),{83,86,83,110},1);
        }
        Vector2 eject=vadd(s.p,{s.facing*10.0f,-51});
        spawn(eject,{s.facing*rf(-35,85),rf(-250,-150)},1.5f,4,{220,170,75,255},2);
    };

    while(!WindowShouldClose()){
        float dt=std::min(GetFrameTime(),0.033f); time+=dt;
        if(health<=0 && IsKeyPressed(KEY_ENTER)){
            s=Soldier{}; zombies.clear(); particles.clear(); tracers.clear(); gibs.clear(); pools.clear();
            projectiles.clear();jokes.clear();tunnels=TunnelWorld(newLayoutSeed());generatedLadders.clear();climbing=false;cameraY=0;weapon=randomWeapon();switchTimer=rf(7,11);switchNotice=0;reloadDuration=weapons[weapon].reload;
            health=100; ammo=weapons[weapon].capacity; kills=0; rounds=0; reload=0; spawnTimer=.8f;
            knifeEquipped=false;knifeSwing=0;knifeCooldown=0;
            crowdTime=0;drillerCooldown=0;drillerNotice=0;
        }
        hurt=std::max(0.0f,hurt-dt*2);
        ladderNotice=std::max(0.0f,ladderNotice-dt);
        knifeSwing=std::max(0.0f,knifeSwing-dt);knifeCooldown=std::max(0.0f,knifeCooldown-dt);
        if(IsKeyPressed(KEY_F)&&health>0&&!climbing){knifeEquipped=!knifeEquipped;reload=0;flash=0;knifeSwing=0;}
        tunnels.generate(s.p);
        drillerCooldown=std::max(0.0f,drillerCooldown-dt);drillerNotice=std::max(0.0f,drillerNotice-dt);
        if(health>0){
            int largest=0;Vector2 spot{};bool activeDriller=false;
            for(const auto &z:zombies)if(z.health>0&&z.kind==6&&z.drillTime>0)activeDriller=true;
            for(const auto &anchor:zombies)if(anchor.health>0&&anchor.grounded&&s.p.y-anchor.p.y>150&&fabsf(s.p.x-anchor.p.x)<650&&tunnels.solidPoint({anchor.p.x,anchor.p.y+5})){
                int count=0;for(const auto &other:zombies)if(other.health>0&&fabsf(other.p.x-anchor.p.x)<170&&fabsf(other.p.y-anchor.p.y)<90)count++;
                if(count>largest){largest=count;spot=anchor.p;}
            }
            if(largest>=6&&!activeDriller&&drillerCooldown==0){
                if(Vector2Distance(spot,crowdSpot)>200)crowdTime=0;
                crowdSpot=spot;crowdTime+=dt;
                if(crowdTime>2.5f){
                    Zombie driller{spot};driller.kind=6;driller.health=180;driller.speed=105;driller.drillTime=3;driller.drillY=spot.y;driller.grounded=true;
                    zombies.push_back(driller);crowdTime=0;drillerCooldown=12;drillerNotice=4;
                }
            }else crowdTime=0;
        }
        for(int c=tunnels.firstSector;c<=tunnels.lastSector;c++)for(int l=tunnels.firstLevel;l<=tunnels.lastLevel;l++){
            std::uint64_t id=(std::uint64_t(c)<<32)|std::uint32_t(l);
            if(tunnels.fixedLadder(c,l)&&generatedLadders.insert(id).second){
                float edge=tunnels.gap(c,l);
                tunnels.ladders.push_back({edge+19,GROUND+l*LEVEL,GROUND+(l+1)*LEVEL,edge-24,false});
            }
        }
        if(health>0){
            if(!knifeEquipped)switchTimer-=dt;switchNotice=std::max(0.0f,switchNotice-dt);
            if(!knifeEquipped&&switchTimer<=0){
                weapon=randomWeapon(weapon);
                ammo=weapons[weapon].capacity;reload=0;reloadDuration=weapons[weapon].reload;
                s.fireCooldown=.45f;flash=0;switchTimer=rf(7,11);switchNotice=2;
            }
            spawnTimer-=dt;
            if(spawnTimer<=0 && zombies.size()<60){
                int route=GetRandomValue(0,3);
                float side=route%2?-1.0f:1.0f;
                float x=clampf(s.p.x+side*rf(480,850),30,WORLD-30);
                if(fabsf(x-s.p.x)<300)x=s.p.x>WORLD/2?40:WORLD-40;
                Zombie z{{x,GROUND}}; z.phase=rf(0,PI2);z.speed=rf(135,190);z.dodge=rf(.7f,2);
                z.kind=GetRandomValue(0,5);
                z.speed=z.kind==0?rf(55,80):z.kind==1?rf(195,235):z.kind==2?rf(115,145):z.kind==3?rf(65,90):rf(135,175);
                z.health=z.kind==3?240:z.kind==1?80:100;
                z.stumble=rf(2,5);
                if(route>=2){z.p.x=clampf(s.p.x+side*rf(220,540),30,WORLD-30);z.p.y=rf(-100,80);z.vy=rf(70,160);}
                if(s.p.y>GROUND+120){z.p.y=GROUND+std::max(1,(int)roundf((s.p.y-GROUND)/LEVEL))*LEVEL;z.vy=0;}
                zombies.push_back(z);
                spawnTimer=std::max(.45f,1.25f-kills*.02f);
            }
            if(reload>0){reload=std::max(0.0f,reload-dt);if(reload==0)ammo=weapons[weapon].capacity;}
            else if(!knifeEquipped&&((IsKeyPressed(KEY_R)&&ammo<weapons[weapon].capacity)||ammo==0))reload=reloadDuration;
        }
        for(auto &z:zombies){
            z.vy+=1120*dt;Vector2 zv={z.health<=0?z.vx:0,z.vy};float impact=z.vy;
            z.grounded=tunnels.move(z.p,zv,dt,90);z.vy=zv.y;
            if(z.health<=0&&z.grounded&&impact>100)z.vy=-impact*.32f;
            if(z.bleed>0){z.bleed-=dt;if(GetRandomValue(0,2)==0)blood({z.p.x,z.p.y-((z.missing&1)?65.0f:43.0f)},{rf(-35,35),-100},2);}
            if(z.health<=0){z.fall+=dt;z.vx*=1-dt*(z.grounded?4:.5f);continue;}
            z.frozen=std::max(0.0f,z.frozen-dt);z.stumble-=dt;
            if(z.kind==6&&z.drillTime>0&&health>0){
                z.climbing=-1;z.vy=0;
                if(z.grounded&&z.frozen==0){
                    z.drillTime=std::max(0.0f,z.drillTime-dt);shake=std::max(shake,2.0f);
                    for(int i=0;i<3;i++)spawn({z.p.x+rf(-12,12),z.p.y-2},{rf(-110,110),rf(-150,-30)},rf(.2f,.6f),rf(2,5),{154,132,87,255},4);
                    if(z.drillTime==0){
                        tunnels.drill(z.p.x,z.drillY);tunnels.generate(s.p);shake=12;
                        for(int i=0;i<30;i++)spawn({z.p.x+rf(-70,70),z.p.y},{rf(-120,120),rf(-170,40)},.7f,rf(3,8),{143,125,87,255},4);
                    }
                }
                continue;
            }
            float distance=s.p.x-z.p.x;
            z.phase+=dt*(z.kind==0?4:z.kind==1?16:11)*(z.frozen>0?.2f:1); z.attack=std::max(0.0f,z.attack-dt);z.dodge=std::max(0.0f,z.dodge-dt);
            if(health>0){
                bool crippled=(z.missing&24)!=0;
                bool tripping=z.kind==4&&z.stumble<.65f;
                if(z.stumble<=0)z.stumble=rf(3,5);
                bool needsUp=s.p.y<z.p.y-100;
                bool wallAhead=tunnels.solidPoint({z.p.x+(distance>0?35.0f:-35.0f),z.p.y-50});
                if((needsUp||wallAhead)&&z.climbing<0){
                    if(z.kind==5&&!z.ladderUsed&&z.grounded){Ladder placed;if(tunnels.proposeLadder(z.p,placed)){placed.personal=false;tunnels.ladders.push_back(placed);z.ladderUsed=true;}}
                    float nearest=500;
                    for(int i=0;i<(int)tunnels.ladders.size();i++){auto l=tunnels.ladders[i];if(fabsf(z.p.y-l.bottom)<50&&fabsf(z.p.x-l.x)<nearest){nearest=fabsf(z.p.x-l.x);z.climbing=i;}}
                }
                float targetX=z.climbing>=0?tunnels.ladders[z.climbing].x:s.p.x;
                if(z.climbing<0&&s.p.y>z.p.y+130){
                    float best=100000;
                    for(auto h:tunnels.holes)if(fabsf(h.y-z.p.y)<80&&fabsf(h.x-z.p.x)<best){best=fabsf(h.x-z.p.x);targetX=h.x;}
                }
                if(z.climbing>=0&&fabsf(z.p.x-targetX)<22){
                    auto l=tunnels.ladders[z.climbing];z.p.x=l.x;z.p.y-=110*dt;z.vy=0;
                    if(z.p.y<=l.top){z.p={l.exitX,l.top};z.climbing=-1;}
                }else if(fabsf(targetX-z.p.x)>29){
                    Vector2 walking={(targetX>z.p.x?1.0f:-1.0f)*(z.speed+std::min(kills,30)*2)*(crippled?.38f:1)*(z.frozen>0?.2f:1)*(tripping?.15f:1),0};
                    tunnels.move(z.p,walking,dt,90);
                }
                Vector2 cursor=GetMousePosition();
                float scale=std::max(.01f,std::min((float)GetScreenWidth()/SW,(float)GetScreenHeight()/SH));
                cursor={(cursor.x-(GetScreenWidth()-SW*scale)/2)/scale+cameraX,(cursor.y-(GetScreenHeight()-SH*scale)/2)/scale+cameraY};
                Vector2 aimDir=Vector2Normalize(Vector2Subtract(cursor,{s.p.x,s.p.y-52}));
                Vector2 toZombie={z.p.x-s.p.x,z.p.y-50-(s.p.y-52)};
                bool threatened=IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&Vector2DotProduct(toZombie,aimDir)>0&&fabsf(toZombie.x*aimDir.y-toZombie.y*aimDir.x)<85;
                if(!crippled&&z.climbing<0&&z.frozen==0&&z.kind!=3&&z.grounded&&s.p.y<z.p.y+130&&z.dodge==0&&((threatened&&fabsf(distance)<750)||fabsf(distance)<190||z.kind==2)){
                    z.vy=z.kind==2?-rf(480,620):-rf(330,470);z.dodge=z.kind==2?rf(.75f,1.2f):rf(1.4f,2.8f);
                }
                if(fabsf(distance)<39&&fabsf(s.p.y-z.p.y)<62&&z.attack==0){health=std::max(0.0f,health-(z.kind==3?20:10));z.attack=.85f;hurt=1;shake=7;}
            }
        }
        for(auto m:tunnels.mines){
            bool triggered=health>0&&fabsf(s.p.x-m.p.x)<20&&fabsf(s.p.y-m.p.y)<16;
            for(const auto &z:zombies)if(z.health>0&&fabsf(z.p.x-m.p.x)<20&&fabsf(z.p.y-m.p.y)<16)triggered=true;
            if(triggered){tunnels.spent.insert(m.id);explode(m.p,GRENADE);
                float distance=Vector2Distance({s.p.x,s.p.y-35},m.p);if(distance<140){health=std::max(0.0f,health-75*(1-distance/180));hurt=1;}
            }
        }
        zombies.erase(std::remove_if(zombies.begin(),zombies.end(),[&](const Zombie&z){return (z.health<=0&&z.fall>9)||fabsf(z.p.x-s.p.x)>2600||fabsf(z.p.y-s.p.y)>1600;}),zombies.end());
        for(auto &j:jokes){j.life-=dt;j.p.y-=22*dt;}
        jokes.erase(std::remove_if(jokes.begin(),jokes.end(),[](const Quip&j){return j.life<=0;}),jokes.end());
        float move=(IsKeyDown(KEY_D)?1.0f:0.0f)-(IsKeyDown(KEY_A)?1.0f:0.0f);
        if(health<=0)move=0;
        if(IsKeyPressed(KEY_E)&&health>0&&!climbing){
            bool found=false;
            for(auto l:tunnels.ladders)if(fabsf(s.p.x-l.x)<75&&fabsf(s.p.y-l.bottom)<45){personalLadder=l;personalLadder.personal=false;found=true;break;}
            if(!found&&s.grounded)found=tunnels.proposeLadder(s.p,personalLadder);
            if(found){climbing=true;s.p.x=personalLadder.x;s.vel={0,0};}else ladderNotice=2;
        }
        float speed=IsKeyDown(KEY_LEFT_SHIFT)?290.0f:195.0f;
        s.vel.x=move*speed;
        if(IsKeyPressed(KEY_SPACE)&&s.grounded&&health>0){s.vel.y=-455;s.grounded=false;}
        if(climbing&&health>0){
            move=0;s.vel={0,0};s.grounded=false;s.p.y-=145*dt;s.walk+=dt*8;
            if(s.p.y<=personalLadder.top){s.p={personalLadder.exitX,personalLadder.top};climbing=false;s.grounded=true;}
        }else{
            s.vel.y+=1120*dt;
            s.grounded=tunnels.move(s.p,s.vel,dt);
        }
        if(fabsf(move)>0.01f&&s.grounded)s.walk+=dt*(IsKeyDown(KEY_LEFT_SHIFT)?13.5f:9.0f);
        s.recoil=std::max(0.0f,s.recoil-dt*8.0f);
        s.fireCooldown=std::max(0.0f,s.fireCooldown-dt);
        flash=std::max(0.0f,flash-dt*9.0f);
        shake=std::max(0.0f,shake-dt*22.0f);
        float targetCamera=clampf(s.p.x-SW*0.36f,0,WORLD-SW);
        cameraX+=(targetCamera-cameraX)*std::min(1.0f,dt*5.5f);
        float targetY=std::max(0.0f,s.p.y-470);
        cameraY+=(targetY-cameraY)*std::min(1.0f,dt*6);
        Vector2 mouse=GetMousePosition();
        int winW=GetScreenWidth(),winH=GetScreenHeight();
        mouse={(mouse.x-(winW-SW*std::min((float)winW/SW,(float)winH/SH))/2)/std::min((float)winW/SW,(float)winH/SH),
               (mouse.y-(winH-SH*std::min((float)winW/SW,(float)winH/SH))/2)/std::min((float)winW/SW,(float)winH/SH)};
        mouse.y+=cameraY;
        Vector2 shoulder={s.p.x-cameraX+5,s.p.y-52};
        float aim=atan2f(mouse.y-shoulder.y,mouse.x-shoulder.x);
        s.facing=(cosf(aim)>=0)?1:-1;
        Vector2 muzzle=vadd({s.p.x+5,s.p.y-52},vmul(dir(aim),weapons[weapon].length+7-s.recoil*8));
        if(!knifeEquipped&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&s.fireCooldown<=0&&reload==0&&ammo>0&&health>0&&!climbing)shoot(muzzle,aim);
        if(knifeEquipped&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&knifeCooldown==0&&health>0&&!climbing){
            knifeSwing=.25f;knifeCooldown=.45f;
            Vector2 origin={s.p.x+5,s.p.y-52};
            for(auto &z:zombies)if(z.health>0){
                Vector2 target={z.p.x,z.p.y-45};
                if(!inKnifeZone(origin,target,aim))continue;
                bool blocked=false;
                for(int i=1;i<=16;i++)if(tunnels.solidPoint(Vector2Lerp(origin,target,i/16.0f))){blocked=true;break;}
                if(blocked)continue;
                Vector2 force=vmul(dir(aim),220);
                z.health=0;die(z,force);
                for(int part=0;part<5;part++)detach(z,part,force);
                blood(target,force,65);shake=8;
            }
        }
        for(auto &p:projectiles){
            p.life-=dt;Vector2 previous=p.p;
            p.v.y+=(p.type==GRENADE||p.type==CONFETTI?580:p.type==BOW||p.type==CROSSBOW?160:0)*dt;
            p.p=vadd(p.p,vmul(p.v,dt));
            Zombie* target=nullptr;
            float closest=2;
            Vector2 step=Vector2Subtract(p.p,previous);float length=Vector2LengthSqr(step);
            for(auto &z:zombies)if(z.health>0){
                Vector2 center={z.p.x,z.p.y-45};
                float t=length>0?clampf(Vector2DotProduct(Vector2Subtract(center,previous),step)/length,0,1):0;
                if(t<closest&&Vector2Distance(center,vadd(previous,vmul(step,t)))<33){target=&z;closest=t;}
            }
            if(target)p.p=vadd(previous,vmul(step,closest));
            bool terrainHit=false;for(int stepIndex=0;stepIndex<=8;stepIndex++)if(tunnels.solidPoint(Vector2Lerp(previous,p.p,stepIndex/8.0f))){p.p=Vector2Lerp(previous,p.p,stepIndex/8.0f);terrainHit=true;break;}
            if(target||terrainHit||p.life<=0||p.p.x<0||p.p.x>WORLD){
                if(p.type==BOW||p.type==CROSSBOW){if(target){target->health-=weapons[p.type].damage;blood(p.p,vmul(Vector2Normalize(p.v),130),30);if(target->health<=0){die(*target,vmul(Vector2Normalize(p.v),180));detach(*target,1,{100,-70});}}}
                else explode(p.p,p.type);
                p.life=0;
            }else if(p.type==RPG)spawn(p.p,{rf(-20,20),rf(-25,5)},.6f,rf(5,10),{105,101,90,120},1);
        }
        projectiles.erase(std::remove_if(projectiles.begin(),projectiles.end(),[](const Projectile&p){return p.life<=0;}),projectiles.end());
        for(auto &p:particles){p.life-=dt;Vector2 previous=p.p;p.p=vadd(p.p,vmul(p.v,dt));
            if(p.kind==1){p.v.x*=1-dt*0.7f;p.v.y-=17*dt;p.size+=dt*17;}
            else if(p.kind==3){p.v.y+=880*dt;if(tunnels.solidPoint(p.p)){if(pools.size()<500)pools.push_back({{p.p.x,previous.y},rf(3,9),24});p.life=0;}}
            else if(p.kind==2){p.v.y+=620*dt;if(tunnels.solidPoint(p.p)){p.p=previous;p.v.y*=-0.3f;p.v.x*=0.7f;}}
            else {p.v.y+=80*dt;p.v=vmul(p.v,1-dt*1.6f);}
        }
        particles.erase(std::remove_if(particles.begin(),particles.end(),[](const Particle&p){return p.life<=0;}),particles.end());
        for(auto &g:gibs){
            g.life-=dt;g.v.y+=900*dt;float impact=g.v.y;bool landed=tunnels.move(g.p,g.v,dt,12);g.angle+=g.spin*dt;
            if(landed){g.v.y=-impact*.22f;g.v.x*=.65f;g.spin*=.6f;}
            if(g.life>12&&g.p.y<GROUND-10&&GetRandomValue(0,3)==0)blood(g.p,{0,30},1);
        }
        gibs.erase(std::remove_if(gibs.begin(),gibs.end(),[](const Gib&g){return g.life<=0;}),gibs.end());
        for(auto &p:pools)p.life-=dt;
        pools.erase(std::remove_if(pools.begin(),pools.end(),[](const BloodPool&p){return p.life<=0;}),pools.end());
        for(auto &t:tracers)t.life-=dt;
        tracers.erase(std::remove_if(tracers.begin(),tracers.end(),[](const Tracer&t){return t.life<=0;}),tracers.end());
        if(GetRandomValue(0,2)==0)spawn({cameraX+rf(-100,SW+100),rf(370,570)},
                                      {rf(-14,14),rf(-22,-5)},rf(3,8),rf(12,35),{85,89,83,50},1);

        BeginTextureMode(scene);
        ClearBackground({15,20,23,255});
        DrawRectangleGradientV(0,0,SW,SH,{23,34,39,255},{98,88,70,255});
        Camera2D verticalCamera{{0,0},{0,cameraY},0,1};BeginMode2D(verticalCamera);
        // Distant sun glow and haze.
        for(int i=12;i>=1;i--)DrawCircleV({960,210},i*23,alpha({245,169,94,255},(unsigned char)(2+i/2)));
        DrawCircleV({960,210},42,{219,160,104,155});
        // Three parallax city silhouettes, deterministic shapes (no asset files).
        for(int layer=0;layer<3;layer++){
            float parallax=0.12f+layer*0.19f;
            Color wall=layer==0?Color{41,51,52,255}:layer==1?Color{43,48,46,255}:Color{38,42,40,255};
            float base=410+layer*53;
            for(int i=-2;i<35;i++){
                float wx=i*155.0f+((i*37+layer*71)%65);
                float x=wx-cameraX*parallax;
                float height=65+(i*79+layer*51+10000)%135;
                rect(x,base-height,110,SH,wall);
                rect(x+12,base-height-18,52,18,wall);
                if(layer==2){
                    for(int row=0;row<4;row++)for(int col=0;col<3;col++)
                        if((i*13+row*7+col*3)%5!=0)
                            rect(x+13+col*29,base-height+15+row*28,12,15,{20,29,30,255});
                }
            }
        }
        // Ground and ruined road.
        rect(0,GROUND-5,SW,SH-GROUND+5,{48,49,44,255});
        rect(0,GROUND+24,SW,SH-GROUND,{35,39,39,255});
        for(int i=0;i<85;i++){
            float wx=i*57.0f;float x=wx-cameraX;
            if(x<-80||x>SW+80)continue;
            float y=GROUND+18+(i*37)%110;
            line({x,y},{x+15+(i*17)%45,y+((i*11)%9)-4},1.3f,{71,69,59,170});
            if(i%6==0)DrawRectangle((int)x,(int)(GROUND+70),37,3,{120,106,75,125});
        }
        // Rubble heaps, sandbags, posts and rebar.
        for(int i=std::max(0,(int)(cameraX/112)-1);i<(int)((cameraX+SW)/112)+2;i++){
            float wx=i*112.0f+35;float x=wx-cameraX;
            if(x<-120||x>SW+120)continue;
            float y=GROUND-4;
            DrawTriangle({x-27,y},{x+28,y},{x+5,y-16-(i*13)%23},{58,57,49,255});
            DrawTriangle({x-18,y},{x+11,y-5},{x-7,y-18},{81,75,60,255});
            if(i%5==0){line({x+42,y},{x+48,y-75},3,{80,76,64,255});
                line({x+48,y-75},{x+60,y-91},2,{80,76,64,255});}
            if(i%7==0){
                for(int j=0;j<3;j++){
                    DrawEllipse((int)(x+60+j*16),(int)(y-7-j%2*9),19,9,{91,84,65,255});
                    DrawEllipseLines((int)(x+60+j*16),(int)(y-7-j%2*9),19,9,{43,43,37,255});
                }
            }
        }
        tunnels.draw(cameraX,time);
        for(auto l:tunnels.ladders)if(fabsf(l.x-s.p.x)<SW+400)drawLadder(l,cameraX);
        if(climbing&&personalLadder.personal)drawLadder(personalLadder,cameraX);
        // Floating smoke behind soldier.
        for(const auto &p:particles)if(p.kind==1){
            float k=clampf(p.life/p.maxLife,0,1);
            DrawCircleV({p.p.x-cameraX,p.p.y},p.size,alpha(p.color,(unsigned char)(p.color.a*k*0.38f)));
        }
        for(const auto &p:pools)DrawEllipse((int)(p.p.x-cameraX),(int)p.p.y,p.size*1.8f,p.size*.35f,alpha({102,9,15,255},(unsigned char)(180*std::min(1.0f,p.life/3))));
        for(const auto &g:gibs){
            Vector2 pos={g.p.x-cameraX,g.p.y};
            unsigned char a=(unsigned char)(255*std::min(1.0f,g.life/2));
            if(g.part==0){
                DrawCircleV(pos,11,alpha({113,118,90,255},a));
                Vector2 eye=vadd(pos,vmul(dir(g.angle),6));DrawCircleV(eye,2,alpha({28,25,20,255},a));
                line(vadd(pos,vmul(dir(g.angle+PI/2),8)),vadd(pos,vmul(dir(g.angle+PI/2),14)),6,alpha({143,15,22,255},a));
            }else{
                Vector2 tip=vadd(pos,vmul(dir(g.angle),g.part<3?23.0f:30.0f));
                line(pos,tip,g.part<3?8.0f:11.0f,alpha({77,83,62,255},a));
                DrawCircleV(pos,5,alpha({153,17,24,255},a));
                line(pos,vadd(pos,vmul(dir(g.angle+PI),5)),2,alpha({204,189,149,255},a));
            }
        }
        // Articulated infected: sprinting, leaping and visible missing body parts.
        for(const auto &z:zombies){
            float x=z.p.x-cameraX; if(x<-120||x>SW+120)continue;
            int facing=s.p.x>z.p.x?1:-1;
            if(z.health<=0){
                float collapse=clampf(z.fall*3,0,1);
                Vector2 base={x,z.p.y-4};
                Vector2 torso={x+facing*38*collapse,z.p.y-53*(1-collapse)-7};
                float tumble=z.spin*z.fall*std::max(0.0f,1-z.fall/2.5f);
                if(z.deathStyle==0)torso=vadd(base,vmul(dir(tumble),35));
                if(z.deathStyle==1){torso={x+facing*30,z.p.y-8};base={x-20,z.p.y-5};}
                if(z.deathStyle==2){torso={x+sinf(z.fall*15)*18*std::max(0.0f,1-z.fall/2),z.p.y-40*(1-collapse)-8};}
                DrawEllipse((int)x,(int)z.p.y+3,40,5,{16,17,15,130});
                line(base,torso,z.kind==3?30:19,zombieColors[z.kind]);
                Vector2 flail=vadd(torso,vmul(dir(tumble+1),24));
                if(!(z.missing&2))line(torso,flail,7,{112,116,87,255});
                if(!(z.missing&8))line(base,vadd(base,vmul(dir(tumble-1),25)),10,{49,52,45,255});
                if(!(z.missing&1))DrawCircleV({torso.x+facing*13,torso.y-9*(1-collapse)},11,{104,108,79,255});
                else DrawCircleV({torso.x+facing*8,torso.y},6,{125,13,20,255});
                continue;
            }
            float gait=sinf(z.phase), bobZ=sinf(z.phase*2)*2;
            Vector2 hipZ={x,z.p.y-30+bobZ}, chestZ={x+facing*7,z.p.y-62+bobZ};
            if(z.kind==4&&z.stumble<.65f){chestZ.x+=facing*22;chestZ.y+=19;}
            if(z.kind==2)chestZ.x+=sinf(z.phase)*5;
            if(z.grounded)DrawEllipse((int)x,(int)z.p.y+3,25,6,{7,10,10,130});
            for(int j=-1;j<=1;j+=2){
                if(z.missing&(1u<<(j<0?3:4))){DrawCircleV({hipZ.x+j*6,hipZ.y+4},5,{135,16,22,255});continue;}
                Vector2 knee={x+j*7+gait*j*7,z.p.y-17};
                Vector2 foot={x+j*9+gait*j*16,z.p.y-3};
                line(hipZ,knee,11,{49,52,45,255});line(knee,foot,9,{42,44,40,255});
                line(foot,{foot.x+facing*(z.kind==2?22:8),foot.y},z.kind==2?10:7,z.kind==2?Color{220,58,48,255}:Color{26,28,26,255});
            }
            line(hipZ,chestZ,z.kind==3?40:25,z.frozen>0?Color{100,190,223,255}:zombieColors[z.kind]);
            if(z.kind==4){line(chestZ,{chestZ.x+facing*5,chestZ.y+22},4,{195,65,58,255});rect(chestZ.x-7,chestZ.y+10,8,6,{223,221,196,255});}
            if(z.kind==1){line({chestZ.x-9,chestZ.y+5},{chestZ.x+9,chestZ.y+5},4,{239,227,190,255});}
            line({chestZ.x-5,chestZ.y+8},{hipZ.x+6,hipZ.y-4},4,{91,43,31,255});
            for(int j=0;j<2;j++){
                if(z.missing&(1u<<(j+1))){DrawCircleV({chestZ.x,chestZ.y+5+j*6},5,{137,17,23,255});continue;}
                Vector2 elbow={chestZ.x+facing*(17+j*5),chestZ.y+9+j*6};
                Vector2 fingers={elbow.x+facing*(17+6*sinf(z.phase+j)),elbow.y-7+(z.attack>.7f?-12.0f:0)};
                line(chestZ,elbow,9,{65,69,53,255});line(elbow,fingers,7,{112,116,87,255});
                for(int k=0;k<3;k++)line({fingers.x,fingers.y+k*2},{fingers.x+facing*7,fingers.y+k*3},1.5f,{124,123,91,255});
            }
            Vector2 head={chestZ.x+facing*4,chestZ.y-17};
            if(z.missing&1){DrawCircleV({chestZ.x,chestZ.y-6},6,{142,16,23,255});continue;}
            DrawCircleV(head,12,{113,118,90,255});
            line({head.x-9,head.y-8},{head.x+5,head.y-11},5,{43,45,36,255});
            rect(head.x+facing*5-2,head.y-3,4,2,{32,33,25,255});
            line({head.x+facing*4,head.y+5},{head.x+facing*9,head.y+7},3,{65,32,26,255});
            if(z.kind==2){
                DrawCircleV({head.x+facing*13,head.y},5,{241,51,53,255});
                DrawTriangle({head.x-12,head.y-10},{head.x+12,head.y-10},{head.x+sinf(z.phase)*5,head.y-37},{224,182,49,255});
                DrawCircleV({head.x+sinf(z.phase)*5,head.y-37},4,{242,60,93,255});
            }
            if(z.kind==3){rect(head.x-14,head.y-17,28,9,{225,184,53,255});DrawEllipse((int)head.x,(int)head.y-10,18,4,{198,152,34,255});}
            if(z.kind==4){rect(head.x-13,head.y-19,26,7,{36,37,40,255});rect(head.x-18,head.y-13,36,3,{36,37,40,255});}
            if(z.kind==5&&!z.ladderUsed)drawLadder({x+cameraX-facing*24,z.p.y-105,z.p.y-7,0,false},cameraX);
            if(z.kind==6){
                rect(head.x-14,head.y-17,28,8,{235,181,54,255});DrawCircleV({head.x+facing*11,head.y-13},4,{255,239,167,255});
                Vector2 tool={x+facing*10,z.p.y-25};
                if(z.drillTime>0){tool.x+=sinf(time*70)*3;tool.y=z.p.y-22;}
                line({chestZ.x,chestZ.y+10},tool,8,{113,119,83,255});
                rect(tool.x-9,tool.y-11,18,22,{229,169,47,255});
                line({tool.x,tool.y+11},{tool.x+(z.drillTime>0?sinf(time*90)*2:0),tool.y+24},5,{178,182,164,255});
                line({tool.x-14,tool.y-8},{tool.x+14,tool.y-8},4,{40,43,39,255});
                if(z.drillTime>0){
                    rect(x-30,z.p.y-132,60,5,{29,30,26,255});rect(x-30,z.p.y-132,60*(1-z.drillTime/3),5,{235,181,54,255});
                    DrawText("DRRRRR!",(int)x-32,(int)z.p.y-150,13,{242,211,121,255});
                }
            }
            DrawText(zombieNames[z.kind],(int)x-38,(int)z.p.y-112,10,{204,194,167,190});
        }
        for(const auto &j:jokes){
            int tx=(int)(j.p.x-cameraX)-MeasureText(quips[j.text],16)/2;
            DrawText(quips[j.text],tx+1,(int)j.p.y+1,16,{16,17,18,220});
            DrawText(quips[j.text],tx,(int)j.p.y,16,{255,224,113,(unsigned char)(255*std::min(1.0f,j.life))});
        }
        float bob=(fabsf(move)>0.01f&&s.grounded)?sinf(s.walk*2)*2.0f:sinf(time*2.1f)*.6f;
        float legSwing=climbing?sinf(s.walk)*7:(fabsf(move)>0.01f&&s.grounded)?sinf(s.walk)*13.0f:0;
        Vector2 hip={s.p.x-cameraX,s.p.y-29+bob};
        Vector2 chest={s.p.x-cameraX,s.p.y-58+bob};
        // Ground shadow.
        if(s.grounded)DrawEllipse((int)(s.p.x-cameraX),(int)(s.p.y+4),31,7,{8,11,12,145});
        // Rear leg and front leg, animated.
        float tuck=s.grounded?0:climbing?5:8;
        Vector2 backKnee={hip.x-9-legSwing*0.5f-s.facing*tuck,hip.y+17-tuck*.4f};
        Vector2 backBoot={hip.x-10-legSwing,s.p.y-3-tuck};
        line(hip,backKnee,14,{47,57,48,255});line(backKnee,backBoot,12,{42,46,41,255});
        line(backBoot,{backBoot.x+s.facing*11,backBoot.y},8,{22,27,28,255});
        Vector2 frontKnee={hip.x+8+legSwing*0.5f+s.facing*tuck,hip.y+17-tuck*.5f};
        Vector2 frontBoot={hip.x+8+legSwing,s.p.y-3-tuck*.6f};
        line(hip,frontKnee,15,{78,85,65,255});line(frontKnee,frontBoot,12,{67,73,58,255});
        line(frontBoot,{frontBoot.x+s.facing*12,frontBoot.y},9,{27,30,28,255});
        // Backpack and vest.
        DrawRectangleRounded({chest.x-21,chest.y-7,24,38},0.22f,4,{42,48,40,255});
        DrawRectangleRounded({chest.x-14,chest.y-12,32,43},0.2f,4,{75,84,63,255});
        rect(chest.x-11,chest.y+6,26,14,{50,58,47,255});
        for(int i=0;i<3;i++)rect(chest.x-10+i*9,chest.y+9,7,10,{97,96,70,255});
        line({chest.x-9,chest.y-10},{chest.x+12,chest.y+23},5,{34,42,38,255});
        // Head, neck, helmet, visor.
        rect(chest.x-5,chest.y-21,11,11,{122,105,79,255});
        DrawCircleV({chest.x+1,chest.y-29},13,{125,111,85,255});
        DrawCircleV({chest.x,chest.y-36},15,{65,75,60,255});
        rect(chest.x-16,chest.y-37,32,6,{46,54,45,255});
        rect(chest.x+s.facing*3-5,chest.y-31,13,6,{22,29,28,255});
        // Weapon follows cursor; drawn as thick lines and rectangular hardware.
        Vector2 hand={chest.x+s.facing*7,chest.y+3};
        bool bowSelected=!knifeEquipped&&(weapon==BOW||weapon==CROSSBOW);
        if(bowSelected)hand={s.p.x-cameraX+5,s.p.y-52};
        float reloadProgress=reload>0?1-reload/reloadDuration:0;
        float reloadTilt=reload>0&&!bowSelected?sinf(reloadProgress*PI)*.85f*s.facing:0;
        Vector2 d=dir(aim+reloadTilt);
        Vector2 gunStart=vadd(hand,vmul(d,-9+s.recoil*7));
        Vector2 gunEnd=vadd(hand,vmul(d,weapons[weapon].length-s.recoil*8));
        Vector2 perp={-d.y,d.x};
        Vector2 support=vadd(hand,vmul(d,22));
        if(reload>0&&!bowSelected){
            float reach=sinf(clampf(reloadProgress/.65f,0,1)*PI);
            support=Vector2Lerp(vadd(hand,vmul(d,17)),{hip.x+s.facing*10,hip.y-1},reach);
        }
        Vector2 elbow={chest.x-s.facing*8,chest.y+21};
        line(chest,elbow,10,{75,81,61,255});line(elbow,support,8,{96,91,70,255});
        DrawCircleV(support,4,{133,115,87,255});
        if(climbing){
            for(int side=-1;side<=1;side+=2){
                Vector2 rung={chest.x+side*10,chest.y-17+sinf(s.walk+side*PI/2)*9};
                line(chest,{chest.x+side*17,chest.y-4},9,{75,81,61,255});
                line({chest.x+side*17,chest.y-4},rung,7,{96,91,70,255});DrawCircleV(rung,4,{133,115,87,255});
            }
        }
        if(knifeEquipped){
            float progress=knifeSwing>0?1-knifeSwing/.25f:.5f;
            float bladeAngle=aim+(knifeSwing>0?(progress*2-1)*KNIFE_HALF_ARC:0);
            Vector2 bladeDir=dir(bladeAngle),bladeNormal={-bladeDir.y,bladeDir.x};
            Vector2 grip=vadd(hand,vmul(bladeDir,22)),tip=vadd(hand,vmul(bladeDir,61));
            line(hand,grip,7,{51,49,40,255});
            line(vadd(grip,vmul(bladeNormal,-8)),vadd(grip,vmul(bladeNormal,8)),4,{101,107,102,255});
            DrawTriangle(vadd(grip,vmul(bladeNormal,-4)),tip,vadd(grip,vmul(bladeNormal,4)),{211,221,215,255});
            line(grip,tip,2,{239,242,224,255});
            if(knifeSwing>0){
                for(int i=0;i<18;i++){
                    float a=aim-KNIFE_HALF_ARC+i*(2*KNIFE_HALF_ARC/18);
                    line(vadd(hand,vmul(dir(a),KNIFE_RANGE)),vadd(hand,vmul(dir(a+2*KNIFE_HALF_ARC/18),KNIFE_RANGE)),3,{230,234,206,(unsigned char)(150*knifeSwing/.25f)});
                }
            }
        }else if(weapon==BOW||weapon==CROSSBOW){
            Vector2 upper=vadd(hand,vadd(vmul(perp,-37),vmul(d,17)));
            Vector2 lower=vadd(hand,vadd(vmul(perp,37),vmul(d,17)));
            Vector2 grip=vadd(hand,vmul(d,30));
            line(upper,grip,4,{110,83,48,255});line(grip,lower,4,{110,83,48,255});
            Vector2 nock=vadd(hand,vmul(d,reload>0?-10:3));
            line(upper,nock,1,{206,195,157,255});line(nock,lower,1,{206,195,157,255});
            if(ammo>0)line(nock,vadd(hand,vmul(d,47)),2,{192,174,123,255});
            if(weapon==CROSSBOW)line(vadd(hand,vmul(d,-15)),vadd(hand,vmul(d,45)),7,{62,69,53,255});
        }else{
        line(gunStart,gunEnd,weapon==RPG?17:weapon==GRENADE?13:8,{24,30,30,255});
        line(vadd(gunStart,vmul(perp,-3)),vadd(gunEnd,vmul(perp,-3)),2,{93,98,89,255});
        line(vadd(hand,vmul(d,7)),vadd(hand,vmul(d,30)),13,{42,49,45,255});
        Vector2 mag=vadd(hand,vmul(d,17));
        if(reload==0||reloadProgress<.18f||reloadProgress>.68f)
            line(mag,vadd(mag,vmul(perp,16)),8,{29,35,34,255});
        else line(support,vadd(support,vmul(perp,15)),8,{29,35,34,255});
        if(reload>0&&reloadProgress>.82f){
            Vector2 bolt=vadd(hand,vmul(d,9+8*sinf((reloadProgress-.82f)/.18f*PI)));
            line(support,bolt,5,{133,115,87,255});
        }
        if(weapon!=PISTOL&&weapon!=UZI)line(vadd(hand,vmul(d,-14)),vadd(hand,vmul(d,-30)),8,{48,50,45,255});
        line(vadd(hand,vmul(d,9)),vadd(hand,vmul(d,31)),3,{115,112,94,255});
        if(weapon==RPG){line(vadd(hand,vmul(d,-32)),gunEnd,13,{71,79,52,255});DrawCircleV(gunEnd,9,{39,43,33,255});}
        if(weapon==GRENADE)DrawCircleV(vadd(hand,vmul(d,13)),12,{59,65,54,255});
        if(weapon==MACHINE_GUN){DrawCircleV(vadd(hand,vadd(vmul(d,13),vmul(perp,15))),12,{49,55,42,255});for(int i=0;i<5;i++)line(vadd(hand,vadd(vmul(d,36+i*6),vmul(perp,-6))),vadd(hand,vadd(vmul(d,36+i*6),vmul(perp,6))),2,{89,91,77,255});}
        if(weapon==SHOTGUN)line(vadd(hand,vadd(vmul(d,30),vmul(perp,7))),vadd(hand,vadd(vmul(d,48),vmul(perp,7))),8,{123,84,48,255});
        if(weapon==SNIPER)line(vadd(hand,vadd(vmul(d,5),vmul(perp,-13))),vadd(hand,vadd(vmul(d,30),vmul(perp,-13))),7,{65,76,69,255});
        if(weapon==FREEZE)line(hand,gunEnd,10,{84,188,225,255});
        if(weapon==CONFETTI){line(hand,gunEnd,17,{166,64,178,255});for(int i=0;i<4;i++)DrawCircleV(vadd(hand,vmul(d,i*12)),4,ColorFromHSV(i*80,.8f,1));}
        }
        Vector2 actualMuzzle=vadd({s.p.x-cameraX+5,s.p.y-52},vmul(d,weapons[weapon].length+7-s.recoil*8));
        if(flash>0&&!knifeEquipped){
            float len=38*flash;
            DrawTriangle(vadd(actualMuzzle,vmul(perp,-9*flash)),
                         vadd(actualMuzzle,vmul(d,len)),
                         vadd(actualMuzzle,vmul(perp,9*flash)),{255,203,99,(unsigned char)(230*flash)});
            DrawCircleV(actualMuzzle,15*flash,{255,229,146,(unsigned char)(200*flash)});
            for(int i=6;i>=1;i--)DrawCircleV(actualMuzzle,i*14*flash,alpha({255,159,71,255},(unsigned char)(2+i)));
        }
        for(const auto &p:projectiles){
            Vector2 pos={p.p.x-cameraX,p.p.y};Vector2 forward=Vector2Normalize(p.v);
            if(p.type==BOW||p.type==CROSSBOW){
                Vector2 tail=Vector2Subtract(pos,vmul(forward,30));line(tail,pos,2,{204,182,125,255});
                Vector2 normal={-forward.y,forward.x};
                line(tail,vadd(tail,vadd(vmul(forward,6),vmul(normal,4))),2,{161,170,145,255});
                line(pos,Vector2Subtract(pos,vmul(forward,5)),4,{178,187,180,255});
            }else if(p.type==RPG){line(Vector2Subtract(pos,vmul(forward,24)),pos,8,{97,111,64,255});DrawCircleV(Vector2Subtract(pos,vmul(forward,27)),5,{255,182,61,255});}
            else {DrawCircleV(pos,6,{88,107,59,255});DrawCircleV(pos,2,{190,171,95,255});}
        }
        // Foreground particles, sparks, and brass.
        for(const auto &p:particles)if(p.kind!=1){
            float k=clampf(p.life/p.maxLife,0,1);
            Color c=alpha(p.color,(unsigned char)(255*k));
            Vector2 pos={p.p.x-cameraX,p.p.y};
            if(p.kind==2)DrawRectanglePro({pos.x,pos.y,p.size*1.7f,p.size*0.7f},{0,0},time*480,c);
            else if(p.kind==3){line(pos,Vector2Subtract(pos,vmul(p.v,.018f)),p.size*k,c);DrawCircleV(pos,p.size*k,c);}
            else if(p.kind==4)DrawRectanglePro({pos.x,pos.y,p.size*2,p.size},{0,0},time*500,c);
            else DrawCircleV(pos,p.size*k,c);
        }
        for(const auto&t:tracers){
            float k=t.life/0.095f;
            Vector2 a={t.a.x-cameraX,t.a.y},b={t.b.x-cameraX,t.b.y};
            line(a,b,3.5f*k,{255,180,80,(unsigned char)(160*k)});
            line(a,b,1.1f*k,{255,246,200,(unsigned char)(255*k)});
        }
        if(s.p.y>GROUND+100){
            // Warm lantern pool follows the soldier through the torchlit masonry.
            for(int ring=7;ring>0;ring--)DrawCircleV({s.p.x-cameraX,s.p.y-48},ring*19,{255,193,92,2});
        }
        EndMode2D();
        // Atmosphere, vignette, film-like grain lines and UI.
        DrawRectangleGradientV(0,0,SW,175,{10,16,19,120},{10,16,19,0});
        DrawRectangleGradientV(0,SH-125,SW,125,{10,16,19,0},{8,12,14,155});
        DrawRectangleGradientH(0,0,130,SH,{6,9,10,125},{6,9,10,0});
        DrawRectangleGradientH(SW-130,0,130,SH,{6,9,10,0},{6,9,10,125});
        for(int i=0;i<55;i++){
            int x=(i*131+int(time*7))%SW, y=(i*173+int(time*3))%SH;
            DrawPixel(x,y,{220,209,180,19});
        }
        DrawText("ASHFALL",36,29,31,{223,213,187,255});
        DrawText("CINEMATIC PROTOTYPE  /  SECTOR 07",38,67,13,{172,171,149,255});
        DrawText(TextFormat("ROUNDS FIRED  %04d",rounds),SW-235,38,17,{224,206,158,255});
        DrawText(TextFormat("HEALTH %03d   KILLS %03d   AMMO %02d / %02d",(int)health,kills,ammo,weapons[weapon].capacity),36,99,18,{224,206,158,255});
        DrawText(knifeEquipped?"KNIFE / LMB SLASH / F RETURN TO GUN":TextFormat("%s   /   AUTO SWITCH IN %.0fs",weapons[weapon].name,ceilf(switchTimer)),36,126,16,{204,194,153,255});
        DrawText(TextFormat("DEPTH %.0fm   E: PLACE / CLIMB / RETRIEVE LADDER",std::max(0.0f,(s.p.y-GROUND)/18)),36,151,14,{204,194,153,255});
        if(climbing)DrawText("CLIMBING / LADDER RETURNS TO YOUR POCKET",SW/2-230,SH-120,17,{238,208,140,255});
        if(ladderNotice>0)DrawText("MOVE CLOSER TO A WALL OR SHAFT EDGE",SW/2-215,SH-120,17,{238,208,140,255});
        if(drillerNotice>0)DrawText("DRILLER ABOVE / FLOOR BREACH INCOMING",SW/2-220,190,19,{245,179,67,255});
        if(switchNotice>0)DrawText("WEAPON CHANGED",SW/2-110,145,20,{242,207,119,255});
        if(reload>0){
            const char* reloadLabel=weapon==BOW||weapon==CROSSBOW?"NOCKING ARROW":weapon==RPG?"LOADING ROCKET":weapon==CONFETTI?"REFILLING PARTY SUPPLIES":weapon==GRENADE?"LOADING GRENADES":reloadProgress<.68f?"RELOADING / MAGAZINE":"RELOADING / CHAMBER";
            DrawText(reloadLabel,SW/2-125,SH-94,18,{224,206,158,255});
            rect(SW/2-125,SH-66,250,4,{40,44,40,255});rect(SW/2-125,SH-66,250*reloadProgress,4,{185,169,116,255});
        }
        if(hurt>0)rect(0,0,SW,SH,{130,15,10,(unsigned char)(hurt*65)});
        if(health<=0){rect(0,0,SW,SH,{8,10,11,180});DrawText("YOU WERE OVERRUN",SW/2-210,290,36,{224,206,180,255});DrawText("ENTER TO RESTART",SW/2-120,345,22,{200,191,167,255});}
        DrawText("A/D MOVE  SHIFT SPRINT  SPACE JUMP  E LADDER  F KNIFE  LMB ATTACK  R RELOAD",36,SH-43,15,{213,207,181,225});
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        float scale=std::min((float)GetScreenWidth()/SW,(float)GetScreenHeight()/SH);
        float ox=(GetScreenWidth()-SW*scale)/2,oy=(GetScreenHeight()-SH*scale)/2;
        float jitterX=rf(-shake,shake)*scale,jitterY=rf(-shake,shake)*scale;
        DrawTexturePro(scene.texture,{0,0,(float)SW,-(float)SH},
                       {ox+jitterX,oy+jitterY,SW*scale,SH*scale},{0,0},0,WHITE);
        EndDrawing();
    }
    UnloadRenderTexture(scene);
    CloseWindow();
}
