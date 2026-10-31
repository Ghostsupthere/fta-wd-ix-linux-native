#include "engine_actors.h"
#include <algorithm>
#include <cmath>
// Water-contact selection, imported from the supplied engine_actors.cpp without
// its Windows object-manager walk (the native client enumerates typed units).
namespace {
constexpr float kMaxWorldCoordinate=100000.0f,kMaxCollisionRadius=50.0f,kMaxCollisionHeight=100.0f;
bool FiniteWithin(float value,float minimum,float maximum){return std::isfinite(value)&&value>=minimum&&value<=maximum;}
bool PlausibleContact(const WaterContact& c) {
    for(float axis:c.position)if(!FiniteWithin(axis,-kMaxWorldCoordinate,kMaxWorldCoordinate))return false;
    return FiniteWithin(c.surface,-kMaxWorldCoordinate,kMaxWorldCoordinate)&&c.radius>0.0f&&
           FiniteWithin(c.radius,0.0f,kMaxCollisionRadius)&&c.height>0.0f&&
           FiniteWithin(c.height,0.0f,kMaxCollisionHeight)&&WaterContactDepth(c)<ClientRippleDepthLimit(c.height);
}
double SquaredGroundDistance(const WaterContact& c,const float centre[3]) {
    const double dx=static_cast<double>(c.position[0])-centre[0],dy=static_cast<double>(c.position[1])-centre[1];
    return dx*dx+dy*dy;
}
bool Precedes(const WaterContact& a,const WaterContact& b,const float centre[3]) {
    const double aDistance=SquaredGroundDistance(a,centre),bDistance=SquaredGroundDistance(b,centre);
    return aDistance!=bDistance?aDistance<bDistance:a.guid<b.guid;
}
bool ValidCentre(const float centre[3]){for(int i=0;i<3;++i)if(!FiniteWithin(centre[i],-kMaxWorldCoordinate,kMaxWorldCoordinate))return false;return true;}
}
namespace engine {
bool SelectWaterContact(WaterContactFrame& frame,const WaterContact& contact,const float centre[3]) {
    if(frame.count>kMaxWaterContacts||!ValidCentre(centre)||!PlausibleContact(contact)||
       SquaredGroundDistance(contact,centre)>static_cast<double>(kWaterContactRange)*kWaterContactRange)return false;
    uint32_t index=0;
    while(index<frame.count&&!Precedes(contact,frame.contacts[index],centre))++index;
    if(index==kMaxWaterContacts)return false;
    const uint32_t count=std::min(frame.count+1,kMaxWaterContacts);
    for(uint32_t i=count-1;i>index;--i)frame.contacts[i]=frame.contacts[i-1];
    frame.contacts[index]=contact;frame.count=count;return true;
}
}
