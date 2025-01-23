#include "Common.h"
#include "GenHeightfield.h"

#include "candlewick/core/debug/DepthViz.h"
#include "candlewick/core/Renderer.h"
#include "candlewick/core/GuiSystem.h"
#include "candlewick/core/DebugScene.h"
#include "candlewick/core/DepthAndShadowPass.h"
#include "candlewick/core/LightUniforms.h"

#include "candlewick/multibody/RobotScene.h"
#include "candlewick/primitives/Plane.h"
#include "candlewick/utils/WriteTextureToImage.h"
#include "candlewick/core/CameraControl.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <robot_descriptions_cpp/robot_load.hpp>

#include <pinocchio/multibody/data.hpp>
#include <pinocchio/multibody/geometry.hpp>
#include <pinocchio/algorithm/kinematics.hpp>
#include <pinocchio/algorithm/joint-configuration.hpp>
#include <pinocchio/algorithm/geometry.hpp>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_gpu.h>

#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

namespace pin = pinocchio;
using namespace candlewick;
using multibody::RobotScene;

/// Application constants

constexpr Uint32 wWidth = 1680;
constexpr Uint32 wHeight = 1050;
constexpr float aspectRatio = float(wWidth) / float(wHeight);

/// Application state

static Radf currentFov = 55.0_radf;
static float nearZ = 0.01f;
static float farZ = 10.f;
static float currentOrthoScale = 1.f;
static CameraProjection cam_type = CameraProjection::PERSPECTIVE;
static Camera camera{
    .projection = perspectiveFromFov(currentFov, aspectRatio, nearZ, farZ),
    .view{lookAt({2.0, 0, 2.}, Float3::Zero())},
};
static bool quitRequested = false;
static bool showDebugViz = false;

static float pixelDensity;
static float displayScale;

static void updateFov(Radf newFov) {
  camera.projection = perspectiveFromFov(newFov, aspectRatio, nearZ, farZ);
  currentFov = newFov;
}

static void updateOrtho(float zoom) {
  float iz = 1.f / zoom;
  camera.projection = orthographicMatrix({iz * aspectRatio, iz}, -8., 8.);
  currentOrthoScale = zoom;
}

void eventLoop(const Renderer &renderer) {
  CylinderCameraControl camControl{camera};
  // update pixel density and display scale
  pixelDensity = SDL_GetWindowPixelDensity(renderer.window);
  displayScale = SDL_GetWindowDisplayScale(renderer.window);
  const float rotSensitivity = 5e-3f * pixelDensity;
  const float panSensitivity = 1e-2f * pixelDensity;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    ImGuiIO &io = ImGui::GetIO();
    if (event.type == SDL_EVENT_QUIT) {
      SDL_Log("Application exit requested.");
      quitRequested = true;
      break;
    }

    if (io.WantCaptureMouse | io.WantCaptureKeyboard)
      continue;
    switch (event.type) {
    case SDL_EVENT_MOUSE_WHEEL: {
      float wy = event.wheel.y;
      const float scaleFac = std::exp(kScrollZoom * wy);
      switch (cam_type) {
      case CameraProjection::ORTHOGRAPHIC:
        updateOrtho(std::clamp(scaleFac * currentOrthoScale, 0.1f, 2.f));
        break;
      case CameraProjection::PERSPECTIVE:
        updateFov(Radf(std::min(currentFov * scaleFac, 170.0_radf)));
        break;
      }
      break;
    }
    case SDL_EVENT_KEY_DOWN: {
      const float step_size = 0.06f;
      switch (event.key.key) {
      case SDLK_LEFT:
        cameraLocalTranslateX(camera, +step_size);
        break;
      case SDLK_RIGHT:
        cameraLocalTranslateX(camera, -step_size);
        break;
      case SDLK_UP:
        cameraWorldTranslateZ(camera, -step_size);
        break;
      case SDLK_DOWN:
        cameraWorldTranslateZ(camera, +step_size);
        break;
      }
      break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
      SDL_MouseButtonFlags mouseButton = event.motion.state;
      bool controlPressed = SDL_GetModState() & SDL_KMOD_CTRL;
      if (mouseButton & SDL_BUTTON_LMASK) {
        if (controlPressed) {
          camControl.moveInOut(0.95f, event.motion.yrel);
        } else {
          camControl.viewportDrag({event.motion.xrel, event.motion.yrel},
                                  rotSensitivity, panSensitivity);
        }
      }
      if (mouseButton & SDL_BUTTON_RMASK) {
        float camXLocRotSpeed = 0.01f * pixelDensity;
        cameraLocalRotateX(camera, camXLocRotSpeed * event.motion.yrel);
      }
      break;
    }
    }
  }
}

Renderer createRenderer(Uint32 width, Uint32 height,
                        SDL_GPUTextureFormat depth_stencil_format) {
  Device dev{auto_detect_shader_format_subset(), true};
  SDL_Window *window = SDL_CreateWindow(__FILE__, int(width), int(height), 0);
  return Renderer{std::move(dev), window, depth_stencil_format};
}

void runDepthPrepass(Renderer &renderer, const Camera &camera,
                     const RobotScene &scene, DepthPassInfo &depthPassInfo) {
  RobotScene::PipelineType pipeline_type = RobotScene::PIPELINE_TRIANGLEMESH;
  entt::registry &registry = scene.registry;
  auto &geom_data = scene.geomData();

  auto robot_view =
      registry
          .view<const RobotScene::Opaque, const multibody::PinGeomObjComponent,
                const RobotScene::MeshMaterialComponent>();
  auto env_view =
      registry
          .view<const RobotScene::Opaque, const RobotScene::TransformComponent,
                const multibody::VisibilityComponent,
                const RobotScene::MeshMaterialComponent>();

  std::vector<OpaqueCastable> castables;
  // collect castable objects
  for (auto [ent, geom_id, meshMaterial] : robot_view.each()) {
    if (meshMaterial.pipeline_type != pipeline_type)
      continue;

    auto pose = geom_data.oMg[geom_id].cast<float>();
    GpuMat4 transform = pose.toHomogeneousMatrix();
    // do *not* put the whole Mesh in. the indices collide!
    for (auto &v : meshMaterial.mesh.views())
      castables.emplace_back(v, transform);
  }

  for (auto [ent, tr, vis, meshMaterial] : env_view.each()) {
    if (!vis || meshMaterial.pipeline_type != pipeline_type)
      continue;

    const Mesh &mesh = meshMaterial.mesh;
    GpuMat4 transform = tr.transform;
    for (auto &v : mesh.views())
      castables.emplace_back(v, transform);
  }
  SDL_assert(castables.size() > 0);
  const GpuMat4 viewProj = camera.viewProj();
  renderDepthOnlyPass(renderer, depthPassInfo, viewProj, castables);
}

int main(int argc, char **argv) {
  CLI::App app{"Ur5 example"};
  bool performRecording{false};
  RobotScene::Config robot_scene_config;

  argv = app.ensure_utf8(argv);
  app.add_flag("-r,--record", performRecording, "Record output");
  app.add_flag("--prepass", robot_scene_config.triangle_has_prepass,
               "Whether to have a prepass for the PBR'd triangle meshes.");
  CLI11_PARSE(app, argc, argv);

  if (!SDL_Init(SDL_INIT_VIDEO))
    return 1;

  Renderer renderer =
      createRenderer(wWidth, wHeight, SDL_GPU_TEXTUREFORMAT_D32_FLOAT);

  entt::registry registry{};

  // Load robot
  pin::Model model;
  pin::GeometryModel geom_model;
  robot_descriptions::loadModelsFromToml("ur.toml", "ur5_gripper", model,
                                         &geom_model, NULL);
  // ADD HEIGHTFIELD GEOM
  // {
  //   auto hfield = generatePerlinNoiseHeightfield(42, 40u, 0.2f);
  //   pin::GeometryObject gobj{"custom_hfield", 0ul, pin::SE3::Identity(),
  //                            hfield};
  //   geom_model.addGeometryObject(gobj);
  // }
  pin::Data pin_data{model};
  pin::GeometryData geom_data{geom_model};

  RobotScene robot_scene{registry, renderer, geom_model, geom_data,
                         robot_scene_config};
  auto &myLight = robot_scene.directionalLight;
  myLight = {
      .direction = {0., -1., -1.},
      .color = {1.0, 1.0, 1.0},
      .intensity = 8.0,
  };

  // Add plane
  const Eigen::Affine3f plane_transform{Eigen::UniformScaling<float>(3.0f)};
  entt::entity plane_entity = robot_scene.addEnvironmentObject(
      loadPlaneTiled(0.25f, 5, 5), plane_transform.matrix());
  auto [plane_obj, plane_vis] =
      registry.get<RobotScene::MeshMaterialComponent,
                   multibody::VisibilityComponent>(plane_entity);

  const size_t numRobotShapes =
      registry.view<const multibody::PinGeomObjComponent>().size();
  SDL_assert(numRobotShapes == geom_model.ngeoms);
  SDL_Log("Registered %zu robot geometry objects.", numRobotShapes);

  /** DEBUG SYSTEM **/
  DebugScene debug_scene{renderer};
  auto &basic_debug_module = debug_scene.addModule<BasicDebugModule>();
  basic_debug_module.grid_color = 0xE0A236ff_rgbaf;

  auto depth_debug = DepthDebugPass::create(renderer, renderer.depth_texture);
  static DepthDebugPass::VizStyle depth_mode = DepthDebugPass::VIZ_GRAYSCALE;

  auto depthPassInfo = DepthPassInfo::create(renderer, plane_obj.mesh.layout);

  GuiSystem gui_system{[&](Renderer &r) {
    static bool demo_window_open = true;

    ImGui::Begin("Renderer info & controls", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Device driver: %s", r.device.driverName());
    ImGui::SeparatorText("Camera");
    bool ortho_change, persp_change;
    ortho_change = ImGui::RadioButton("Orthographic", (int *)&cam_type,
                                      int(CameraProjection::ORTHOGRAPHIC));
    ImGui::SameLine();
    persp_change = ImGui::RadioButton("Perspective", (int *)&cam_type,
                                      int(CameraProjection::PERSPECTIVE));
    switch (cam_type) {
    case CameraProjection::ORTHOGRAPHIC:
      ortho_change |=
          ImGui::DragFloat("zoom", &currentOrthoScale, 0.01f, 0.1f, 2.f, "%.3f",
                           ImGuiSliderFlags_AlwaysClamp);
      if (ortho_change)
        updateOrtho(currentOrthoScale);
      break;
    case CameraProjection::PERSPECTIVE:
      Degf newFov{currentFov};
      persp_change |= ImGui::DragFloat("fov", newFov, 1.f, 15.f, 90.f, "%.3f",
                                       ImGuiSliderFlags_AlwaysClamp);
      persp_change |= ImGui::SliderFloat("Far plane", &farZ, 1.0f, 10.f);
      if (persp_change)
        updateFov(Radf(newFov));
      break;
    }

    ImGui::SeparatorText("Env. status");
    ImGui::Checkbox("Render plane", &plane_vis.status);
    ImGui::Checkbox("Render grid", &basic_debug_module.enableGrid);
    ImGui::Checkbox("Render triad", &basic_debug_module.enableTriad);
    if (ImGui::Checkbox("Show depth debug", &showDebugViz)) {
      // do stuff here
      if (showDebugViz) {
        SDL_Log("Turned on depth debug viz.");
      }
    }
    if (showDebugViz) {
      ImGui::RadioButton("Grayscale", (int *)&depth_mode, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Heatmap", (int *)&depth_mode, 1);
    }

    ImGui::SeparatorText("Lights");
    ImGui::DragFloat("intens.", &myLight.intensity, 0.1f, 0.1f, 10.0f);
    ImGui::ColorEdit3("color", myLight.color.data());
    ImGui::Separator();
    ImGui::ColorEdit4("grid color", basic_debug_module.grid_color.data(),
                      ImGuiColorEditFlags_AlphaPreview);
    ImGui::ColorEdit4("plane color", plane_obj.materials[0].baseColor.data());
    ImGui::End();
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    ImGui::ShowDemoWindow(&demo_window_open);
  }};

  if (!gui_system.init(renderer)) {
    return 1;
  }

  // MAIN APPLICATION LOOP

  Uint32 frameNo = 0;

  Eigen::VectorXd q0 = pin::neutral(model);
  Eigen::VectorXd q1 = pin::randomConfiguration(model);

  media::VideoRecorder recorder{NoInit};
  if (performRecording)
    recorder = media::VideoRecorder{wWidth, wHeight, "ur5.mp4"};

  auto record_callback = [=, &renderer, &recorder]() {
    auto swapchain_format = renderer.getSwapchainTextureFormat();
    media::videoWriteTextureToFrame(renderer.device, recorder,
                                    renderer.swapchain, swapchain_format,
                                    wWidth, wHeight);
  };

  while (!quitRequested) {
    // logic
    eventLoop(renderer);
    double phi = 0.5 * (1. + std::sin(frameNo * 1e-2));
    Eigen::VectorXd q = pin::interpolate(model, q0, q1, phi);
    pin::forwardKinematics(model, pin_data, q);
    pin::updateGeometryPlacements(model, pin_data, geom_model, geom_data);

    // acquire command buffer and swapchain
    robot_scene.directionalLight = myLight;
    renderer.beginFrame();

    if (renderer.waitAndAcquireSwapchain()) {
      if (showDebugViz) {
        runDepthPrepass(renderer, camera, robot_scene, depthPassInfo);
        renderDepthDebug(renderer, depth_debug, {depth_mode, nearZ, farZ});
      } else {
        if (robot_scene.pbrHasPrepass())
          runDepthPrepass(renderer, camera, robot_scene, depthPassInfo);
        robot_scene.render(renderer, camera);
        debug_scene.render(renderer, camera);
      }
      gui_system.render(renderer);
    } else {
      SDL_Log("Failed to acquire swapchain: %s", SDL_GetError());
      continue;
    }

    renderer.endFrame();

    if (performRecording) {
      record_callback();
    }
    frameNo++;
  }

  depthPassInfo.release(renderer.device);
  depth_debug.release(renderer.device);
  robot_scene.release();
  debug_scene.release();
  gui_system.release();
  renderer.destroy();
  SDL_DestroyWindow(renderer.window);
  SDL_Quit();
  return 0;
}
