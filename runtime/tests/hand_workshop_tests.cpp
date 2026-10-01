#include "vr/hand_workshop.h"
#include <cstdio>
#include <set>
#include <cstdlib>
using namespace mkw::vr::hand_workshop;
void Check(bool v) { if(!v)std::abort(); }
int main(int argc,char** argv) {
 {
  using namespace mkw::vr;
  std::array<CaptureTrigger,2> triggers{};
  Mtx34 grip=kIdentityMtx34,wrist=kIdentityMtx34,offset{};wrist[11]=-.055f;
  // The trigger used to enter the workshop must be released before capturing.
  Check(!triggers[0].Capture(.9f,true,false,grip,wrist,offset));
  Check(!triggers[0].Capture(0,true,false,grip,wrist,offset));
  // A normal press captures irrespective of a global menu pointer position.
  Check(triggers[0].Capture(.65f,true,false,grip,wrist,offset));
  Check(std::abs(offset[11]+.055f)<.0001f);
  Check(ConsumePointer(triggers,0));Check(!ConsumePointer(triggers,1));
  Check(!triggers[0].Capture(.9f,true,false,grip,wrist,offset));
  Check(ConsumePointer(triggers,0));
  triggers[1].Release(0);
  Check(triggers[1].Capture(.9f,true,false,grip,wrist,offset));
  Check(ConsumePointer(triggers,0) && ConsumePointer(triggers,1));
  triggers[0].Release(0);Check(!ConsumePointer(triggers,0) && ConsumePointer(triggers,1));
  triggers[1].Release(0);
  // A distant press belongs to menu navigation, never to calibration.
  grip[3]=.4f;
  Check(!triggers[0].Capture(.9f,true,false,grip,wrist,offset));Check(!ConsumePointer(triggers,0));
  grip[3]=0;
  Check(!triggers[0].Capture(.9f,true,false,grip,wrist,offset)); // no late capture while held
  triggers[0].Release(0);
  Check(!triggers[0].Capture(.9f,false,false,grip,wrist,offset));
  Check(!triggers[0].Capture(.9f,true,true,grip,wrist,offset));
 }
 Check(Key("mario_body__mario_all_tx")=="mario_body");
 Check(Key("kinopico_body__kinopico_all_tx")!="mario_body");
 Check(Key("body__bb_peach_all_tx")!=Key("body__bb_daisy_all_tx"));
 Check(Key("k_hair_model__kino_g_hair")==Key("kino_girl_body__kino_g_hair"));
 for(auto bytes: {std::vector<uint8_t>{},std::vector<uint8_t>(16,0)}) {
  bool failed=false;try {Unpack(bytes);}catch(const std::exception&) {failed=true;}Check(failed);
 }
 if(argc>1) {
  std::set<std::string> keys;
  for(unsigned i=0;i<std::size(characters);++i) {
   try {const auto m=Load(argv[1],i);Check(keys.insert(m.key).second);
    Check(m.rgba.size()==size_t(m.width)*m.height*4);
    for(const auto& wrist:m.wrist) {mkw::vr::Mtx34 inverse{};Check(mkw::vr::InvertMtx(wrist,inverse));}
    for(unsigned hand=0;hand<2;++hand) {
     using namespace mkw::vr;
     const float scale=.005f/CharacterCockpitScale(m.eye_height);
     const auto preview=PlaceHand(m.hands[hand],hand,scale);
     const auto forward=detail::TransformDirection(preview.wrist,{1,0,0});
     Check(std::abs(forward.x)<.0001f && std::abs(forward.y)<.0001f && std::abs(forward.z+1)<.0001f);
     float lo[3]{1e6,1e6,1e6},hi[3]{-1e6,-1e6,-1e6};
     for(auto vertex:m.hands[hand]) {
      const auto p=detail::TransformPoint(preview.mesh,vertex.position[0],vertex.position[1],vertex.position[2]);
      const float position[]{p.x,p.y,p.z};
      for(unsigned axis=0;axis<3;++axis) {lo[axis]=std::min(lo[axis],position[axis]);hi[axis]=std::max(hi[axis],position[axis]);}
     }
     // Placing the blue cross at the visible hand centre must capture every
     // character. Inverse-binding already-local vertices made this fail.
     Mtx34 grip=kIdentityMtx34,offset{};
     const float centre[]{hand==0?-.23f:.23f,-.23f,-.48f};
     for(unsigned axis=0;axis<3;++axis) {
      Check(std::abs((lo[axis]+hi[axis])*.5f-centre[axis])<.0001f);
      grip[axis*4+3]=centre[axis];
     }
     CaptureTrigger trigger;trigger.Release(0);
     Check(trigger.Capture(1,true,false,grip,preview.wrist,offset));
     const auto reconstructed=ComposeMtx(grip,offset);
     for(unsigned k=0;k<12;++k)Check(std::abs(reconstructed[k]-preview.wrist[k])<.0001f);
    }
    std::printf("%s: %s, %zu / %zu vertices, %u x %u texture\n",characters[i].name,m.key.c_str(),m.hands[0].size(),m.hands[1].size(),m.width,m.height);
   }catch(const std::exception& e) {std::fprintf(stderr,"%s: %s\n",characters[i].name,e.what());return 1;}
  }
 }
}
