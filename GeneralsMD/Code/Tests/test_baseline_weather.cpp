// Original precipitation/wind policy plus real SDL admission before a view exists.
#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "GameClient/Snow.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Platform/RenderPost.h"
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "dx8wrapper.h"
#include "dx11runtime.h"
#include "ww3d.h"
#include "wwmath.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
const Char *g_strFile="data\\Generals.str";
const Char *g_csfFile="data\\%s\\Generals.csf";
static char prefix[]="";char *gAppPrefix=prefix;
RenderWindow ApplicationHWnd=NULL;Bool ApplicationIsBorderless=FALSE;
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
static void cpu() {
    CHECK(weatherKindForMap(TRUE,TRUE,100,100)==WEATHER_KIND_NONE);
    CHECK(weatherKindForMap(TRUE,FALSE,0,100)==WEATHER_KIND_NONE); // authored rain/snow never doubles
    CHECK(weatherKindForMap(FALSE,TRUE,100,100)==WEATHER_KIND_SNOW);
    CHECK(weatherKindForMap(FALSE,FALSE,41,100)==WEATHER_KIND_SAND);
    CHECK(weatherKindForMap(FALSE,FALSE,40,100)==WEATHER_KIND_RAIN);
    CHECK(weatherKindForMap(FALSE,FALSE,0,0)==WEATHER_KIND_RAIN);
    CHECK(std::fabs(weatherWrap(1124,0,1024)-100)<.001f);
    CHECK(std::fabs(weatherWrap(-100,0,1024)-924)<.001f);
    CHECK(std::fabs(weatherWrap(700,300,1024)-weatherWrap(700,500,1024))<.001f);
    CHECK(NativePostEnvironmentPolicy::chooseHaze(true,false,true,true,true,true)==NativePostEnvironmentPolicy::Weather);
    CHECK(NativePostEnvironmentPolicy::chooseHaze(true,false,true,true,true,false)==NativePostEnvironmentPolicy::WarmLight);
}
class WeatherFixture : public W3DSnowManager {
public:
    WeatherFixture(){m_startingHeights=nullptr;m_fullTimePeriod=1;}
};
static int gpu() {
    if(!SDL_Init(SDL_INIT_VIDEO))return 77;
    SDL_SetHint("ZH_OFFSCREEN_FRAMES","1");SDL_SetHint("ZH_OFFSCREEN_HZ","0");
    initMemoryManager();WWMath::Init();bool initialized=DX8Wrapper::Init(NULL);
    if(!initialized || WW3D::Set_Render_Device(0,16,16,32,1,false,false,false)!=WW3D_ERROR_OK) {
        if(initialized)DX8Wrapper::Shutdown();WWMath::Shutdown();SDL_Quit();return 77;
    }
    auto *device=static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if(!device || !device->Get_Gpu() || !device->Get_Gpu()->Supports_Scene_Depth()) {
        DX8Wrapper::Shutdown();WWMath::Shutdown();SDL_Quit();return 77;
    }
    CHECK(!Direct3D11_Present_Is_Enabled()); // baseline's gate rejects this actual renderer
    CHECK(RenderPost::EnvironmentAvailable());
    {
        GlobalData data;auto *savedData=TheWritableGlobalData;TheWritableGlobalData=&data;
        ScriptEngine script;auto *savedScript=TheScriptEngine;TheScriptEngine=&script;
        auto &breeze=const_cast<BreezeInfo &>(script.getBreezeInfo());breeze={};
        breeze.m_directionVec.x=1;breeze.m_breezePeriod=LOGICFRAMES_PER_GAME_SECOND*5;
        WeatherFixture weather;auto *savedSnow=TheSnowManager;TheSnowManager=&weather;
        data.m_weatherEffects=TRUE;data.m_classicGraphics=FALSE;
        RenderPost::Settings settings;settings.Chain="off";device->Begin_Post(settings);
        CHECK(!device->Post_Active()); // native weather needs neither post chain nor current view
        weather.update();CHECK(weather.isWeatherOn());CHECK(weather.getWeatherKind()==WEATHER_KIND_NONE);
        CHECK(weather.getWeatherWind().x>.06f && weather.getWeatherWind().y==0);
        CHECK(weatherBreezeInfo().m_intensity>.05f);
        CHECK(weatherBreezeInfo().m_breezePeriod==LOGICFRAMES_PER_GAME_SECOND*3);
        // Script visibility hides precipitation/haze; the existing visual wind stays enabled.
        weather.setVisible(FALSE);weather.update();CHECK(!weather.isVisible() && weather.isWeatherOn());
        data.m_weatherEffects=FALSE;weather.update();CHECK(!weather.isWeatherOn());
        CHECK(&weatherBreezeInfo()==&script.getBreezeInfo());
        data.m_weatherEffects=TRUE;data.m_classicGraphics=TRUE;weather.update();CHECK(!weather.isWeatherOn());
        data.m_classicGraphics=FALSE;weather.update();CHECK(weather.isWeatherOn());
        weather.reset();CHECK(!weather.isWeatherOn() && weather.getWeatherKind()==WEATHER_KIND_NONE && weather.isVisible());
        TheSnowManager=savedSnow;TheScriptEngine=savedScript;TheWritableGlobalData=savedData;
    }
    DX8Wrapper::Shutdown();WWMath::Shutdown();SDL_Quit();return failures?1:0;
}
int main(int argc,char **argv){cpu();int result=argc>1 && !std::strcmp(argv[1],"--gpu")?gpu():0;std::printf("Baseline native weather: %u checks, %u failures\n",checks,failures);return result?result:failures?1:0;}
