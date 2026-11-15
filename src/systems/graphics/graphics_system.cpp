// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "graphics_system.hpp"

#include "graphics_scene.hpp"

#include "base/log.hpp"
#include "base/utils.hpp"

#include "managers/resourcemanagerpool.hpp"

#include "rendering/r3d_messages.hpp"

namespace blunted {

  GraphicsSystem::GraphicsSystem() : systemType(e_SystemType_Graphics) {
    renderer3DTask = NULL;
    task = NULL;
  }

  GraphicsSystem::~GraphicsSystem() {
  }

  void GraphicsSystem::Initialize(const Properties &config) {

    textureResourceManager = std::shared_ptr < ResourceManager<Texture> > (new ResourceManager<Texture>("texture"));
    vertexBufferResourceManager = std::shared_ptr < ResourceManager<VertexBuffer> > (new ResourceManager<VertexBuffer>("vertexbuffer"));
    ResourceManagerPool::GetInstance().RegisterManager(e_ResourceType_Texture, textureResourceManager);
    ResourceManagerPool::GetInstance().RegisterManager(e_ResourceType_VertexBuffer, vertexBufferResourceManager);

    // initialize renderer on the Main Thread (OpenGL context + SDL window affinity)
    if (config.Get("graphics3d_renderer", "opengl") == "opengl") renderer3DTask = new OpenGLRenderer3D();
    renderer3DTask->InitMainThread();

    bool fullscreen = config.GetBool("context_fullscreen", false);
    bool vsync = config.GetBool("context_vsync", true);

    int defaultW = 1920;
    int defaultH = 1080;
    SDL_DisplayMode desktopMode;
    if (SDL_GetDesktopDisplayMode(0, &desktopMode) == 0 && desktopMode.w > 0 && desktopMode.h > 0) {
      if (fullscreen) {
        defaultW = desktopMode.w;
        defaultH = desktopMode.h;
      } else if (desktopMode.w > 1920 && desktopMode.h > 1080) {
        defaultW = 1920;
        defaultH = 1080;
      } else if (desktopMode.w >= 1920 && desktopMode.h >= 1080) {
        defaultW = 1600;
        defaultH = 900;
      } else {
        defaultW = std::min(1280, desktopMode.w);
        defaultH = std::min(720, desktopMode.h);
      }
    }

    width = config.GetInt("context_x", defaultW);
    height = config.GetInt("context_y", defaultH);
    bpp = config.GetInt("context_bpp", 32);
    int aaModeInt = clamp(config.GetInt("context_aa", (int)e_AntiAliasing_FXAA_Fast), 0, 2);
    renderer3DTask->SetVSync(vsync);
    renderer3DTask->SetAntiAliasingMode((e_AntiAliasingMode)aaModeInt);

    intrusive_ptr<Renderer3DMessage_CreateContext> createContext(new Renderer3DMessage_CreateContext(width, height, bpp, fullscreen));
    renderer3DTask->messageQueue.PushMessage(createContext);
    createContext->Wait();

    if (!createContext->success) {
      Log(e_FatalError, "GraphicsSystem", "Initialize", "Could not create context");
    } else {
      Log(e_Notice, "GraphicsSystem", "Initialize", "Created context on Main Thread, resolution " + int_to_str(width) + " * " + int_to_str(height) + " @ " + int_to_str(bpp) + " bpp");
    }

    task = new GraphicsTask(this);
    task->Run();
  }

  void GraphicsSystem::Exit() {
    // shutdown system task
    intrusive_ptr<Message_Shutdown> shutdown(new Message_Shutdown());
    task->messageQueue.PushMessage(shutdown);
    shutdown->Wait();

    task->Join();
    delete task;
    task = NULL;

    textureResourceManager.reset();
    vertexBufferResourceManager.reset();

    // shutdown renderer on Main Thread
    renderer3DTask->ShutdownMainThread();
    delete renderer3DTask;
    renderer3DTask = NULL;
  }

  e_SystemType GraphicsSystem::GetSystemType() const {
    return systemType;
  }

  ISystemScene *GraphicsSystem::CreateSystemScene(std::shared_ptr<IScene> scene) {
    if (scene->GetSceneType() == e_SceneType_Scene2D) {
      GraphicsScene *graphicsScene = new GraphicsScene(this);
      scene->Attach(graphicsScene->GetInterpreter(e_SceneType_Scene2D));
      return graphicsScene;
    }
    if (scene->GetSceneType() == e_SceneType_Scene3D) {
      GraphicsScene *graphicsScene = new GraphicsScene(this);
      scene->Attach(graphicsScene->GetInterpreter(e_SceneType_Scene3D));
      return graphicsScene;
    }
    return NULL;
  }

  ISystemTask *GraphicsSystem::GetTask() {
    return task;
  }

  Renderer3D *GraphicsSystem::GetRenderer3D() {
    return renderer3DTask;
  }

  MessageQueue<Overlay2DQueueEntry> &GraphicsSystem::GetOverlay2DQueue() {
    return overlay2DQueue;
  }

}
