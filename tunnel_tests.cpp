#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>
constexpr float GROUND=575;
void line(Vector2 a,Vector2 b,float w,Color c){DrawLineEx(a,b,w,c);}
void rect(float x,float y,float w,float h,Color c){DrawRectangleRec({x,y,w,h},c);}
#include "tunnels.h"
int main(){
    int failures=0;
    auto check=[&](bool value,const char* message){if(!value){std::printf("FAIL: %s\n",message);failures++;}};
    TunnelWorld world;world.generate({420,GROUND});
    Vector2 p={420,GROUND},v={0,100};check(world.move(p,v,.016f)&&p.y==GROUND,"surface supports player");
    p={750,GROUND};v={0,100};check(!world.move(p,v,.016f)&&p.y>GROUND,"entrance is an actual opening");
    for(int i=0;i<120;i++){v.y+=1120*.016f;world.move(p,v,.016f);}
    check(fabsf(p.y-(GROUND+LEVEL))<.1f,"entrance lands on lower tunnel floor");
    p={719,GROUND+LEVEL};Ladder ladder;
    check(world.proposeLadder(p,ladder),"personal ladder can reach entrance wall");
    check(ladder.top==GROUND&&ladder.bottom==GROUND+LEVEL,"ladder spans correct levels");
    p={ladder.exitX,ladder.top};v={0,10};check(world.move(p,v,.016f),"ladder exit has solid footing");
    p={580,GROUND+LEVEL};v={300,0};world.move(p,v,.3f);
    check(p.x<=597.1f,"partition blocks horizontal movement");
    p={500,GROUND+200};v={0,-1000};world.move(p,v,.2f);
    check(p.y-96>=GROUND+28-.1f,"ceiling stops upward movement");
    check(!world.mines.empty(),"underground mines generated");
    auto id=world.mines.front().id;world.spent.insert(id);world.generate({420,GROUND});
    bool respawned=false;for(auto m:world.mines)if(m.id==id)respawned=true;
    check(!respawned,"detonated mines stay detonated after regeneration");
    world.generate({50000,GROUND+LEVEL*40});
    check(world.firstSector>20&&world.firstLevel>30&&!world.solids.empty(),"generation extends beyond original map horizontally and vertically");
    check(world.solids.size()<200,"active terrain stays bounded");
    std::printf("Tunnel checks: %s\n",failures?"FAILED":"PASSED");return failures?1:0;
}
