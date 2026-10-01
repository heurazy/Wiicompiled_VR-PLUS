// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside mkw::vr, after the cockpit/guest layout helpers.
namespace {
struct CharacterBone {
    uint32_t node=0, matrix=0;
    int parent=-1;
    std::string name;
};
struct CharacterRig {
    uint32_t driver=0, model=0;
    std::vector<CharacterBone> bones;
    int head=-1;
    std::array<std::array<int,3>,2> arms{{{{-1,-1,-1}},{{-1,-1,-1}}}};
    std::array<Mtx34,2> wrist_offset{};
    std::array<bool,2> calibrated{};
    std::vector<Mtx34> seated_pose;
    std::array<float,3> seated_scale{1,1,1};
    body_ik::LegAnimationGate leg_animation;
    std::string calibration_key;
};
CharacterRig g_character_rig;
unsigned g_body_logged_state=UINT32_MAX;
void ResetCharacterBodyLocked() {
    g_character_rig={};
    g_body_logged_state=UINT32_MAX;
    g_character_hands.store(0,std::memory_order_release);
}

// Preserve the entire palette, including envelope matrices overwritten by
// CalcSkinning. Only the derived view palettes survive this render transaction.
struct CharacterPalette {
    uint32_t address=0, attributes=0;
    std::vector<Mtx34> original;
    std::vector<uint32_t> original_attributes;
    std::vector<std::unique_ptr<CharacterPalette>> attachments;
    void Restore() noexcept {
        for(auto& attachment:attachments) attachment->Restore();
        try {
            for (size_t i=0;i<original.size();++i) {
                for (unsigned k=0;k<12;++k) Memory::WriteFloat32(address+uint32_t(i)*48+k*4,original[i][k]);
                Memory::Write32(attributes+uint32_t(i)*4,original_attributes[i]);
            }
        } catch (const Memory::AccessViolation&) {}
        original.clear();
    }
    ~CharacterPalette() { Restore(); }
};

bool ReadCharacterRig(uint32_t driver, uint32_t mdl, uint32_t palette_count) {
    if (g_character_rig.driver==driver && g_character_rig.model==mdl && !g_character_rig.bones.empty())
        return true;
    g_character_rig={};
    const uint32_t offset=Memory::Read32(mdl+0x14);
    if (!offset || offset>0x100000) return false;
    const uint32_t dictionary=mdl+offset;
    if (!Memory::Contains(dictionary,8)) return false;
    const uint32_t count=Memory::Read32(dictionary+4);
    if (!count || count>128 || !Memory::Contains(dictionary,8+(count+1)*16)) return false;
    CharacterRig rig;
    rig.driver=driver;rig.model=mdl;
    for (uint32_t i=1;i<=count;++i) {
        const uint32_t relative=Memory::Read32(dictionary+8+i*16+12);
        if (!relative || relative>0x100000) return false;
        const uint32_t node=dictionary+relative;
        if (!Memory::Contains(node,0xd0)) return false;
        const uint32_t matrix=Memory::Read32(node+0x10);
        if (matrix>=palette_count) return false;
        for(const auto& previous:rig.bones) if(previous.matrix==matrix) return false;
        const uint32_t name=node+Memory::Read32(node+8);
        rig.bones.push_back({node,matrix,-1,ReadGuestName(name,64)});
    }
    for (size_t i=0;i<rig.bones.size();++i) {
        auto& bone=rig.bones[i];
        const int32_t relative=static_cast<int32_t>(Memory::Read32(bone.node+0x5c));
        if (relative) {
            const uint32_t parent=bone.node+relative;
            for (size_t p=0;p<rig.bones.size();++p) if (rig.bones[p].node==parent) bone.parent=static_cast<int>(p);
            if (bone.parent<0) return false;
        }
        if (bone.name=="face_1" || bone.name=="head" || bone.name=="head1") rig.head=static_cast<int>(i);
        for (int hand=0;hand<2;++hand) {
            const char* names[2][3]={{"arm_l1","arm_l2","wrist_l1"},{"arm_r1","arm_r2","wrist_r1"}};
            for (int joint=0;joint<3;++joint) if(bone.name==names[hand][joint]) rig.arms[hand][joint]=static_cast<int>(i);
        }
    }
    for (auto arm:rig.arms) if (arm[0]<0 || arm[1]<0 || arm[2]<0 ||
            rig.bones[arm[1]].parent!=arm[0] || rig.bones[arm[2]].parent!=arm[1]) return false;
    const auto positions=mdl+Memory::Read32(mdl+0x18);
    if(!Memory::Contains(positions,40) || !Memory::Read32(positions+4))return false;
    const auto array=positions+Memory::Read32(positions+36);
    if(!Memory::Contains(array,16))return false;
    rig.calibration_key=hand_workshop::Key(ReadGuestName(array+Memory::Read32(array+0xc),128));
    if(rig.calibration_key.empty() || (rig.head<0 && rig.calibration_key!="king_teresa_body"))return false;
    g_character_rig=std::move(rig);
    RT_LOG(RT_TAG_RUNTIME) << "[mkw-vr] character body IK: rig ready, " << count << " bones" << std::endl;
    return true;
}

bool DescendsFrom(int bone,int ancestor) {
    for (size_t steps=0;steps<g_character_rig.bones.size() && bone>=0;++steps) {
        if (bone==ancestor) return true;
        bone=g_character_rig.bones[bone].parent;
    }
    return false;
}

// Visibility is decided before ScnRoot gathers its draw lists. Restoring only
// inside CalcView is too late to bring an already culled driver back.
bool CanShowCharacterBodyLocked() {
    if (!RuntimeConfigFile::VrBodyIk() || g_state.mode!=CameraMode::FirstPerson ||
        g_state.seat!=FirstPersonSeat::Cockpit || !g_state.cockpit.valid) return false;
    try {
        uint32_t driver=LocalDriver(g_state.player_kart.accessor), director=0,mdl=0,mii=0;
        if (!driver || !ReadGuestPointer(driver+kDriverModelOffset,director) ||
            !ReadGuestPointer(director+kModelDirectorResMdlOffset,mdl) || !Memory::Contains(mdl,0x90) ||
            Memory::Read32(mdl)!=kMdl0Magic) return false;
        // Miis have separate face models; do not expose their unmasked heads.
        if (ReadGuestPointer(driver+0x100,mii)) return false;
        const uint32_t revision=Memory::Read32(mdl+8);
        if (revision!=8 && revision!=9 && revision!=11) return false;
        const uint32_t info=mdl+(revision==11?0x4cu:0x40u);
        const uint32_t offset=Memory::Read32(info+0x24);
        if (!offset || offset>0x100000 || !Memory::Contains(info+offset,4)) return false;
        const uint32_t count=Memory::Read32(info+offset);
        return count>0 && count<=256 && ReadCharacterRig(driver,mdl,count);
    } catch(const Memory::AccessViolation&) { return false; }
}

unsigned PrepareCharacterBody(CpuContext* context, CharacterPalette& saved) {
    std::lock_guard lock(g_mutex);
    if (!RuntimeConfigFile::VrBodyIk() || !g_state.armed || g_state.opening_pending || g_state.bullet_active ||
        g_state.mode!=CameraMode::FirstPerson || g_state.seat!=FirstPersonSeat::Cockpit || !g_state.cockpit.valid)
        return 0;
    auto tracking=OpenXRReadDriving();
    const uint64_t now=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (!tracking.cockpit_active || tracking.vehicle_identity!=g_state.cockpit.body ||
        !tracking.published_ns || now<tracking.published_ns ||
        now-tracking.published_ns>250000000ull) tracking={};
    if (!CanShowCharacterBodyLocked()) return 0;
    const uint32_t driver=LocalDriver(g_state.player_kart.accessor);
    uint32_t director=0, mdl=0, ex=0, scn=0, palette=0, attributes=0;
    if (!driver || !ReadGuestPointer(driver+kDriverModelOffset,director) ||
        !ReadGuestPointer(director+kModelDirectorResMdlOffset,mdl) ||
        !ReadGuestPointer(director+kModelDirectorScnMdlExOffset,ex) || !ReadGuestPointer(ex,scn) ||
        !ReadGuestPointer(scn+kScnMdlWorldMtxArrayOffset,palette) ||
        !ReadGuestPointer(scn+0xf0,attributes) || !Memory::Contains(mdl,0x90) || Memory::Read32(mdl)!=kMdl0Magic)
        return 0;
    // Revisions 10/11 added header entries before ResMdlInfo.
    const uint32_t revision=Memory::Read32(mdl+8);
    if (revision!=8 && revision!=9 && revision!=11) return 0;
    const uint32_t info=mdl+(revision==11?0x4cu:0x40u);
    const uint32_t table_offset=Memory::Read32(info+0x24);
    if (table_offset>0x100000 || !Memory::Contains(info+table_offset,4)) return 0;
    const uint32_t count=Memory::Read32(info+table_offset);
    if (!count || count>256 || !Memory::Contains(palette,count*48) || !Memory::Contains(attributes,count*4) ||
        !ReadCharacterRig(driver,mdl,count)) return 0;
    Mtx34 view{}, pose{}, seat_to_world{};
    FirstPersonAnchor anchor{};
    const char* failed=nullptr;
    const auto kart=ReadPlayerKartPose(g_state.player_kart,pose);
    if (kart.failed_step || !ReadSceneViewMatrix(view)) return 0;
    ComputeCockpitAnchorLocked(view,kart,pose,anchor,failed);
    if (failed || !anchor.valid || !(anchor.units_per_meter>0) ||
        !InvertMtx(ComposeMtx(anchor.anchor_from_scene,view),seat_to_world)) return 0;
    saved.address=palette;saved.attributes=attributes;
    saved.original.resize(count);saved.original_attributes.resize(count);
    for (uint32_t i=0;i<count;++i) {
        if (!ReadGuestMtx34(palette+i*48,saved.original[i])) { saved.original.clear();return 0; }
        saved.original_attributes[i]=Memory::Read32(attributes+i*4);
    }
    auto modified=saved.original;
    // The avatar rides in the same comfort frame as the camera and wheel;
    // impact/trick animation must not throw the shoulders away from the hands.
    Mtx34 inverse_body{};
    if (!InvertMtx(g_state.cockpit.body_pose,inverse_body)) return 0;
    // Latch the seated skeleton once for this driver/model. Native drift and
    // character-specific trick animations must never overwrite its reference.
    // Store it in kart-local space so vehicle movement and lightning still work.
    const bool cached=g_character_rig.seated_pose.size()==count;
    if(!cached) {
        g_character_rig.seated_pose.resize(count);
        g_character_rig.seated_scale=g_state.cockpit.player_scale;
        for(const auto& bone:g_character_rig.bones)
            g_character_rig.seated_pose[bone.matrix]=ComposeMtx(inverse_body,modified[bone.matrix]);
    }
    for (const auto& bone:g_character_rig.bones) {
        auto local=g_character_rig.seated_pose[bone.matrix];
        for(int row=0;row<3;++row) {
            const float ratio=g_state.cockpit.player_scale[row]/g_character_rig.seated_scale[row];
            for(int col=0;col<4;++col) local[row*4+col]*=ratio;
        }
        modified[bone.matrix]=ComposeMtx(g_state.cockpit.seat_body,local);
    }
    // Restore only the leg branches' live articulation, relative to their
    // seated pelvis. Reading world-space leg palettes directly would import
    // the character's animated torso/root lean and jump into the comfort pose.
    uint32_t movement=0;
    const bool trick=ReadGuestPointer(kart.accessor+kKartAccessorMovementOffset,movement) && IsTrickAnimating(movement);
    // Movement::Boost at 0x108 stores its signed MT countdown at +4.
    // Other boost types have separate timers; none are changed here.
    const bool mini_turbo=movement && Memory::Contains(movement+0x10c,2) &&
        static_cast<int16_t>(Memory::Read16(movement+0x10c))>0;
    const bool drifting=movement && Memory::Contains(movement+0xfc,2) &&
        Memory::Read16(movement+0xfc)!=0;
    // The driver has two animation layers. Its dash/celebration can persist
    // beyond the MT countdown, or be selected before that countdown changes.
    // Do not infer leg safety from the boost or drift state alone.
    const bool driving_animation=Memory::Contains(driver+0xf4,8) &&
        body_ik::IsDrivingLegAnimation(Memory::Read16(driver+0xf4)) &&
        body_ik::IsDrivingLegAnimation(Memory::Read16(driver+0xfa));
    if(g_character_rig.leg_animation.AllowNative(trick,mini_turbo,drifting,now,!driving_animation)) {
        const auto seated=modified;
        for(size_t root=0;root<g_character_rig.bones.size();++root) {
            const auto& leg=g_character_rig.bones[root];
            if(leg.name!="leg_l1" && leg.name!="leg_r1") continue;
            if(leg.parent<0) continue;
            const auto parent=g_character_rig.bones[leg.parent].matrix;
            for(size_t i=0;i<g_character_rig.bones.size();++i) {
                if(!DescendsFrom(static_cast<int>(i),static_cast<int>(root))) continue;
                const auto matrix=g_character_rig.bones[i].matrix;
                body_ik::AnimateSeatedLimb(seated[parent],saved.original[parent],
                                         saved.original[matrix],modified[matrix]);
            }
        }
    }
    // Keep the native hip/leg placement in the vehicle. Camera reach fitting
    // must not translate the entire character out of its authored seat.
    int spine=-1;
    for(size_t i=0;i<g_character_rig.bones.size();++i)
        if(g_character_rig.bones[i].name=="spin") spine=static_cast<int>(i);
    if (spine>=0) {
        Mtx34 world_to_seat{};
        if (!InvertMtx(seat_to_world,world_to_seat)) return 0;
        const auto left=body_ik::Position(modified[g_character_rig.bones[g_character_rig.arms[0][0]].matrix]);
        const auto right=body_ik::Position(modified[g_character_rig.bones[g_character_rig.arms[1][0]].matrix]);
        const auto l=detail::TransformPoint(world_to_seat,left.x,left.y,left.z);
        const auto r=detail::TransformPoint(world_to_seat,right.x,right.y,right.z);
        const float clearance=std::abs(l.x-r.x)*0.55f+0.10f*anchor.units_per_meter;
        const float back=std::max(0.0f,clearance-(l.z+r.z)*0.5f);
        const auto pivot=body_ik::Position(modified[g_character_rig.bones[spine].matrix]);
        const auto seated=detail::TransformPoint(world_to_seat,pivot.x,pivot.y,pivot.z);
        const float lean=std::clamp(back/std::max(-seated.y,0.35f*anchor.units_per_meter),0.0f,0.4f);
        const auto bend=body_ik::Align(pivot,detail::TransformDirection(seat_to_world,{0,1,0}),
                                     detail::TransformDirection(seat_to_world,{0,1,lean}));
        for(size_t i=0;i<g_character_rig.bones.size();++i) if(DescendsFrom(static_cast<int>(i),spine)) {
            const uint32_t matrix=g_character_rig.bones[i].matrix;
            modified[matrix]=ComposeMtx(bend,modified[matrix]);
        }
    }
    // HMD lean bends the chest about the seated spine, while hips and feet keep
    // their seated placement. Limit lean rather than stretching the torso.
    if (tracking.head_tracked && detail::IsFiniteMtx34(tracking.seat_from_head)) {
        if (spine>=0) {
            const auto& h=tracking.seat_from_head;
            const float x=std::clamp(h[3],-0.25f,0.25f), z=std::clamp(h[11],-0.25f,0.25f);
            const auto up=detail::TransformDirection(seat_to_world,{0,1,0});
            const auto leaning=detail::TransformDirection(seat_to_world,{x*0.8f,1,z*0.8f});
            const auto bend=body_ik::Align(body_ik::Position(modified[g_character_rig.bones[spine].matrix]),up,leaning);
            for(size_t i=0;i<g_character_rig.bones.size();++i) if(DescendsFrom(static_cast<int>(i),spine)) {
                const uint32_t matrix=g_character_rig.bones[i].matrix;
                modified[matrix]=ComposeMtx(bend,modified[matrix]);
            }
        }
    }
    const auto rest_pose=modified;
    const auto calibration=OpenXRReadBodyHandCalibration();
    const bool calibrating=calibration.active && calibration.vehicle==g_state.cockpit.body &&
                           calibration.model==g_character_rig.calibration_key;
    const auto& offsets=RuntimeConfigFile::Get().vrBodyHandCalibrations;
    const auto calibrated_offset=offsets.find(g_character_rig.calibration_key);
    const bool custom_offset=calibrated_offset!=offsets.end() && calibrated_offset->second.size()==24 &&
        std::all_of(calibrated_offset->second.begin(),calibrated_offset->second.end(),
            [](float value){return detail::IsFiniteFloat(&value) && std::abs(value)<=1.05f;});
    unsigned hands=0;
    for (int hand=0;hand<2;++hand) {
        if (!tracking.hands[hand].tracked) { g_character_rig.calibrated[hand]=false;continue; }
        const auto indices=g_character_rig.arms[hand];
        const auto& bones=g_character_rig.bones;
        const Mtx34& shoulder=modified[bones[indices[0]].matrix];
        const Mtx34& elbow=modified[bones[indices[1]].matrix];
        const Mtx34& wrist=modified[bones[indices[2]].matrix];
        auto grip=tracking.hands[hand].seat_from_grip;
        for (int row=0;row<3;++row) grip[row*4+3]*=anchor.units_per_meter;
        Mtx34 target=ComposeMtx(seat_to_world,grip);
        if (!detail::IsFiniteMtx34(target)) continue;
        // Mirror the palm centring correction in controller space: using seat
        // X instead would shift sideways incorrectly when the hand is turned.
        auto lateral=detail::TransformDirection(target,{1,0,0});
        if(!custom_offset && detail::Normalize(lateral)) {
            const auto offset=body_ik::Mul(lateral,(hand==0?-1.0f:1.0f)*0.015f*anchor.units_per_meter);
            target[3]+=offset.x;target[7]+=offset.y;target[11]+=offset.z;
        }
        const auto pole=detail::TransformDirection(seat_to_world,{hand==0?-0.7f:0.7f,-1.0f,0.25f});
        // The cartoon's short arms need fitting to a human reach. Fit only their
        // length (never the torso or hand width), with bounded scaling and enough reach for the current target.
        const auto upper_segment=body_ik::Sub(body_ik::Position(elbow),body_ik::Position(shoulder));
        const auto lower_segment=body_ik::Sub(body_ik::Position(wrist),body_ik::Position(elbow));
        const float natural=body_ik::Length(upper_segment)+body_ik::Length(lower_segment);
        if(natural<0.001f) continue;
        const float requested=body_ik::Length(body_ik::Sub(body_ik::Position(target),body_ik::Position(shoulder)));
        const float palm_distance=0.055f*anchor.units_per_meter;
        // Use the calibrated wrist, not the palm, for reach fitting. Otherwise
        // large gloves can unnecessarily lengthen and fold the whole arm.
        auto reach_target=body_ik::Position(target);
        if(custom_offset) {
            Mtx34 local{};
            std::copy_n(calibrated_offset->second.begin()+hand*12,12,local.begin());
            for(int row=0;row<3;++row) local[row*4+3]*=anchor.units_per_meter;
            reach_target=body_ik::Position(ComposeMtx(target,local));
        }
        const float reach=custom_offset ? body_ik::Length(body_ik::Sub(reach_target,body_ik::Position(shoulder)))
                                       : requested+palm_distance;
        const float fit=body_ik::FitArmReach(natural,reach,anchor.units_per_meter);
        // Native cartoon sleeves are very thick at headset viewing distances.
        // Thin only the perpendicular cross section; keep joints and hands fixed.
        constexpr float arm_cross_section=0.65f;
        const auto fitted_elbow=body_ik::Add(body_ik::Position(shoulder),body_ik::Mul(upper_segment,fit));
        const auto fitted_wrist=body_ik::Add(fitted_elbow,body_ik::Mul(lower_segment,fit));
        auto solved=body_ik::Solve(body_ik::Position(shoulder),fitted_elbow,fitted_wrist,
                                       body_ik::Position(target),pole);
        if (!solved.valid) continue;
        Mtx34 upper=body_ik::MapSegment(body_ik::Position(shoulder),body_ik::Position(shoulder),upper_segment,
            body_ik::Sub(solved.elbow,body_ik::Position(shoulder)),arm_cross_section);
        Mtx34 lower=body_ik::MapSegment(body_ik::Position(elbow),solved.elbow,lower_segment,
                                      body_ik::Sub(solved.wrist,solved.elbow),arm_cross_section);
        Mtx34 hand_pose=ComposeMtx(lower,wrist);
        Mtx34 rotation=target;
        rotation[3]=rotation[7]=rotation[11]=0;
        if (!g_character_rig.calibrated[hand]) {
            Mtx34 inverse{};
            if (!InvertMtx(rotation,inverse)) continue;
            auto rest=hand_pose;rest[3]=rest[7]=rest[11]=0;
            for(int column=0;column<3;++column) {
                const float length=body_ik::Length({rest[column],rest[4+column],rest[8+column]});
                if (length>0.0001f) for(int row=0;row<3;++row) rest[row*4+column]/=length;
            }
            g_character_rig.wrist_offset[hand]=ComposeMtx(inverse,rest);
            g_character_rig.calibrated[hand]=true;
        }
        hand_pose=ComposeMtx(rotation,g_character_rig.wrist_offset[hand]);
        for(int column=0;column<3;++column) {
            const float length=body_ik::Length({wrist[column],wrist[4+column],wrist[8+column]});
            for(int row=0;row<3;++row) hand_pose[row*4+column]*=length;
        }
        Mtx34 inverse_wrist{};
        if (!InvertMtx(wrist,inverse_wrist)) continue;
        const auto palm_axis=detail::TransformDirection(inverse_wrist,lower_segment);
        auto wrist_target=body_ik::WristForPalm(body_ik::Position(target),hand_pose,palm_axis,palm_distance);
        if(custom_offset) {
            const auto& v=calibrated_offset->second;
            Mtx34 local{};std::copy_n(v.begin()+hand*12,12,local.begin());
            for(int row=0;row<3;++row) local[row*4+3]*=anchor.units_per_meter;
            hand_pose=ComposeMtx(target,local);
            wrist_target=body_ik::Position(hand_pose);
            for(int column=0;column<3;++column) {
                const float length=body_ik::Length({wrist[column],wrist[4+column],wrist[8+column]});
                for(int row=0;row<3;++row) hand_pose[row*4+column]*=length;
            }
        }
        if(calibrating && calibration.valid[hand]) {
            auto frozen=calibration.seat_from_wrist[hand];
            for(int row=0;row<3;++row) frozen[row*4+3]*=anchor.units_per_meter;
            hand_pose=ComposeMtx(seat_to_world,frozen);
            wrist_target=body_ik::Position(hand_pose);
            for(int column=0;column<3;++column) {
                const float length=body_ik::Length({wrist[column],wrist[4+column],wrist[8+column]});
                for(int row=0;row<3;++row) hand_pose[row*4+column]*=length;
            }
        }
        solved=body_ik::Solve(body_ik::Position(shoulder),fitted_elbow,fitted_wrist,wrist_target,pole);
        if(!solved.valid) continue;
        upper=body_ik::MapSegment(body_ik::Position(shoulder),body_ik::Position(shoulder),upper_segment,
                                 body_ik::Sub(solved.elbow,body_ik::Position(shoulder)),arm_cross_section);
        lower=body_ik::MapSegment(body_ik::Position(elbow),solved.elbow,lower_segment,
                                 body_ik::Sub(solved.wrist,solved.elbow),arm_cross_section);
        hand_pose[3]=solved.wrist.x;hand_pose[7]=solved.wrist.y;hand_pose[11]=solved.wrist.z;
        Mtx34 world_to_seat{};
        if(InvertMtx(seat_to_world,world_to_seat)) {
            auto reference=ComposeMtx(world_to_seat,hand_pose);
            for(int row=0;row<3;++row) reference[row*4+3]/=anchor.units_per_meter;
            for(int column=0;column<3;++column) {
                const float length=body_ik::Length({reference[column],reference[4+column],reference[8+column]});
                if(length>0.0001f) for(int row=0;row<3;++row) reference[row*4+column]/=length;
            }
            OpenXRPublishBodyHandPose(g_state.cockpit.body,g_character_rig.calibration_key,hand,reference);
        }
        const Mtx34 hand_transform=ComposeMtx(hand_pose,inverse_wrist);
        for (size_t i=0;i<bones.size();++i) {
            const uint32_t matrix=bones[i].matrix;
            if (DescendsFrom(static_cast<int>(i),indices[2])) modified[matrix]=ComposeMtx(hand_transform,rest_pose[matrix]);
            else if (DescendsFrom(static_cast<int>(i),indices[1])) modified[matrix]=ComposeMtx(lower,rest_pose[matrix]);
            else if (DescendsFrom(static_cast<int>(i),indices[0])) modified[matrix]=ComposeMtx(upper,rest_pose[matrix]);
        }
        if(g_character_rig.head<0) {
            const uint32_t matrix=bones[indices[1]].matrix;
            modified[matrix]=ComposeMtx(hand_transform,rest_pose[matrix]);
        }
        hands|=1u<<hand;
    }
    // Smaller cartoon hands, without moving the tracked wrist
    // or shortening reach. Apply after posing, also when tracking is absent.
    constexpr float limb_visual_scale=0.5f;
    for(int hand=0;hand<2;++hand) {
        const auto indices=g_character_rig.arms[hand];
        const auto& bones=g_character_rig.bones;
        const auto wrist=body_ik::Position(modified[bones[indices[2]].matrix]);
        const Mtx34 smaller_hand{limb_visual_scale,0,0,wrist.x*(1-limb_visual_scale),
                                0,limb_visual_scale,0,wrist.y*(1-limb_visual_scale),
                                0,0,limb_visual_scale,wrist.z*(1-limb_visual_scale)};
        for(size_t i=0;i<bones.size();++i) {
            const uint32_t matrix=bones[i].matrix;
            if(DescendsFrom(static_cast<int>(i),indices[g_character_rig.head<0?1:2])) modified[matrix]=ComposeMtx(smaller_hand,modified[matrix]);
        }
    }
    // Collapse the face and all its children at its own pivot, before skinning.
    // A tiny invertible scale keeps NW4R normal-matrix calculations defined.
    Mtx34 hide=kIdentityMtx34;
    if(g_character_rig.head>=0) {
    const auto head=body_ik::Position(modified[g_character_rig.bones[g_character_rig.head].matrix]);
    hide={0.0001f,0,0,head.x*0.9999f,0,0.0001f,0,head.y*0.9999f,0,0,0.0001f,head.z*0.9999f};
    for (size_t i=0;i<g_character_rig.bones.size();++i) if (DescendsFrom(static_cast<int>(i),g_character_rig.head)) {
        const uint32_t matrix=g_character_rig.bones[i].matrix;
        modified[matrix]=ComposeMtx(hide,modified[matrix]);
    }
    }
    if (RuntimeConfigFile::VrBodyIkHandsOnly() || g_character_rig.head<0) {
        const auto body_pivot=body_ik::Position(modified[g_character_rig.bones.front().matrix]);
        for(size_t i=0;i<g_character_rig.bones.size();++i) {
            bool hand_bone=false;
            auto pivot=body_pivot;
            for(int hand=0;hand<2;++hand) {
                const auto arm=g_character_rig.arms[hand];
                if(DescendsFrom(static_cast<int>(i),arm[g_character_rig.head<0?1:2])) hand_bone=true;
                else if(DescendsFrom(static_cast<int>(i),arm[0]))
                    pivot=body_ik::Position(modified[g_character_rig.bones[arm[2]].matrix]);
            }
            if(hand_bone) continue;
            // Collapse each hidden joint to its chosen pivot, not merely its
            // own origin: otherwise the old articulated arm remains visible.
            const uint32_t matrix=g_character_rig.bones[i].matrix;
            auto& m=modified[matrix];
            for(int row=0;row<3;++row) for(int col=0;col<3;++col) m[row*4+col]*=0.0001f;
            m[3]=pivot.x;m[7]=pivot.y;m[11]=pivot.z;
        }
    }
    for (uint32_t i=0;i<count;++i) for(unsigned k=0;k<12;++k)
        Memory::WriteFloat32(palette+i*48+k*4,modified[i][k]);
    uint32_t mix=0;
    if (ReadGuestPointer(scn+0x10c,mix)) {
        CpuContext skin=*context;
        // ResMdl is a C++ resource handle passed by address, not a raw MDL0.
        skin.gpr[3]=palette;skin.gpr[4]=attributes;skin.gpr[5]=scn+0xe8;skin.gpr[6]=mix;
        func_80067F70(&skin);
    }
    // Toadette's pigtails have a separate physics skeleton. Hiding the face
    // bone alone leaves those hanging across the eyes, so collapse only
    // validated hair attachments, never vehicle parts or unknown accessories.
    uint32_t attachment_count=0;
    if(Memory::TryRead32(driver+kModelHolderCountOffset,attachment_count) && attachment_count<=6) {
        for(uint32_t attachment=0;attachment<attachment_count;++attachment) {
            uint32_t part=0,part_mdl=0,part_ex=0,part_scn=0,part_palette=0,part_attributes=0;
            if(!ReadGuestPointer(driver+kModelHolderArrayOffset+attachment*4,part) || part==director ||
               !ReadGuestPointer(part+kModelDirectorResMdlOffset,part_mdl) ||
               !Memory::Contains(part_mdl,0x90) || Memory::Read32(part_mdl)!=kMdl0Magic ||
               Memory::Read32(part_mdl+8)!=11) continue;
            const auto name=ReadGuestName(part_mdl+Memory::Read32(part_mdl+0x48),64);
            if(name.find("hair")==std::string::npos) continue;
            if(!ReadGuestPointer(part+kModelDirectorScnMdlExOffset,part_ex) || !ReadGuestPointer(part_ex,part_scn) ||
               !ReadGuestPointer(part_scn+0xec,part_palette) || !ReadGuestPointer(part_scn+0xf0,part_attributes)) continue;
            const uint32_t part_offset=Memory::Read32(part_mdl+0x70);
            if(part_offset>0x100000 || !Memory::Contains(part_mdl+0x4c+part_offset,4)) continue;
            const uint32_t part_count=Memory::Read32(part_mdl+0x4c+part_offset);
            if(!part_count || part_count>64 || !Memory::Contains(part_palette,part_count*48) ||
               !Memory::Contains(part_attributes,part_count*4)) continue;
            auto copy=std::make_unique<CharacterPalette>();
            copy->address=part_palette;copy->attributes=part_attributes;
            bool valid=true;
            for(uint32_t i=0;i<part_count;++i) {
                Mtx34 matrix{};
                if(!ReadGuestMtx34(part_palette+i*48,matrix)) { valid=false;break; }
                copy->original.push_back(matrix);
                copy->original_attributes.push_back(Memory::Read32(part_attributes+i*4));
            }
            if(!valid) { copy->original.clear();continue; }
            saved.attachments.push_back(std::move(copy));
            for(uint32_t i=0;i<part_count;++i) {
                const auto matrix=ComposeMtx(hide,saved.attachments.back()->original[i]);
                for(unsigned k=0;k<12;++k) Memory::WriteFloat32(part_palette+i*48+k*4,matrix[k]);
            }
            uint32_t part_mix=0;
            if(ReadGuestPointer(part_scn+0x10c,part_mix)) {
                CpuContext skin=*context;
                skin.gpr[3]=part_palette;skin.gpr[4]=part_attributes;skin.gpr[5]=part_scn+0xe8;skin.gpr[6]=part_mix;
                func_80067F70(&skin);
            }
        }
    }
    RestoreModelVisibilityLocked();
    return hands | 4u;
}
} // namespace

bool MkwVRCharacterHandActive(size_t hand) noexcept {
    return hand<2 && (g_character_hands.load(std::memory_order_acquire)&(1u<<hand))!=0;
}

extern "C" void CharacterBodyCalcView(CpuContext* context) {
    uint32_t camera=0;
    {
        std::lock_guard lock(g_mutex);
        if (RuntimeConfigFile::VrBodyIk() && g_state.enabled &&
            g_state.mode==CameraMode::FirstPerson && g_state.camera_address!=0)
            camera=g_state.camera_address;
    }
    if (camera!=0) MkwVRFirstPersonUpdate(static_cast<uint64_t>(static_cast<uint32_t>(::g_gxFrameCount)),camera);
    CharacterPalette saved;
    unsigned active=0;
    try { active=PrepareCharacterBody(context,saved); }
    catch (const Memory::AccessViolation&) { saved.Restore(); }
    if (active!=0 && active!=g_body_logged_state) {
        g_body_logged_state=active;
        RT_LOG(RT_TAG_RUNTIME) << "[mkw-vr] character body IK applied before CalcView: head hidden="
                              << bool(active&4u) << ", left hand=" << bool(active&1u)
                              << ", right hand=" << bool(active&2u) << std::endl;
    }
    g_character_hands.store(active,std::memory_order_release);
    func_8006FA50(context);
    // RAII restores world/skin palettes even if translated drawing throws.
}
