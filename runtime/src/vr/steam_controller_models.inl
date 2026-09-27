// SteamVR supplies the mesh, material and animated transform for each component.
// Cache geometry; only component transforms and button landmarks change per frame.
struct MenuControllerComponent {
    std::string name, modelName;
    TutorialModel vertices;
    bool ready=false;
};
struct MenuControllerModel {
    std::string name;
    std::vector<MenuControllerComponent> parts;
};
std::array<MenuControllerModel,2> g_menuControllers;
std::array<std::array<std::string,5>,2> g_controllerKeys{{
    {"STICK","X","Y","TRIGGER","GRIP"},
    {"STICK CLICK","A","B","TRIGGER","GRIP"}}};

bool UpdateSteamControllerModels(VR_IVRSystem_FnTable* system,VR_IVRRenderModels_FnTable* render,VR_IVRInput_FnTable* input) {
    if(!system || !render) return false;
    mkw::vr::PhysicalOptionsButtons physical{};
    bool published=false;
    for(int hand=0;hand<2;++hand) {
        const auto device=system->GetTrackedDeviceIndexForControllerRole(hand?
            ETrackedControllerRole_TrackedControllerRole_RightHand:ETrackedControllerRole_TrackedControllerRole_LeftHand);
        if(device==k_unTrackedDeviceIndexInvalid) continue;
        char name[1024]{};ETrackedPropertyError error=ETrackedPropertyError_TrackedProp_Success;
        system->GetStringTrackedDeviceProperty(device,ETrackedDeviceProperty_Prop_RenderModelName_String,name,sizeof(name),&error);
        if(error!=ETrackedPropertyError_TrackedProp_Success || !name[0]) continue;
        auto& cached=g_menuControllers[hand];
        if(cached.name!=name) {
            cached={};cached.name=name;
            const auto count=std::min(128u,render->GetComponentCount(name));
            for(uint32_t i=0;i<count;++i) {
                char component[1024]{},mesh[1024]{};
                render->GetComponentName(name,i,component,sizeof(component));
                render->GetComponentRenderModelName(name,component,mesh,sizeof(mesh));
                if(component[0] && mesh[0]) cached.parts.push_back({component,mesh});
            }
            if(cached.parts.empty()) cached.parts.push_back({"",name});
            std::fprintf(stderr,"[vr-menu] Controller %d: %s (%zu animated components)\n",hand,name,cached.parts.size());
        }
        VRControllerState_t state{};
        const bool stateValid=system->GetControllerState(device,&state,sizeof(state));
        RenderModel_ControllerMode_State_t mode{};
        VRInputValueHandle_t devicePath=0;
        if(input) input->GetInputSourceHandle(const_cast<char*>(hand?"/user/hand/right":"/user/hand/left"),&devicePath);
        const auto component=[&](const char* key,RenderModel_ComponentState_t& out) {
            if(devicePath && render->GetComponentStateForDevicePath(name,const_cast<char*>(key),devicePath,&mode,&out)) return true;
            return render->GetComponentState(name,const_cast<char*>(key),&state,&mode,&out);
        };
        RenderModel_ComponentState_t grip{};
        // Neutral grip reference must not move when the squeeze button animates.
        VRControllerState_t neutral{};
        if(!render->GetComponentState(name,const_cast<char*>("openxr_grip"),&neutral,&mode,&grip) &&
           !render->GetComponentState(name,const_cast<char*>("grip"),&neutral,&mode,&grip)) continue;
        const auto inGrip=[&](const std::array<float,3>& p) {
            const auto& m=grip.mTrackingToComponentLocal.m;
            const float x=p[0]-m[0][3],y=p[1]-m[1][3],z=p[2]-m[2][3];
            return std::array<float,3>{m[0][0]*x+m[1][0]*y+m[2][0]*z,
                m[0][1]*x+m[1][1]*y+m[2][1]*z,m[0][2]*x+m[1][2]*y+m[2][2]*z};
        };
        // Match actual runtime landmarks, including Index's A/B on the left.
        const char* candidates[5][4]={{"thumbstick","joystick","trackpad",nullptr},
            {hand?"button_a":"button_x","button_a","a",nullptr},
            {hand?"button_b":"button_y","button_b","b",nullptr},
            {"trigger","button_trigger",nullptr,nullptr},
            {"button_grip","grip","handgrip",nullptr}};
        std::array<float,15> anchors{};
        for(int row=0;row<5;++row) {
            RenderModel_ComponentState_t button{};bool found=false;
            const char* foundKey=nullptr;
            for(const auto* key:candidates[row]) if(key && component(key,button)) {
                found=true;
                foundKey=key;
                if(row==0) g_controllerKeys[hand][row]=std::string(key)=="trackpad"?(hand?"PAD CLICK":"PAD"):(hand?"STICK CLICK":"STICK");
                if(row==1) g_controllerKeys[hand][row]=std::string(key)=="button_x"?"X":"A";
                if(row==2) g_controllerKeys[hand][row]=std::string(key)=="button_y"?"Y":"B";
                break;
            }
            if(hand==0 && (row==1 || row==2)) {
                const uint64_t mask=foundKey && render->GetComponentButtonMask ?
                    render->GetComponentButtonMask(name,const_cast<char*>(foundKey)) : 0;
                const bool pressed=(button.uProperties&EVRComponentProperty_VRComponentProperty_IsPressed)!=0 ||
                    (stateValid && mask && (state.ulButtonPressed&mask)!=0);
                if(row==1) { physical.valid=found;physical.x=pressed; }
                else { physical.valid=physical.valid && found;physical.y=pressed; }
            }
            // A missing optional landmark must not discard the entire controller.
            const auto& m=button.mTrackingToComponentLocal.m;
            const auto p=found?inGrip({m[0][3],m[1][3],m[2][3]}):std::array<float,3>{0,-.015f*row,0};
            std::copy(p.begin(),p.end(),anchors.begin()+row*3);
        }
        TutorialModel output;
        bool complete=true;
        for(auto& part:cached.parts) {
            if(!part.ready) {
                RenderModel_t* mesh=nullptr;
                auto status=render->LoadRenderModel_Async(part.modelName.data(),&mesh);
                if(status==EVRRenderModelError_VRRenderModelError_Loading) {complete=false;continue;}
                if(!mesh || status!=EVRRenderModelError_VRRenderModelError_None) {part.ready=true;continue;}
                RenderModel_TextureMap_t* texture=nullptr;
                const auto textureStatus=render->LoadTexture_Async(mesh->diffuseTextureId,&texture);
                if(textureStatus==EVRRenderModelError_VRRenderModelError_Loading) {
                    render->FreeRenderModel(mesh);complete=false;continue;
                }
                const bool rgba=texture && texture->rubTextureMapData && texture->unWidth && texture->unHeight &&
                    texture->format==EVRRenderModelTextureFormat_VRRenderModelTextureFormat_RGBA8_SRGB;
                const uint32_t material=rgba?uint32_t(mesh->diffuseTextureId)+1:0;
                if(rgba) aurora_set_vr_controller_texture(material,texture->unWidth,texture->unHeight,texture->rubTextureMapData);
                if(mesh->rVertexData && mesh->rIndexData && mesh->unTriangleCount<100000) {
                    part.vertices.reserve(size_t(mesh->unTriangleCount)*3);
                    // Preserve UVs: vertex-color baking loses small button markings.
                    const auto add=[&](const RenderModel_Vertex_t& v) {
                        const float shade=.8f+.2f*std::clamp(v.vNormal.v[1]*.5f+.5f,0.f,1.f);
                        auto vertex=TutorialModelVertex(v.vPosition.v[0],v.vPosition.v[1],v.vPosition.v[2],shade,shade,shade);
                        vertex.uv[0]=v.rfTextureCoord[0];vertex.uv[1]=v.rfTextureCoord[1];vertex.material=material;
                        part.vertices.push_back(vertex);
                    };
                    for(uint32_t i=0;i<mesh->unTriangleCount;++i) {
                        const auto* ix=mesh->rIndexData+3*i;
                        if(ix[0]>=mesh->unVertexCount || ix[1]>=mesh->unVertexCount || ix[2]>=mesh->unVertexCount) continue;
                        for(int j=0;j<3;++j) add(mesh->rVertexData[ix[j]]);
                    }
                }
                if(texture) render->FreeTexture(texture);
                render->FreeRenderModel(mesh);part.ready=true;
            }
            RenderModel_ComponentState_t pose{};
            if(!part.name.empty() && !component(part.name.c_str(),pose)) continue;
            if(!part.name.empty() && !(pose.uProperties&EVRComponentProperty_VRComponentProperty_IsVisible)) continue;
            const auto& m=pose.mTrackingToComponentRenderModel.m;
            for(const auto& v:part.vertices) {
                const auto* p=v.position;
                const auto position=inGrip(part.name.empty()?std::array<float,3>{p[0],p[1],p[2]}:
                    std::array<float,3>{m[0][0]*p[0]+m[0][1]*p[1]+m[0][2]*p[2]+m[0][3],
                    m[1][0]*p[0]+m[1][1]*p[1]+m[1][2]*p[2]+m[1][3],m[2][0]*p[0]+m[2][1]*p[1]+m[2][2]*p[2]+m[2][3]});
                auto transformed=v;
                std::copy(position.begin(),position.end(),transformed.position);
                output.push_back(transformed);
            }
        }
        if(complete && !output.empty()) {
            aurora_set_vr_controller_model(hand,output.data(),static_cast<uint32_t>(output.size()));
            aurora_set_vr_controller_anchors(hand,anchors.data());published=true;
        }
    }
    mkw::vr::OpenXRPublishPhysicalOptionsButtons(physical);
    return published;
}
