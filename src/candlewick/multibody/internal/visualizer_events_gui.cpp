#include "../Visualizer.h"
#include "candlewick/core/CameraControls.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace candlewick::multibody {

void add_light_gui(DirectionalLight &light) {
  ImGui::SliderFloat("intensity", &light.intensity, 0.1f, 10.f);
  ImGui::DragFloat3("direction", light.direction.data(), 0.0f, -1.f, 1.f);
  light.direction.stableNormalize();
  ImGui::ColorEdit3("color", light.color.data());
}

void camera_params_gui(CylindricalCamera &controller,
                       CameraControlParams &params) {
  if (ImGui::TreeNode("Camera controls")) {
    ImGui::SliderFloat("Rot. sensitivity", &params.rotSensitivity, 0.001f,
                       0.01f);
    ImGui::SliderFloat("Zoom sensitivity", &params.zoomSensitivity, 0.001f,
                       0.1f);
    ImGui::SliderFloat("Pan sensitivity", &params.panSensitivity, 0.001f,
                       0.01f);
    ImGui::SliderFloat("Local rot. sensitivity", &params.localRotSensitivity,
                       0.001f, 0.04f);
    ImGui::Checkbox("Invert Y", &params.yInvert);
    if (ImGui::Button("Reset target")) {
      controller.lookAt1(Float3::Zero());
    }
    ImGui::TreePop();
  }
}

void Visualizer::default_gui_exec(Visualizer &viz) {
  auto &render = viz.renderer;
  auto &light = viz.robotScene->directionalLight;
  ImGui::Begin("Renderer info & controls", nullptr,
               ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("Device driver: %s", render.device.driverName());

  ImGui::SeparatorText("Lights");
  ImGui::SetItemTooltip("Configuration for lights");
  add_light_gui(light);

  camera_params_gui(viz.controller, viz.cameraParams);

  if (ImGui::TreeNode("Debug Hud elements")) {
    ImGui::CheckboxFlags("hud.Grid", (int *)&viz.m_environmentFlags,
                         ENV_EL_GRID);
    ImGui::CheckboxFlags("hud.Triad", (int *)&viz.m_environmentFlags,
                         ENV_EL_TRIAD);
    ImGui::TreePop();
  }

  ImGui::End();
}

void mouse_wheel_handler(CylindricalCamera &controller,
                         const CameraControlParams &params,
                         SDL_MouseWheelEvent event) {
  controller.moveInOut(1.f - params.zoomSensitivity, event.y);
}

void mouse_motion_handler(CylindricalCamera &controller,
                          const CameraControlParams &params,
                          const SDL_MouseMotionEvent &event) {
  Float2 mvt{event.xrel, event.yrel};
  SDL_MouseButtonFlags mb = event.state;
  // check if left mouse pressed
  if (mb & SDL_BUTTON_LMASK) {
    controller.viewportDrag(mvt, params.rotSensitivity, params.panSensitivity,
                            params.yInvert);
  }
  if (mb & params.mouseButtons.panButton) {
    controller.pan(mvt, params.panSensitivity);
  }
  if (mb & SDL_BUTTON_RMASK) {
    Radf rot_angle = params.localRotSensitivity * mvt.y();
    camera_util::localRotateXAroundOrigin(controller.camera, rot_angle);
  }
}

void Visualizer::processEvents() {
  ImGuiIO &io = ImGui::GetIO();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_QUIT) {
      SDL_Log("Exiting application...");
      m_shouldExit = true;
    }

    if (io.WantCaptureMouse | io.WantCaptureKeyboard)
      continue;

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
      // camera mouse control
      if (m_cameraControl)
        mouse_motion_handler(this->controller, cameraParams, event.motion);
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      if (m_cameraControl)
        mouse_wheel_handler(this->controller, cameraParams, event.wheel);
      break;
    }
  }
}

} // namespace candlewick::multibody
