// Textured Touch Plus assets supplied through WebXR Input Profiles (MIT).
// Preprocessed offline; no GLB parsing, image decoding or networking on the render thread.
struct QuestControllerRecord { float position[3],color[3],uv[2],delta[9][3]; };
struct QuestControllerAsset {
    std::vector<QuestControllerRecord> records;
    TutorialModel vertices;
    std::array<float,15> anchors{};
};
std::array<QuestControllerAsset,2> g_questControllerAssets;
bool PublishQuestControllers() {
    static bool attempted=false,ready=false;
    if(!attempted) {
        attempted=true;
#if defined(__ANDROID__)
        const char* resources=std::getenv("MKW_ANDROID_RESOURCES_DIR");
        if(!resources) return false;
#else
        const char* base=SDL_GetBasePath();
        if(!base) return false;
        const std::string resources=std::string(base)+"resources";
#endif
        ready=true;
        for(uint32_t hand=0;hand<2;++hand) {
            auto& asset=g_questControllerAssets[hand];
            const std::string prefix=std::string(resources)+"/quest_touch_plus/"+(hand?"right":"left");
            std::ifstream model(prefix+".wccontroller",std::ios::binary);
            char magic[4]{};uint32_t count=0,deltas=0;
            model.read(magic,4);model.read(reinterpret_cast<char*>(&count),4);model.read(reinterpret_cast<char*>(&deltas),4);
            if(std::string_view(magic,4)!="WCC1" || !count || count>100000 || count%3 || deltas!=9) {ready=false;break;}
            model.read(reinterpret_cast<char*>(asset.anchors.data()),sizeof(asset.anchors));
            asset.records.resize(count);asset.vertices.resize(count);
            model.read(reinterpret_cast<char*>(asset.records.data()),count*sizeof(QuestControllerRecord));
            if(!model) {ready=false;break;}
            std::ifstream texture(prefix+".rgba",std::ios::binary);
            uint32_t width=0,height=0;
            texture.read(reinterpret_cast<char*>(&width),4);texture.read(reinterpret_cast<char*>(&height),4);
            if(!width || !height || width>4096 || height>4096) {ready=false;break;}
            std::vector<uint8_t> rgba(size_t(width)*height*4);
            texture.read(reinterpret_cast<char*>(rgba.data()),rgba.size());
            if(!texture) {ready=false;break;}
            aurora_set_vr_controller_texture(0x51530000+hand,width,height,rgba.data());
            aurora_set_vr_controller_anchors(hand,asset.anchors.data());
            std::fprintf(stderr,"[vr-menu] Loaded Touch Plus %s: %u textured vertices\n",hand?"right":"left",count);
        }
    }
    if(!ready) return false;
    const auto input=mkw::vr::OpenXRReadUiSnapshot();
    for(uint32_t hand=0;hand<2;++hand) {
        auto& asset=g_questControllerAssets[hand];const auto& buttons=input.buttons[hand];
        const float values[9]={float(buttons.thumbstick_click),float(buttons.primary),float(buttons.secondary),
            buttons.trigger,buttons.squeeze,std::max(0.f,-buttons.stick_x),std::max(0.f,buttons.stick_x),
            std::max(0.f,-buttons.stick_y),std::max(0.f,buttons.stick_y)};
        for(size_t i=0;i<asset.records.size();++i) {
            const auto& source=asset.records[i];auto& vertex=asset.vertices[i];
            for(int axis=0;axis<3;++axis) {
                vertex.position[axis]=source.position[axis];vertex.color[axis]=source.color[axis];
                for(int control=0;control<9;++control) vertex.position[axis]+=source.delta[control][axis]*std::clamp(values[control],0.f,1.f);
            }
            std::copy_n(source.uv,2,vertex.uv);vertex.material=0x51530000+hand;
        }
        aurora_set_vr_controller_model(hand,asset.vertices.data(),static_cast<uint32_t>(asset.vertices.size()));
    }
    return true;
}
