#include <imgui.h>
#include <atomic>
#include <iostream>
#include <thread>

thread_local ImGuiContext* AuroraImGuiContext = nullptr;

int main() {
  auto* desktop = ImGui::CreateContext();
  int rendererMarker = 1;
  ImGui::GetIO().BackendRendererUserData = &rendererMarker;
  auto* panel = ImGui::CreateContext();
  std::atomic<int> stage{0};
  std::atomic<bool> valid{true};
  std::thread worker([&] {
    if (ImGui::GetCurrentContext() != nullptr) valid = false;
    ImGui::SetCurrentContext(desktop);
    stage.store(1);
    while (stage.load() != 2) std::this_thread::yield();
    // Producer deliberately holds the backend-less panel context while the
    // worker reads its own desktop backend. Global GImGui fails this check.
    if (ImGui::GetCurrentContext() != desktop ||
        ImGui::GetIO().BackendRendererUserData != &rendererMarker) valid = false;
    stage.store(3);
  });
  while (stage.load() != 1) std::this_thread::yield();
  ImGui::SetCurrentContext(panel);
  stage.store(2);
  while (stage.load() != 3) std::this_thread::yield();
  if (ImGui::GetCurrentContext() != panel || ImGui::GetIO().BackendRendererUserData != nullptr) valid = false;
  worker.join();
  ImGui::DestroyContext(panel);
  ImGui::SetCurrentContext(desktop);
  ImGui::GetIO().BackendRendererUserData = nullptr;
  ImGui::DestroyContext(desktop);
  if (!valid) { std::cerr << "ImGui contexts leaked between producer and worker\n"; return 1; }
  std::cout << "ImGui producer/renderer contexts remain independent\n";
}
