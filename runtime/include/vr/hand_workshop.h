// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "vr/body_ik.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cctype>

namespace mkw::vr::hand_workshop {
struct Character { const char* name; const char* archive; };
inline constexpr Character characters[]{
 {"Mario","ma_kart-mr"},{"Luigi","ma_kart-lg"},{"Peach","ma_kart-pc"},{"Daisy","ma_kart-ds"},
 {"Yoshi","ma_kart-ys"},{"Birdo","ma_kart-ca"},{"Diddy Kong","ma_kart-dd"},{"Bowser Jr.","ma_kart-jr"},
 {"Baby Mario","sa_kart-bmr"},{"Baby Luigi","sa_kart-blg"},{"Baby Peach","sa_kart-bpc"},{"Baby Daisy","sa_kart-bds"},
 {"Toad","sa_kart-ko"},{"Toadette","sa_kart-kk"},{"Koopa Troopa","sa_kart-nk"},{"Dry Bones","sa_kart-ka"},
 {"Wario","la_kart-wr"},{"Waluigi","la_kart-wl"},{"Donkey Kong","la_kart-dk"},{"Bowser","la_kart-kp"},
 {"King Boo","la_kart-kt"},{"Rosalina","la_kart-rs"},{"Funky Kong","la_kart-fk"},{"Dry Bowser","la_kart-bk"}
};
inline std::string Key(std::string name) {
 const auto delimiter=name.find("__");
 if(name.starts_with("body__")) name=name.substr(6);
 else if(delimiter!=std::string::npos) name.resize(delimiter);
 if(name=="k_hair_model") name="kino_girl_body";
 for(char& c:name) if(!std::isalnum(static_cast<unsigned char>(c)) && c!='_') c='_';
 return name;
}
struct Vertex { std::array<float,3> position{}; std::array<float,2> uv{}; };
// Rigid GX vertices are already local to the selected skinning matrix. They
// must not be transformed by the model's inverse bind matrix a second time.
// Native hands extend along +X; their left/right local palm axes are mirrored.
inline Mtx34 NeutralWrist(unsigned hand) {
 return hand==0?Mtx34{0,1,0,0,0,0,-1,0,-1,0,0,0}:
                Mtx34{0,-1,0,0,0,0,1,0,-1,0,0,0};
}
struct PreviewHand {
 Mtx34 wrist=kIdentityMtx34,mesh=kIdentityMtx34;
};
inline PreviewHand PlaceHand(const std::vector<Vertex>& vertices,unsigned hand,float scale) {
 if(vertices.empty() || hand>=2 || !detail::IsFiniteFloat(&scale) || scale<=0)
  throw std::runtime_error("Invalid character hand preview");
 PreviewHand result;result.wrist=NeutralWrist(hand);result.mesh=result.wrist;
 for(auto& value:result.mesh)value*=scale;
 std::array<float,3> min{1e6f,1e6f,1e6f},max{-1e6f,-1e6f,-1e6f};
 for(const auto& vertex:vertices) {
  const auto p=detail::TransformPoint(result.mesh,vertex.position[0],vertex.position[1],vertex.position[2]);
  const float coords[]{p.x,p.y,p.z};
  for(int axis=0;axis<3;++axis) {min[axis]=std::min(min[axis],coords[axis]);max[axis]=std::max(max[axis],coords[axis]);}
 }
 const float centre[]{hand==0?-.23f:.23f,-.23f,-.48f};
 for(int axis=0;axis<3;++axis) {
  result.wrist[axis*4+3]=centre[axis]-(min[axis]+max[axis])*.5f;
  result.mesh[axis*4+3]=result.wrist[axis*4+3];
 }
 return result;
}
// A near-palm trigger press belongs to calibration, irrespective of where its
// aim ray intersects the UI. Keep ownership until release so it cannot also
// click Previous/Next. Distant presses remain available to UI navigation.
struct CaptureTrigger {
 bool armed=false,consumed=false;
 void Release(float trigger) { if(trigger<.2f) {armed=true;consumed=false;} }
 bool Capture(float trigger,bool tracked,bool alreadyCaptured,const Mtx34& grip,const Mtx34& wrist,Mtx34& offset) {
  Release(trigger);
  if(!armed || alreadyCaptured || !tracked || trigger<.55f)return false;
  armed=false;
  if(!body_ik::CalibrateGripPose(grip,wrist,offset) || body_ik::Length(body_ik::Position(offset))>=.18f)return false;
  consumed=true;return true;
 }
};
inline bool ConsumePointer(const std::array<CaptureTrigger,2>& triggers,unsigned hand) {
 return hand<2 && triggers[hand].consumed;
}
struct Model {
 std::string key;
 std::array<std::vector<Vertex>,2> hands; // wrist-local coordinates, not model bind space
 std::array<Mtx34,2> wrist{};
 uint32_t width=0,height=0;
 float eye_height=110.f;
 std::vector<uint8_t> rgba;
};
// Bounded reader: malformed/mismatched archives never touch guest memory.
struct Reader {
 const std::vector<uint8_t>& data;
 void Check(size_t at,size_t length) const { if(at>data.size() || length>data.size()-at) throw std::runtime_error("Truncated character archive"); }
 uint8_t U8(size_t at) const { Check(at,1);return data[at]; }
 uint16_t U16(size_t at) const { Check(at,2);return (data[at]<<8)|data[at+1]; }
 uint32_t U32(size_t at) const { Check(at,4);return (uint32_t(data[at])<<24)|(uint32_t(data[at+1])<<16)|(data[at+2]<<8)|data[at+3]; }
 float F32(size_t at) const { auto u=U32(at);float f;std::memcpy(&f,&u,4);if(!detail::IsFiniteFloat(&f)) throw std::runtime_error("Invalid character coordinate");return f; }
 std::string Name(size_t at) const { std::string s;for(int i=0;i<128;++i) { auto c=U8(at+i);if(!c)return s;s+=char(c); }throw std::runtime_error("Invalid character name"); }
 std::vector<size_t> Dictionary(size_t at) const {
  const auto count=U32(at+4);if(count>4096) throw std::runtime_error("Invalid character dictionary");
  Check(at+8,size_t(count+1)*16);std::vector<size_t> result;
  for(uint32_t i=1;i<=count;++i) { const size_t p=at+U32(at+8+i*16+12);Check(p,4);result.push_back(p); }
  return result;
 }
};
inline std::vector<uint8_t> Unpack(const std::vector<uint8_t>& input) {
 Reader r{input};if(r.U32(0)!=0x59617a30) throw std::runtime_error("Expected Yaz0 character archive");
 const auto size=r.U32(4);if(!size || size>16*1024*1024) throw std::runtime_error("Character archive exceeds 16 MB limit");
 std::vector<uint8_t> out;out.reserve(size);size_t at=16;
 while(out.size()<size) {
  const auto code=r.U8(at++);
  for(int bit=7;bit>=0 && out.size()<size;--bit) {
   if(code&(1<<bit)) out.push_back(r.U8(at++));
   else {
    const auto a=r.U8(at++),b=r.U8(at++);size_t count=a>>4;
    if(count) count+=2;else count=size_t(r.U8(at++))+18;
    const size_t distance=((a&15)<<8)+b+1;
    if(distance>out.size() || count>size-out.size()) throw std::runtime_error("Invalid Yaz0 back reference");
    while(count--) out.push_back(out[out.size()-distance]);
   }
  }
 }
 return out;
}
inline std::array<uint8_t,4> RGB565(uint16_t c) {
 return {uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31),255};
}
inline void Texture(const Reader& r,size_t tex,Model& model) {
 model.width=r.U16(tex+0x1c);model.height=r.U16(tex+0x1e);
 if(!model.width || !model.height || model.width>1024 || model.height>1024 || r.U32(tex+0x20)!=14)
  throw std::runtime_error("Unsupported character texture (expected CMPR)");
 const size_t start=tex+r.U32(tex+0x10);size_t at=start;
 model.rgba.resize(size_t(model.width)*model.height*4);
 for(unsigned y=0;y<model.height;y+=8) for(unsigned x=0;x<model.width;x+=8)
  for(unsigned sub=0;sub<4;++sub) {
   const auto c0=r.U16(at),c1=r.U16(at+2);std::array<std::array<uint8_t,4>,4> colors{RGB565(c0),RGB565(c1)};
   for(int k=0;k<3;++k) {
    colors[2][k]=(c0>c1?(2*colors[0][k]+colors[1][k])/3:(colors[0][k]+colors[1][k])/2);
    colors[3][k]=(c0>c1?(colors[0][k]+2*colors[1][k])/3:0);
   }
   colors[2][3]=255;colors[3][3]=c0>c1?255:0;
   for(unsigned row=0;row<4;++row) {
    const auto bits=r.U8(at+4+row);
    for(unsigned col=0;col<4;++col) {
     const auto px=x+(sub%2)*4+col,py=y+(sub/2)*4+row;
     if(px<model.width && py<model.height) std::copy(colors[(bits>>(6-2*col))&3].begin(),colors[(bits>>(6-2*col))&3].end(),model.rgba.begin()+(py*model.width+px)*4);
    }
   }
   at+=8;
  }
}
inline float Component(const Reader& r,size_t at,uint32_t type,unsigned fraction) {
 const float scale=std::ldexp(1.f,-int(fraction));
 switch(type) {
 case 0:return r.U8(at)*scale;case 1:return int8_t(r.U8(at))*scale;
 case 2:return r.U16(at)*scale;case 3:return int16_t(r.U16(at))*scale;case 4:return r.F32(at);
 default:throw std::runtime_error("Unsupported character vertex format");
 }
}
inline std::vector<std::array<float,3>> Array(const Reader& r,size_t header,bool uv) {
 const auto type=r.U32(header+(uv?0x18:0x18));const unsigned stride=r.U8(header+0x1d),fraction=r.U8(header+0x1c);
 const auto count=r.U16(header+0x1e);const size_t data=header+r.U32(header+8);
 const unsigned components=uv?(r.U32(header+0x14)==1?2:1):(r.U32(header+0x14)==1?3:2);
 const unsigned bytes=type==4?4:type>=2?2:1;if(stride<bytes*components) throw std::runtime_error("Invalid character vertex stride");
 r.Check(data,size_t(count)*stride);std::vector<std::array<float,3>> out(count);
 for(unsigned i=0;i<count;++i) for(unsigned j=0;j<components;++j) out[i][j]=Component(r,data+i*stride+j*bytes,type,fraction);
 return out;
}
inline Model Parse(const std::vector<uint8_t>& data) {
 Reader r{data};Model out;size_t mdl=0;std::array<uint32_t,2> wristIds{UINT32_MAX,UINT32_MAX};
 Mtx34 face=kIdentityMtx34;bool faceFound=false;
 std::array<Mtx34,2> wristFromSkin{kIdentityMtx34,kIdentityMtx34};
 std::string textureName;
 for(size_t p=0;p+0x90<data.size();p+=4) if(r.U32(p)==0x4d444c30) {
  const auto size=r.U32(p+4),revision=r.U32(p+8);if(revision!=11 || size<0x90)continue;r.Check(p,size);
  const auto bones=r.Dictionary(p+r.U32(p+0x14));
  for(auto bone:bones) {
   const auto name=r.Name(bone+r.U32(bone+8));
   if(name=="face_1" || name=="head" || name=="head1") {
    faceFound=true;for(int i=0;i<12;++i)face[i]=r.F32(bone+0x70+i*4);
   }
   const int hand=name=="wrist_l1"?0:name=="wrist_r1"?1:-1;
   if(hand>=0) {
    wristIds[hand]=r.U32(bone+0x10);
    // King Boo's terminal wrist is not in the skinning palette. Its hand
    // geometry is rigidly owned by the immediately preceding arm segment.
    const auto parent=bone+int32_t(r.U32(bone+0x5c));
    if(r.U32(p+r.U32(p+0x18)+4)>0) {
     const auto arrays=r.Dictionary(p+r.U32(p+0x18));
     if(!arrays.empty() && r.Name(arrays[0]+r.U32(arrays[0]+0xc)).starts_with("king_teresa")) {
      wristIds[hand]=r.U32(parent+0x10);
      Mtx34 wrist{},skin{},inverse{};
      for(int i=0;i<12;++i) {wrist[i]=r.F32(bone+0x70+i*4);skin[i]=r.F32(parent+0x70+i*4);}
      if(!InvertMtx(wrist,inverse))throw std::runtime_error("Invalid character wrist bind matrix");
      wristFromSkin[hand]=ComposeMtx(inverse,skin);
     }
    }
    for(int i=0;i<12;++i) out.wrist[hand][i]=r.F32(bone+0x70+i*4);
   }
  }
  if(wristIds[0]!=UINT32_MAX && wristIds[1]!=UINT32_MAX) { mdl=p;break; }
 }
 if(!mdl) throw std::runtime_error("No character hand skeleton found");
 const auto positions=r.Dictionary(mdl+r.U32(mdl+0x18));
 const auto texcoords=r.Dictionary(mdl+r.U32(mdl+0x24));
 if(positions.empty() || texcoords.empty()) throw std::runtime_error("No character geometry found");
 if(faceFound)for(auto array:positions) {
  if(r.Name(array+r.U32(array+0xc)).find("_eye")==std::string::npos)continue;
  const auto eye=detail::TransformPoint(face,(r.F32(array+0x20)+r.F32(array+0x2c))*.5f,
       (r.F32(array+0x24)+r.F32(array+0x30))*.5f,(r.F32(array+0x28)+r.F32(array+0x34))*.5f);
  out.eye_height=eye.y;break;
 }
 out.key=Key(r.Name(positions[0]+r.U32(positions[0]+0xc)));
 const auto posName=r.Name(positions[0]+r.U32(positions[0]+0xc));const auto delim=posName.find("__");
 if(delim==std::string::npos) throw std::runtime_error("No character material found");
 const auto materials=r.Dictionary(mdl+r.U32(mdl+0x30));
 std::vector<uint32_t> shapeMaterials(256,UINT32_MAX);
 for(auto definition:r.Dictionary(mdl+r.U32(mdl+0x10))) {
  // DrawOpa/DrawXlu streams are identified by their first draw command.
  if(r.U8(definition)!=4)continue;
  for(size_t at=definition;r.U8(at)==4;at+=8) {
   const auto material=r.U16(at+1),shape=r.U16(at+3);
   if(shape<shapeMaterials.size() && material<materials.size())shapeMaterials[shape]=material;
  }
 }
 const auto shapes=r.Dictionary(mdl+r.U32(mdl+0x38));
 for(unsigned shapeIndex=0;shapeIndex<shapes.size();++shapeIndex) {
  const auto shape=shapes[shapeIndex];
  const unsigned posId=r.U16(shape+0x48),uvId=r.U16(shape+0x50);
  if(posId>=positions.size() || uvId>=texcoords.size())continue;
  auto pos=Array(r,positions[posId],false),uv=Array(r,texcoords[uvId],true);
  const auto lo=r.U32(shape+0xc),hi=r.U32(shape+0x10);
  std::array<unsigned,12> offsets{},types{};unsigned stride=0;
  for(int bit=0;bit<9;++bit) stride+=(lo>>bit)&1;
  for(unsigned attr=0;attr<12;++attr) {
   offsets[attr]=stride;types[attr]=attr<4?(lo>>(9+attr*2))&3:(hi>>((attr-4)*2))&3;
   if(types[attr]==1)throw std::runtime_error("Direct character attributes unsupported");
   if(types[attr])stride+=types[attr]-1;
  }
  if(types[0]<2 || types[4]<2 || !stride) continue;
  const size_t group=shape+0x24,start=group+r.U32(group+8),length=r.U32(group+4);r.Check(start,length);
  std::array<uint32_t,10> matrices;matrices.fill(UINT32_MAX);size_t at=start;
  struct Indexed { Vertex vertex; uint32_t bone; };
  const auto triangle=[&](const Indexed& a,const Indexed& b,const Indexed& c) {
   for(int hand=0;hand<2;++hand) if(a.bone==wristIds[hand] && b.bone==wristIds[hand] && c.bone==wristIds[hand]) {
   auto& mesh=out.hands[hand];if(mesh.size()>60000)throw std::runtime_error("Character hand mesh too large");
    if(shapeIndex>=shapeMaterials.size() || shapeMaterials[shapeIndex]>=materials.size())throw std::runtime_error("Missing character hand material");
    const auto material=materials[shapeMaterials[shapeIndex]],sampler=material+r.U32(material+0x30);
    const auto name=r.Name(sampler+r.U32(sampler));
    if(!textureName.empty() && textureName!=name)throw std::runtime_error("Multiple character hand materials unsupported");
    textureName=name;
    for(auto vertex: {a.vertex,b.vertex,c.vertex}) {
     const auto p=detail::TransformPoint(wristFromSkin[hand],vertex.position[0],vertex.position[1],vertex.position[2]);
     vertex.position={p.x,p.y,p.z};mesh.push_back(vertex);
    }
   }
  };
  while(at<start+length) {
   const auto cmd=r.U8(at++);if(!cmd)continue;
   if(cmd==0x20 || cmd==0x28 || cmd==0x30 || cmd==0x38) {
    if(at+4>start+length)throw std::runtime_error("Truncated matrix load");
    if(cmd==0x20) { const unsigned address=r.U16(at+2)&0xfff;if(address%12 || address/12>=10)throw std::runtime_error("Invalid matrix slot");matrices[address/12]=r.U16(at); }
    at+=4;continue;
   }
   const auto primitive=cmd&0xf8;if(primitive!=0x80 && primitive!=0x90 && primitive!=0x98 && primitive!=0xa0)throw std::runtime_error("Unsupported character primitive");
   const auto count=r.U16(at);at+=2;if(count<3 || count>(start+length-at)/stride)throw std::runtime_error("Truncated character primitive");
   std::vector<Indexed> vertices;vertices.reserve(count);
   for(unsigned i=0;i<count;++i) {
    const size_t v=at+i*stride;
    const unsigned pi=types[0]==2?r.U8(v+offsets[0]):r.U16(v+offsets[0]);
    const unsigned ti=types[4]==2?r.U8(v+offsets[4]):r.U16(v+offsets[4]);
    if(pi>=pos.size() || ti>=uv.size())throw std::runtime_error("Invalid character vertex index");
    auto bone=r.U32(shape+8);
    if(lo&1) { const unsigned slot=r.U8(v);if(slot%3 || slot/3>=10)throw std::runtime_error("Invalid character matrix selector");bone=matrices[slot/3]; }
    vertices.push_back({{pos[pi],{uv[ti][0],uv[ti][1]}},bone});
   }
   if(primitive==0x80) { if(count%4)throw std::runtime_error("Invalid quad count");for(unsigned i=0;i<count;i+=4) {triangle(vertices[i],vertices[i+1],vertices[i+2]);triangle(vertices[i],vertices[i+2],vertices[i+3]);} }
   else if(primitive==0x90) {if(count%3)throw std::runtime_error("Invalid triangle count");for(unsigned i=0;i<count;i+=3)triangle(vertices[i],vertices[i+1],vertices[i+2]);}
   else for(unsigned i=2;i<count;++i) { const auto a=primitive==0xa0?0:i-2,b=i-1;if(primitive==0x98 && i%2)triangle(vertices[b],vertices[a],vertices[i]);else triangle(vertices[a],vertices[b],vertices[i]); }
   at+=size_t(count)*stride;
  }
 }
 if(out.hands[0].empty() || out.hands[1].empty())throw std::runtime_error("Character has no rigid hand triangles");
 for(size_t p=0;p+0x40<data.size();p+=4) if(r.U32(p)==0x54455830 && r.Name(p+r.U32(p+0x14))==textureName) {Texture(r,p,out);break;}
 if(out.rgba.empty())throw std::runtime_error("Character hand texture missing");
 return out;
}
inline Model Load(const std::filesystem::path& root,unsigned index) {
 if(index>=std::size(characters))throw std::runtime_error("Invalid character selection");
 const auto path=root/"files"/"Race"/"Kart"/(std::string(characters[index].archive)+".szs");
 std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Character archive missing from installed game DATA");
 const auto length=file.tellg();if(length<=0 || length>16*1024*1024)throw std::runtime_error("Invalid character archive size");
 std::vector<uint8_t> bytes(static_cast<size_t>(length));file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),length))throw std::runtime_error("Could not read character archive");
 return Parse(Unpack(bytes));
}
}
