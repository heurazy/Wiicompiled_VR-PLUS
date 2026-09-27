#pragma once

// The host builds menus while the GPU worker renders sealed draw lists. A
// process-global current context lets the worker pick up the VR-only context,
// which has no renderer backend. Each thread must select its own context.
struct ImGuiContext;
extern thread_local ImGuiContext* AuroraImGuiContext;
#define GImGui AuroraImGuiContext

#define IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS 1

typedef struct SDL_IOStream SDL_IOStream;
typedef SDL_IOStream* ImFileHandle;
typedef unsigned long long ImU64;

ImFileHandle ImFileOpen(const char* filename, const char* mode);
bool ImFileClose(ImFileHandle file);
ImU64 ImFileGetSize(ImFileHandle file);
ImU64 ImFileRead(void* data, ImU64 size, ImU64 count, ImFileHandle file);
ImU64 ImFileWrite(const void* data, ImU64 size, ImU64 count, ImFileHandle file);
