// Ported from heurazy/mario-kart-wii-VR-port. Callouts inspired by BigWalkVR (MIT).
// Native headset ImGui canvas; does not replace the upstream settings context.
mkw::vr::TutorialFlow g_tutorial;
mkw::vr::SettingsPauseFlow g_settingsPause;
std::string g_onboardingError;
int g_introductionKind=0; // 0 settings, 1 welcome, 2/3 control guides
struct TutorialInput {
    bool steamvr=false,remote=false,active=false,reverse=false,trick=false,confirm=false,brake=false;
    float steering_x=0,item=0,accelerate=0;
    std::array<float,2> ui_grips{};
    std::array<mkw::vr::UiHandPose,2> ui_hands{};
};
TutorialInput TutorialControls() {
    const auto ui=mkw::vr::OpenXRReadUiSnapshot();
    TutorialInput input{};input.active=ui.active;input.steamvr=true;
    input.remote=mkw::vr::OpenXRGetControllerMode()==mkw::vr::OpenXRControllerMode::WiiRemote;
    input.ui_hands=ui.hands;input.steering_x=ui.buttons[0].stick_x;
    input.trick=ui.buttons[0].primary;input.item=ui.buttons[0].secondary;
    input.reverse=ui.buttons[0].trigger>.5f;input.accelerate=ui.buttons[1].trigger;
    input.confirm=ui.buttons[1].primary;input.brake=ui.buttons[1].secondary;
    for(int hand=0;hand<2;++hand) input.ui_grips[hand]=ui.buttons[hand].squeeze;
    return input;
}
namespace {
using TutorialModel=std::vector<AuroraVRControllerVertex>;

AuroraVRControllerVertex TutorialModelVertex(float x,float y,float z,float r,float g,float b) {
    return {{x,y,z},{r,g,b}};
}

void AddTutorialBox(TutorialModel& out,float cx,float cy,float cz,float sx,float sy,float sz,
                    float r,float g,float b) {
    const float x0=cx-sx*.5f,x1=cx+sx*.5f;
    const float y0=cy-sy*.5f,y1=cy+sy*.5f;
    const float z0=cz-sz*.5f,z1=cz+sz*.5f;
    const AuroraVRControllerVertex v[8]={
        TutorialModelVertex(x0,y0,z0,r,g,b),TutorialModelVertex(x1,y0,z0,r,g,b),
        TutorialModelVertex(x1,y1,z0,r,g,b),TutorialModelVertex(x0,y1,z0,r,g,b),
        TutorialModelVertex(x0,y0,z1,r,g,b),TutorialModelVertex(x1,y0,z1,r,g,b),
        TutorialModelVertex(x1,y1,z1,r,g,b),TutorialModelVertex(x0,y1,z1,r,g,b)};
    constexpr uint8_t tri[]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
    for(uint8_t i:tri) out.push_back(v[i]);
}

void AddTutorialRing(TutorialModel& out,float cy,float cz,float radius,float thickness,float r,float g,float b) {
    constexpr int segments=20;
    for(int i=0;i<segments;++i) {
        const float a=float(i)*6.28318530718f/segments;
        const float n=float(i+1)*6.28318530718f/segments;
        const float ca=std::cos(a),sa=std::sin(a),cn=std::cos(n),sn=std::sin(n);
        const float ro=radius+thickness*.5f,ri=radius-thickness*.5f;
        const auto a0=TutorialModelVertex(ca*ro,cy+sa*ro,cz,r,g,b);
        const auto a1=TutorialModelVertex(ca*ri,cy+sa*ri,cz,r,g,b);
        const auto b0=TutorialModelVertex(cn*ro,cy+sn*ro,cz,r,g,b);
        const auto b1=TutorialModelVertex(cn*ri,cy+sn*ri,cz,r,g,b);
        out.insert(out.end(),{a0,b0,b1,a0,b1,a1});
    }
}

TutorialModel MakeQuestTutorialController(bool right) {
    TutorialModel out;
    const float tint=.08f;
    AddTutorialBox(out,0,-.045f,.018f,.035f,.11f,.037f,tint,tint+.05f,tint+.08f);
    AddTutorialBox(out,0,.018f,-.005f,.07f,.055f,.045f,.18f,.22f,.28f);
    AddTutorialRing(out,.055f,-.012f,.048f,.010f,.12f,.16f,.21f);
    AddTutorialBox(out,right?.020f:-.020f,.035f,-.031f,.018f,.012f,.010f,.85f,.88f,.92f);
    AddTutorialBox(out,right?-.014f:.014f,.018f,-.034f,.022f,.018f,.010f,.32f,.36f,.42f);
    return out;
}

bool PublishFallbackTutorialControllers() {
    static const auto left=MakeQuestTutorialController(false);
    static const auto right=MakeQuestTutorialController(true);
    aurora_set_vr_controller_model(0,left.data(),static_cast<uint32_t>(left.size()));
    aurora_set_vr_controller_model(1,right.data(),static_cast<uint32_t>(right.size()));
    return true;
}

#if defined(__ANDROID__)
#include "quest_controller_models.inl"
#endif

#if defined(_WIN32)
HMODULE LoadOpenVrLibraryForTutorial() {
    if(auto* loaded=GetModuleHandleW(L"openvr_api.dll")) return loaded;
    if(auto* direct=LoadLibraryW(L"openvr_api.dll")) return direct;
    wchar_t steamPath[1024]{};DWORD size=sizeof(steamPath);DWORD type=0;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,&type,steamPath,&size)==ERROR_SUCCESS) {
        std::wstring path=steamPath;
        std::replace(path.begin(),path.end(),L'/',L'\\');
        path+=L"\\steamapps\\common\\SteamVR\\bin\\win64\\openvr_api.dll";
        if(auto* fromSteam=LoadLibraryW(path.c_str())) return fromSteam;
    }
    return nullptr;
}

#include "steam_controller_models.inl"

bool PublishSteamVrTutorialControllers() {
    using InitFn=uint32_t (*)(EVRInitError*,EVRApplicationType);
    using GetInterfaceFn=intptr_t (*)(const char*,EVRInitError*);
    HMODULE library=LoadOpenVrLibraryForTutorial();
    if(!library) return false;
    const auto init=reinterpret_cast<InitFn>(GetProcAddress(library,"VR_InitInternal"));
    const auto getInterface=reinterpret_cast<GetInterfaceFn>(GetProcAddress(library,"VR_GetGenericInterface"));
    if(!init || !getInterface) return false;
    EVRInitError error=EVRInitError_VRInitError_None;
    // Utility clients cannot query tracked devices. A background client can
    // read controller models without taking the OpenXR scene application's focus.
    // Keep this shared OpenVR client alive: shutting it down here can invalidate
    // interfaces still used by SDL or the SteamVR OpenXR runtime.
    static bool initialized=false;
    if(!initialized) {
        init(&error,EVRApplicationType_VRApplication_Background);
        initialized=error==EVRInitError_VRInitError_None;
    }
    if(error!=EVRInitError_VRInitError_None) {
        std::fprintf(stderr,"[vr-tutorial] OpenVR background initialization failed: %d; using fallback models\n",int(error));
        return false;
    }
    const std::string systemName=std::string("FnTable:")+IVRSystem_Version;
    const std::string renderName=std::string("FnTable:")+IVRRenderModels_Version;
    auto* system=reinterpret_cast<VR_IVRSystem_FnTable*>(getInterface(systemName.c_str(),&error));
    if(!system || error!=EVRInitError_VRInitError_None) return false;
    auto* render=reinterpret_cast<VR_IVRRenderModels_FnTable*>(getInterface(renderName.c_str(),&error));
    if(!render || error!=EVRInitError_VRInitError_None) return false;
    const std::string inputName=std::string("FnTable:")+IVRInput_Version;
    auto* input=reinterpret_cast<VR_IVRInput_FnTable*>(getInterface(inputName.c_str(),&error));
    return UpdateSteamControllerModels(system,render,error==EVRInitError_VRInitError_None?input:nullptr);
}
#endif

const char* ControllerKey(int hand,int row) {
#if defined(_WIN32)
    return g_controllerKeys[hand][row].c_str();
#else
    static const char* keys[2][5]={{"STICK","X","Y","TRIGGER","GRIP"},{"STICK CLICK","A","B","TRIGGER","GRIP"}};
    return keys[hand][row];
#endif
}

void EnsureTutorialControllerModels(bool steamvr) {
    static bool fallbackPublished=false;
    static double lastUpdate=-1;
    if(!fallbackPublished) fallbackPublished=PublishFallbackTutorialControllers();
#if defined(__ANDROID__)
    PublishQuestControllers();
#endif
#if defined(_WIN32)
    if(steamvr && lastUpdate!=ImGui::GetTime()) {
        lastUpdate=ImGui::GetTime();
        PublishSteamVrTutorialControllers();
    }
#else
    (void)steamvr;
#endif
}
}

void DrawControllerGuide(const TutorialInput& input,bool cockpit) {
    EnsureTutorialControllerModels(input.steamvr);
    AuroraVRUiGuide guide{};guide.active=true;
    const auto* viewport=ImGui::GetMainViewport();
    const bool swap=RuntimeConfigFile::Get().vrButtonMapping.swapItemTrick;
    const bool swapDrift=RuntimeConfigFile::Get().vrButtonMapping.swapCockpitDriftBrake;
    const auto area=ImGui::GetContentRegionAvail();
    const float column=area.x*.5f;
    const auto top=ImGui::GetCursorScreenPos();
    guide.headerEnd=(top.y-viewport->Pos.y)/viewport->Size.y;
    const float graphicHeight=std::min(360.0f,std::max(220.0f,area.y-130));
    auto* draw=ImGui::GetWindowDrawList();
    for(int hand=0;hand<2;++hand) {
        const float left=top.x+column*hand;
        ImVec2 center{left+column*.24f,top.y+graphicHeight*.45f};
        // The model outline and button callouts follow tracked hand motion in
        // a bounded teaching panel, keeping every label within reading reach.
        const auto& pose=input.ui_hands[hand];
        if(pose.valid) {
            const auto anchor=mkw::vr::ControllerCalloutAnchor(pose,hand?1.0f:-1.0f);
            center.x+=std::clamp(anchor[0]*55,-24.0f,24.0f);
            center.y-=std::clamp(anchor[1]*55,-24.0f,24.0f);
        }
        const ImU32 accent=hand?IM_COL32(245,190,80,255):IM_COL32(40,225,210,255);
        draw->AddText({left+12,top.y},accent,hand?"RIGHT CONTROLLER":"LEFT CONTROLLER");
        draw->AddCircleFilled(center,42,IM_COL32(54,65,82,255),32);
        draw->AddRectFilled({center.x-23,center.y+16},{center.x+22,center.y+117},IM_COL32(54,65,82,255),18);
        const auto label=[&](ImVec2 button,float row,const char* key,const char* action,bool pressed) {
            const ImVec2 text{left+column*.43f,top.y+24+row*38};
            auto* uv=guide.labels[hand*5+int(row)];
            uv[0]=(text.x-viewport->Pos.x)/viewport->Size.x;
            uv[1]=(text.y-viewport->Pos.y)/viewport->Size.y;
            uv[2]=(left+column-4-viewport->Pos.x)/viewport->Size.x;
            uv[3]=(text.y+35-viewport->Pos.y)/viewport->Size.y;
            draw->AddCircleFilled(button,pressed?9:6,pressed?IM_COL32(255,255,255,255):accent);
            draw->AddLine(button,{text.x-6,text.y+7},accent,1.5f);
            draw->AddText(text,accent,key);
            draw->AddText({text.x,text.y+18},IM_COL32(240,240,240,255),action);
        };
        if(!hand) {
            label({center.x-15,center.y-16},0,ControllerKey(hand,0),"Steer / navigate",std::abs(input.steering_x)>.3f);
            label({center.x+13,center.y+10},1,ControllerKey(hand,1),input.remote?"Wii Minus":swap?"Use / hold item":"Trick / wheelie",input.trick);
            label({center.x+21,center.y-12},2,ControllerKey(hand,2),input.remote?"Wii Remote Y":swap?"Trick / wheelie":"Use / hold item",input.item>.5f);
            label({center.x,center.y-38},3,"TRIGGER",input.remote?"Use / hold item":"Brake / reverse",input.reverse);
            label({center.x-23,center.y+57},4,"GRIP",cockpit?"Hold wheel":"Not used",input.ui_grips[0]>.5f);
        } else {
            label({center.x-15,center.y-16},0,ControllerKey(hand,0),"Change camera",false);
            label({center.x+13,center.y+10},1,ControllerKey(hand,1),cockpit&&!input.remote?(swapDrift?"Brake / confirm":"Drift / confirm"):"Accelerate / confirm",input.confirm);
            label({center.x+21,center.y-12},2,ControllerKey(hand,2),input.remote?"Look behind":cockpit&&swapDrift?"Drift / back":"Brake / back",input.brake);
            label({center.x,center.y-38},3,"TRIGGER",input.remote?"Hop / drift":"Accelerate",input.accelerate>.5f);
            label({center.x-23,center.y+57},4,"GRIP",cockpit?"Hold wheel":"Hop / drift",input.ui_grips[1]>.5f);
        }
    }
    ImGui::Dummy(ImVec2(area.x,graphicHeight));
    guide.footerStart=(ImGui::GetCursorScreenPos().y-viewport->Pos.y)/viewport->Size.y;
    aurora_set_vr_ui_guide(&guide);
    ImGui::TextWrapped("Raise the right controller for motion tricks (Wii Remote mode). Button actions follow F10 > Controllers. %s / %s are the left face buttons.",ControllerKey(0,1),ControllerKey(0,2));
    ImGui::TextWrapped("X + Y: VR options. SteamVR: tap X for tricks, hold X 0.65 seconds to pause. Other runtimes: Menu pauses.");
    if(cockpit) ImGui::TextWrapped("Hold either grip near the wheel. Release both grips to steer with the stick. The wheel keeps its centre beyond full lock.");
    if(!input.ui_hands[0].valid || !input.ui_hands[1].valid) ImGui::TextDisabled("Raise your controllers to see live button highlights.");
}


bool MarioKartPaused() {
    auto* cpu=TryGetCpuContext();
    if (!cpu) return false;
    CpuContext probe=*cpu;
    try {
        CpuContextScope scope(&probe);
        func_80554E14(&probe);
        return probe.gpr[3]!=0;
    } catch (const Memory::AccessViolation&) { return false; }
}
void UpdateIntroduction() {
    g_introductionKind=0;
    if (!mkw::vr::OpenXRIsRunning()) {
        g_settingsPause={};
        mkw::vr::OpenXRSetIntroductionActive(false);
        mkw::vr::MkwVRPolicySetSettingsVisible(mkw::vr::OpenXRSettingsPanelOpen());
        return;
    }
    EnsureTutorialControllerModels(true);
    const auto settingsPolicy=mkw::vr::MkwVRPolicyGetSnapshot();
    const bool settingsRace=settingsPolicy.scene.mode==mkw::vr::VRSceneMode::Race &&
        settingsPolicy.camera.valid && !mkw::vr::MkwVRRaceIntroActive();
    if(g_settingsPause.Update(settingsRace,mkw::vr::OpenXRSettingsPanelOpen(),
                             settingsRace && MarioKartPaused(),ImGui::GetTime()))
        mkw::vr::OpenXRRequestTutorialPause();
    if (!RuntimeConfigFile::Get().vrWelcomeComplete) {
        g_introductionKind=1;
    } else {
        const auto policy=mkw::vr::MkwVRPolicyGetSnapshot();
        const bool race=policy.scene.mode==mkw::vr::VRSceneMode::Race && policy.scene.local_player_count==1 &&
            policy.camera.valid && !mkw::vr::MkwVRRaceIntroActive();
        const bool cockpit=mkw::vr::MkwVRGetCameraMode()==mkw::vr::CameraMode::FirstPerson;
        const bool paused=race && MarioKartPaused();
        if ((!mkw::vr::OpenXRSettingsPanelOpen() || g_tutorial.stage==mkw::vr::TutorialFlow::Stage::Showing) &&
            g_tutorial.Update(race,cockpit,paused,RuntimeConfigFile::Get().vrTutorialCompleted,ImGui::GetTime()))
            mkw::vr::OpenXRRequestTutorialPause();
        if (g_tutorial.stage==mkw::vr::TutorialFlow::Stage::Showing)
            g_introductionKind=cockpit?3:2;
    }
    mkw::vr::OpenXRSetIntroductionActive(g_introductionKind!=0);
    mkw::vr::MkwVRPolicySetSettingsVisible(g_introductionKind!=0 || mkw::vr::OpenXRSettingsPanelOpen());
}
void DrawIntroductionWindow() {
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
    if (ImGui::Begin("VR introduction",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings)) {
        if (g_introductionKind==1) {
            ImGui::TextUnformatted("Welcome to Mario Kart Wii VR");ImGui::Separator();
            ImGui::TextWrapped("Choose your default race camera. Aim either controller and pull its trigger to select. You can change this later in VR settings.");
            int choice=RuntimeConfigFile::Get().vrDefaultCamera;
            const char* titles[]{"Original / third person","First person","Diorama"};
            const char* descriptions[]{"Follow the kart with the original camera. See your driver and steer with the stick.",
                "Sit in the driver's seat. Grab the steering wheel or handlebar with your grips and steer with your hands.",
                "Watch the race from an elevated miniature-world view. Adjust distance, height and world scale in VR settings."};
            for(int mode=0;mode<3;++mode) {
                if(ImGui::RadioButton(titles[mode],choice==mode)) RuntimeConfigFile::Mutable().vrDefaultCamera=mode;
                ImGui::Indent(32);ImGui::TextWrapped("%s",descriptions[mode]);ImGui::Unindent(32);ImGui::Spacing();
            }
            ImGui::TextWrapped("Click the right stick to cycle all three cameras. A guide appears in your first race, then again when you first try the other driving mode.");
            if(ImGui::Button("Save camera and start game",ImVec2(-1,64))) {
                choice=RuntimeConfigFile::Get().vrDefaultCamera;
                if(RuntimeConfigFile::WriteSetting("vr","default_camera",std::to_string(choice)) &&
                   RuntimeConfigFile::WriteSetting("vr","welcome_complete","true")) {
                    RuntimeConfigFile::Mutable().vrWelcomeComplete=true;
                    mkw::vr::MkwVRSetCameraMode(static_cast<mkw::vr::CameraMode>(choice));
                    mkw::vr::OpenXRSetSettingsPanelOpen(false);
                    mkw::vr::OpenXRSetIntroductionActive(false);
                    g_introductionKind=0;g_onboardingError.clear();
                } else g_onboardingError="Could not save your choice. Check the configuration folder permissions.";
            }
        } else {
            ImGui::TextUnformatted(g_introductionKind==3?"First-person driving controls":"Third-person and Diorama controls");
            ImGui::Separator();
            ImGui::TextWrapped("Mario Kart is paused. Point at Continue and pull either trigger when ready.");
            DrawControllerGuide(TutorialControls(),g_introductionKind==3);
            if(ImGui::Button("Continue racing",ImVec2(-1,64))) {
                const unsigned completed=RuntimeConfigFile::Get().vrTutorialCompleted|g_tutorial.bit;
                if(RuntimeConfigFile::WriteSetting("vr","tutorial_completed",std::to_string(completed))) {
                    RuntimeConfigFile::Mutable().vrTutorialCompleted=completed;
                    mkw::vr::OpenXRSetSettingsPanelOpen(false);
                    mkw::vr::OpenXRSetIntroductionActive(false);
                    g_tutorial={};g_introductionKind=0;g_onboardingError.clear();
                    mkw::vr::OpenXRRequestTutorialPause();
                } else g_onboardingError="Could not save tutorial progress. Check the configuration folder permissions.";
            }
        }
        if(!g_onboardingError.empty()) ImGui::TextWrapped("%s",g_onboardingError.c_str());
    }
    ImGui::End();
    if(g_introductionKind==0) {
        mkw::vr::OpenXRSetIntroductionActive(false);
        mkw::vr::MkwVRPolicySetSettingsVisible(mkw::vr::OpenXRSettingsPanelOpen());
        aurora_set_vr_ui_guide(nullptr);
    }
}
