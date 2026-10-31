#include "WoW/Source/WorldClient/Linux/WxlCloudNoise.h"
#include <cstdio>
#include <cstdlib>
static void Require(bool value,const char* label){if(!value){std::fprintf(stderr,"FAILED: %s\n",label);std::exit(1);}}
int main(){
    // glibc's full-range rand() reproduces the reported out-of-range table.
    const int32_t glibcMax=2147483647;
    const float unmasked=1.0f-(glibcMax*0.000030518509f+glibcMax*0.000030518509f);
    Require(unmasked<-100000.0f,"old formula is out of range on glibc");
    srand(1);
    for(unsigned i=0;i<1000000;++i){
        const float noise=WxlCloudNoiseFromRand(rand());
        Require(noise>=-1.0001f&&noise<=1.0f,"noise stays within [-1, 1]");
    }
    Require(WxlCloudNoiseFromRand(0)==1.0f,"zero maps to 1");
    Require(WxlCloudNoiseFromRand(0x7FFF)<-0.9999f,"Windows RAND_MAX maps to -1");
    Require(WxlCloudNoiseFromRand(glibcMax)==WxlCloudNoiseFromRand(0x7FFF),"high bits ignored");
    std::puts("PASS: cloud noise table stays in the Windows [-1, 1] range");
}
