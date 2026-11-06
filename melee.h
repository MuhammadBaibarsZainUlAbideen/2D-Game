#pragma once
constexpr float KNIFE_RANGE=95.0f;
constexpr float KNIFE_HALF_ARC=1.05f;
inline bool inKnifeZone(Vector2 origin,Vector2 target,float aim){
    float dx=target.x-origin.x,dy=target.y-origin.y;
    float distanceSquared=dx*dx+dy*dy;
    return distanceSquared<=KNIFE_RANGE*KNIFE_RANGE&&
        (distanceSquared<1||dx*cosf(aim)+dy*sinf(aim)>=sqrtf(distanceSquared)*cosf(KNIFE_HALF_ARC));
}
