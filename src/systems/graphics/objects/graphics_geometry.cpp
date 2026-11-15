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

#include "graphics_geometry.hpp"

#include "managers/resourcemanagerpool.hpp"

#include "../graphics_scene.hpp"
#include "../graphics_system.hpp"

#include "base/geometry/trianglemeshutils.hpp"

namespace blunted {

  GraphicsGeometry::GraphicsGeometry(GraphicsScene *graphicsScene)
      : GraphicsObject(graphicsScene),
        vertexBufferIndices(std::make_shared<std::vector<VertexBufferIndex>>()) {
  }

  GraphicsGeometry::~GraphicsGeometry() {
  }

  void GraphicsGeometry::SetBoneMatrices(const std::vector<Matrix4> &matrices) {
    boneMatrices = matrices;
    if (matrices.empty()) {
      packedBoneMatrices.reset();
      return;
    }
    if (!packedBoneMatrices || packedBoneMatrices.use_count() > 1) {
      packedBoneMatrices = std::make_shared<std::array<float, 256>>();
    }
    float *dst = packedBoneMatrices->data();
    const unsigned int count = matrices.size() < 16 ? (unsigned int)matrices.size() : 16u;
    for (unsigned int b = 0; b < count; b++) {
      memcpy(&dst[b * 16], matrices[b].elements, 16 * sizeof(float));
    }
    for (unsigned int b = count; b < 16; b++) {
      for (int m = 0; m < 16; m++) {
        dst[b * 16 + m] = (float)MATRIX4_IDENTITY[m];
      }
    }
  }

  blunted::intrusive_ptr<Interpreter> GraphicsGeometry::GetInterpreter(e_ObjectType objectType) {
    if (objectType == e_ObjectType_Geometry) {
      blunted::intrusive_ptr<GraphicsGeometry_GeometryInterpreter> geometryInterpreter(new GraphicsGeometry_GeometryInterpreter(this));
      return geometryInterpreter;
    }
    else if (objectType == e_ObjectType_Skybox) {
      blunted::intrusive_ptr<GraphicsGeometry_SkyboxInterpreter> skyboxInterpreter(new GraphicsGeometry_SkyboxInterpreter(this));
      return skyboxInterpreter;
    }
    Log(e_FatalError, "GraphicsGeometry", "GetInterpreter", "No appropriate interpreter found for this ObjectType");
    return blunted::intrusive_ptr<GraphicsGeometry_GeometryInterpreter>();
  }

  void GraphicsGeometry::SetPosition(const Vector3 &newPosition) {
    position = newPosition;
  }

  Vector3 GraphicsGeometry::GetPosition() const {
    return position;
  }

  void GraphicsGeometry::SetRotation(const Quaternion &newRotation) {
    rotation = newRotation;
  }

  Quaternion GraphicsGeometry::GetRotation() const {
    return rotation;
  }




  GraphicsGeometry_GeometryInterpreter::GraphicsGeometry_GeometryInterpreter(GraphicsGeometry *caller) : caller(caller), usesIndices(false) {
  }

  void LoadMaterials(Renderer3D *renderer3D, const Material *material, Renderer3DMaterial &r3dMaterial, blunted::intrusive_ptr < Resource<Texture> > diffuseTexture, blunted::intrusive_ptr < Resource<Texture> > normalTexture, blunted::intrusive_ptr < Resource<Texture> > specularTexture, blunted::intrusive_ptr < Resource<Texture> > illuminationTexture) {

    if (material->diffuseTexture) {
      blunted::intrusive_ptr < Resource<Surface> > surface = material->diffuseTexture;

      bool texAlreadyThere = false;
      diffuseTexture =
        ResourceManagerPool::GetInstance().GetManager<Texture>(e_ResourceType_Texture)->
          Fetch(surface->GetIdentString(), false, texAlreadyThere, true); // false == don't try to use loader

      if (!texAlreadyThere) {
        //printf("%s\n", surface->GetIdentString().c_str());
        surface->resourceMutex.lock();
        SDL_Surface *image = surface->GetResource()->GetData();
        diffuseTexture->GetResource()->SetRenderer3D(renderer3D);
        bool repeat = true;
        bool mipmaps = true;
        bool bilinear = true;
        bool alpha = SDL_ISPIXELFORMAT_ALPHA((image->format->format));
        diffuseTexture->GetResource()->CreateTexture(alpha ? e_InternalPixelFormat_SRGBA8 : e_InternalPixelFormat_SRGB8, alpha ? e_PixelFormat_RGBA : e_PixelFormat_RGB, image->w, image->h, alpha, repeat, mipmaps, bilinear);
        diffuseTexture->GetResource()->UpdateTexture(image, alpha, true);
        surface->resourceMutex.unlock();
      }
    }

    if (material->normalTexture) {
      blunted::intrusive_ptr < Resource<Surface> > surface = material->normalTexture;

      bool texAlreadyThere = false;
      normalTexture =
        ResourceManagerPool::GetInstance().GetManager<Texture>(e_ResourceType_Texture)->
          Fetch(surface->GetIdentString(), false, texAlreadyThere, true); // false == don't try to use loader

      if (!texAlreadyThere) {
        surface->resourceMutex.lock();
        SDL_Surface *image = surface->GetResource()->GetData();
        normalTexture->GetResource()->SetRenderer3D(renderer3D);
        normalTexture->GetResource()->CreateTexture(e_InternalPixelFormat_RGB8, e_PixelFormat_RGB, image->w, image->h, false, true, true, true);
        normalTexture->GetResource()->UpdateTexture(image, false, true);
        surface->resourceMutex.unlock();
      }
    }

    if (material->specularTexture) {
      blunted::intrusive_ptr < Resource<Surface> > surface = material->specularTexture;

      bool texAlreadyThere = false;
      specularTexture =
        ResourceManagerPool::GetInstance().GetManager<Texture>(e_ResourceType_Texture)->
          Fetch(surface->GetIdentString(), false, texAlreadyThere, true); // false == don't try to use loader

      if (!texAlreadyThere) {
        surface->resourceMutex.lock();
        SDL_Surface *image = surface->GetResource()->GetData();
        specularTexture->GetResource()->SetRenderer3D(renderer3D);
        specularTexture->GetResource()->CreateTexture(e_InternalPixelFormat_RGB8, e_PixelFormat_RGB, image->w, image->h, false, true, true, true);
        specularTexture->GetResource()->UpdateTexture(image, false, true);
        surface->resourceMutex.unlock();
      }
    }

    if (material->illuminationTexture) {
      blunted::intrusive_ptr < Resource<Surface> > surface = material->illuminationTexture;

      bool texAlreadyThere = false;
      illuminationTexture =
        ResourceManagerPool::GetInstance().GetManager<Texture>(e_ResourceType_Texture)->
          Fetch(surface->GetIdentString(), false, texAlreadyThere, true); // false == don't try to use loader

      if (!texAlreadyThere) {
        surface->resourceMutex.lock();
        SDL_Surface *image = surface->GetResource()->GetData();
        illuminationTexture->GetResource()->SetRenderer3D(renderer3D);
        illuminationTexture->GetResource()->CreateTexture(e_InternalPixelFormat_RGB8, e_PixelFormat_RGB, image->w, image->h, false, true, true, true);
        illuminationTexture->GetResource()->UpdateTexture(image, false, true);
        surface->resourceMutex.unlock();
      }
    }

    if (diffuseTexture) r3dMaterial.diffuseTexture = diffuseTexture;
    if (normalTexture) r3dMaterial.normalTexture = normalTexture;
    if (specularTexture) r3dMaterial.specularTexture = specularTexture;
    if (illuminationTexture) r3dMaterial.illuminationTexture = illuminationTexture;
    r3dMaterial.shininess = material->shininess;
    r3dMaterial.specular_amount = material->specular_amount;
    r3dMaterial.self_illumination = material->self_illumination;
  }

  void GraphicsGeometry_GeometryInterpreter::OnLoad(blunted::intrusive_ptr<Geometry> geometry) {

    // todo: rewrite that indices/usesIndices thing, it's very unclear what's going on

    //printf("loading %s\n", geometry->GetName().c_str());

    blunted::intrusive_ptr < Resource<GeometryData> > resource = geometry->GetGeometryData();

    resource->resourceMutex.lock();
    bool dynamicBuffer = resource->GetResource()->IsDynamic();

    bool alreadyThere = false;
    // todo: use the resource mutex ??
    caller->vertexBuffer =
      ResourceManagerPool::GetInstance().GetManager<VertexBuffer>(e_ResourceType_VertexBuffer)->
        Fetch(resource->GetIdentString(), false, alreadyThere, true); // false == don't try to use loader
    //printf("%s, %i\n", resource->GetIdentString().c_str(), alreadyThere);

    const std::vector < MaterializedTriangleMesh > &triangleMeshes = resource->GetResource()->GetTriangleMeshesRef();
    Renderer3D *renderer3D = caller->GetGraphicsScene()->GetGraphicsSystem()->GetRenderer3D();

    if (!caller->vertexBufferIndices || caller->vertexBufferIndices.use_count() > 1) {
      caller->vertexBufferIndices = std::make_shared<std::vector<VertexBufferIndex>>();
    } else {
      caller->vertexBufferIndices->clear();
    }
    caller->vertexBufferIndices->reserve(triangleMeshes.size());

    //std::vector<Triangle*> triangles;

    float *vertices = 0;
    int verticesDataSize = 0;
    std::vector<unsigned int> indices;
    std::vector<unsigned int> indicesTest;
    std::vector<float> combinedSkinData;
    bool anySkinData = false;
    int indicesSize = 0;
    if (!alreadyThere) {
      caller->vertexBuffer->resourceMutex.lock();
      for (unsigned int i = 0; i < triangleMeshes.size(); i++) {
        verticesDataSize += triangleMeshes[i].verticesDataSize;
        indicesSize += triangleMeshes[i].indices.size();
        if (!triangleMeshes[i].skinData.empty()) anySkinData = true;
      }
      vertices = new float[verticesDataSize];
      indices.reserve(indicesSize);
      if (anySkinData) {
        int totalVertices = (verticesDataSize / GetTriangleMeshElementCount()) / 3;
        combinedSkinData.reserve(totalVertices * 8);
      }
    }

    int startIndex = 0;
    int currentSize = 0;

    for (unsigned int i = 0; i < triangleMeshes.size(); i++) {

      // material

      const Material *material = &triangleMeshes.at(i).material;
      blunted::intrusive_ptr < Resource<Texture> > diffuseTexture;
      blunted::intrusive_ptr < Resource<Texture> > normalTexture;
      blunted::intrusive_ptr < Resource<Texture> > specularTexture;
      blunted::intrusive_ptr < Resource<Texture> > illuminationTexture;

      Renderer3DMaterial r3dMaterial;
      LoadMaterials(renderer3D, material, r3dMaterial, diffuseTexture, normalTexture, specularTexture, illuminationTexture);


      // mesh

      startIndex += currentSize;
      currentSize = 0;

      if (!alreadyThere) {
        /*
        for (int e = 0; e < GetTriangleMeshElementCount(); e++) {
          memcpy(&triangleMesh[e * (tmeshsize / GetTriangleMeshElementCount()) + startIndex * 3 * 3], &triangleMeshes[i].triangleMesh[e * (triangleMeshes[i].triangleMeshSize / GetTriangleMeshElementCount())], triangleMeshes[i].triangleMeshSize / GetTriangleMeshElementCount() * sizeof(float));
        }
        */

        for (int e = 0; e < GetTriangleMeshElementCount(); e++) {
          //printf("%s: e: %i, verticesDataSize: %i, startIndex: %i, triangleMeshes[i].verticesDataSize: %i\n", geometry->GetName().c_str(), e, verticesDataSize, startIndex, triangleMeshes[i].verticesDataSize);
          memcpy(&vertices[e * (verticesDataSize / GetTriangleMeshElementCount()) + startIndex],
                 &triangleMeshes[i].vertices[e * (triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount())],
                 triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount() * sizeof(float));
        }

        if (anySkinData) {
          int subVerts = (triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount()) / 3;
          if ((int)triangleMeshes[i].skinData.size() == subVerts * 8) {
            combinedSkinData.insert(combinedSkinData.end(), triangleMeshes[i].skinData.begin(), triangleMeshes[i].skinData.end());
          } else {
            for (int sv = 0; sv < subVerts; sv++) {
              combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f);
              combinedSkinData.push_back(1.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f);
            }
          }
        }

        //indices.insert(indices.end(), triangleMeshes[i].indices.begin(), triangleMeshes[i].indices.end());
        for (unsigned int index = 0; index < triangleMeshes[i].indices.size(); index++) {
          indices.push_back(startIndex / 3 + triangleMeshes[i].indices.at(index));
        }

      }

      currentSize = triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount();

      VertexBufferIndex vbIndex;
      if (indices.size() > 0) {
        vbIndex.startIndex = (indices.size() - triangleMeshes[i].indices.size()); // start index id
        vbIndex.size = triangleMeshes[i].indices.size(); // number of indices
      } else {
        vbIndex.startIndex = startIndex / 3; // start vertex id
        vbIndex.size = currentSize / 3; // number of vertices
      }

      vbIndex.material = r3dMaterial;
      caller->vertexBufferIndices->push_back(vbIndex);
    }

    resource->resourceMutex.unlock();

    caller->SetBoneMatrices(geometry->GetBoneMatrices());

    if (indices.size() > 0) usesIndices = true; else usesIndices = false;

    if (!alreadyThere) {
      caller->vertexBuffer->GetResource()->SetTriangleMesh(vertices, verticesDataSize, indices);
      caller->vertexBuffer->GetResource()->SetSkinData(combinedSkinData);
      caller->vertexBuffer->GetResource()->CreateOrUpdateVertexBuffer(renderer3D, dynamicBuffer);
      caller->vertexBuffer->resourceMutex.unlock();
    }

  }

  void GraphicsGeometry_GeometryInterpreter::OnUpdateGeometry(blunted::intrusive_ptr<Geometry> geometry, bool updateMaterials) {

    // todo: right now, you can only go from using no indices to using indices. you cannot go back to not using indices anymore. fix this.
    // todo: rewrite that indices/usesIndices thing either way, it's very unclear what's going on

    blunted::intrusive_ptr < Resource<GeometryData> > resource = geometry->GetGeometryData();

    resource->resourceMutex.lock();

    bool dynamicBuffer = resource->GetResource()->IsDynamic();
    const std::vector < MaterializedTriangleMesh > &triangleMeshes = resource->GetResource()->GetTriangleMeshesRef();
    Renderer3D *renderer3D = caller->GetGraphicsScene()->GetGraphicsSystem()->GetRenderer3D();

    caller->vertexBuffer->resourceMutex.lock();
    float *vertices = 0;
    int currentVerticesDataSize = caller->vertexBuffer->GetResource()->GetVerticesDataSize();

    std::vector<unsigned int> indices;
    std::vector<float> combinedSkinData;
    bool anySkinData = false;
    int verticesDataSize = 0;
    int indicesSize = 0;
    for (unsigned int i = 0; i < triangleMeshes.size(); i++) {
      verticesDataSize += triangleMeshes[i].verticesDataSize;
      indicesSize += triangleMeshes[i].indices.size();
      if (!triangleMeshes[i].skinData.empty()) anySkinData = true;
    }
    indices.reserve(indicesSize);
    if (anySkinData) {
      int totalVertices = (verticesDataSize / GetTriangleMeshElementCount()) / 3;
      combinedSkinData.reserve(totalVertices * 8);
    }
    bool newFloatData = false;
    bool updateIndices = false;
    if (verticesDataSize == currentVerticesDataSize) {
      vertices = caller->vertexBuffer->GetResource()->GetTriangleMesh();
      newFloatData = false;
      updateIndices = updateMaterials; // todo: can we decouple indices and materials in this function? now we need to update indices on material update and the other way around
    } else {
      vertices = new float[verticesDataSize];
      newFloatData = true;
      updateIndices = true;
    }

    int startIndex = 0;
    int startIndicesIndex = 0; // omfg
    int currentSize = 0;

    if (updateIndices) {
      if (!caller->vertexBufferIndices || caller->vertexBufferIndices.use_count() > 1) {
        caller->vertexBufferIndices = std::make_shared<std::vector<VertexBufferIndex>>();
      } else {
        caller->vertexBufferIndices->clear();
      }
      caller->vertexBufferIndices->reserve(triangleMeshes.size());
    }

    for (unsigned int i = 0; i < triangleMeshes.size(); i++) {


      // mesh

      startIndex += currentSize;
      currentSize = 0;

      for (int e = 0; e < GetTriangleMeshElementCount(); e++) {
        //printf("%s: e: %i, verticesDataSize: %i, startIndex: %i, triangleMeshes[i].verticesDataSize: %i\n", geometry->GetName().c_str(), e, verticesDataSize, startIndex, triangleMeshes[i].verticesDataSize);
        memcpy(&vertices[e * (verticesDataSize / GetTriangleMeshElementCount()) + startIndex],
               &triangleMeshes[i].vertices[e * (triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount())],
               triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount() * sizeof(float));
      }

      if (anySkinData) {
        int subVerts = (triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount()) / 3;
        if ((int)triangleMeshes[i].skinData.size() == subVerts * 8) {
          combinedSkinData.insert(combinedSkinData.end(), triangleMeshes[i].skinData.begin(), triangleMeshes[i].skinData.end());
        } else {
          for (int sv = 0; sv < subVerts; sv++) {
            combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f);
            combinedSkinData.push_back(1.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f); combinedSkinData.push_back(0.0f);
          }
        }
      }

      if (!usesIndices) { // can only set indices once (todo: make OnUpdateIndices function, or something like that)
        for (unsigned int index = 0; index < triangleMeshes[i].indices.size(); index++) {
          indices.push_back(startIndex / 3 + triangleMeshes[i].indices.at(index));
          //printf("vertex index: %i + %i = %i\n", startIndex * 3, triangleMeshes[i].indices.at(index), startIndex * 3 + triangleMeshes[i].indices.at(index));
        }
      }

      currentSize = triangleMeshes[i].verticesDataSize / GetTriangleMeshElementCount();

      if (updateIndices) {


        // material

        const Material *material = &triangleMeshes.at(i).material;
        blunted::intrusive_ptr < Resource<Texture> > diffuseTexture;
        blunted::intrusive_ptr < Resource<Texture> > normalTexture;
        blunted::intrusive_ptr < Resource<Texture> > specularTexture;
        blunted::intrusive_ptr < Resource<Texture> > illuminationTexture;

        Renderer3DMaterial r3dMaterial;
        LoadMaterials(renderer3D, material, r3dMaterial, diffuseTexture, normalTexture, specularTexture, illuminationTexture);


        // indices

        VertexBufferIndex vbIndex;

        // this code makes this version only work on meshes that are the same size/materialorder as their previous version
        vbIndex.material = r3dMaterial;

        if ((!usesIndices && indices.size() > 0) || usesIndices) { // include the first time using indices
          vbIndex.startIndex = startIndicesIndex;
          vbIndex.size = triangleMeshes[i].indices.size(); // number of indices
        } else {
          vbIndex.startIndex = startIndex / 3; // start vertex id
          vbIndex.size = currentSize / 3; // number of vertices
        }

        startIndicesIndex += triangleMeshes[i].indices.size();

        //printf("st %i VS %i\n", (indices.size() - triangleMeshes[i].indices.size()), startIndex / 3);
        //printf("si %i VS %i\n", triangleMeshes[i].indices.size(), currentSize / 3);

        caller->vertexBufferIndices->push_back(vbIndex);

      }

    }

    resource->resourceMutex.unlock();

    if (indices.size() > 0) usesIndices = true;

    if (newFloatData) {
      caller->vertexBuffer->GetResource()->SetTriangleMesh(vertices, verticesDataSize, indices);
    } else {
      caller->vertexBuffer->GetResource()->TriangleMeshWasUpdatedExternally(verticesDataSize, indices);
    }
    caller->vertexBuffer->GetResource()->SetSkinData(combinedSkinData);
    caller->vertexBuffer->GetResource()->CreateOrUpdateVertexBuffer(renderer3D, dynamicBuffer);
    caller->vertexBuffer->resourceMutex.unlock();
  }

  void GraphicsGeometry_GeometryInterpreter::OnUnload() {
    //printf("resetting link to vertexbuffer.. ");
    caller->vertexBuffer.reset();
    caller->vertexBufferIndices.reset();
    caller->packedBoneMatrices.reset();
    delete caller;
    caller = 0;
    //printf("[OK]\n");
  }

  void GraphicsGeometry_GeometryInterpreter::OnMove(const Vector3 &position) {
    caller->SetPosition(position);
  }

  void GraphicsGeometry_GeometryInterpreter::OnRotate(const Quaternion &rotation) {
    caller->SetRotation(rotation);
  }

  void GraphicsGeometry_GeometryInterpreter::OnSetBoneMatrices(const std::vector<Matrix4> &matrices) {
    caller->SetBoneMatrices(matrices);
  }

  void GraphicsGeometry_GeometryInterpreter::GetVertexBufferQueue(std::deque<VertexBufferQueueEntry> &queue) {
    VertexBufferQueueEntry queueEntry;
    queueEntry.vertexBufferIndices = caller->vertexBufferIndices;
    queueEntry.vertexBuffer = caller->vertexBuffer;
    queueEntry.vaoIDCache = caller->vertexBuffer ? caller->vertexBuffer->GetResource()->GetVaoID() : 0;
    queueEntry.firstTexIDCache = (caller->vertexBufferIndices && !caller->vertexBufferIndices->empty() &&
                                  (*caller->vertexBufferIndices)[0].material.diffuseTexture)
                                   ? (*caller->vertexBufferIndices)[0].material.diffuseTexture->GetResource()->GetID()
                                   : 0;
    queueEntry.position = caller->GetPosition();
    queueEntry.rotation = caller->GetRotation();
    if (caller->packedBoneMatrices) {
      queueEntry.hasSkinning = true;
      queueEntry.boneMatrices = caller->packedBoneMatrices;
    } else {
      queueEntry.hasSkinning = false;
    }
    queue.push_back(std::move(queueEntry));
  }

  void GraphicsGeometry_GeometryInterpreter::OnSynchronize() {
    OnLoad(static_cast<Geometry*>(subjectPtr));
    //OnUpdateGeometry(static_cast<Geometry*>(subjectPtr));
    // todo: implement properly
  }

  void GraphicsGeometry_GeometryInterpreter::OnPoke() {
  }




  // SKYBOX INTERPRETER

  GraphicsGeometry_SkyboxInterpreter::GraphicsGeometry_SkyboxInterpreter(GraphicsGeometry *caller) : GraphicsGeometry_GeometryInterpreter(caller) {
  }

  void GraphicsGeometry_SkyboxInterpreter::OnPoke() {
  }

}
