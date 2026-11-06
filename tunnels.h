#pragma once
#include <unordered_set>
#include <cstdint>

constexpr float SECTOR=1400, LEVEL=260;
struct Ladder { float x,top,bottom,exitX; bool personal=false; };
struct Mine { Vector2 p; std::uint64_t id; };
struct DrillHole { float x,y,width; };
struct TunnelWorld {
    std::uint32_t seed=1;
    explicit TunnelWorld(std::uint32_t layoutSeed=1):seed(layoutSeed){}
    std::vector<Rectangle> solids;
    std::vector<Ladder> ladders;
    std::vector<Mine> mines;
    std::vector<DrillHole> holes;
    std::unordered_set<std::uint64_t> spent;
    int firstSector=0,lastSector=0,firstLevel=0,lastLevel=0;
    std::uint32_t sample(int sector,int level,std::uint32_t feature) const {
        std::uint32_t h=seed^std::uint32_t(sector)*0x9e3779b9u^std::uint32_t(level)*0x85ebca6bu^feature*0xc2b2ae35u;
        h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;return h^(h>>16);
    }
    float gap(int sector,int level) const {
        float base=level==0?660.0f:(level%2?200.0f:1000.0f);
        return sector*SECTOR+base+sample(sector,level,1)%(level==0?260:170);
    }
    float gapWidth(int sector,int level) const {return 115.0f+sample(sector,level,2)%76;}
    bool fixedLadder(int sector,int level) const {return sample(sector,level,3)%3==0;}
    void drill(float x,float y){
        for(auto h:holes)if(fabsf(h.x-x)<50&&fabsf(h.y-y)<40)return;
        holes.push_back({x,y,160});
    }
    void generate(Vector2 player){
        solids.clear();mines.clear();
        firstSector=std::max(0,int(player.x/SECTOR)-2);lastSector=int(player.x/SECTOR)+2;
        int depth=std::max(0,int((player.y-GROUND)/LEVEL));
        firstLevel=std::max(0,depth-3);lastLevel=depth+4;
        for(int c=firstSector;c<=lastSector;c++)for(int l=firstLevel;l<=lastLevel;l++){
            float y=GROUND+l*LEVEL,x=c*SECTOR,g=gap(c,l),width=gapWidth(c,l);
            solids.push_back({x,y,g-x,28});solids.push_back({g+width,y,x+SECTOR-g-width,28});
            if(l>0){
                // Tall broken partitions create raised passages that require ladders.
                float wall=x+580+sample(c,l,4)%260;
                // Keep the arrival shaft clear so falling players land on the floor.
                float above=gap(c,l-1),aboveWidth=gapWidth(c,l-1);
                if(wall+65>above-25&&wall<above+aboveWidth+25)wall=x+100;
                float height=115+sample(c,l,5)%41;
                solids.push_back({wall,y-height,40.0f+sample(c,l,6)%26,height});
                if(sample(c,l,7)%2==0){
                    float ledge=x+450;
                    if(ledge>g+width+25||ledge+75<g-25)solids.push_back({ledge,y-65,75,65});
                }
                std::uint64_t id=(std::uint64_t(c)<<32)|std::uint32_t(l);
                float mineX=x+420+sample(c,l,8)%550;
                if((mineX>g-25&&mineX<g+width+25)||(mineX>wall-25&&mineX<wall+90))mineX=x+1320;
                if(sample(c,l,9)%5!=0&&!spent.count(id))mines.push_back({{mineX,y-3},id});
            }
        }
        // Split actual collision geometry; drilled openings survive streaming out and back.
        for(auto hole:holes){
            std::vector<Rectangle> cut;
            for(auto r:solids){
                float left=hole.x-hole.width/2,right=hole.x+hole.width/2;
                if(r.y+r.height<hole.y||r.y>hole.y+30||r.x>=right||r.x+r.width<=left){cut.push_back(r);continue;}
                if(r.x<left)cut.push_back({r.x,r.y,left-r.x,r.height});
                if(r.x+r.width>right)cut.push_back({right,r.y,r.x+r.width-right,r.height});
            }
            solids.swap(cut);
        }
        mines.erase(std::remove_if(mines.begin(),mines.end(),[&](const Mine&m){
            for(auto h:holes)if(fabsf(m.p.x-h.x)<h.width/2&&fabsf(m.p.y-h.y)<35)return true;return false;
        }),mines.end());
    }
    // Swept axis collision keeps feet on slabs and stops bodies crossing ceilings/walls.
    bool move(Vector2 &p,Vector2 &v,float dt,float height=96){
        float oldX=p.x;p.x=std::max(18.0f,p.x+v.x*dt);
        for(auto r:solids)if(p.y>r.y+.5f&&p.y-height<r.y+r.height-.5f){
            if(v.x>0&&oldX+13<=r.x+.5f&&p.x+13>=r.x)p.x=r.x-13;
            else if(v.x<0&&oldX-13>=r.x+r.width-.5f&&p.x-13<=r.x+r.width)p.x=r.x+r.width+13;
        }
        float oldY=p.y;p.y+=v.y*dt;bool grounded=false;
        for(auto r:solids)if(p.x+10>r.x&&p.x-10<r.x+r.width){
            if(v.y>=0&&oldY<=r.y+.6f&&p.y>=r.y){p.y=r.y;v.y=0;grounded=true;}
            else if(v.y<0&&oldY-height>=r.y+r.height-.6f&&p.y-height<=r.y+r.height){p.y=r.y+r.height+height;v.y=0;}
        }
        return grounded;
    }
    bool solidPoint(Vector2 p) const { for(auto r:solids)if(CheckCollisionPointRec(p,r))return true;return false; }
    bool proposeLadder(Vector2 p,Ladder &out) const {
        float best=110;
        for(auto r:solids){
            if(r.y>p.y-100||r.y<p.y-LEVEL-35)continue;
            for(int side=0;side<2;side++){
                float edge=side?r.x+r.width:r.x;
                float x=edge+(side?19:-19),distance=fabsf(p.x-x);
                if(distance<best){best=distance;out={x,r.y,p.y,edge+(side?-24:24),true};}
            }
        }
        return best<110;
    }
    void draw(float cameraX,float time) const {
        for(int c=firstSector;c<=lastSector;c++)for(int l=std::max(1,firstLevel);l<=lastLevel;l++){
            float x=c*SECTOR-cameraX,y=GROUND+(l-1)*LEVEL+28;
            rect(x,y,SECTOR,LEVEL-28,{18,20,21,255});
            // Offset masonry courses, water stains, support arches and rusty pipes.
            for(int row=0;row<6;row++)for(int col=0;col<19;col++){
                float bx=x+col*80-(row%2)*40,by=y+row*39;
                Color stone={(unsigned char)(31+(col*7+row*3+c)%13),(unsigned char)(32+(row*5+col)%10),30,255};
                rect(bx+1,by+1,77,36,stone);line({bx+8,by+7},{bx+38,by+12},1,{48,46,39,100});
            }
            line({x,y+27},{x+SECTOR,y+27},7,{61,48,36,255});
            for(int a=0;a<4;a++){
                float ax=x+a*340+35+sample(c,l,20+a)%65;
                rect(ax,y,14,LEVEL-28,{53,50,42,255});rect(ax+14,y,7,LEVEL-28,{12,15,15,130});
                float ty=y+96,tx=ax+85;
                line({tx-5,ty+25},{tx,ty},6,{99,65,37,255});rect(tx-9,ty+8,18,6,{38,35,30,255});
                float flame=1+sinf(time*16+ax)*.13f+sinf(time*23+ax)*.08f;
                for(int glow=7;glow>0;glow--)DrawCircleV({tx,ty-7},glow*18*flame,{255,139,44,(unsigned char)(2+glow/2)});
                DrawEllipse((int)tx,(int)(ty-7),8*flame,18*flame,{245,109,28,235});
                DrawEllipse((int)tx,(int)(ty-3),4*flame,10*flame,{255,224,122,255});
            }
            DrawText(TextFormat("SERVICE SECTOR %d / DEPTH %d",c,l),(int)x+170,(int)y+55,13,{118,107,80,190});
        }
        for(auto r:solids){r.x-=cameraX;DrawRectangleRec(r,{68,63,51,255});rect(r.x,r.y,r.width,5,{113,101,76,255});
            for(float x=r.x+15;x<r.x+r.width;x+=63)line({x,r.y+8},{x+22,r.y+20},1,{38,39,34,255});}
        for(int c=firstSector;c<=lastSector;c++)for(int l=firstLevel;l<=lastLevel;l++){
            float g=gap(c,l)-cameraX,y=GROUND+l*LEVEL,w=gapWidth(c,l);
            rect(g,y,w,28,{13,16,17,255});
            line({g,y},{g,y+28},4,{104,91,66,255});line({g+w,y},{g+w,y+28},4,{104,91,66,255});
            if(l>0){DrawText("DROP",(int)g+48,(int)y-15,10,{181,122,59,200});}
        }
        for(int c=firstSector;c<=lastSector;c++){
            float g=gap(c,0)-cameraX;
            rect(g-12,GROUND-42,9,42,{161,133,54,255});rect(g+gapWidth(c,0)+3,GROUND-42,9,42,{161,133,54,255});
            DrawText("TUNNEL ACCESS",(int)g-4,(int)GROUND-63,13,{233,195,100,255});
        }
        for(auto h:holes){
            float x=h.x-cameraX;
            rect(x-h.width/2,h.y,h.width,28,{9,12,13,255});
            for(int i=0;i<5;i++){
                float edge=x-h.width/2;
                DrawTriangle({edge-5,h.y+i*6},{edge+8,h.y+i*6+3},{edge-5,h.y+i*6+8},{103,88,62,255});
            }
        }
        for(auto m:mines){Vector2 p={m.p.x-cameraX,m.p.y};DrawEllipse((int)p.x,(int)p.y,12,4,{78,88,47,255});DrawCircleV({p.x,p.y-3},2,sinf(time*6)>0?RED:MAROON);}
    }
};
inline void drawLadder(const Ladder &l,float cameraX){
    Color c=l.personal?Color{188,181,143,255}:Color{124,104,66,255};
    line({l.x-cameraX-10,l.top-8},{l.x-cameraX-10,l.bottom},4,c);
    line({l.x-cameraX+10,l.top-8},{l.x-cameraX+10,l.bottom},4,c);
    for(float y=l.bottom-8;y>l.top-8;y-=17)line({l.x-cameraX-10,y},{l.x-cameraX+10,y},3,c);
}
