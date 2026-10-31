#include "Engine/Source/ModernAssets/WxlVisuals/native_config.h"
#include "Engine/Source/ModernAssets/WxlVisuals/selection_policy.h"
#include "Engine/Source/ModernAssets/WxlVisuals/engine_lights.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unistd.h>
static void Require(bool value,const char* label){if(!value){std::fprintf(stderr,"FAILED: %s\n",label);std::exit(1);}}
int main(){
    char file[]="/tmp/wxl-native-visual-config-XXXXXX";int fd=mkstemp(file);Require(fd>=0,"temporary INI");
    FILE* stream=fdopen(fd,"w");Require(stream!=nullptr,"INI stream");
    std::fputs("[CoAVolFog]\nQuality=99\nWaterQuality=-10\nDensity=nan\nNoiseScale=-4\nMaxDistance=1\nWaterClarity=99\nWaterFoam=invalid\nLocalLightPhase=-99\nWaterWaves=inf\nWaterZoneColors=0.75 ; comment\n[Other]\nWaterWind=999\n",stream);std::fclose(stream);
    Config config=ReadNativeVisualConfig(file);unlink(file);
    Require(config.quality==3&&config.waterQuality==1,"quality bounds");
    Require(config.density==1&&config.waterWaves==1&&config.waterFoam==1,"invalid and nonfinite defaults");
    Require(config.noiseScale==0.001f&&config.maxDistance==200&&config.waterClarity==4&&config.localLightPhase==-0.9f,"numeric limits");
    Require(config.waterZoneColors==0.75f&&config.waterWind==2,"comments and section boundary");
    VisualSelection selection;selection.enabled=false;ApplyVisualSelection(config,selection);
    Require(!config.water&&!config.enable&&!config.localLights&&!VisualFogEnabled(config),"master disables renderers");
    config=Config{};selection=VisualSelection{};selection.distanceFog=false;selection.distanceHaze=false;
    selection.godRays=true;selection.localLights=true;ApplyVisualSelection(config,selection);
    Require(config.godRays>0&&config.stockFog==0&&config.farFog==0&&config.haze==0&&VisualFogEnabled(config),"rays-only profile keeps native distance fog");
    Require(!config.localLights&&config.sunScatter==0&&config.groundFog==0,"no unwanted medium effects");
    LocalLightInputs lights;const float camera[3]={0,0,0};
    for(unsigned i=1;i<=20;++i){LocalPointLight light;light.color[0]=float(i);light.attenuation[0]=1;light.attenuation[2]=0.01f;light.nativeId=i;
        engine::SelectLocalPointLight(lights,light,camera,{true,1});}
    Require(lights.pointLightCount==8,"bounded point-light selection");
    for(unsigned i=0;i<8;++i)Require(lights.pointLights[i].color[0]==float(20-i),"brightest lights ordered first");
    LocalPointLight invalid=lights.pointLights[0];invalid.color[0]=std::numeric_limits<float>::quiet_NaN();
    Require(!engine::SelectLocalPointLight(lights,invalid,camera,{true,1}),"reject nonfinite light");
    invalid=lights.pointLights[0];invalid.attenuation[1]=-1;
    Require(!engine::SelectLocalPointLight(lights,invalid,camera,{true,1}),"reject invalid attenuation");
    invalid=lights.pointLights[0];invalid.position[0]=10000;
    Require(!engine::SelectLocalPointLight(lights,invalid,camera,{true,1}),"reject distant light");
    float uploaded[3]={};const float bright[3]={4,1,0.25f};engine::UploadedPointLightColor(bright,{true,1},uploaded);
    Require(uploaded[0]==4&&uploaded[1]>0&&uploaded[1]<1,"linear conversion retains bright-light peak");
    std::puts("PASS: native visual INI, finite bounds, selective/rays-only policy, bounded point lights");
}
