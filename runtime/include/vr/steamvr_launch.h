// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace mkw::vr {
// Select the loader for this process only; never change the user's active runtime.
inline bool SelectSteamVrRuntime() {
#if defined(_WIN32)
    auto read=[](HKEY key,const wchar_t* sub,const wchar_t* name) {
        wchar_t value[32768]{}; DWORD size=sizeof(value);
        return RegGetValueW(key,sub,name,RRF_RT_REG_SZ,nullptr,value,&size)==ERROR_SUCCESS
            ? std::filesystem::path(value):std::filesystem::path{};
    };
    auto active=read(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\OpenXR\\1",L"ActiveRuntime");
    auto steam=read(HKEY_CURRENT_USER,L"SOFTWARE\\Valve\\Steam",L"SteamPath");
    std::vector<std::filesystem::path> candidates;
    if(active.filename()==L"steamxr_win64.json") candidates.push_back(active);
    if(!steam.empty()) {
        candidates.push_back(steam/L"steamapps/common/SteamVR/steamxr_win64.json");
        std::ifstream file(steam/L"steamapps/libraryfolders.vdf");
        std::string line; const std::regex path(R"vdf("path"\s*"([^"]+)")vdf");
        while(std::getline(file,line)) { std::smatch match;
            if(std::regex_search(line,match,path))
                candidates.push_back(std::filesystem::u8path(match[1].str())/L"steamapps/common/SteamVR/steamxr_win64.json");
        }
    }
    for(const auto& json:candidates) {
        std::error_code ec;
        if(!std::filesystem::is_regular_file(json,ec)) continue;
        if(!SetEnvironmentVariableW(L"XR_RUNTIME_JSON",json.c_str())) return false;
        return true;
    }
#endif
    return false;
}
}
