// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "opengl_renderer3d.hpp"

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include <GL/gl.h>
//#include <GL/glcorearb.h>  // can be used to check for core profile only
#endif

#ifdef WIN32
#include <SDL2/SDL_opengl_glext.h>
#endif

#include <cmath>
#include <SDL2/SDL.h>

#include "base/log.hpp"
#include "managers/environmentmanager.hpp"
#include "managers/usereventmanager.hpp"
#include "types/command.hpp"
#include "base/sdl_surface.hpp"
#include "base/utils.hpp"
#include "base/math/bluntmath.hpp"

#include "base/geometry/aabb.hpp"
#include "base/geometry/trianglemeshutils.hpp"

#include "../resources/texture.hpp"


namespace blunted {

struct GLfunctions {
#define SDL_PROC(ret,func,params) ret (APIENTRYP func) params;
#include "sdl_glfuncs.h"
#undef SDL_PROC
};

  GLfunctions mapping;

  OpenGLRenderer3D::OpenGLRenderer3D() : context(0), window(0), contextIsActive(true), isFullscreen(false), vsyncEnabled(true), aaMode(e_AntiAliasing_FXAA_Quality), maxTextureSize(4096), windowedWidth(1600), windowedHeight(900) {
    FOV = 45;
    overallBrightness = 128;

    _cache_activeTextureUnit = -1;
    for (int i = 0; i < 16; i++) {
      _cache_boundTexture[i] = -1;
    }

    currentShader = shaders.end();

    //SetPriorityClass(thread.native_handle(), HIGH_PRIORITY_CLASS);
  };

  OpenGLRenderer3D::~OpenGLRenderer3D() {
  };

  void OpenGLRenderer3D::SwapBuffers() {
    SDL_GL_SwapWindow(window);
  }

  void OpenGLRenderer3D::SetMatrix(const std::string &shaderUniformName, const Matrix4 &matrix) {
    if (currentShader == shaders.end()) return;
    const Shader &sh = currentShader->second;
    int loc = -2;
    if (shaderUniformName == "modelMatrix") loc = sh.loc_modelMatrix;
    else if (shaderUniformName == "viewMatrix") loc = sh.loc_viewMatrix;
    else if (shaderUniformName == "projectionMatrix") loc = sh.loc_projectionMatrix;
    else if (shaderUniformName == "orthoProjectionMatrix") loc = sh.loc_orthoProjectionMatrix;
    else if (shaderUniformName == "orthoViewMatrix") loc = sh.loc_orthoViewMatrix;
    else if (shaderUniformName == "model") loc = sh.loc_model;
    else if (shaderUniformName == "projection") loc = sh.loc_projection;

    if (loc >= 0) {
      mapping.glUniformMatrix4fv(loc, 1, true, (float*)matrix.elements);
    } else if (loc == -2) {
      SetUniformMatrix4(currentShader->first, shaderUniformName, matrix);
    }
  }

  //void OpenGLRenderer3D::RenderOverlay2D(MessageQueue<Overlay2DQueueEntry> &overlay2DQueue) {
  void OpenGLRenderer3D::RenderOverlay2D(const std::vector<Overlay2DQueueEntry> &overlay2DQueue) {

    assert(context);

    if (overlay2DQueue.empty()) {
      return;
    }

    int drawableW = context_width;
    int drawableH = context_height;
    GetDrawableSize(drawableW, drawableH);

    float targetAspect = (float)context_width / (float)std::max(1, context_height);
    float drawableAspect = (float)drawableW / (float)std::max(1, drawableH);
    int outW = drawableW;
    int outH = drawableH;
    int outX = 0;
    int outY = 0;
    if (std::fabs(drawableAspect - targetAspect) > 0.01f) {
      if (drawableAspect > targetAspect) {
        outH = drawableH;
        outW = int(std::round(outH * targetAspect));
        outX = (drawableW - outW) / 2;
      } else {
        outW = drawableW;
        outH = int(std::round(outW / targetAspect));
        outY = (drawableH - outH) / 2;
      }
    }
    SetViewport(outX, outY, std::max(1, outW), std::max(1, outH));

    mapping.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mapping.glEnable(GL_BLEND);
    mapping.glDisable(GL_DEPTH_TEST);
    SetDepthMask(false);
    mapping.glDisable(GL_CULL_FACE);

    UseShader("overlay");

    Matrix4 orthoMatrix = CreateOrthoMatrix(0, context_width, context_height, 0, 0.1, 10);
    SetMatrix("projection", orthoMatrix);

    mapping.glBindVertexArray(overlayBuffer.vertexArrayID);
    SetTextureUnit(0);

    for (unsigned int i = 0; i < overlay2DQueue.size(); i++) {
      const Overlay2DQueueEntry &queueEntry = overlay2DQueue[i];

      Matrix4 modelMatrix(MATRIX4_IDENTITY);
      modelMatrix.SetTranslation(Vector3(queueEntry.position[0], queueEntry.position[1], -1.0f));
      modelMatrix.SetScale(Vector3(queueEntry.size[0], queueEntry.size[1], 1.0f));
      SetMatrix("model", modelMatrix);

      BindTexture(queueEntry.texture->GetResource()->GetID());
      mapping.glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    mapping.glBindVertexArray(0);

    SetViewport(0, 0, drawableW, drawableH);

    mapping.glEnable(GL_DEPTH_TEST);
    mapping.glEnable(GL_CULL_FACE);
    SetDepthMask(true);
    mapping.glDisable(GL_BLEND);
  }

  void OpenGLRenderer3D::RenderOverlay2D() {
    assert(context);

    Matrix4 orthoMatrix = CreateOrthoMatrix(-1, 1, -1, 1, 0.0f, 1.0f);
    SetMatrix("orthoProjectionMatrix", orthoMatrix);
    Matrix4 viewMatrix(MATRIX4_IDENTITY);
    viewMatrix.SetTranslation(Vector3(0, 0, -0.5f));
    SetMatrix("orthoViewMatrix", viewMatrix);

    SetCullingMode(e_CullingMode_Off);

    SetTextureUnit(4); // noise
    BindTexture(noiseTexID);
    SetTextureUnit(0);

    mapping.glBindVertexArray(quadBuffer.vertexArrayID);
    mapping.glDrawArrays(GL_TRIANGLES, 0, 6);
    mapping.glBindVertexArray(0);

    // unbind noise
    SetTextureUnit(4);
    BindTexture(0);

    SetCullingMode(e_CullingMode_Back);
  }

  void drawSphere(float r, int lats, int longs) {
  // Never called (because light.type is always set to 0) and uses deprecated methods.
  // Possible implementation, that uses modern OpenGl can be found here:
  // https://gist.github.com/zwzmzd/0195733fa1210346b00d
  /*
    int i, j;
    for (i = 0; i <= lats; i++) {
      float lat0 = pi * (-0.5 + (float) (i - 1) / lats);
      float z0 = std::sin(lat0);
      float zr0 = std::cos(lat0);

      float lat1 = pi * (-0.5 + (float) i / lats);
      float z1 = std::sin(lat1);
      float zr1 = std::cos(lat1);

      mapping.glBegin(GL_QUAD_STRIP);
      for (j = 0; j <= longs; j++) {
          float lng = 2 * pi * (float) (j - 1) / longs;
          float x = std::cos(lng);
          float y = std::sin(lng);

          mapping.glNormal3f(x * zr0, y * zr0, z0);
          mapping.glVertex3f(x * zr0 * r, y * zr0 * r, z0 * r);
          mapping.glNormal3f(x * zr1, y * zr1, z1);
          mapping.glVertex3f(x * zr1 * r, y * zr1 * r, z1 * r);
      }
      mapping.glEnd();
    }
   */
  }

  void OpenGLRenderer3D::RenderLights(std::deque<LightQueueEntry> &lightQueue, const Matrix4 &projectionMatrix, const Matrix4 &viewMatrix) {
    if (currentShader == shaders.end()) return;
    const Shader &sh = currentShader->second;
    Vector3 cameraPos = viewMatrix.GetInverse().GetTranslation();

    if (sh.loc_cameraPosition >= 0) {
      mapping.glUniform3f(sh.loc_cameraPosition, cameraPos.coords[0], cameraPos.coords[1], cameraPos.coords[2]);
    }

    Matrix4 toTexCoords(MATRIX4_IDENTITY);
    toTexCoords.SetTranslation(Vector3(0.5f, 0.5f, 0.5f));
    toTexCoords.SetScale(Vector3(0.5f, 0.5f, 0.5f));

    Matrix4 orthoMatrix = CreateOrthoMatrix(-1, 1, -1, 1, 0.0f, 1.0f);
    Matrix4 xviewMatrix(MATRIX4_IDENTITY);
    xviewMatrix.SetTranslation(Vector3(0, 0, -0.5f));
    Matrix4 identityModel(MATRIX4_IDENTITY);

    std::deque<LightQueueEntry>::iterator lightIter = lightQueue.begin();

    while (lightIter != lightQueue.end()) {
      const LightQueueEntry &light = (*lightIter);

      if (sh.loc_hasShadow >= 0) {
        mapping.glUniform1i(sh.loc_hasShadow, (int)light.hasShadow);
      }
      if (light.hasShadow) {
        SetTextureUnit(7);
        BindTexture(light.shadowMapTexture->GetResource()->GetID());

        if (sh.loc_lightViewProjectionMatrix >= 0) {
          Matrix4 lvp = toTexCoords * (*lightIter).lightProjectionMatrix * (*lightIter).lightViewMatrix;
          mapping.glUniformMatrix4fv(sh.loc_lightViewProjectionMatrix, 1, true, (float*)lvp.elements);
        }
      }

      if (sh.loc_lightColor >= 0) {
        mapping.glUniform3f(sh.loc_lightColor, light.color.coords[0], light.color.coords[1], light.color.coords[2]);
      }
      if (sh.loc_lightRadius >= 0) {
        mapping.glUniform1f(sh.loc_lightRadius, light.radius);
      }
      if (sh.loc_lightPosition >= 0) {
        mapping.glUniform3f(sh.loc_lightPosition, light.position.coords[0], light.position.coords[1], light.position.coords[2]);
      }

      int quad_or_sphere = 1;
      if (light.type == 0) {
        quad_or_sphere = 0;
      } else if (light.type == 1) {
        AABB aabb = light.aabb;
        if (cameraPos.GetDistance(light.position) <= aabb.GetRadius()) {
          quad_or_sphere = 0;
        } else {
          quad_or_sphere = 1;
        }
      }

      if (quad_or_sphere == 0) {
        SetCullingMode(e_CullingMode_Off);
        SetDepthFunction(e_DepthFunction_Always);

        if (sh.loc_projectionMatrix >= 0) mapping.glUniformMatrix4fv(sh.loc_projectionMatrix, 1, true, (float*)orthoMatrix.elements);
        if (sh.loc_viewMatrix >= 0) mapping.glUniformMatrix4fv(sh.loc_viewMatrix, 1, true, (float*)xviewMatrix.elements);
        if (sh.loc_modelMatrix >= 0) mapping.glUniformMatrix4fv(sh.loc_modelMatrix, 1, true, (float*)identityModel.elements);

        mapping.glBindVertexArray(quadBuffer.vertexArrayID);
        mapping.glDrawArrays(GL_TRIANGLES, 0, 6);
        mapping.glBindVertexArray(0);
      } else {
        SetCullingMode(e_CullingMode_Back);
        SetDepthFunction(e_DepthFunction_Less);

        AABB aabb = light.aabb;

        SetMatrix("projectionMatrix", projectionMatrix);
        SetMatrix("viewMatrix", viewMatrix);
        Matrix4 modelMatrix(MATRIX4_IDENTITY);
        modelMatrix.SetTranslation(light.position);
        SetMatrix("modelMatrix", modelMatrix);
        drawSphere(aabb.GetRadius() * 0.52f, 6, 6);
      }

      if (light.hasShadow) {
        SetTextureUnit(7);
        BindTexture(0);
        SetTextureUnit(0);
      }

      lightIter++;
    }

  }


  // --- new & improved


  // init & exit

  VertexBufferID OpenGLRenderer3D::CreateSimpleVertexBuffer(GLfloat *vertices, unsigned int size) {
    GLuint VBO, VAO;
    mapping.glGenVertexArrays(1, &VAO);
    mapping.glGenBuffers(1, &VBO);

    mapping.glBindVertexArray(VAO);

    mapping.glBindBuffer(GL_ARRAY_BUFFER, VBO);
    mapping.glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);

    mapping.glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)0);
    mapping.glEnableVertexAttribArray(0);

    mapping.glBindBuffer(GL_ARRAY_BUFFER, 0);
    mapping.glBindVertexArray(0);

    VertexBufferID vertexBufferId;
    vertexBufferId.bufferID = VBO;
    vertexBufferId.vertexArrayID = VAO;
    return vertexBufferId;
  }

  void OpenGLRenderer3D::DeleteSimpleVertexBuffer(VertexBufferID vertexBufferID) {
    GLuint glVertexBufferID = vertexBufferID.bufferID;
    //Log(e_Notice, "OpenGLRenderer3D", "DeleteSimpleVertexBuffer", "Deleting simple vertex buffer, id " + int_to_str(vertexBufferID.bufferID));
    mapping.glDeleteBuffers(1, &glVertexBufferID);

    GLuint glVertexArrayID = vertexBufferID.vertexArrayID;
    mapping.glDeleteVertexArrays(1, &glVertexArrayID);
  }

// This method was introduced to replace OpenGL's fixed function pipeline
// (e.g. glBegin, glEnd, etc.) that was used for overlay rendering.
  void OpenGLRenderer3D::InitializeOverlayAndQuadBuffers() {
    GLfloat overlayVertices[] = {
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,

        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f
    };
    overlayBuffer = CreateSimpleVertexBuffer(overlayVertices, sizeof(overlayVertices));

    GLfloat quadVertices[] = {
        -1.0f,  1.0f, 0.0f, 1.0f,
        1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,

        -1.0f,  1.0f, 0.0f, 1.0f,
        1.0f,  1.0f, 0.0f, 1.0f,
        1.0f, -1.0f, 0.0f, 1.0f
    };
    quadBuffer = CreateSimpleVertexBuffer(quadVertices, sizeof(quadVertices));
  }

  bool OpenGLRenderer3D::CreateContext(int width, int height, int bpp, bool fullscreen) {
    this->context_width = width;
    this->context_height = height;
    this->context_bpp = bpp;
    this->isFullscreen = fullscreen;
    this->windowedWidth = width;
    this->windowedHeight = height;

    // default values
    this->cameraNear = 3.0f;
    this->cameraFar = 340.0f;

    // SDL subsystems must be initialized before setting attributes
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen) {
      windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    window = SDL_CreateWindow("Gameplay Football",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              width, height,
                              windowFlags);
    context = SDL_GL_CreateContext(window);
    SetVSync(vsyncEnabled);


    //    *reinterpret_cast<void**>(&(mapping.func)) =
    //    SDL_GL_GetProcAddress(#func); *reinterpret_cast<void**>(&(mapping.func))
    //    = (void*) func;
#define SDL_PROC(ret,func,params)                                        \
  do {                                                                     \
    *reinterpret_cast<void **>(&(mapping.func)) =                          \
        SDL_GL_GetProcAddress(#func);                                      \
    if (!mapping.func) {                                                   \
      printf("Couldn't load GL function %s: %s\n", #func, SDL_GetError()); \
      SDL_SetError("Couldn't load GL function %s: %s\n", #func,            \
                   SDL_GetError());                                        \
      exit(1);                                                             \
    }                                                                      \
  } while (0);
#include "sdl_glfuncs.h"
#undef SDL_PROC

    std::string glVersionString = (char*)mapping.glGetString(GL_VERSION);
    Log(e_Notice, "OpenGLRenderer3D", "CreateContext", "Using OpenGL version " + glVersionString);

    int glVersion[2] = { 0, 0 };
    mapping.glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]);
    mapping.glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]);

    Log(e_Notice, "OpenGLRenderer3D", "CreateContext", "OpenGL major/minor " + int_to_str(glVersion[0]) + "." + int_to_str(glVersion[1]));
    if (!context) {
      std::string errorString = SDL_GetError();
      Log(e_FatalError, "OpenGLRenderer3D", "CreateContext", "Failed on SDL error: " + errorString);
      return false;
    }
    bool higherThan32 = false;
    if (glVersion[0] < 4) {
      if (glVersion[0] == 3 && glVersion[1] >= 2) higherThan32 = true;
    } else higherThan32 = true;

    if (!higherThan32) Log(e_Warning, "OpenGLRenderer3D", "CreateContext", "OpenGL version not equal to or higher than 3.2 (or not reported as such)");

#ifdef WIN32
    // VK: TODO check if centering is still required with SDL2
    /*
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWMInfo(&wmInfo)) {

      HWND hWnd = wmInfo.window;
      // center window
      // todo: needs linux version for centering as well. not sure how. find out.
      if (!fullscreen) {
        //#undef _WIN32_WINNT
        //#define _WIN32_WINNT 0x0500
        //#undef WINVER
        //#define WINVER 0x0500
        //#include <windows.h>
        //#include <winuser.h>
        HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monInfo;
        monInfo.cbSize = sizeof(MONITORINFO);
        bool getMonInfo = GetMonitorInfo(mon, &monInfo);
        if (getMonInfo != 0) {
          RECT rect = monInfo.rcWork;
          int xSize = rect.right - rect.left;
          int xCenter = rect.left + (xSize * 0.5f);
          int ySize = rect.bottom - rect.top;
          int yCenter = rect.top + (ySize * 0.5f);
          int xWin = xCenter - (width * 0.5f);
          int yWin = yCenter - (height * 0.5f);
          xWin = std::max(xWin, 0); // super annoying to have a window's controls being unreachable
          yWin = std::max(yWin, 0);
          xWin -= 3; // window border size (educated guess - is a user setting, after all)
          yWin -= 12; // window border size + taskbar (educated guess - is a user setting, after all)
          SetWindowPos(hWnd, NULL, xWin, yWin, 0, 0, SWP_NOSIZE);
        }
        //SetWindowPos(hWnd, NULL, 976, 0, 0, 0, SWP_NOSIZE);
      }

      //HDC hDC = GetDC(wmInfo.window);
      //wglMakeCurrent(hDC, wmInfo.hglrc);
    }
     */
#endif

#ifdef WIN32
    bool success = false;//wglSwapIntervalEXT(-1);
    //if (!success) wglSwapIntervalEXT(1);
    //if (!success) printf("ANTI TEAR NOT SUPPORTED\n\n\n\n\n");
#endif

#ifdef __linux__
    // VK TODO: investigate it
    bool success = false;//glXSwapIntervalSGI(-1); // anti-tear blah
    //if (!success) mapping.glXSwapIntervalSGI(1);
    //if (!success) printf("ANTI TEAR NOT SUPPORTED\n\n\n\n\n");
#endif

    //SDL_ShowCursor(SDL_DISABLE); // todo: make customizable
    //SDL_WarpMouse(1280 * 3, 1024);
    largest_supported_anisotropy = 2;
    mapping.glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
    GLint glMaxTex = 4096;
    mapping.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glMaxTex);
    if (glMaxTex >= 2048) maxTextureSize = (int)glMaxTex;
    //largest_supported_anisotropy = clamp(largest_supported_anisotropy, 0, 8); // don't overdo it


    //GLfloat color[4] = { 0, 0, 0, 0 };
    //mapping.glMaterialfv(GL_FRONT, GL_SPECULAR, color);
    //mapping.glMateriali(GL_FRONT, GL_SHININESS, 80);

    // enable color tracking
    //mapping.glEnable(GL_COLOR_MATERIAL);
    //mapping.glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    mapping.glDisable(GL_MULTISAMPLE);
    mapping.glCullFace(GL_BACK);


    // shaders

    LoadShader("simple", "media/shaders/simple");
    LoadShader("lighting", "media/shaders/lighting");
    LoadShader("ambient", "media/shaders/ambient");
    LoadShader("zphase", "media/shaders/zphase");
    LoadShader("postprocess", "media/shaders/postprocess");
    LoadShader("aa", "media/shaders/aa");
    LoadShader("overlay", "media/shaders/overlay");

    currentShader = shaders.begin();


    SDL_Surface *noise = IMG_Load("media/shaders/noise.png");
    noiseTexID = CreateTexture(e_InternalPixelFormat_RGB8, e_PixelFormat_RGB, noise->w, noise->h, false, true, false, true, false);
    UpdateTexture(noiseTexID, noise, false, false);
    SDL_FreeSurface(noise);

    // create buffers for overlay and simple quad rendering to use shaders instead of fixed pipeline
    InitializeOverlayAndQuadBuffers();

    mapping.glClearColor(0, 0, 0, 0);
    mapping.glClearDepth(1.0);
    mapping.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    mapping.glDisable(GL_MULTISAMPLE);

    return true;
  }

  void OpenGLRenderer3D::Exit() {
    DeleteTexture(noiseTexID);

    std::map<std::string, Shader>::iterator shaderIter = shaders.begin();
    while (shaderIter != shaders.end()) {
      mapping.glDeleteShader((*shaderIter).second.vertexShaderID);
      mapping.glDeleteShader((*shaderIter).second.fragmentShaderID);
      mapping.glDeleteProgram((*shaderIter).second.programID);
      shaderIter++;
    }

    currentShader = shaders.end();

    DeleteSimpleVertexBuffer(overlayBuffer);
    DeleteSimpleVertexBuffer(quadBuffer);

    // assert(views.size() == 0);
    // todo: make views erase-able, as for now a views vector index is used when requesting views, which prohibits erasion
    // (actually, erasion 'works', but it does leave 'empty' records in the views vector, which is ugly and memory-leaky)
  }

  void OpenGLRenderer3D::ComputeViewRenderSize(const View &view, int &renderW, int &renderH) {
    int drawableW = context_width;
    int drawableH = context_height;
    GetDrawableSize(drawableW, drawableH);

    float targetAspect = (float)context_width / (float)std::max(1, context_height);
    float drawableAspect = (float)drawableW / (float)std::max(1, drawableH);
    int fitW = drawableW;
    int fitH = drawableH;
    if (std::fabs(drawableAspect - targetAspect) > 0.01f) {
      if (drawableAspect > targetAspect) {
        fitH = drawableH;
        fitW = int(std::round(fitH * targetAspect));
      } else {
        fitW = drawableW;
        fitH = int(std::round(fitW / targetAspect));
      }
    }

    int nativeViewW = std::max(64, int(std::round(view.width_percent * 0.01f * fitW)));
    int nativeViewH = std::max(64, int(std::round(view.height_percent * 0.01f * fitH)));

    float ssaaScale = (aaMode == e_AntiAliasing_Ultra_SSAA) ? 1.35f : 1.0f;
    int targetW = std::max(64, int(std::round(nativeViewW * ssaaScale)));
    int targetH = std::max(64, int(std::round(nativeViewH * ssaaScale)));

    int maxSafeW = std::min(maxTextureSize, std::max(nativeViewW, 3840));
    int maxSafeH = std::min(maxTextureSize, std::max(nativeViewH, 2160));
    if (targetW > maxSafeW || targetH > maxSafeH) {
      float clampScale = std::min((float)maxSafeW / (float)targetW, (float)maxSafeH / (float)targetH);
      targetW = std::max(64, int(std::round(targetW * clampScale)));
      targetH = std::max(64, int(std::round(targetH * clampScale)));
    }

    renderW = (targetW + 1) & ~1;
    renderH = (targetH + 1) & ~1;
  }

  void OpenGLRenderer3D::AllocateViewBuffers(View &view, int renderW, int renderH) {
    view.width = renderW;
    view.height = renderH;

    std::vector<e_TargetAttachment> targets;

    // G-buffer
    view.gBufferID = CreateFrameBuffer();
    BindFrameBuffer(view.gBufferID);

    view.gBuffer_DepthTexID = CreateTexture(e_InternalPixelFormat_DepthComponent32F, e_PixelFormat_DepthComponent, renderW, renderH, false, false, false, false, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Depth, view.gBuffer_DepthTexID);

    view.gBuffer_AlbedoTexID = CreateTexture(e_InternalPixelFormat_RGBA16F, e_PixelFormat_RGBA, renderW, renderH, false, false, false, false, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Color0, view.gBuffer_AlbedoTexID);

    view.gBuffer_NormalTexID = CreateTexture(e_InternalPixelFormat_RGBA16F, e_PixelFormat_RGBA, renderW, renderH, false, false, false, false, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Color1, view.gBuffer_NormalTexID);

    view.gBuffer_AuxTexID = CreateTexture(e_InternalPixelFormat_R8, e_PixelFormat_Red, renderW, renderH, false, false, false, false, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Color2, view.gBuffer_AuxTexID);

    targets.push_back(e_TargetAttachment_Color0);
    targets.push_back(e_TargetAttachment_Color1);
    targets.push_back(e_TargetAttachment_Color2);
    SetRenderTargets(targets);
    targets.clear();

    if (!CheckFrameBufferStatus()) Log(e_FatalError, "OpenGLRenderer3D", "AllocateViewBuffers", "Could not create G-buffer");

    // Accumulation buffer
    view.accumBufferID = CreateFrameBuffer();
    BindFrameBuffer(view.accumBufferID);

    view.accumBuffer_AccumTexID = CreateTexture(e_InternalPixelFormat_RGBA16F, e_PixelFormat_RGBA, renderW, renderH, false, false, false, true, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Color0, view.accumBuffer_AccumTexID);

    view.accumBuffer_ModifierTexID = CreateTexture(e_InternalPixelFormat_RG16F, e_PixelFormat_RG, renderW, renderH, false, false, false, true, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Color1, view.accumBuffer_ModifierTexID);

    targets.push_back(e_TargetAttachment_Color0);
    targets.push_back(e_TargetAttachment_Color1);
    SetRenderTargets(targets);
    targets.clear();

    if (!CheckFrameBufferStatus()) Log(e_FatalError, "OpenGLRenderer3D", "AllocateViewBuffers", "Could not create Accumulation buffer");

    // Tone-mapped sRGB + Perceptual Luma post-process buffer for Anti-Aliasing pass
    view.postProcessBufferID = CreateFrameBuffer();
    BindFrameBuffer(view.postProcessBufferID);

    view.postProcessTexID = CreateTexture(e_InternalPixelFormat_RGBA8, e_PixelFormat_RGBA, renderW, renderH, true, false, false, true, false);
    SetFrameBufferTexture2D(e_TargetAttachment_Color0, view.postProcessTexID);

    targets.push_back(e_TargetAttachment_Color0);
    SetRenderTargets(targets);
    targets.clear();

    if (!CheckFrameBufferStatus()) Log(e_FatalError, "OpenGLRenderer3D", "AllocateViewBuffers", "Could not create PostProcess buffer");

    BindFrameBuffer(0);
    targets.push_back(e_TargetAttachment_Back);
    SetRenderTargets(targets);
    targets.clear();
  }

  void OpenGLRenderer3D::DestroyViewBuffers(View &view) {
    if (view.gBufferID != 0) {
      BindFrameBuffer(view.gBufferID);
      SetFrameBufferTexture2D(e_TargetAttachment_Depth, 0);
      SetFrameBufferTexture2D(e_TargetAttachment_Color0, 0);
      SetFrameBufferTexture2D(e_TargetAttachment_Color1, 0);
      SetFrameBufferTexture2D(e_TargetAttachment_Color2, 0);
      DeleteFrameBuffer(view.gBufferID);
      view.gBufferID = 0;
    }

    if (view.accumBufferID != 0) {
      BindFrameBuffer(view.accumBufferID);
      SetFrameBufferTexture2D(e_TargetAttachment_Color0, 0);
      SetFrameBufferTexture2D(e_TargetAttachment_Color1, 0);
      DeleteFrameBuffer(view.accumBufferID);
      view.accumBufferID = 0;
    }

    if (view.postProcessBufferID != 0) {
      BindFrameBuffer(view.postProcessBufferID);
      SetFrameBufferTexture2D(e_TargetAttachment_Color0, 0);
      DeleteFrameBuffer(view.postProcessBufferID);
      view.postProcessBufferID = 0;
    }

    BindFrameBuffer(0);

    if (view.gBuffer_AlbedoTexID != 0) { DeleteTexture(view.gBuffer_AlbedoTexID); view.gBuffer_AlbedoTexID = 0; }
    if (view.gBuffer_NormalTexID != 0) { DeleteTexture(view.gBuffer_NormalTexID); view.gBuffer_NormalTexID = 0; }
    if (view.gBuffer_DepthTexID != 0)  { DeleteTexture(view.gBuffer_DepthTexID);  view.gBuffer_DepthTexID = 0; }
    if (view.gBuffer_AuxTexID != 0)    { DeleteTexture(view.gBuffer_AuxTexID);    view.gBuffer_AuxTexID = 0; }

    if (view.accumBuffer_AccumTexID != 0)    { DeleteTexture(view.accumBuffer_AccumTexID);    view.accumBuffer_AccumTexID = 0; }
    if (view.accumBuffer_ModifierTexID != 0) { DeleteTexture(view.accumBuffer_ModifierTexID); view.accumBuffer_ModifierTexID = 0; }

    if (view.postProcessTexID != 0) { DeleteTexture(view.postProcessTexID); view.postProcessTexID = 0; }
  }

  void OpenGLRenderer3D::RefreshAllViews() {
    if (!context) return;
    for (size_t i = 0; i < views.size(); ++i) {
      View &view = views[i];
      if (!view.active) continue;
      int newW = view.width;
      int newH = view.height;
      ComputeViewRenderSize(view, newW, newH);
      if (newW != view.width || newH != view.height) {
        DestroyViewBuffers(view);
        AllocateViewBuffers(view, newW, newH);
      }
    }
  }

  int OpenGLRenderer3D::CreateView(float x_percent, float y_percent, float width_percent, float height_percent) {

    Log(e_Notice, "OpenGLRenderer3D", "CreateView", "Creating new view, id " + int_to_str(views.size()));

    View view;
    view.active = true;
    view.target = e_ViewRenderTarget_Context;
    view.targetTexID = 0;
    view.x_percent = x_percent;
    view.y_percent = y_percent;
    view.width_percent = width_percent;
    view.height_percent = height_percent;
    view.x = int(floor(x_percent * 0.01 * context_width));
    view.y = int(floor(y_percent * 0.01 * context_height));
    view.baseWidth = std::max(1, int(floor(width_percent * 0.01 * context_width)));
    view.baseHeight = std::max(1, int(floor(height_percent * 0.01 * context_height)));

    int renderW = view.baseWidth;
    int renderH = view.baseHeight;
    ComputeViewRenderSize(view, renderW, renderH);
    AllocateViewBuffers(view, renderW, renderH);

    views.push_back(view);

    return views.size() - 1;
  }

  View &OpenGLRenderer3D::GetView(int viewID) {
    return views.at(viewID);
  }

  void OpenGLRenderer3D::DeleteView(int viewID) {

    Log(e_Notice, "OpenGLRenderer3D", "DeleteView", "Deleting view, id " + int_to_str(viewID));

    View *view = &views.at(viewID);
    view->active = false;
    DestroyViewBuffers(*view);
  }

  // general

  void OpenGLRenderer3D::SetCullingMode(e_CullingMode cullingMode) {

    switch (cullingMode) {

      case e_CullingMode_Off:     mapping.glDisable(GL_CULL_FACE);
                                  break;

      case e_CullingMode_Front:   mapping.glEnable(GL_CULL_FACE);
                                  mapping.glCullFace(GL_FRONT);
                                  break;

      case e_CullingMode_Back:    mapping.glEnable(GL_CULL_FACE);
                                  mapping.glCullFace(GL_BACK);
                                  break;
    }
  }

  void OpenGLRenderer3D::SetBlendingMode(e_BlendingMode blendingMode) {
    switch (blendingMode) {

      case e_BlendingMode_Off:    mapping.glDisable(GL_BLEND);
                                  break;

      case e_BlendingMode_On:     mapping.glEnable(GL_BLEND);
                                  break;
    }
  }

  GLenum GetGLBlendingFunction(e_BlendingFunction blendingFunction) {

    GLenum GLfunc = GL_ONE;

    switch (blendingFunction) {

      case e_BlendingFunction_Zero : GLfunc = GL_ZERO;
                                     break;

      case e_BlendingFunction_One :  GLfunc = GL_ONE;
                                     break;
    }

    return GLfunc;
  }

  void OpenGLRenderer3D::SetBlendingFunction(e_BlendingFunction blendingFunction1, e_BlendingFunction blendingFunction2) {

    GLenum func1 = GetGLBlendingFunction(blendingFunction1);
    GLenum func2 = GetGLBlendingFunction(blendingFunction2);

    mapping.glBlendFunc(func1, func2);
  }

  void OpenGLRenderer3D::SetDepthFunction(e_DepthFunction depthFunction) {
    switch (depthFunction) {

      case e_DepthFunction_Never:           mapping.glDepthFunc(GL_NEVER);
                                            break;

      case e_DepthFunction_Equal:           mapping.glDepthFunc(GL_EQUAL);
                                            break;

      case e_DepthFunction_Greater:         mapping.glDepthFunc(GL_GREATER);
                                            break;

      case e_DepthFunction_Less:            mapping.glDepthFunc(GL_LESS);
                                            break;

      case e_DepthFunction_GreaterOrEqual:  mapping.glDepthFunc(GL_GEQUAL);
                                            break;

      case e_DepthFunction_LessOrEqual:     mapping.glDepthFunc(GL_LEQUAL);
                                            break;

      case e_DepthFunction_NotEqual:        mapping.glDepthFunc(GL_NOTEQUAL);
                                            break;

      case e_DepthFunction_Always:          mapping.glDepthFunc(GL_ALWAYS);
                                            break;
    }
  }

  void OpenGLRenderer3D::SetDepthTesting(bool OnOff) {
    if (OnOff) {
      mapping.glEnable(GL_DEPTH_TEST);
    } else {
      mapping.glDisable(GL_DEPTH_TEST);
    }
  }

  void OpenGLRenderer3D::SetDepthMask(bool OnOff) {
    if (OnOff) {
      mapping.glDepthMask(GL_TRUE);
    } else {
      mapping.glDepthMask(GL_FALSE);
    }
  }

  void OpenGLRenderer3D::SetTextureMode(e_TextureMode textureMode) {
    // todo: err its not a mode i guess
    // Never called. Uses deprecated methods
    /*
      switch (textureMode) {

        case e_TextureMode_Off:     mapping.glDisable(GL_TEXTURE_2D);
                                    break;

        case e_TextureMode_2D:      mapping.glEnable(GL_TEXTURE_2D);
                                    break;
      }
     */
    }

    void OpenGLRenderer3D::SetColor(const Vector3 &color, float alpha) {
      mapping.glColor4f(color.coords[0], color.coords[1], color.coords[2], alpha);
    }

    void OpenGLRenderer3D::SetColorMask(bool r, bool g, bool b, bool alpha) {
      mapping.glColorMask(r, g, b, alpha);
    }

    void OpenGLRenderer3D::ClearBuffer(const Vector3 &color, bool clearDepth, bool clearColor) {
  //xx    glClearColor(color.coords[0], color.coords[1], color.coords[2], 0.0);
      //REDUNDANT? deprecated?
  //xx    glClearDepth(1.0f);
      //glClear((GL_COLOR_BUFFER_BIT && clearColor) | (GL_DEPTH_BUFFER_BIT && clearDepth));
      GLbitfield mask = 0;
      if (clearDepth) mask |= GL_DEPTH_BUFFER_BIT;
      if (clearColor) mask |= GL_COLOR_BUFFER_BIT;
      mapping.glClear(mask);
    }

    Matrix4 OpenGLRenderer3D::CreatePerspectiveMatrix(float aspectRatio, float nearCap, float farCap) {
      //XX deprecated glMatrixMode(GL_PROJECTION);
      //XX deprecated glLoadIdentity();
      float Xnear = nearCap;
      if (Xnear == -1) Xnear = cameraNear;
      float Xfar = farCap;
      if (Xfar == -1) Xfar = cameraFar;
      //XX deprecated gluPerspective(FOV, aspectRatio, Xnear, Xfar);

      Matrix4 projectionMatrix;
      projectionMatrix.ConstructProjection(FOV, aspectRatio, Xnear, Xfar);

      return projectionMatrix;
    }

    Matrix4 OpenGLRenderer3D::CreateOrthoMatrix(float left, float right, float bottom, float top, float nearCap, float farCap) {
      //XX deprecated glMatrixMode(GL_PROJECTION);
      //XX deprecated glLoadIdentity();
      float Xnear = nearCap;
      if (Xnear == -1) Xnear = cameraNear;
      float Xfar = farCap;
      if (Xfar == -1) Xfar = cameraFar;
      //XX deprecated glOrtho(left, right, bottom, top, Xnear, Xfar);

      Matrix4 orthoMatrix;
      orthoMatrix.ConstructOrtho(left, right, bottom, top, Xnear, Xfar);

      return orthoMatrix;
    }


    // vertex buffers

    GLenum GetGLVertexBufferUsage(e_VertexBufferUsage usage) {

      GLenum GLfunc = GL_STREAM_DRAW;

      switch (usage) {

        case e_VertexBufferUsage_StreamDraw: GLfunc = GL_STREAM_DRAW;
                                             break;

        case e_VertexBufferUsage_StreamRead: GLfunc = GL_STREAM_READ;
                                             break;

        case e_VertexBufferUsage_StreamCopy: GLfunc = GL_STREAM_COPY;
                                             break;

        case e_VertexBufferUsage_StaticDraw: GLfunc = GL_STATIC_DRAW;
                                             break;

        case e_VertexBufferUsage_StaticRead: GLfunc = GL_STATIC_READ;
                                             break;

        case e_VertexBufferUsage_StaticCopy: GLfunc = GL_STATIC_COPY;
                                             break;

        case e_VertexBufferUsage_DynamicDraw: GLfunc = GL_DYNAMIC_DRAW;
                                              break;

        case e_VertexBufferUsage_DynamicRead: GLfunc = GL_DYNAMIC_READ;
                                              break;

        case e_VertexBufferUsage_DynamicCopy: GLfunc = GL_DYNAMIC_COPY;
                                              break;

      }

      return GLfunc;
    }

    VertexBufferID OpenGLRenderer3D::CreateVertexBuffer(float *vertices, unsigned int verticesDataSize, std::vector<unsigned int> indices, e_VertexBufferUsage usage, const std::vector<float> &skinData) {

      GLuint iid; // element indices
      unsigned int vertexArrayID;
      GLuint skinBid = 0;

      bool pingPong = false;
      if (usage == e_VertexBufferUsage_DynamicDraw || usage == e_VertexBufferUsage_DynamicRead || usage == e_VertexBufferUsage_DynamicCopy ||
          usage == e_VertexBufferUsage_StreamDraw || usage == e_VertexBufferUsage_StreamRead || usage == e_VertexBufferUsage_StreamCopy) pingPong = true;
      pingPong = false; // use orphaning instead
      int number = 1;
      if (pingPong) number = 2;

      int firstBuffer_id = 0;
      int firstArray_id = 0;

      if (!skinData.empty()) {
        mapping.glGenBuffers(1, &skinBid);
        mapping.glBindBuffer(GL_ARRAY_BUFFER, skinBid);
        mapping.glBufferData(GL_ARRAY_BUFFER, skinData.size() * sizeof(float), &skinData[0], GL_STATIC_DRAW);
        mapping.glBindBuffer(GL_ARRAY_BUFFER, 0);
      }

      for (int b = 0; b < number; b++) {
        GLuint vid;
        mapping.glGenVertexArrays(1, &vid);
        mapping.glBindVertexArray(vid);
        vertexArrayID = vid;
        if (b == 0) firstArray_id = vertexArrayID;


        GLuint bid;
        mapping.glGenBuffers(1, &bid);
        int buffer_id = bid;
        if (b == 0) firstBuffer_id = buffer_id;
        //printf("array size: %i\n", triangleCount * 3 * 3);
        mapping.glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
        mapping.glBufferData(GL_ARRAY_BUFFER, verticesDataSize * sizeof(float), NULL, GetGLVertexBufferUsage(usage));
        mapping.glBufferSubData(GL_ARRAY_BUFFER, 0, verticesDataSize * sizeof(float), vertices);


        // generate a buffer for the indices

        if (indices.size() == 0) {
          for (unsigned int i = 0; i < verticesDataSize / GetTriangleMeshElementCount() / 3; i++) {
            indices.push_back(i);
          }
        }
        mapping.glGenBuffers(1, &iid);
        mapping.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iid);
        mapping.glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), NULL, GetGLVertexBufferUsage(usage));
        mapping.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices.size() * sizeof(unsigned int), &indices[0]);


        #define BUFFER_OFFSET( i ) ((char *)NULL + (i))
        for (int i = 0; i < GetTriangleMeshElementCount(); i++) {
          mapping.glVertexAttribPointer((GLuint)i, 3, GL_FLOAT, GL_FALSE, 0, BUFFER_OFFSET(verticesDataSize / GetTriangleMeshElementCount() * i * sizeof(float)));
          mapping.glEnableVertexAttribArray(i);
        }

        if (skinBid != 0) {
          mapping.glBindBuffer(GL_ARRAY_BUFFER, skinBid);
          mapping.glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), BUFFER_OFFSET(0));
          mapping.glEnableVertexAttribArray(5);
          mapping.glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), BUFFER_OFFSET(4 * sizeof(float)));
          mapping.glEnableVertexAttribArray(6);
        }

        if (b == 1) {
          VAOReadIndex.insert(std::pair<int, int>(firstArray_id, 0));
          VBOPingPongMap.insert(std::pair<int, int>(firstBuffer_id, buffer_id));
          VAOPingPongMap.insert(std::pair<int, int>(firstArray_id, vertexArrayID));
        }
      }

      mapping.glBindVertexArray(0);

      mapping.glBindBuffer(GL_ARRAY_BUFFER, 0);
      mapping.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

      VertexBufferID vertexBufferID;
      vertexBufferID.bufferID = firstBuffer_id;
      vertexBufferID.vertexArrayID = firstArray_id;
      vertexBufferID.elementArrayID = iid;
      vertexBufferID.skinBufferID = skinBid;

      //vertexArrayID = firstArray_id;
      return vertexBufferID;
    }

    void OpenGLRenderer3D::UpdateVertexBuffer(VertexBufferID vertexBufferID, float *vertices, unsigned int verticesDataSize) {
      int writeVertexBufferID = vertexBufferID.bufferID;
      int writeVertexArrayID = vertexBufferID.vertexArrayID;

      int pingPongReadSwitch = 0;
      //bool pingPong = false;
      std::map<int, int>::iterator pingPongIter = VAOReadIndex.find(vertexBufferID.vertexArrayID);
      if (pingPongIter != VAOReadIndex.end()) {
        pingPongReadSwitch = pingPongIter->second;
        //pingPong = true;

        pingPongIter->second++;
        if (pingPongIter->second == 2) pingPongIter->second = 0;
      }

      if (pingPongReadSwitch == 0) {
        pingPongIter = VBOPingPongMap.find(vertexBufferID.bufferID);
        if (pingPongIter != VBOPingPongMap.end()) {
          writeVertexBufferID = pingPongIter->second;
        }

        pingPongIter = VAOPingPongMap.find(vertexBufferID.vertexArrayID);
        if (pingPongIter != VAOPingPongMap.end()) {
          writeVertexArrayID = pingPongIter->second;
        }

      }

      mapping.glBindVertexArray((GLuint)writeVertexArrayID);
      mapping.glBindBuffer(GL_ARRAY_BUFFER, writeVertexBufferID); // todo: why is this necessary? shouldn't this be part of the vao?

      //glBufferData(GL_ARRAY_BUFFER, verticesDataSize * sizeof(float), vertices, GL_DYNAMIC_DRAW);

  //    glBufferData(GL_ARRAY_BUFFER, verticesDataSize * sizeof(float), NULL, GL_DYNAMIC_DRAW);

  //    if (!pingPong) {
        // i don't think we can use glMapBufferRange with GL_MAP_UNSYNCHRONIZED_BIT in a non-pingponged situation, but may want to test this sometimes
  //      glBufferSubData(GL_ARRAY_BUFFER, 0, verticesDataSize * sizeof(float), vertices);
  //    } else {
        //glBufferData(GL_ARRAY_BUFFER, verticesDataSize * sizeof(float), NULL, GL_STREAM_DRAW);
        //float *ptr = (float*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

        // the next 2 statements, as well as the invalidate_bit stuff in the 3rd statement, should do the same thing: orphaning the vertexbuffer.
        // however, on my current AMD (HD 6850), only the first seems to actually work! is this an AMD driver bug? can't find anything about it on the interwebz..

        mapping.glBufferData(GL_ARRAY_BUFFER, verticesDataSize * sizeof(float), NULL, GL_DYNAMIC_DRAW);
        //glInvalidateBufferData(vertexBufferID.bufferID);
        float *ptr = (float*)mapping.glMapBufferRange(GL_ARRAY_BUFFER, 0, verticesDataSize * sizeof(float), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT); // GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
        memcpy(ptr, vertices, verticesDataSize * sizeof(float));
        mapping.glUnmapBuffer(GL_ARRAY_BUFFER);

  //      glBufferData(GL_ARRAY_BUFFER, verticesDataSize * sizeof(float), vertices, GL_DYNAMIC_DRAW);

  //    }

      // this fence doesn't seem necessary. the frame swap will glflush things anyway, so as long as you don't update the mesh and render stuff in the same frame, you should be okay.
      // ^ investigate if this is correct thinking. not sure. hairstyles seem to be late, so there is something wrong with omitting the fence.
      // if it's true, users of the engine will need to make sure to not update and render in parallel. maybe make this an option? maybe put something about it in a future manual?
      //GLsync syncObj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  /*fenceoff
      // delete possible previous sync object
      std::map<int, GLsync>::iterator syncIter = VAOfence.find(writeVertexArrayID);
      if (syncIter != VAOfence.end()) {
        glDeleteSync(syncIter->second);
        VAOfence.erase(syncIter);
      }
  */
    //VAOfence.insert(std::pair<int, GLsync>(writeVertexArrayID, syncObj));

    mapping.glBindVertexArray(0);
    mapping.glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  void OpenGLRenderer3D::DeleteVertexBuffer(VertexBufferID vertexBufferID) {
    //Log(e_Notice, "OpenGLRenderer3D", "DeleteVertexBuffer", "Deleting vertex buffer, id " + int_to_str(vertexBufferID.bufferID));
    GLuint glVertexBufferID = vertexBufferID.bufferID;
    mapping.glDeleteBuffers(1, &glVertexBufferID);

    GLuint glElementArrayID = vertexBufferID.elementArrayID;
    mapping.glDeleteBuffers(1, &glElementArrayID);

    if (vertexBufferID.skinBufferID != 0) {
      GLuint glSkinBufferID = vertexBufferID.skinBufferID;
      mapping.glDeleteBuffers(1, &glSkinBufferID);
    }

    GLuint glVertexArrayID = vertexBufferID.vertexArrayID;
    mapping.glDeleteVertexArrays(1, &glVertexArrayID);

    std::map<int, int>::iterator pingPongIter = VBOPingPongMap.find(vertexBufferID.bufferID);
    if (pingPongIter != VBOPingPongMap.end()) {
      glVertexBufferID = pingPongIter->second;
      mapping.glDeleteBuffers(1, &glVertexBufferID);
      VBOPingPongMap.erase(pingPongIter);
    }

    pingPongIter = VAOPingPongMap.find(vertexBufferID.vertexArrayID);
    if (pingPongIter != VAOPingPongMap.end()) {
      glVertexArrayID = pingPongIter->second;
      mapping.glDeleteVertexArrays(1, &glVertexArrayID);
      VAOPingPongMap.erase(pingPongIter);
    }

    pingPongIter = VAOReadIndex.find(vertexBufferID.vertexArrayID);
    if (pingPongIter != VAOReadIndex.end()) {
      VAOReadIndex.erase(pingPongIter);
    }

/*fenceoff
    // delete sync object
    std::map<int, GLsync>::iterator syncIter = VAOfence.find(vertexBufferID.vertexArrayID);
    if (syncIter != VAOfence.end()) {
      glDeleteSync(syncIter->second);
      VAOfence.erase(syncIter);
    }
*/
  }

  void DrawBufferChunk(int startIndex, int count) {
    if (count > 0) {
      #define BUFFER_OFFSET( i ) ((char *)NULL + (i))
      mapping.glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, BUFFER_OFFSET(startIndex * sizeof(unsigned int)));
    }
  }

  void OpenGLRenderer3D::RenderVertexBuffer(const std::deque<VertexBufferQueueEntry> &vertexBufferQueue, e_RenderMode renderMode) {

    VertexBuffer *vertexBuffer;

    signed int currentDiffuseTextureID = -1;
    signed int currentNormalTextureID = -1;
    signed int currentSpecularTextureID = -1;
    signed int currentIlluminationTextureID = -1;
    signed int currentBoundBuffer = -1;

    int bufferSwitches = 0;

    Matrix4 transform;
    int currentHasSkinning = -1;
    const bool hasShader = (currentShader != shaders.end());
    const Shader *sh = hasShader ? &currentShader->second : nullptr;
    const bool hasPingPong = !VAOReadIndex.empty();

    std::deque<VertexBufferQueueEntry>::const_iterator vertexBufferQueueIter = vertexBufferQueue.begin();
    while (vertexBufferQueueIter != vertexBufferQueue.end()) {
      const VertexBufferQueueEntry *queueEntry = &(*vertexBufferQueueIter);

      vertexBuffer = queueEntry->vertexBuffer->GetResource();
      int rawVaoID = vertexBuffer->GetVaoID();
      if (currentBoundBuffer != rawVaoID) {
        bufferSwitches++;

        int vaoID = rawVaoID;
        if (hasPingPong) {
          std::map<int, int>::iterator pingPongIter = VAOReadIndex.find(vaoID);
          if (pingPongIter != VAOReadIndex.end() && pingPongIter->second == 1) {
            std::map<int, int>::iterator vaoIter = VAOPingPongMap.find(vaoID);
            if (vaoIter != VAOPingPongMap.end()) {
              vaoID = vaoIter->second;
            }
          }
        }

        currentBoundBuffer = vaoID;
        mapping.glBindVertexArray(vaoID);
      }

      transform.Construct(queueEntry->position, Vector3(1, 1, 1), queueEntry->rotation);
      if (sh && sh->loc_modelMatrix >= 0) {
        mapping.glUniformMatrix4fv(sh->loc_modelMatrix, 1, true, (float*)transform.elements);
      }

      int wantSkinning = (queueEntry->hasSkinning && queueEntry->boneMatrices) ? 1 : 0;
      if (wantSkinning != currentHasSkinning) {
        if (sh && sh->loc_hasSkinning >= 0) {
          mapping.glUniform1i(sh->loc_hasSkinning, wantSkinning);
        }
        currentHasSkinning = wantSkinning;
      }
      if (wantSkinning == 1 && sh && sh->loc_boneMatrices >= 0) {
        mapping.glUniformMatrix4fv(sh->loc_boneMatrices, 16, true, queueEntry->boneMatrices->data());
      }

      if (queueEntry->vertexBufferIndices && !queueEntry->vertexBufferIndices->empty()) {
        bool sequential = true;
        struct BufferChunk {
          int startIndex = 0;
          int count = 0;
        } bufferChunk;

        const std::vector<VertexBufferIndex> &indicesVec = *queueEntry->vertexBufferIndices;
        const size_t numIndices = indicesVec.size();
        for (size_t idx = 0; idx < numIndices; ++idx) {
          const VertexBufferIndex *vbIndex = &indicesVec[idx];

          if (renderMode != e_RenderMode_GeometryOnly) {
            int diffuseTextureID = 0;
            int normalTextureID = 0;
            int specularTextureID = 0;
            int illuminationTextureID = 0;
            if (vbIndex->material.diffuseTexture) {
              diffuseTextureID = vbIndex->material.diffuseTexture->GetResource()->GetID();
            }

            if (renderMode == e_RenderMode_Full) {
              if (vbIndex->material.normalTexture) {
                normalTextureID = vbIndex->material.normalTexture->GetResource()->GetID();
              }
              if (vbIndex->material.specularTexture) {
                specularTextureID = vbIndex->material.specularTexture->GetResource()->GetID();
              }
              if (vbIndex->material.illuminationTexture) {
                illuminationTextureID = vbIndex->material.illuminationTexture->GetResource()->GetID();
              }
            }

            if (diffuseTextureID != currentDiffuseTextureID ||
                normalTextureID != currentNormalTextureID ||
                specularTextureID != currentSpecularTextureID ||
                illuminationTextureID != currentIlluminationTextureID) {

              if (sequential) {
                DrawBufferChunk(bufferChunk.startIndex, bufferChunk.count);
                sequential = false;
              }

              bool has_normal = (normalTextureID != 0);
              bool has_specular = (specularTextureID != 0);
              bool has_illumination = (illuminationTextureID != 0);

              if (renderMode == e_RenderMode_Full) {
                if (has_normal && normalTextureID != currentNormalTextureID) {
                  SetTextureUnit(1);
                  BindTexture(normalTextureID);
                }
                if (has_specular && specularTextureID != currentSpecularTextureID) {
                  SetTextureUnit(2);
                  BindTexture(specularTextureID);
                }
                if (has_illumination && illuminationTextureID != currentIlluminationTextureID) {
                  SetTextureUnit(3);
                  BindTexture(illuminationTextureID);
                }
              }

              SetTextureUnit(0);
              BindTexture(diffuseTextureID);

              if (renderMode == e_RenderMode_Full && sh) {
                if (sh->loc_materialParams >= 0) {
                  mapping.glUniform3f(sh->loc_materialParams, vbIndex->material.shininess, vbIndex->material.specular_amount, vbIndex->material.self_illumination.coords[0]);
                }
                if (sh->loc_materialBools >= 0) {
                  mapping.glUniform3f(sh->loc_materialBools, (float)(int)has_normal, (float)(int)has_specular, (float)(int)has_illumination);
                }
              }

              currentDiffuseTextureID = diffuseTextureID;
              currentNormalTextureID = normalTextureID;
              currentSpecularTextureID = specularTextureID;
              currentIlluminationTextureID = illuminationTextureID;
            }
          } // if !GeometryOnly

          int start = vbIndex->startIndex;
          int count = vbIndex->size;

          if (sequential && bufferChunk.startIndex + bufferChunk.count != start) {
            DrawBufferChunk(bufferChunk.startIndex, bufferChunk.count);
            sequential = false;
          }

          if (sequential) {
            bufferChunk.count += count;
          } else {
            bufferChunk.startIndex = start;
            bufferChunk.count = count;
            sequential = true;
          }

          if (sequential && (idx + 1 == numIndices)) {
            DrawBufferChunk(bufferChunk.startIndex, bufferChunk.count);
            sequential = false;
          }
        }
      }

      vertexBufferQueueIter++;
    }

    if (currentHasSkinning == 1 && sh && sh->loc_hasSkinning >= 0) {
      mapping.glUniform1i(sh->loc_hasSkinning, 0);
    }

    if (renderMode != e_RenderMode_GeometryOnly) {
      if (renderMode == e_RenderMode_Full) {
        SetTextureUnit(1);
        BindTexture(0);
        SetTextureUnit(2);
        BindTexture(0);
        SetTextureUnit(3);
        BindTexture(0);
      }
      SetTextureUnit(0);
      BindTexture(0);
    }

    mapping.glBindVertexArray(0);
  }

  void OpenGLRenderer3D::RenderAABB(std::list<VertexBufferQueueEntry> &vertexBufferQueue) {
    /* VK: Not used. Delete it
    mapping.glDisable(GL_LIGHTING);
    mapping.glPolygonMode(GL_FRONT, GL_FILL);
    mapping.glColor3f(0, 1, 0);

    mapping.glBegin(GL_LINES);

    std::list<VertexBufferQueueEntry>::iterator vertexBufferQueueIter = vertexBufferQueue.begin();
    while (vertexBufferQueueIter != vertexBufferQueue.end()) {
      VertexBufferQueueEntry *queueEntry = &(*vertexBufferQueueIter);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);


      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);


      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      vertexBufferQueueIter++;
    }

    if (currentHasSkinning == 1) {
      SetUniformInt(currentShader->first, "has_skinning", 0);
    }

    mapping.glEnd();
    */
  }

  void OpenGLRenderer3D::RenderAABB(std::list<LightQueueEntry> &lightQueue) {
    /* VK: Not used. Delete it
    mapping.glDisable(GL_LIGHTING);
    mapping.glPolygonMode(GL_FRONT, GL_FILL);
    mapping.glColor3f(0, 0, 1);

    mapping.glBegin(GL_LINES);

    std::list<LightQueueEntry>::iterator lightQueueIter = lightQueue.begin();
    while (lightQueueIter != lightQueue.end()) {
      LightQueueEntry *queueEntry = &(*lightQueueIter);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);


      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);


      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.minxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.minxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.minxyz.coords[2]);
      mapping.glVertex3f(queueEntry->aabb.maxxyz.coords[0], queueEntry->aabb.maxxyz.coords[1], queueEntry->aabb.maxxyz.coords[2]);

      lightQueueIter++;
    }

    mapping.glEnd();
     */
  }


  // lights

  void OpenGLRenderer3D::SetLight(const Vector3 &position, const Vector3 &color, float radius) {
    /* VK: Not used. Delete it
    Vector3 pos = position;

    //printf("%f %f %f\n", cameraPos.coords[0], cameraPos.coords[1], cameraPos.coords[2]);

    GLfloat positionF[4] = { pos.coords[0], pos.coords[1], pos.coords[2], 1.0 };
    mapping.glLightfv(GL_LIGHT0, GL_POSITION, positionF);
    SetUniformFloat3(currentShader->first, "lightPosition", pos.coords[0], pos.coords[1], pos.coords[2]);

    GLfloat diffuseF[4] = { color.coords[0], color.coords[1], color.coords[2], radius };
    mapping.glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseF);

    GLfloat specular[3] = { 1, 1, 1 };
    mapping.glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    mapping.glEnable(GL_LIGHT0);
    */
  }


  // textures

  GLenum GetGLPixelFormat(e_PixelFormat pixelFormat) {
    GLenum format = GL_RGB;

    switch (pixelFormat) {

      case e_PixelFormat_Alpha:          format = GL_ALPHA; break;
      case e_PixelFormat_Red:            format = GL_RED; break;
      case e_PixelFormat_RG:             format = GL_RG; break;
      case e_PixelFormat_RGB:            format = GL_RGB; break;
      case e_PixelFormat_RGBA:           format = GL_RGBA; break; // todo: BGRA preferred on windows?
      case e_PixelFormat_DepthComponent: format = GL_DEPTH_COMPONENT; break;
      case e_PixelFormat_Luminance:
        Log(e_Error, "OpenGLRenderer3D", "GetGLPixelFormat",
            "e_PixelFormat_Luminance not supported");
        break;
    }

    return format;
  }

  GLenum GetGLInternalPixelFormat(e_InternalPixelFormat pixelFormat) {
    GLenum format = GL_RGB8;

    switch (pixelFormat) {

      case e_InternalPixelFormat_R8:        format = GL_R8; break;
      case e_InternalPixelFormat_RG16F:     format = GL_RG16F; break;
      case e_InternalPixelFormat_RGB8:      format = GL_RGB8; break;
      case e_InternalPixelFormat_SRGB8:     format = GL_SRGB8; break;
      case e_InternalPixelFormat_RGB16:     format = GL_RGB16; break;
      case e_InternalPixelFormat_RGBA4:     format = GL_RGBA4; break;
      case e_InternalPixelFormat_RGBA8:     format = GL_RGBA8; break;
      case e_InternalPixelFormat_SRGBA8:    format = GL_SRGB8_ALPHA8; break;
      case e_InternalPixelFormat_RGBA16:    format = GL_RGBA16; break;
      case e_InternalPixelFormat_RGBA16F:   format = GL_RGBA16F; break;
      case e_InternalPixelFormat_RGBA32F:   format = GL_RGBA32F; break;
      case e_InternalPixelFormat_RGB5_A1:   format = GL_RGB5_A1; break;
      case e_InternalPixelFormat_DepthComponent:    format = GL_DEPTH_COMPONENT; break;
      case e_InternalPixelFormat_DepthComponent16:  format = GL_DEPTH_COMPONENT16; break;
      case e_InternalPixelFormat_DepthComponent24:  format = GL_DEPTH_COMPONENT24; break;
      case e_InternalPixelFormat_DepthComponent32:  format = GL_DEPTH_COMPONENT32; break;
      case e_InternalPixelFormat_DepthComponent32F: format = GL_DEPTH_COMPONENT32F; break;
      case e_InternalPixelFormat_StencilIndex8: format = GL_STENCIL_INDEX8; break;
    }

    return format;
  }

  int OpenGLRenderer3D::CreateTexture(e_InternalPixelFormat internalPixelFormat, e_PixelFormat pixelFormat, int width, int height, bool alpha, bool repeat, bool mipmaps, bool filter, bool multisample, bool compareDepth) {

    GLuint texID;
    mapping.glGenTextures(1, &texID);
    BindTexture(texID);

    GLint filter_min, filter_mag;

    if (filter == true) {
      filter_min = (mipmaps) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
      filter_mag = GL_LINEAR;
    } else {
      filter_min = (mipmaps) ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
      filter_mag = GL_NEAREST;
    }

    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_min);
    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mag);

    GLenum dataType = GL_UNSIGNED_BYTE;
    if (internalPixelFormat == e_InternalPixelFormat_DepthComponent32F ||
        internalPixelFormat == e_InternalPixelFormat_RG16F ||
        internalPixelFormat == e_InternalPixelFormat_RGBA16F ||
        internalPixelFormat == e_InternalPixelFormat_RGBA32F) {
      dataType = GL_FLOAT;
    } else if (internalPixelFormat == e_InternalPixelFormat_DepthComponent24 ||
               internalPixelFormat == e_InternalPixelFormat_DepthComponent32) {
      dataType = GL_UNSIGNED_INT;
    } else if (internalPixelFormat == e_InternalPixelFormat_DepthComponent16) {
      dataType = GL_UNSIGNED_SHORT;
    }

    if (internalPixelFormat == e_InternalPixelFormat_DepthComponent16 ||
        internalPixelFormat == e_InternalPixelFormat_DepthComponent24 ||
        internalPixelFormat == e_InternalPixelFormat_DepthComponent32 ||
        internalPixelFormat == e_InternalPixelFormat_DepthComponent32F) {
      if (compareDepth) {
        mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const GLfloat borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        mapping.glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
      }
    }

    if (filter && !compareDepth) {
      SetMaxAnisotropy();
    }

    if (!multisample) {
      mapping.glTexImage2D(GL_TEXTURE_2D, 0, GetGLInternalPixelFormat(internalPixelFormat), width, height, 0, GetGLPixelFormat(pixelFormat), dataType, NULL);
    } else {
      // todo: needs opengl 3.2 libs!
      //glTexImage2DMultiSample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 5, GetGLInternalPixelFormat(internalPixelFormat), width, height, GL_TRUE);
    }

/* test if this actually helps
    GLclampf prior = (width * height) / 1048576.0; // 1024*1024 tex = max priority
    //printf("%f\n", (pot(source->w) * pot(source->h)) / 1048576.0);
    glPrioritizeTextures(1, (const GLuint*)&texID, &prior);
*/
    BindTexture(0);

    return texID;
  }

  void OpenGLRenderer3D::ResizeTexture(int textureID, SDL_Surface *source, e_InternalPixelFormat internalPixelFormat, e_PixelFormat pixelFormat, bool alpha, bool mipmaps) {

    BindTexture(textureID);
    int x = 0;
    int y = 0;
    int width = source->w;
    int height = source->h;

    SDL_LockSurface(source);
    mapping.glTexImage2D(GL_TEXTURE_2D, 0, GetGLInternalPixelFormat(internalPixelFormat), width, height, 0, GetGLPixelFormat(pixelFormat), GL_UNSIGNED_BYTE, source->pixels);
    SDL_UnlockSurface(source);

    if (mipmaps) {
      mapping.glGenerateMipmap(GL_TEXTURE_2D);
    }

    bool repeat = false;
    bool filter = true;
    GLint filter_min, filter_mag;

    if (filter == true) {
      filter_min = (mipmaps) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
      filter_mag = GL_LINEAR;
      if (mipmaps) SetMaxAnisotropy();
    } else {
      filter_min = (mipmaps) ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
      filter_mag = GL_NEAREST;
    }

    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_min);
    mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mag);
    if (internalPixelFormat == e_InternalPixelFormat_DepthComponent16 ||
        internalPixelFormat == e_InternalPixelFormat_DepthComponent24 ||
        internalPixelFormat == e_InternalPixelFormat_DepthComponent32 ||
        internalPixelFormat == e_InternalPixelFormat_DepthComponent32F) {
      mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
      // Post-GLSL 1.3, DEPTH_TEXTURE_MODE is deprecated and GLSL behaves as if
      // its always set to LUMINANCE
      //mapping.glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);
    }


    //GLclampf prior = (width * height) / 1048576.0; // 1024*1024 tex = max priority
    //printf("%f\n", (pot(source->w) * pot(source->h)) / 1048576.0);
    //glPrioritizeTextures(1, (const GLuint*)&textureID, &prior);

    BindTexture(0);
  }

  void OpenGLRenderer3D::UpdateTexture(int textureID, SDL_Surface *source, bool alpha, bool mipmaps) {
    BindTexture(textureID);

    GLint type2 = (alpha) ? GL_RGBA : GL_RGB;

    SDL_LockSurface(source);
    mapping.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source->w, source->h, type2, GL_UNSIGNED_BYTE,
            source->pixels);
    SDL_UnlockSurface(source);

    if (mipmaps) {
      mapping.glGenerateMipmap(GL_TEXTURE_2D);
      SetMaxAnisotropy();
      mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      mapping.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    BindTexture(0);
  }

  void OpenGLRenderer3D::DeleteTexture(int textureID) {
    for (int i = 0; i < 16; i++) {
      if (_cache_boundTexture[i] == textureID) {
        _cache_boundTexture[i] = -1;
      }
    }
    GLuint texID = textureID;
    mapping.glDeleteTextures(1, &texID);
  }

  void OpenGLRenderer3D::CopyFrameBufferToTexture(int textureID, int width, int height) {
    BindTexture(textureID);
    mapping.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
  }

  void OpenGLRenderer3D::BindTexture(int textureID) {
    if (_cache_activeTextureUnit >= 0 && _cache_activeTextureUnit < 16) {
      if (_cache_boundTexture[_cache_activeTextureUnit] == textureID) {
        return;
      }
      _cache_boundTexture[_cache_activeTextureUnit] = textureID;
    }
    mapping.glBindTexture(GL_TEXTURE_2D, (GLuint)textureID);
  }

  void OpenGLRenderer3D::SetTextureUnit(int textureUnit) {
    if (textureUnit != _cache_activeTextureUnit) {
      //assert(glActiveTexture);
      mapping.glActiveTexture(GL_TEXTURE0 + (GLuint)textureUnit);
      _cache_activeTextureUnit = textureUnit;
    }
  }

  void OpenGLRenderer3D::SetClientTextureUnit(int textureUnit) {
    //assert(glClientActiveTexture);
    mapping.glClientActiveTexture(GL_TEXTURE0 + (GLuint)textureUnit);
  }

  void OpenGLRenderer3D::SetMaxAnisotropy() {
//#ifndef __gl_glcorearb_h_  // Can be used to check for Core Profile Mode on Linux
    mapping.glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            largest_supported_anisotropy);
//#endif
  }
  // frame buffer objects

  GLenum GetGLTargetAttachment(e_TargetAttachment targetAttachment) {
    GLenum target = GL_FRONT;

    switch (targetAttachment) {

      case e_TargetAttachment_None:    target = GL_NONE; break;
      case e_TargetAttachment_Front:   target = GL_FRONT_LEFT; break;
      case e_TargetAttachment_Back:    target = GL_BACK_LEFT; break;
      case e_TargetAttachment_Depth:   target = GL_DEPTH_ATTACHMENT; break;
      case e_TargetAttachment_Stencil: target = GL_STENCIL_ATTACHMENT; break;
      case e_TargetAttachment_Color0:  target = GL_COLOR_ATTACHMENT0; break;
      case e_TargetAttachment_Color1:  target = GL_COLOR_ATTACHMENT1; break;
      case e_TargetAttachment_Color2:  target = GL_COLOR_ATTACHMENT2; break;
      case e_TargetAttachment_Color3:  target = GL_COLOR_ATTACHMENT3; break;
    }

    return target;
  }

  int OpenGLRenderer3D::CreateFrameBuffer() {
    GLuint id;
    mapping.glGenFramebuffers(1, &id);
    return id;
  }

  void OpenGLRenderer3D::DeleteFrameBuffer(int fbID) {
    const GLuint id = fbID;
    mapping.glDeleteFramebuffers(1, &id);
  }

  void OpenGLRenderer3D::BindFrameBuffer(int fbID) {
    mapping.glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbID);
  }

  void OpenGLRenderer3D::SetFrameBufferRenderBuffer(e_TargetAttachment targetAttachment, int rbID) {
    mapping.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GetGLTargetAttachment(targetAttachment), GL_RENDERBUFFER, rbID);
  }

  void OpenGLRenderer3D::SetFrameBufferTexture2D(e_TargetAttachment targetAttachment, int texID) {
    mapping.glFramebufferTexture2D(GL_FRAMEBUFFER, GetGLTargetAttachment(targetAttachment), GL_TEXTURE_2D, texID, 0);
  }

  bool OpenGLRenderer3D::CheckFrameBufferStatus() {
    GLenum status = mapping.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
      Log(e_Notice, "OpenGLRenderer3D", "CheckFrameBufferStatus", "Framebuffer state #" + int_to_str(status));
      return true;
    } else {
      Log(e_FatalError, "OpenGLRenderer3D", "CheckFrameBufferStatus", "Framebuffer error state #" + int_to_str(status));
      return false;
    }
  }


  // render buffers

  int OpenGLRenderer3D::CreateRenderBuffer() {
    GLuint id;
    mapping.glGenRenderbuffers(1, &id);
    return id;
  }

  void OpenGLRenderer3D::DeleteRenderBuffer(int rbID) {
    GLuint glrbID = rbID;
    mapping.glDeleteRenderbuffers(1, &glrbID);
  }

  void OpenGLRenderer3D::BindRenderBuffer(int rbID) {
    mapping.glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)rbID);
  }

  void OpenGLRenderer3D::SetRenderBufferStorage(e_InternalPixelFormat internalPixelFormat, int width, int height) {
    mapping.glRenderbufferStorage(GL_RENDERBUFFER, GetGLInternalPixelFormat(internalPixelFormat), width, height);
  }


  // render targets

  void OpenGLRenderer3D::SetRenderTargets(const std::vector<e_TargetAttachment> &targetAttachments) {
    if (targetAttachments.empty()) return;
    if (targetAttachments.size() <= 8) {
      GLenum targets[8];
      for (size_t i = 0; i < targetAttachments.size(); i++) {
        targets[i] = GetGLTargetAttachment(targetAttachments[i]);
      }
      mapping.glDrawBuffers((GLsizei)targetAttachments.size(), targets);
    } else {
      std::vector<GLenum> targets(targetAttachments.size());
      for (size_t i = 0; i < targetAttachments.size(); i++) {
        targets[i] = GetGLTargetAttachment(targetAttachments[i]);
      }
      mapping.glDrawBuffers((GLsizei)targetAttachments.size(), targets.data());
    }
  }


  // utility

  void OpenGLRenderer3D::SetFOV(float angle) {
    FOV = angle;
  }

  void OpenGLRenderer3D::PushAttribute(int attr) {
    mapping.glPushAttrib((GLbitfield)attr);
  }

  void OpenGLRenderer3D::PopAttribute() {
    mapping.glPopAttrib();
  }

  void OpenGLRenderer3D::SetViewport(int x, int y, int width, int height) {
    mapping.glViewport(x, y, width, height);
  }

  void OpenGLRenderer3D::GetContextSize(int &width, int &height, int &bpp) {
    width = context_width;
    height = context_height;
    bpp = context_bpp;
  }

  void OpenGLRenderer3D::GetDrawableSize(int &width, int &height) {
    if (window) {
      SDL_GL_GetDrawableSize(window, &width, &height);
      if (width < 1) width = context_width;
      if (height < 1) height = context_height;
    } else {
      width = context_width;
      height = context_height;
    }
  }

  void OpenGLRenderer3D::SetWindowMode(int width, int height, bool fullscreen) {
    if (!window) return;
    isFullscreen = fullscreen;
    if (fullscreen) {
      SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
      SDL_SetWindowFullscreen(window, 0);
      if (width > 0 && height > 0) {
        windowedWidth = width;
        windowedHeight = height;
        SDL_SetWindowSize(window, width, height);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
      }
    }
    RefreshAllViews();
  }

  bool OpenGLRenderer3D::IsFullscreen() const {
    return isFullscreen;
  }

  void OpenGLRenderer3D::SetVSync(bool enabled) {
    vsyncEnabled = enabled;
    if (!context) return;
    if (enabled) {
      if (SDL_GL_SetSwapInterval(-1) == -1) {
        SDL_GL_SetSwapInterval(1);
      }
    } else {
      SDL_GL_SetSwapInterval(0);
    }
  }

  bool OpenGLRenderer3D::GetVSync() const {
    return vsyncEnabled;
  }

  void OpenGLRenderer3D::SetAntiAliasingMode(e_AntiAliasingMode mode) {
    aaMode = mode;
    RefreshAllViews();
  }

  e_AntiAliasingMode OpenGLRenderer3D::GetAntiAliasingMode() const {
    return aaMode;
  }

  void OpenGLRenderer3D::SetPolygonOffset(float scale, float bias) {
    mapping.glPolygonOffset(scale, bias); // hint: doesn't work with glsl since i'm using my own calculations
  }

  // shaders

  void LoadGLShader(GLuint shaderID, const std::string &filename) {
    std::vector<std::string> source;
    file_to_vector(filename, source);

    std::string source_flat;
    for (int i = 0; i < (signed int)source.size(); i++) {
      source_flat.append(source.at(i).c_str());
      source_flat.append("\n");
    }

   	const char *sourceChar = source_flat.c_str();
   	mapping.glShaderSource(shaderID, 1, &sourceChar, NULL);
    mapping.glCompileShader(shaderID);

    GLint compiled;
    mapping.glGetShaderiv(shaderID, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      //printf("shader compilation error (%s)\n", filename.c_str());
      Log(e_Warning, "", "LoadGLShader", "shader compilation error (" + filename + ")");
    }

    GLint blen = 0;
    GLsizei slen = 0;
    mapping.glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH , &blen);
    if (blen > 1) {
      GLchar* compiler_log = (GLchar*)malloc(blen);
      mapping.glGetShaderInfoLog(shaderID, blen, &slen, compiler_log);
      //printf("shader compilation info: %s\n", compiler_log);
      Log(e_Warning, "", "LoadGLShader", "shader compilation info (" + filename + "):\n" + std::string(compiler_log));
      free(compiler_log);
    }
  }

  void GeneratePoissonKernel(float *kernel, unsigned int kernelSize) {

    bool usePrecalculatedSet = false;

    if (!usePrecalculatedSet || kernelSize != 32) {
      unsigned int candidateSize = 32;

      std::vector<Vector3> samples(kernelSize);
      std::vector<Vector3> candidates(candidateSize);

      for (unsigned int i = 0; i < kernelSize; i++) {


        // first, create a set of candidates. all these are on the 'hull' of a hemisphere.

        for (unsigned int candidate = 0; candidate < candidateSize; candidate++) {
          //candidates[candidate] = Vector3(random(-1.0f, 1.0f), random(-1.0f, 1.0f), random(0.001f, 1.0f));
          //candidates[candidate] = candidates[candidate].GetNormalized(0) * random(0.001f, 1.0f);

          // http://www.pouet.net/topic.php?which=9613&page=1 (evenly distributed points on hemisphere)
          // "To generate uniformly distributed random points on a sphere, you generate them on a unit cylinder (but not the caps), and project onto the sphere. While it's not intuitive to see, this actually gives uniform distribution."
          // (also see page 2 for corrections!)
          float s = random(0.0f, 2.0f * pi);
          float t = random(-1.0f, 1.0f);
          float m = sqrt(1.0f - t * t);
          candidates[candidate] = Vector3(sin(s) * m, cos(s) * m, fabs(t)); // fabs for hemi
        }


        // second, pick the candidate furthest away from the samples already there. this will distribute them evenly all over the hull.

        int furthestCandidateID = 0;
        float furthestCandidateDistance = 0.0f;
        for (unsigned int candidate = 0; candidate < candidateSize; candidate++) {
          float distanceToClosestSample = 1.0f;
          for (unsigned int x = 0; x < i; x++) {
            float distance = candidates[candidate].GetDistance(samples[x]);
            if (distance < distanceToClosestSample) distanceToClosestSample = distance;
          }
          if (distanceToClosestSample > furthestCandidateDistance) {
            furthestCandidateDistance = distanceToClosestSample;
            furthestCandidateID = candidate;
          }
        }

        samples[i] = candidates[furthestCandidateID];
      }

      for (unsigned int i = 0; i < kernelSize; i++) {


        // third, randomize the length, so the samples fill the volume.

        float minDistance = 0.14f;
        samples[i] = samples[i].GetNormalized(0) * random(minDistance, 1.0f);

        // information around the normal vec is most useful
        samples[i] = samples[i] * 0.75f + Vector3(0, 0, 1) * 0.25f;

        kernel[i * 3 + 0] = samples[i].coords[0];
        kernel[i * 3 + 1] = samples[i].coords[1];
        kernel[i * 3 + 2] = samples[i].coords[2];

        //printf("samples[%i].Set(%f, %f, %f);\n", i, samples[i].coords[0], samples[i].coords[1], samples[i].coords[2]);
      }

    } else { // PRECALCULATED SET

      std::vector<Vector3> samples(kernelSize);

      // these samples seem relatively close to z = 0 (much 'ground effect' on flat surface)
      samples[0].Set(-0.164502, 0.198563, 0.847836);
      samples[1].Set(-0.371169, -0.311732, 0.271810);
      samples[2].Set(0.395387, -0.169534, 0.356410);
      samples[3].Set(0.199861, 0.154104, 0.106548);
      samples[4].Set(0.003958, -0.331189, 0.151894);
      samples[5].Set(-0.128020, 0.276624, 0.133732);
      samples[6].Set(-0.576721, 0.032381, 0.472200);
      samples[7].Set(0.042701, 0.189697, 0.215682);
      samples[8].Set(0.095873, -0.029585, 0.376675);
      samples[9].Set(-0.018931, -0.559727, 0.723850);
      samples[10].Set(0.354553, -0.531634, 0.250052);
      samples[11].Set(0.211686, 0.082632, 0.250673);
      samples[12].Set(-0.586181, -0.060375, 0.086480);
      samples[13].Set(0.529458, -0.009977, 0.084485);
      samples[14].Set(-0.452887, -0.303541, 0.721515);
      samples[15].Set(-0.223906, 0.180655, 0.100284);
      samples[16].Set(-0.278485, -0.709826, 0.082907);
      samples[17].Set(0.195230, 0.204728, 0.573875);
      samples[18].Set(-0.426917, 0.460452, 0.610940);
      samples[19].Set(0.115942, -0.134919, 0.290054);
      samples[20].Set(-0.089691, -0.266767, 0.910242);
      samples[21].Set(0.259113, 0.638836, 0.159923);
      samples[22].Set(0.621548, -0.384182, 0.089235);
      samples[23].Set(0.261258, 0.331540, 0.306923);
      samples[24].Set(0.170094, -0.678424, 0.471292);
      samples[25].Set(-0.398759, -0.300976, 0.082431);
      samples[26].Set(-0.291338, 0.080905, 0.159125);
      samples[27].Set(-0.391219, 0.082053, 0.638896);
      samples[28].Set(0.051567, 0.476222, 0.765788);
      samples[29].Set(-0.181921, 0.545340, 0.430360);
      samples[30].Set(0.542024, 0.161835, 0.173717);
      samples[31].Set(-0.059420, 0.778527, 0.233026);

      Vector3 average;
      for (unsigned int i = 0; i < kernelSize; i++) {
        average += samples[i];
      }
      average /= kernelSize;
      printf("SSAO kernel average position: ");
      average.Print();
      Vector3 newAverage;
      for (unsigned int i = 0; i < kernelSize; i++) {
        samples[i] -= average.Get2D();
        kernel[i * 3 + 0] = samples[i].coords[0];
        kernel[i * 3 + 1] = samples[i].coords[1];
        kernel[i * 3 + 2] = samples[i].coords[2];
        newAverage += samples[i];
      }
      newAverage /= kernelSize;
      printf("SSAO kernel average position (corrected): ");
      newAverage.Print();
    }

  }

  void OpenGLRenderer3D::LoadShader(const std::string &name, const std::string &filename) {
    GLint info;

    Shader shader;
    shader.name = filename;

    shader.vertexShaderID = mapping.glCreateShader(GL_VERTEX_SHADER);
    LoadGLShader(shader.vertexShaderID, filename + ".vert");
    mapping.glGetShaderiv(shader.vertexShaderID, GL_COMPILE_STATUS, &info);
    if (info != GL_TRUE || mapping.glIsShader(shader.vertexShaderID) != GL_TRUE) {
      Log(e_FatalError, "OpenGLRenderer3D", "LoadShader", "Could not compile vertex program: " + name);
    }

    shader.fragmentShaderID = mapping.glCreateShader(GL_FRAGMENT_SHADER);
    LoadGLShader(shader.fragmentShaderID, filename + ".frag");
    mapping.glGetShaderiv(shader.fragmentShaderID, GL_COMPILE_STATUS, &info);
    if (info != GL_TRUE || mapping.glIsShader(shader.fragmentShaderID) != GL_TRUE) {
      Log(e_FatalError, "OpenGLRenderer3D", "LoadShader", "Could not compile fragment program: " + name);
    }

    shader.programID = mapping.glCreateProgram();
    mapping.glAttachShader(shader.programID, shader.vertexShaderID);
    mapping.glAttachShader(shader.programID, shader.fragmentShaderID);

    shaders.insert(std::pair<std::string, Shader>(name, shader));

    if (name == "zphase") {
      mapping.glBindAttribLocation(shader.programID, 0, "position");
      mapping.glBindAttribLocation(shader.programID, 5, "boneIndices");
      mapping.glBindAttribLocation(shader.programID, 6, "boneWeights");
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout");
    }
    if (name == "simple") {
      mapping.glBindAttribLocation(shader.programID, 0, "position");
      mapping.glBindAttribLocation(shader.programID, 1, "normal");
      mapping.glBindAttribLocation(shader.programID, 2, "texcoord");
      mapping.glBindAttribLocation(shader.programID, 3, "tangent");
      mapping.glBindAttribLocation(shader.programID, 4, "bitangent");
      mapping.glBindAttribLocation(shader.programID, 5, "boneIndices");
      mapping.glBindAttribLocation(shader.programID, 6, "boneWeights");
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout0");
      mapping.glBindFragDataLocation(shader.programID, 1, "stdout1");
      mapping.glBindFragDataLocation(shader.programID, 2, "stdout2");
    }
    if (name == "ambient") {
      mapping.glBindAttribLocation(shader.programID, 0, "position");
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout");
    }
    if (name == "lighting") {
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout0");
    }
    if (name == "postprocess") {
      mapping.glBindAttribLocation(shader.programID, 0, "position");
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout");
    }
    if (name == "aa") {
      mapping.glBindAttribLocation(shader.programID, 0, "position");
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout");
    }
    if (name == "overlay") {
      mapping.glBindAttribLocation(shader.programID, 0, "vertex");
      mapping.glBindFragDataLocation(shader.programID, 0, "stdout");
    }

    mapping.glLinkProgram(shader.programID);

    mapping.glUseProgram(shader.programID);

    Shader &linkedShader = shaders[name];
    auto lookupOpt = [&](const char *uName) -> int {
      int loc = mapping.glGetUniformLocation(linkedShader.programID, uName);
      linkedShader.uniformLocations[uName] = loc;
      return loc;
    };
    linkedShader.loc_projectionMatrix = lookupOpt("projectionMatrix");
    linkedShader.loc_viewMatrix = lookupOpt("viewMatrix");
    linkedShader.loc_modelMatrix = lookupOpt("modelMatrix");
    linkedShader.loc_orthoProjectionMatrix = lookupOpt("orthoProjectionMatrix");
    linkedShader.loc_orthoViewMatrix = lookupOpt("orthoViewMatrix");
    linkedShader.loc_projection = lookupOpt("projection");
    linkedShader.loc_model = lookupOpt("model");
    linkedShader.loc_hasSkinning = lookupOpt("has_skinning");
    linkedShader.loc_boneMatrices = mapping.glGetUniformLocation(linkedShader.programID, "boneMatrices");
    if (linkedShader.loc_boneMatrices == -1) {
      linkedShader.loc_boneMatrices = mapping.glGetUniformLocation(linkedShader.programID, "boneMatrices[0]");
    }
    linkedShader.uniformLocations["boneMatrices"] = linkedShader.loc_boneMatrices;
    linkedShader.loc_materialParams = lookupOpt("materialparams");
    linkedShader.loc_materialBools = lookupOpt("materialbools");
    linkedShader.loc_cameraPosition = lookupOpt("cameraPosition");
    linkedShader.loc_hasShadow = lookupOpt("has_shadow");
    linkedShader.loc_lightViewProjectionMatrix = lookupOpt("lightViewProjectionMatrix");
    linkedShader.loc_lightColor = lookupOpt("lightColor");
    linkedShader.loc_lightRadius = lookupOpt("lightRadius");
    linkedShader.loc_lightPosition = lookupOpt("lightPosition");

    currentShader = shaders.find(name);

    if (name == "zphase") {
      SetUniformInt("zphase", "has_skinning", 0);
    }
    if (name == "simple") {
      SetUniformInt("simple", "has_skinning", 0);
      SetUniformInt("simple", "map_albedo", 0);
      SetUniformInt("simple", "map_normal", 1);
      SetUniformInt("simple", "map_specular", 2);
      SetUniformInt("simple", "map_illumination", 3);
    }
    if (name == "ambient") {
      SetUniformInt("ambient", "map_albedo", 0);
      SetUniformInt("ambient", "map_normal", 1);
      SetUniformInt("ambient", "map_depth", 2);
      SetUniformInt("ambient", "map_aux", 3);
      SetUniformInt("ambient", "map_noise", 4);

      SetUniformFloat("ambient", "contextX", (float)0.0);
      SetUniformFloat("ambient", "contextY", (float)0.0);
      SetUniformFloat("ambient", "contextWidth", (float)context_width);
      SetUniformFloat("ambient", "contextHeight", (float)context_height);

      unsigned int kernelSize = 32;
      std::vector<float> SSAO_kernel(kernelSize * 3);
      GeneratePoissonKernel(&SSAO_kernel[0], kernelSize);
      SetUniformFloat3Array("ambient", "SSAO_kernel", kernelSize, &SSAO_kernel[0]);
    }
    if (name == "lighting") {
      SetUniformInt("lighting", "map_albedo", 0);
      SetUniformInt("lighting", "map_normal", 1);
      SetUniformInt("lighting", "map_depth", 2);
      SetUniformInt("lighting", "map_shadow", 7);

      SetUniformFloat("lighting", "contextX", (float)0.0);
      SetUniformFloat("lighting", "contextY", (float)0.0);
      SetUniformFloat("lighting", "contextWidth", (float)context_width);
      SetUniformFloat("lighting", "contextHeight", (float)context_height);
    }
    if (name == "postprocess") {
      SetUniformInt("postprocess", "map_accumulation", 0);
      SetUniformInt("postprocess", "map_modifier", 1);
      SetUniformInt("postprocess", "map_depth", 2);

      SetUniformFloat("postprocess", "contextX", (float)0.0);
      SetUniformFloat("postprocess", "contextY", (float)0.0);
      SetUniformFloat("postprocess", "contextWidth", (float)context_width);
      SetUniformFloat("postprocess", "contextHeight", (float)context_height);
    }
    if (name == "aa") {
      SetUniformInt("aa", "map_postprocess", 0);
      SetUniformInt("aa", "map_modifier", 1);

      SetUniformFloat("aa", "contextX", (float)0.0);
      SetUniformFloat("aa", "contextY", (float)0.0);
      SetUniformFloat("aa", "contextWidth", (float)context_width);
      SetUniformFloat("aa", "contextHeight", (float)context_height);
      SetUniformFloat2("aa", "sourceTexelSize", 1.0f / (float)std::max(1, context_width), 1.0f / (float)std::max(1, context_height));
      SetUniformInt("aa", "aaMode", (int)aaMode);
    }

    SetTextureUnit(0);

    UseShader("");
  }

  void OpenGLRenderer3D::SetFramebufferGammaCorrection(bool onOff) {
    if (onOff) {
      mapping.glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
      mapping.glDisable(GL_FRAMEBUFFER_SRGB);
    }
  }

  void OpenGLRenderer3D::UseShader(const std::string &name) {
    if (name.empty()) {
      currentShader = shaders.end();
      mapping.glUseProgram(0);
      return;
    }
    if (currentShader != shaders.end() && currentShader->first == name) {
      return;
    }
    std::map<std::string, Shader>::iterator shaderIter = shaders.find(name);
    if (shaderIter != shaders.end()) {
      currentShader = shaderIter;
      mapping.glUseProgram((*shaderIter).second.programID);
    } else {
      currentShader = shaders.end();
      mapping.glUseProgram(0);
    }
  }

  int OpenGLRenderer3D::ResolveUniformLocation(Shader &shader, const std::string &varName, bool allowArrayElemZero) {
    auto iter = shader.uniformLocations.find(varName);
    if (iter != shader.uniformLocations.end()) {
      return iter->second;
    }
    GLint location = mapping.glGetUniformLocation(shader.programID, varName.c_str());
    if (location == -1 && allowArrayElemZero) {
      std::string elemZero = varName + "[0]";
      location = mapping.glGetUniformLocation(shader.programID, elemZero.c_str());
    }
    if (location == -1) {
      Log(e_Error, "OpenGLRenderer3D", "ResolveUniformLocation", "Uniform location for shader '" + shader.name + "' not found: " + varName);
    }
    shader.uniformLocations.emplace(varName, location);
    return location;
  }

  void OpenGLRenderer3D::SetUniformInt(const std::string &shaderName, const std::string &varName, int value) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniform1i(location, value);
  }

  void OpenGLRenderer3D::SetUniformInt3(const std::string &shaderName, const std::string &varName, int value1, int value2, int value3) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniform3i(location, value1, value2, value3);
  }

  void OpenGLRenderer3D::SetUniformFloat(const std::string &shaderName, const std::string &varName, float value) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniform1f(location, value);
  }

  void OpenGLRenderer3D::SetUniformFloat2(const std::string &shaderName, const std::string &varName, float value1, float value2) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniform2f(location, value1, value2);
  }

  void OpenGLRenderer3D::SetUniformFloat3(const std::string &shaderName, const std::string &varName, float value1, float value2, float value3) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniform3f(location, value1, value2, value3);
  }

  void OpenGLRenderer3D::SetUniformFloat3Array(const std::string &shaderName, const std::string &varName, int count, float *values) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniform3fv(location, count, values);
  }

  void OpenGLRenderer3D::SetUniformMatrix4(const std::string &shaderName, const std::string &varName, const Matrix4 &mat) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName);
    if (location != -1) mapping.glUniformMatrix4fv(location, 1, true, (float*)mat.elements);
  }

  void OpenGLRenderer3D::SetUniformMatrix4Array(const std::string &shaderName, const std::string &varName, int count, const float *values) {
    auto shaderIter = (currentShader != shaders.end() && currentShader->first == shaderName) ? currentShader : shaders.find(shaderName);
    assert(shaderIter != shaders.end());
    GLint location = ResolveUniformLocation(shaderIter->second, varName, true);
    if (location != -1) mapping.glUniformMatrix4fv(location, count, true, values);
  }

  void OpenGLRenderer3D::HDRCaptureOverallBrightness() {
    float brightness = 0.0;
    int count = 0;
    for (int i = 0; i < 2; i++) {
//    for (int x = context_width * 0.2; x <= context_width * 0.8; x += context_width * 0.01) {
//      for (int y = context_height * 0.2; y <= context_height * 0.8; y += context_height * 0.01) {
      int x = random(context_width * 0.2f, context_width * 0.8f);
      int y = random(context_height * 0.2f, context_height * 0.8f);
      GLubyte pixel[3];
      mapping.glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, (void*)pixel);
      brightness += (pixel[0] + pixel[1] + pixel[2]) / 3.0f;
      count++;
    }
    brightness /= (float)count;
    //brightness /= 256.0;

    overallBrightness = overallBrightness * 0.99f + brightness * 0.01f;

    //printf("overall brightness: %f (%f)\n", overallBrightness, brightness);
  }

  float OpenGLRenderer3D::HDRGetOverallBrightness() {
    return overallBrightness;
  }


  // main-thread OpenGL lifecycle

  void OpenGLRenderer3D::InitMainThread() {
    Log(e_Notice, "OpenGLRenderer3D", "InitMainThread", "Initializing OpenGLRenderer3D on Main Thread");

#ifdef WIN32
    SetProcessDPIAware();
#endif
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");

    SDL_Init(SDL_INIT_VIDEO);

    int flags = IMG_INIT_JPG | IMG_INIT_PNG;
    int inited = IMG_Init(flags);

    if ((inited & flags) != flags) {
      printf("IMG_Init: Failed to init required jpg and png support!\n");
      printf("IMG_Init: %s\n", IMG_GetError());
    }

    messageQueue.EnableInlineExecutionOnCurrentThread(this);
  }

  void OpenGLRenderer3D::RunMainLoop() {
    Log(e_Notice, "OpenGLRenderer3D", "RunMainLoop", "Entering OpenGLRenderer3D Main Thread event/render loop");

    SDL_Event event;
    bool quit = false;
    while (!quit) {
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_WINDOWEVENT) {
          if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            contextIsActive = false;
          } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            contextIsActive = true;
          } else if (event.window.event == SDL_WINDOWEVENT_RESIZED && !isFullscreen) {
            windowedWidth = event.window.data1;
            windowedHeight = event.window.data2;
            RefreshAllViews();
          }
        }

        switch (event.type) {
          case SDL_QUIT:
            EnvironmentManager::GetInstance().SignalQuit();
            break;
          case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_F12) {
              EnvironmentManager::GetInstance().SignalQuit();
            } else if (event.key.repeat == 0 &&
                       (event.key.keysym.sym == SDLK_F11 ||
                        ((event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) &&
                         (event.key.keysym.mod & KMOD_ALT)))) {
              SetWindowMode(windowedWidth, windowedHeight, !isFullscreen);
              continue;
            }
            break;
          default:
            break;
        }

        if (contextIsActive) {
          UserEventManager::GetInstance().InputSDLEvent(event);
        }
      }

      bool isMessage = false;
      intrusive_ptr<Command> message = messageQueue.WaitForMessage(isMessage, 4);
      if (isMessage) {
        if (!message->Handle(this)) quit = true;
        message.reset();
        for (int burst = 0; burst < 128 && !quit; burst++) {
          bool hasMore = false;
          intrusive_ptr<Command> nextMsg = messageQueue.GetMessage(hasMore);
          if (!hasMore) break;
          if (!nextMsg->Handle(this)) quit = true;
          nextMsg.reset();
        }
      }
    }

    Log(e_Notice, "OpenGLRenderer3D", "RunMainLoop", "Exited OpenGLRenderer3D Main Thread event/render loop");
  }

  void OpenGLRenderer3D::ShutdownMainThread() {
    Log(e_Notice, "OpenGLRenderer3D", "ShutdownMainThread", "Shutting down OpenGLRenderer3D on Main Thread");

    // Drain any remaining commands on the main thread before tearing down GL context
    for (;;) {
      bool hasMsg = false;
      intrusive_ptr<Command> msg = messageQueue.GetMessage(hasMsg);
      if (!hasMsg) break;
      if (msg) msg->Handle(this);
    }

    messageQueue.DisableInlineExecution();

    Exit();

    IMG_Quit();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    if (messageQueue.GetPending() > 0) {
      Log(e_Error, "OpenGLRenderer3D", "ShutdownMainThread", int_to_str(messageQueue.GetPending()) + " messages left on quit!");
    }
  }

  void OpenGLRenderer3D::operator()() {
    InitMainThread();
    RunMainLoop();
    ShutdownMainThread();
  }

}
