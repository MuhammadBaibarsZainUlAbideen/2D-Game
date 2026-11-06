#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
constexpr float GROUND=575;
void line(Vector2 a,Vector2 b,float w,Color c){DrawLineEx(a,b,w,c);}
void rect(float x,float y,float w,float h,Color c){DrawRectangleRec({x,y,w,h},c);}
#include "tunnels.h"
#include "melee.h"
int main(){
    int failures=0;
    auto check=[&](bool value,const char* message){if(!value){std::printf("FAIL: %s\n",message);failures++;}};
    TunnelWorld world;world.generate({420,GROUND});
    Vector2 p={420,GROUND},v={0,100};check(world.move(p,v,.016f)&&p.y==GROUND,"surface supports player");
    float entrance=world.gap(0,0);
    p={entrance+60,GROUND};v={0,100};check(!world.move(p,v,.016f)&&p.y>GROUND,"entrance is an actual opening");
    for(int i=0;i<120;i++){v.y+=1120*.016f;world.move(p,v,.016f);}
    check(fabsf(p.y-(GROUND+LEVEL))<.1f,"entrance lands on lower tunnel floor");
    p={entrance+19,GROUND+LEVEL};Ladder ladder;
    check(world.proposeLadder(p,ladder),"personal ladder can reach entrance wall");
    check(ladder.top==GROUND&&ladder.bottom==GROUND+LEVEL,"ladder spans correct levels");
    p={ladder.exitX,ladder.top};v={0,10};check(world.move(p,v,.016f),"ladder exit has solid footing");
    Rectangle partition{};for(auto r:world.solids)if(r.height>100&&r.width<100&&r.y+r.height==GROUND+LEVEL){partition=r;break;}
    p={partition.x-30,GROUND+LEVEL};v={300,0};world.move(p,v,.3f);
    check(p.x<=partition.x-12.9f,"partition blocks horizontal movement");
    p={500,GROUND+200};v={0,-1000};world.move(p,v,.2f);
    check(p.y-96>=GROUND+28-.1f,"ceiling stops upward movement");
    check(!world.mines.empty(),"underground mines generated");
    auto id=world.mines.front().id;world.spent.insert(id);world.generate({420,GROUND});
    bool respawned=false;for(auto m:world.mines)if(m.id==id)respawned=true;
    check(!respawned,"detonated mines stay detonated after regeneration");
    world.generate({50000,GROUND+LEVEL*40});
    check(world.firstSector>20&&world.firstLevel>30&&!world.solids.empty(),"generation extends beyond original map horizontally and vertically");
    check(world.solids.size()<200,"active terrain stays bounded");
    TunnelWorld same(1),different(2);same.generate({420,GROUND});different.generate({420,GROUND});
    TunnelWorld original(1);original.generate({420,GROUND});
    bool identical=same.solids.size()==original.solids.size();
    for(size_t i=0;identical&&i<same.solids.size();i++){
        auto a=same.solids[i],b=original.solids[i];identical=a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;
    }
    check(identical,"same run seed reproduces exact terrain");
    bool changed=false;for(int c=0;c<3;c++)for(int l=0;l<4;l++)if(same.gap(c,l)!=different.gap(c,l)||same.gapWidth(c,l)!=different.gapWidth(c,l))changed=true;
    check(changed,"different run seeds change tunnel geometry");
    float remembered=same.gap(0,1);same.generate({50000,GROUND+LEVEL*40});same.generate({420,GROUND});
    check(same.gap(0,1)==remembered,"returning to a sector preserves its layout");
    for(unsigned seed=1;seed<=100;seed++){
        TunnelWorld candidate(seed);candidate.generate({420,GROUND});
        Vector2 drop={candidate.gap(0,0)+60,GROUND},velocity={0,0};
        for(int i=0;i<90;i++){velocity.y+=1120*.016f;candidate.move(drop,velocity,.016f);}
        check(fabsf(drop.y-(GROUND+LEVEL))<.1f,"randomized surface entrance remains traversable");
    }
    check(inKnifeZone({0,0},{60,0},0),"knife hits nearby target in front");
    check(!inKnifeZone({0,0},{120,0},0),"knife cannot hit beyond its range");
    check(!inKnifeZone({0,0},{-30,0},0),"knife cannot hit behind soldier");
    check(!inKnifeZone({0,0},{0,60},0),"knife excludes targets outside the aim arc");
    check(inKnifeZone({0,0},{0,-60},-1.5707963f),"knife can aim upward in tunnels");
    TunnelWorld drilled(8);drilled.generate({420,GROUND});
    check(drilled.solidPoint({420,GROUND+10}),"undrilled floor blocks passage");
    drilled.drill(420,GROUND);drilled.generate({420,GROUND});
    check(!drilled.solidPoint({420,GROUND+10}),"drilling removes floor collision");
    check(drilled.solidPoint({320,GROUND+10})&&drilled.solidPoint({520,GROUND+10}),"drilling preserves floor on either side");
    p={420,GROUND};v={0,100};check(!drilled.move(p,v,.016f)&&p.y>GROUND,"zombies can drop through drilled breach");
    drilled.generate({50000,GROUND+LEVEL*40});drilled.generate({420,GROUND});
    check(!drilled.solidPoint({420,GROUND+10}),"drilled breach persists when revisiting sector");
    drilled.drill(425,GROUND);check(drilled.holes.size()==1,"duplicate drill requests do not stack identical holes");
    std::printf("Tunnel checks: %s\n",failures?"FAILED":"PASSED");return failures?1:0;
}
