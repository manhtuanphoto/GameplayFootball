// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "r3d_messages.hpp"

#include <algorithm>

#include "../resources/texture.hpp"

namespace blunted {

  bool R3DM_SortVertexBufferQueueEntries(const VertexBufferQueueEntry &vb1, const VertexBufferQueueEntry &vb2) {
    if (vb1.hasSkinning != vb2.hasSkinning) return !vb1.hasSkinning;
    if (vb1.vaoIDCache != vb2.vaoIDCache) return vb1.vaoIDCache < vb2.vaoIDCache;
    return vb1.firstTexIDCache < vb2.firstTexIDCache;
  }

  bool Renderer3DMessage_RenderView::Execute(void *caller) {

    Renderer3D *renderer = static_cast<Renderer3D*>(caller);

    View &view = renderer->GetView(viewID);

    Matrix4 projectionMatrix = renderer->CreatePerspectiveMatrix(view.baseWidth / (std::max(1, view.baseHeight) * 1.0f), buffer.cameraNearCap, buffer.cameraFarCap);
    Matrix4 viewMatrix = buffer.cameraMatrix;

    std::sort(buffer.visibleGeometry.begin(), buffer.visibleGeometry.end(), R3DM_SortVertexBufferQueueEntries);

    int width;
    int height;
    int bpp;
    renderer->GetContextSize(width, height, bpp);
    // opengl window starts lower left, so invert y
    //printf("%i %i %i %i\n", view.x, height - view.y, view.width, view.height);
    renderer->SetViewport(view.x, height - (view.y + view.baseHeight), view.baseWidth, view.baseHeight);

    renderer->SetFOV(buffer.cameraFOV);

    float depthParamNear = buffer.cameraFarCap / (buffer.cameraFarCap - buffer.cameraNearCap);
    float depthParamFar = (buffer.cameraFarCap * buffer.cameraNearCap) / (buffer.cameraNearCap - buffer.cameraFarCap);

    std::vector<e_TargetAttachment> targets;


    // render skybox

    if (buffer.skyboxes.size() > 0) { // todo: use shader?
      Matrix4 skyboxMatrix = viewMatrix;
      skyboxMatrix.SetTranslation(Vector3(0, 0, 0));
      //XX renderer->SetMatrixMode(e_MatrixMode_ModelView);
      //XX renderer->LoadMatrix(skyboxMatrix);

      renderer->UseShader("");

      //renderer->SetMatrix("viewMatrix", buffer.cameraMatrix);

      targets.push_back(e_TargetAttachment_Back);
      renderer->SetRenderTargets(targets);
      targets.clear();

      //renderer->ClearBuffer(Vector3(0, 0, 0), true, true);
      renderer->SetCullingMode(e_CullingMode_Back);
      renderer->SetBlendingMode(e_BlendingMode_Off);
      renderer->SetDepthFunction(e_DepthFunction_Less);
      renderer->SetDepthTesting(false);
      renderer->SetDepthMask(false);
      renderer->RenderVertexBuffer(buffer.skyboxes, e_RenderMode_Diffuse);
      renderer->SetDepthMask(true);
      renderer->SetDepthTesting(true);
    }


    // render vertexbuffers


    // pre z phase

    // disabled: geometry phase is already doing this, right?
    // enabled again: the geometry phase writes to the gbuffer, this writes to the accumbuffer that the lighting uses
    // disabled again: seems to work without z-phase, even with ambient phase on z equal. i guess the depth buffer is kept indepedently of the bound frame buffer.
    // disabled again: alpha pass (lame transparency) won't work with this

    bool zphase = false;
    if (zphase) {
      //XXrenderer->SetMatrixMode(e_MatrixMode_ModelView);

      renderer->SetMatrix("projectionMatrix", projectionMatrix);
      renderer->SetMatrix("viewMatrix", viewMatrix);

      renderer->UseShader("zphase");

      renderer->BindFrameBuffer(view.gBufferID);

      targets.push_back(e_TargetAttachment_None);
      renderer->SetRenderTargets(targets);
      targets.clear();

      renderer->SetCullingMode(e_CullingMode_Back);
      renderer->SetBlendingMode(e_BlendingMode_Off);
      renderer->SetDepthFunction(e_DepthFunction_Less);
      renderer->SetDepthTesting(true);
      renderer->SetDepthMask(true);
      renderer->ClearBuffer(Vector3(0, 0, 0), true, false);
      renderer->RenderVertexBuffer(buffer.visibleGeometry, e_RenderMode_GeometryOnly);
    }



    // geometry phase

    renderer->UseShader("simple");

    renderer->BindFrameBuffer(view.gBufferID);

    // framebuffer starts at lower left 0,0
    renderer->SetViewport(0, 0, view.width, view.height);

    renderer->SetMatrix("projectionMatrix", projectionMatrix);
    renderer->SetMatrix("viewMatrix", viewMatrix);

    // work-around ati bug: clearing z with color buffers attached could be slow
    // http://www.infinity-universe.com/Infinity/index.php?option=com_content&task=view&id=105&Itemid=27
    //targets.push_back(e_TargetAttachment_Depth);

    targets.push_back(e_TargetAttachment_Color0);
    targets.push_back(e_TargetAttachment_Color1);
    targets.push_back(e_TargetAttachment_Color2);
    renderer->SetRenderTargets(targets);
    targets.clear();

    renderer->SetCullingMode(e_CullingMode_Back);
    renderer->SetBlendingMode(e_BlendingMode_Off);
    renderer->SetDepthTesting(true);

    if (zphase) {
      renderer->SetDepthFunction(e_DepthFunction_Equal); // changed from less
      renderer->SetDepthMask(false); // changed
      renderer->ClearBuffer(Vector3(0, 0, 0), false, true); // changed
    } else {
      renderer->SetDepthFunction(e_DepthFunction_Less);
      renderer->SetDepthMask(true);
      renderer->ClearBuffer(Vector3(0, 0, 0), true, true);
    }

    renderer->RenderVertexBuffer(buffer.visibleGeometry, e_RenderMode_Full);



    // lighting phase

    // output goes to accumulation buffer
    renderer->BindFrameBuffer(view.accumBufferID);
    targets.push_back(e_TargetAttachment_Color0);
    targets.push_back(e_TargetAttachment_Color1);
    renderer->SetRenderTargets(targets);
    targets.clear();

    // blend the remaining
    renderer->SetTextureUnit(1);
    renderer->BindTexture(view.gBuffer_NormalTexID);
    renderer->SetTextureUnit(2);
    renderer->BindTexture(view.gBuffer_DepthTexID);
    renderer->SetTextureUnit(3);
    renderer->BindTexture(view.gBuffer_AuxTexID);
    renderer->SetTextureUnit(0);
    renderer->BindTexture(view.gBuffer_AlbedoTexID);

    //renderer->ClearBuffer(Vector3(0, 0, 0), false, true);

    // ambient
    renderer->UseShader("ambient");

    renderer->SetUniformFloat("ambient", "contextWidth", (float)view.width);
    renderer->SetUniformFloat("ambient", "contextHeight", (float)view.height);
    renderer->SetUniformFloat("ambient", "contextX", (float)0);
    renderer->SetUniformFloat("ambient", "contextY", (float)0);
    renderer->SetUniformFloat2("ambient", "cameraClip", depthParamNear, depthParamFar);
    Matrix4 inverseProjectionViewMatrix = (projectionMatrix * viewMatrix).GetInverse();
    renderer->SetUniformMatrix4("ambient", "inverseProjectionViewMatrix", inverseProjectionViewMatrix);
    renderer->SetUniformMatrix4("ambient", "projectionMatrix", projectionMatrix);
    renderer->SetUniformMatrix4("ambient", "viewMatrix", viewMatrix);

    renderer->SetDepthTesting(false);
    renderer->SetDepthMask(false);

    renderer->RenderOverlay2D();

    // lights
    //renderer->SetMatrixMode(e_MatrixMode_ModelView);
    targets.push_back(e_TargetAttachment_Color0);
    renderer->SetRenderTargets(targets);
    targets.clear();

    renderer->UseShader("lighting");

    renderer->SetBlendingMode(e_BlendingMode_On);
    renderer->SetBlendingFunction(e_BlendingFunction_One, e_BlendingFunction_One);

    renderer->SetUniformFloat("lighting", "contextWidth", (float)view.width);
    renderer->SetUniformFloat("lighting", "contextHeight", (float)view.height);
    renderer->SetUniformFloat("lighting", "contextX", (float)0);
    renderer->SetUniformFloat("lighting", "contextY", (float)0);
    renderer->SetUniformMatrix4("lighting", "inverseProjectionViewMatrix", inverseProjectionViewMatrix);
    renderer->SetUniformMatrix4("lighting", "projectionMatrix", projectionMatrix);
    renderer->SetUniformMatrix4("lighting", "viewMatrix", viewMatrix);

    //renderer->SetUniformFloat2("lighting", "cameraClip", depthParamNear, depthParamFar);

    renderer->SetDepthTesting(false);
    renderer->SetDepthMask(false);

    renderer->RenderLights(buffer.visibleLights, projectionMatrix, viewMatrix);

    renderer->SetBlendingMode(e_BlendingMode_Off);
    renderer->SetDepthMask(true);
    renderer->SetCullingMode(e_CullingMode_Off);

    renderer->SetTextureUnit(1);
    renderer->BindTexture(0);
    renderer->SetTextureUnit(2);
    renderer->BindTexture(0);
    renderer->SetTextureUnit(3);
    renderer->BindTexture(0);
    renderer->SetTextureUnit(0);
    renderer->BindTexture(0);



    // 1. Tone-map + SSAO + Haze into intermediate sRGB+Luma buffer (view.postProcessBufferID)

    renderer->BindFrameBuffer(view.postProcessBufferID);

    targets.push_back(e_TargetAttachment_Color0);
    renderer->SetRenderTargets(targets);
    targets.clear();

    renderer->SetViewport(0, 0, view.width, view.height);

    renderer->UseShader("postprocess");

    renderer->SetUniformFloat("postprocess", "contextWidth", (float)view.width);
    renderer->SetUniformFloat("postprocess", "contextHeight", (float)view.height);
    renderer->SetUniformFloat("postprocess", "contextX", 0.0f);
    renderer->SetUniformFloat("postprocess", "contextY", 0.0f);
    renderer->SetUniformFloat2("postprocess", "cameraClip", depthParamNear, depthParamFar);
    renderer->SetUniformFloat("postprocess", "fogScale", 0.8f - NormalizedClamp(buffer.cameraFOV, 20, 100) * 0.6f);

    renderer->SetTextureUnit(2);
    renderer->BindTexture(view.gBuffer_DepthTexID);
    renderer->SetTextureUnit(1);
    renderer->BindTexture(view.accumBuffer_ModifierTexID);
    renderer->SetTextureUnit(0);
    renderer->BindTexture(view.accumBuffer_AccumTexID);

    renderer->SetDepthTesting(false);
    renderer->SetDepthMask(false);
    renderer->SetFramebufferGammaCorrection(false);
    renderer->RenderOverlay2D();

    renderer->SetTextureUnit(2);
    renderer->BindTexture(0);
    renderer->SetTextureUnit(1);
    renderer->BindTexture(0);
    renderer->SetTextureUnit(0);
    renderer->BindTexture(0);


    // 2. Anti-Aliasing + Supersample Resolve + CAS onto BackBuffer (FrameBuffer 0)

    renderer->BindFrameBuffer(0);

    targets.push_back(e_TargetAttachment_Back);
    renderer->SetRenderTargets(targets);
    targets.clear();

    int drawableW = width;
    int drawableH = height;
    renderer->GetDrawableSize(drawableW, drawableH);
    renderer->SetViewport(0, 0, drawableW, drawableH);
    renderer->ClearBuffer(Vector3(0, 0, 0), false, true);

    float targetAspect = (float)width / (float)std::max(1, height);
    float drawableAspect = (float)drawableW / (float)std::max(1, drawableH);
    int fitW = drawableW;
    int fitH = drawableH;
    int fitX = 0;
    int fitY = 0;
    if (std::fabs(drawableAspect - targetAspect) > 0.01f) {
      if (drawableAspect > targetAspect) {
        fitH = drawableH;
        fitW = int(std::round(fitH * targetAspect));
        fitX = (drawableW - fitW) / 2;
      } else {
        fitW = drawableW;
        fitH = int(std::round(fitW / targetAspect));
        fitY = (drawableH - fitH) / 2;
      }
    }

    int outX = fitX + int(std::floor(view.x * (float)fitW / (float)std::max(1, width)));
    int outY = fitY + int(std::floor((height - (view.y + view.baseHeight)) * (float)fitH / (float)std::max(1, height)));
    int outW = std::max(1, int(std::round(view.baseWidth * (float)fitW / (float)std::max(1, width))));
    int outH = std::max(1, int(std::round(view.baseHeight * (float)fitH / (float)std::max(1, height))));

    renderer->UseShader("aa");

    renderer->SetUniformFloat("aa", "contextWidth", (float)outW);
    renderer->SetUniformFloat("aa", "contextHeight", (float)outH);
    renderer->SetUniformFloat("aa", "contextX", (float)outX);
    renderer->SetUniformFloat("aa", "contextY", (float)outY);
    renderer->SetUniformFloat2("aa", "sourceTexelSize", 1.0f / (float)std::max(1, view.width), 1.0f / (float)std::max(1, view.height));
    renderer->SetUniformInt("aa", "aaMode", (int)renderer->GetAntiAliasingMode());

    renderer->SetViewport(outX, outY, outW, outH);

    renderer->SetTextureUnit(1);
    renderer->BindTexture(view.accumBuffer_ModifierTexID);
    renderer->SetTextureUnit(0);
    renderer->BindTexture(view.postProcessTexID);

    renderer->SetDepthTesting(false);
    renderer->SetDepthMask(false);
    renderer->SetFramebufferGammaCorrection(false);
    renderer->RenderOverlay2D();

    renderer->SetTextureUnit(1);
    renderer->BindTexture(0);
    renderer->SetTextureUnit(0);
    renderer->BindTexture(0);


    // too slow
    //renderer->HDRCaptureOverallBrightness();

    // back to the context viewport
    renderer->UseShader("");

    targets.push_back(e_TargetAttachment_Back);
    renderer->SetRenderTargets(targets);
    targets.clear();

    renderer->SetViewport(0, 0, width, height);

    buffer.visibleGeometry.clear();
    buffer.visibleLights.clear();
    buffer.skyboxes.clear();

    return true;
  }


  bool Renderer3DMessage_RenderShadowMap::Execute(void *caller) {
    Renderer3D *renderer = static_cast<Renderer3D*>(caller);

    // SORTING update: because we pass a const reference, we need to sort vertexbuffers in graphics_light (or wherever we invoke this)

    // disabled because it's slower to sort (vector) than to switch vertexbuffers
    //std::sort(map.visibleGeometry.begin(), map.visibleGeometry.end(), R3DM_SortVertexBufferQueueEntries);
    //std::sort(map.visibleGeometry.begin(), map.visibleGeometry.end(), SortVertexBufferQueueEntries);

    // sorting a list however might be ok. needs to be tested in a situation with lots of different id's
    //map.visibleGeometry.sort(R3DM_SortVertexBufferQueueEntries);

    renderer->UseShader("zphase");

    //renderer->SetOrtho(-30, 30, -30, 30, 90, 110);
    //renderer->SetFOV(40);
    //renderer->SetPerspective(1.0, 90.0, 110.0);

    renderer->BindFrameBuffer(map.frameBufferID);

    int shadowW, shadowH;
    map.texture->GetResource()->GetSize(shadowW, shadowH);
    renderer->SetViewport(0, 0, shadowW, shadowH);

    renderer->ClearBuffer(Vector3(0, 0, 0), true, false);

    renderer->SetMatrix("projectionMatrix", map.lightProjectionMatrix);
    renderer->SetMatrix("viewMatrix", map.lightViewMatrix);

    std::vector<e_TargetAttachment> targets;
    targets.push_back(e_TargetAttachment_None);
    renderer->SetRenderTargets(targets);
    targets.clear();

    renderer->SetCullingMode(e_CullingMode_Front);
    renderer->SetBlendingMode(e_BlendingMode_Off);
    renderer->SetDepthFunction(e_DepthFunction_Less);
    renderer->SetDepthTesting(true);
    renderer->SetDepthMask(true);

    renderer->RenderVertexBuffer(map.visibleGeometry, e_RenderMode_GeometryOnly);

    renderer->BindFrameBuffer(0);
    renderer->UseShader("");

    targets.push_back(e_TargetAttachment_Back);
    renderer->SetRenderTargets(targets);
    targets.clear();

    // restore context viewport
    int width, height, bpp;
    renderer->GetContextSize(width, height, bpp);
    renderer->SetViewport(0, 0, width, height);
    //renderer->SetPerspective(width / (height * 1.0));

    renderer->SetCullingMode(e_CullingMode_Back);

    return true;
  }

  bool Renderer3DMessage_CreateFrameBuffer::Execute(void *caller) {

    Renderer3D *renderer = static_cast<Renderer3D*>(caller);

    frameBufferID = renderer->CreateFrameBuffer();
    renderer->BindFrameBuffer(frameBufferID);

    // texture buffers
    if (target1 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target1, texID1);
    if (target2 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target2, texID2);
    if (target3 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target3, texID3);
    if (target4 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target4, texID4);
    if (target5 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target5, texID5);

    // all draw buffers must specify attachment points that have images attached. so to be sure, select none
    std::vector<e_TargetAttachment> targets;
    targets.push_back(e_TargetAttachment_None);
    renderer->SetRenderTargets(targets);
    targets.clear();

    if (!renderer->CheckFrameBufferStatus()) Log(e_FatalError, "Renderer3DMessage_CreateFrameBuffer", "Execute", "Could not create framebuffer");

    renderer->BindFrameBuffer(0);

    targets.push_back(e_TargetAttachment_Back);
    renderer->SetRenderTargets(targets);
    targets.clear();

    return true;
  }

  bool Renderer3DMessage_DeleteFrameBuffer::Execute(void *caller) {

    Renderer3D *renderer = static_cast<Renderer3D*>(caller);

    renderer->BindFrameBuffer(frameBufferID);
    if (target1 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target1, 0);
    if (target2 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target2, 0);
    if (target3 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target3, 0);
    if (target4 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target4, 0);
    if (target5 != e_TargetAttachment_None) renderer->SetFrameBufferTexture2D(target5, 0);
    renderer->DeleteFrameBuffer(frameBufferID);

    return true;
  }

}
