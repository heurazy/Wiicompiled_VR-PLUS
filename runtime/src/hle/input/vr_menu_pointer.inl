// Extend the native GameCube-backed VR controller's UI state, leaving its
// racing state and Mario Kart's own button hit testing untouched.
// PAL layout verified against UpdateImpl (8051FC84) and UIState::Reset;
// controller/UI action offsets documented by Seeky/SwareJonge/_tZ in
// https://github.com/MelgMKW/Pulsar/blob/main/GameSource/MarioKartWii/Input/Controller.hpp
#include "vr/game_menu_pointer.h"
#include "vr/openxr_settings_panel.h"
#include "vr/mkw_vr_policy.h"
#include <aurora/aurora.h>

extern "C" void func_805201B0(CpuContext*);
namespace {
bool VrGameMenuActive() {
    const auto policy=mkw::vr::MkwVRPolicyGetSnapshot();
    return policy.session_active && !policy.settings_visible &&
        policy.presentation==mkw::vr::VRPresentationMode::VirtualScreen;
}
void VrMenuControllerUpdate(CpuContext* ctx) {
    const uint32_t controller=ctx->gpr[3],ui=ctx->gpr[5];
    func_805201B0(ctx); // Call the original body, not registry dispatch.
    if(!Memory::Contains(controller+0x9f) || !Memory::Contains(ui+0x33)) return;
    if(Memory::Read32(controller+0x9c)!=0) return; // VR owns only player one.
    static mkw::vr::GameMenuPointer pointer;
    static bool owned=false;
    const auto input=mkw::vr::OpenXRReadPortControls();
    const auto policy=mkw::vr::MkwVRPolicyGetSnapshot();
    const bool enabled=VrGameMenuActive() && input.active;
    const auto hit=pointer.Update(input,enabled,policy.config.hud_width_meters,
        policy.config.hud_width_meters/input.ui_aspect,policy.config.hud_distance_meters);
    if(!enabled && !owned) return;
    owned=enabled;
    auto flags=Memory::Read8(ui+0x30);
    Memory::Write8(ui+0x30,(flags&~0x40)|(hit.valid?0x40:0));
    if(hit.valid) {
        Memory::WriteFloat32(ui+0x1c,hit.x);Memory::WriteFloat32(ui+0x20,hit.y);
        Memory::WriteFloat32(ui+0x24,1);Memory::WriteFloat32(ui+0x28,0);
        Memory::WriteFloat32(ui+0x2c,1);
    }
    if(enabled) {
        // The left trigger normally maps to reverse/B. Here either trigger
        // validates only its ray target; physical A/B and sticks still work.
        auto actions=Memory::Read16(ui+4);
        actions &= ~uint16_t(3);
        if(input.confirm || hit.down) actions|=1;
        if(input.brake) actions|=2;
        Memory::Write16(ui+4,actions);
    }
}
uint32_t VrMenuPointerEnabled(uint32_t controller) {
    if(!VrGameMenuActive() || !mkw::vr::OpenXRReadPortControls().active ||
       !Memory::Contains(controller+0x9f)) return 0;
    return Memory::Read32(controller)==0x808B2E48u && Memory::Read32(controller+0x9c)==0;
}
void VrMenuControllerType(CpuContext* ctx) {
    // The native pointer hit-test explicitly permits only Wii core/Nunchuk.
    // Advertise pointer capability only to those two checks; racing, saves,
    // controller assignment and all other callers retain the GameCube type.
    const bool pointerCheck=ctx->lr==0x805F2020u || ctx->lr==0x805F203Cu;
    ctx->gpr[3]=pointerCheck && VrMenuPointerEnabled(ctx->gpr[3])?1:3;
}
}
