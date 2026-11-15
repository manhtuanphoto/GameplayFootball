#include "gltfloader.hpp"
#include "gltfdata.hpp"

#include "base/log.hpp"
#include "base/utils.hpp"
#include "base/geometry/triangle.hpp"
#include "base/geometry/trianglemeshutils.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "scene/resources/surface.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace blunted {

  namespace {

    struct ColumnMat4 {
      float m[16];

      static ColumnMat4 Identity() {
        ColumnMat4 r;
        std::memset(r.m, 0, sizeof(r.m));
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
      }

      static ColumnMat4 FromTRS(const Vector3 &t, const Quaternion &q, const Vector3 &s) {
        ColumnMat4 r = Identity();
        float x = q.elements[0], y = q.elements[1], z = q.elements[2], w = q.elements[3];
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        r.m[0]  = (1.0f - 2.0f * (yy + zz)) * s.coords[0];
        r.m[1]  = (2.0f * (xy + wz))        * s.coords[0];
        r.m[2]  = (2.0f * (xz - wy))        * s.coords[0];

        r.m[4]  = (2.0f * (xy - wz))        * s.coords[1];
        r.m[5]  = (1.0f - 2.0f * (xx + zz)) * s.coords[1];
        r.m[6]  = (2.0f * (yz + wx))        * s.coords[1];

        r.m[8]  = (2.0f * (xz + wy))        * s.coords[2];
        r.m[9]  = (2.0f * (yz - wx))        * s.coords[2];
        r.m[10] = (1.0f - 2.0f * (xx + yy)) * s.coords[2];

        r.m[12] = t.coords[0];
        r.m[13] = t.coords[1];
        r.m[14] = t.coords[2];
        return r;
      }

      ColumnMat4 operator*(const ColumnMat4 &rhs) const {
        ColumnMat4 out;
        for (int col = 0; col < 4; ++col) {
          for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
              m[0 * 4 + row] * rhs.m[col * 4 + 0] +
              m[1 * 4 + row] * rhs.m[col * 4 + 1] +
              m[2 * 4 + row] * rhs.m[col * 4 + 2] +
              m[3 * 4 + row] * rhs.m[col * 4 + 3];
          }
        }
        return out;
      }

      Vector3 TransformPoint(const Vector3 &p) const {
        return Vector3(
          m[0] * p.coords[0] + m[4] * p.coords[1] + m[8]  * p.coords[2] + m[12],
          m[1] * p.coords[0] + m[5] * p.coords[1] + m[9]  * p.coords[2] + m[13],
          m[2] * p.coords[0] + m[6] * p.coords[1] + m[10] * p.coords[2] + m[14]
        );
      }

      Vector3 TransformVector(const Vector3 &v) const {
        return Vector3(
          m[0] * v.coords[0] + m[4] * v.coords[1] + m[8]  * v.coords[2],
          m[1] * v.coords[0] + m[5] * v.coords[1] + m[9]  * v.coords[2],
          m[2] * v.coords[0] + m[6] * v.coords[1] + m[10] * v.coords[2]
        );
      }
    };

    void ComputeGLTFGlobalMatrices(const GLTFDocument &doc, std::vector<ColumnMat4> &outGlobals) {
      outGlobals.assign(doc.nodes.size(), ColumnMat4::Identity());
      std::vector<uint8_t> state(doc.nodes.size(), 0);

      auto solveNode = [&](auto &self, int idx) -> void {
        if (idx < 0 || idx >= static_cast<int>(doc.nodes.size())) return;
        if (state[idx] == 2 || state[idx] == 1) return;
        state[idx] = 1;
        const GLTFNode &n = doc.nodes[idx];
        ColumnMat4 local = ColumnMat4::FromTRS(n.translation, n.rotation, n.scale);
        if (n.parent >= 0 && n.parent < static_cast<int>(doc.nodes.size())) {
          self(self, n.parent);
          outGlobals[idx] = outGlobals[n.parent] * local;
        } else {
          outGlobals[idx] = local;
        }
        state[idx] = 2;
      };

      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        solveNode(solveNode, static_cast<int>(i));
      }
    }

    SDL_Surface *NormalizeSDLSurfaceFormat(SDL_Surface *raw, bool forceRGB24) {
      if (!raw) return nullptr;
      bool hasAlpha = !forceRGB24 && SDL_ISPIXELFORMAT_ALPHA(raw->format->format);
      Uint32 targetFmt = hasAlpha ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_RGB24;
      if (raw->format->format == targetFmt) return raw;
      SDL_Surface *converted = SDL_ConvertSurfaceFormat(raw, targetFmt, 0);
      SDL_FreeSurface(raw);
      return converted;
    }

    blunted::intrusive_ptr<Resource<Surface>> CreateSolidColorSurface(const std::string &identKey,
                                                                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
      auto surfMgr = ResourceManagerPool::GetInstance().GetManager<Surface>(e_ResourceType_Surface);
      bool alreadyThere = false;
      auto res = surfMgr->Fetch(identKey, false, alreadyThere, true);
      if (!alreadyThere) {
        Uint32 fmt = (a < 255) ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_RGB24;
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 4, 4, (a < 255) ? 32 : 24, fmt);
        if (surf) {
          Uint32 mapped = SDL_MapRGBA(surf->format, r, g, b, a);
          SDL_FillRect(surf, nullptr, mapped);
          res->GetResource()->SetData(surf);
        }
      }
      return res;
    }

    blunted::intrusive_ptr<Resource<Surface>> LoadGLTFTextureSurface(const GLTFDocument &doc,
                                                                     int texIndex,
                                                                     bool forceRGB24,
                                                                     const std::string &semanticHint) {
      if (texIndex < 0 || texIndex >= static_cast<int>(doc.textures.size())) {
        return blunted::intrusive_ptr<Resource<Surface>>();
      }
      int imgIdx = doc.textures[texIndex].source;
      if (imgIdx < 0 || imgIdx >= static_cast<int>(doc.images.size())) {
        return blunted::intrusive_ptr<Resource<Surface>>();
      }

      const GLTFImage &img = doc.images[imgIdx];
      auto surfMgr = ResourceManagerPool::GetInstance().GetManager<Surface>(e_ResourceType_Surface);

      // 1. External image URI on disk
      if (!img.uri.empty() && img.decodedUriBytes.empty()) {
        std::string candidate1 = doc.baseDir + img.uri;
        std::string candidate2 = doc.baseDir + "../textures/" + get_file_name(img.uri);
        std::string chosenPath;
        std::error_code ec;
        if (fs::exists(candidate1, ec)) {
          fs::path normP = fs::path(candidate1).lexically_normal();
          chosenPath = normP.generic_string();
        } else if (fs::exists(candidate2, ec)) {
          fs::path normP = fs::path(candidate2).lexically_normal();
          chosenPath = normP.generic_string();
        } else if (fs::exists(img.uri, ec)) {
          chosenPath = img.uri;
        }

        if (!chosenPath.empty()) {
          return surfMgr->Fetch(chosenPath, true, true);
        }
      }

      // 2. Embedded bufferView or data:...;base64,... image bytes
      const uint8_t *bytesPtr = nullptr;
      size_t bytesLen = 0;
      if (!img.decodedUriBytes.empty()) {
        bytesPtr = img.decodedUriBytes.data();
        bytesLen = img.decodedUriBytes.size();
      } else if (img.bufferView >= 0 && img.bufferView < static_cast<int>(doc.bufferViews.size())) {
        const GLTFBufferView &bv = doc.bufferViews[img.bufferView];
        if (bv.buffer >= 0 && bv.buffer < static_cast<int>(doc.buffers.size())) {
          const auto &buf = doc.buffers[bv.buffer];
          if (bv.byteOffset + bv.byteLength <= buf.size()) {
            bytesPtr = buf.data() + bv.byteOffset;
            bytesLen = bv.byteLength;
          }
        }
      }

      if (bytesPtr && bytesLen > 0) {
        std::string ident;
        if (!semanticHint.empty()) {
          ident = semanticHint;
        } else {
          std::ostringstream oss;
          oss << get_file_name(doc.sourcePath) << "_img" << imgIdx << (forceRGB24 ? "_rgb" : "");
          ident = oss.str();
        }

        bool alreadyThere = false;
        auto res = surfMgr->Fetch(ident, false, alreadyThere, true);
        if (!alreadyThere) {
          SDL_RWops *rw = SDL_RWFromConstMem(bytesPtr, static_cast<int>(bytesLen));
          SDL_Surface *decoded = rw ? IMG_Load_RW(rw, 1) : nullptr;
          decoded = NormalizeSDLSurfaceFormat(decoded, forceRGB24);
          if (!decoded) {
            Log(e_Warning, "GLTFLoader", "LoadGLTFTextureSurface",
                "Failed to decode embedded texture in " + doc.sourcePath + ": " + IMG_GetError());
            return blunted::intrusive_ptr<Resource<Surface>>();
          }
          res->GetResource()->SetData(decoded);
        }
        return res;
      }

      return blunted::intrusive_ptr<Resource<Surface>>();
    }

    Material BuildEngineMaterial(const GLTFDocument &doc, int matIndex) {
      Material mat;
      mat.shininess = 0.25f;
      mat.specular_amount = 0.08f;
      mat.self_illumination = Vector3(0.0f, 0.0f, 0.0f);

      if (matIndex < 0 || matIndex >= static_cast<int>(doc.materials.size())) {
        mat.diffuseTexture = CreateSolidColorSurface("gltf_default_white", 230, 230, 230, 255);
        return mat;
      }

      const GLTFMaterial &gm = doc.materials[matIndex];

      // Allow custom .glb player characters to opt into team kit / skin swapping by naming a material "kit" or "skin"
      std::string lowerMatName = gm.name;
      std::transform(lowerMatName.begin(), lowerMatName.end(), lowerMatName.begin(), ::tolower);
      std::string semanticHint;
      if (lowerMatName == "kit" || lowerMatName == "kit_template" || lowerMatName == "jersey") {
        semanticHint = "kit_template.png";
      } else if (lowerMatName == "skin" || lowerMatName == "body_skin") {
        semanticHint = "skin.jpg";
      }

      if (gm.baseColorTexture >= 0) {
        mat.diffuseTexture = LoadGLTFTextureSurface(doc, gm.baseColorTexture, false, semanticHint);
      }

      if (!mat.diffuseTexture) {
        uint8_t r = static_cast<uint8_t>(clamp(gm.baseColorFactor[0] * 255.0f, 0.0f, 255.0f));
        uint8_t g = static_cast<uint8_t>(clamp(gm.baseColorFactor[1] * 255.0f, 0.0f, 255.0f));
        uint8_t b = static_cast<uint8_t>(clamp(gm.baseColorFactor[2] * 255.0f, 0.0f, 255.0f));
        uint8_t a = static_cast<uint8_t>(clamp(gm.baseColorFactor[3] * 255.0f, 0.0f, 255.0f));
        std::string key = semanticHint.empty()
          ? ("gltf_solid_" + int_to_str(r) + "_" + int_to_str(g) + "_" + int_to_str(b) + "_" + int_to_str(a))
          : semanticHint;
        mat.diffuseTexture = CreateSolidColorSurface(key, r, g, b, a);
      }

      if (gm.normalTexture >= 0) {
        mat.normalTexture = LoadGLTFTextureSurface(doc, gm.normalTexture, true, "");
      }

      if (gm.emissiveTexture >= 0) {
        mat.illuminationTexture = LoadGLTFTextureSurface(doc, gm.emissiveTexture, false, "");
      }

      if (gm.specularTexture >= 0) {
        mat.specularTexture = LoadGLTFTextureSurface(doc, gm.specularTexture, false, "");
      } else if (gm.metallicRoughnessTexture >= 0) {
        mat.specularTexture = LoadGLTFTextureSurface(doc, gm.metallicRoughnessTexture, false, "");
      }

      // Map PBR roughness/metallic to GameplayFootball's Blinn-Phong deferred G-buffer parameters,
      // or use exact legacy shininess/specularAmount when stored in material extras
      float smoothness = clamp(1.0f - gm.roughnessFactor, 0.02f, 0.98f);
      mat.shininess = (gm.legacyShininess >= 0.0f)
        ? gm.legacyShininess
        : clamp(smoothness * smoothness, 0.01f, 1.0f);
      mat.specular_amount = (gm.legacySpecularAmount >= 0.0f)
        ? gm.legacySpecularAmount
        : clamp(smoothness * (0.04f + 0.35f * gm.metallicFactor), 0.002f, 0.6f);
      mat.self_illumination = Vector3(gm.emissiveFactor[0], gm.emissiveFactor[1], gm.emissiveFactor[2]);

      return mat;
    }

    void ComputeVertexNormals(const std::vector<Vector3> &positions,
                              const std::vector<unsigned int> &indices,
                              std::vector<Vector3> &outNormals) {
      outNormals.assign(positions.size(), Vector3(0.0f, 0.0f, 0.0f));
      for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        unsigned int i0 = indices[t + 0];
        unsigned int i1 = indices[t + 1];
        unsigned int i2 = indices[t + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;
        Vector3 e1 = positions[i1] - positions[i0];
        Vector3 e2 = positions[i2] - positions[i0];
        Vector3 fn = e1.GetCrossProduct(e2);
        outNormals[i0] += fn;
        outNormals[i1] += fn;
        outNormals[i2] += fn;
      }
      for (auto &n : outNormals) {
        if (n.GetLength() > 1e-6f) n.Normalize();
        else n = Vector3(0.0f, 0.0f, 1.0f);
      }
    }

    void ComputeVertexTangents(const std::vector<Vector3> &positions,
                               const std::vector<Vector3> &normals,
                               const std::vector<Vector3> &uvs,
                               const std::vector<unsigned int> &indices,
                               std::vector<Vector3> &outTangents,
                               std::vector<Vector3> &outBitangents) {
      size_t vCount = positions.size();
      outTangents.assign(vCount, Vector3(0.0f, 0.0f, 0.0f));
      outBitangents.assign(vCount, Vector3(0.0f, 0.0f, 0.0f));

      for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        unsigned int i0 = indices[t + 0];
        unsigned int i1 = indices[t + 1];
        unsigned int i2 = indices[t + 2];
        if (i0 >= vCount || i1 >= vCount || i2 >= vCount) continue;

        Vector3 dp1 = positions[i1] - positions[i0];
        Vector3 dp2 = positions[i2] - positions[i0];
        float du1 = uvs[i1].coords[0] - uvs[i0].coords[0];
        float dv1 = uvs[i1].coords[1] - uvs[i0].coords[1];
        float du2 = uvs[i2].coords[0] - uvs[i0].coords[0];
        float dv2 = uvs[i2].coords[1] - uvs[i0].coords[1];

        float denom = du1 * dv2 - du2 * dv1;
        if (std::fabs(denom) > 1e-8f) {
          float r = 1.0f / denom;
          Vector3 sdir = (dp1 * dv2 - dp2 * dv1) * r;
          Vector3 tdir = (dp2 * du1 - dp1 * du2) * r;
          outTangents[i0] += sdir;
          outTangents[i1] += sdir;
          outTangents[i2] += sdir;
          outBitangents[i0] += tdir;
          outBitangents[i1] += tdir;
          outBitangents[i2] += tdir;
        }
      }

      for (size_t v = 0; v < vCount; ++v) {
        const Vector3 &n = normals[v];
        Vector3 t = outTangents[v] - n * n.GetDotProduct(outTangents[v]);
        if (t.GetLength() > 1e-6f) {
          t.Normalize();
          Vector3 b = n.GetCrossProduct(t);
          if (b.GetDotProduct(outBitangents[v]) < 0.0f) b = -b;
          b.Normalize(Vector3(0.0f, 1.0f, 0.0f));
          outTangents[v] = t;
          outBitangents[v] = b;
        } else {
          Vector3 axis = (std::fabs(n.coords[0]) < 0.9f) ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 1.0f, 0.0f);
          t = axis.GetCrossProduct(n).GetNormalized(Vector3(1.0f, 0.0f, 0.0f));
          Vector3 b = n.GetCrossProduct(t).GetNormalized(Vector3(0.0f, 1.0f, 0.0f));
          outTangents[v] = t;
          outBitangents[v] = b;
        }
      }
    }

  } // namespace

  GLTFLoader::GLTFLoader() : Loader<GeometryData>() {
  }

  GLTFLoader::~GLTFLoader() {
  }

  void GLTFLoader::Load(std::string filename, blunted::intrusive_ptr<Resource<GeometryData>> resource) {
    GLTFDocument doc;
    std::string err;
    if (!LoadGLTFDocument(filename, doc, err)) {
      Log(e_FatalError, "GLTFLoader", "Load", err);
      return;
    }

    std::vector<ColumnMat4> gltfGlobalMats;
    ComputeGLTFGlobalMatrices(doc, gltfGlobalMats);

    // Precompute humanoid bind alignment if document has skins/joints
    Quaternion bindAlignRot[e_Joint_Count];
    int primaryNodeForJoint[e_Joint_Count];
    float rigUnitScale = 1.0f;
    ComputeBindPoseLimbAlignment(doc, bindAlignRot, primaryNodeForJoint, rigUnitScale);

    // Precompute per-joint source pivot & segment scale for seamless T-pose/A-pose -> straight.anim.util mapping
    Vector3 srcJointPos[e_Joint_Count];
    float segScale[e_Joint_Count];
    bool hasValidJoint[e_Joint_Count];
    for (int j = 0; j < e_Joint_Count; ++j) {
      segScale[j] = 1.0f;
      if (primaryNodeForJoint[j] >= 0) {
        srcJointPos[j] = doc.nodes[primaryNodeForJoint[j]].engineGlobalPos * rigUnitScale;
        hasValidJoint[j] = true;
      } else {
        srcJointPos[j] = GetEngineStraightBindJointPos(j);
        hasValidJoint[j] = false;
      }
    }

    auto computePairScale = [&](int parentJ, int childJ) {
      if (!hasValidJoint[parentJ] || !hasValidJoint[childJ]) return;
      float srcLen = (srcJointPos[childJ] - srcJointPos[parentJ]).GetLength();
      float dstLen = (GetEngineStraightBindJointPos(childJ) - GetEngineStraightBindJointPos(parentJ)).GetLength();
      if (srcLen > 0.03f && dstLen > 0.03f) {
        segScale[parentJ] = clamp(dstLen / srcLen, 0.72f, 1.35f);
      }
    };
    computePairScale(e_Joint_LeftShoulder, e_Joint_LeftElbow);
    computePairScale(e_Joint_RightShoulder, e_Joint_RightElbow);
    computePairScale(e_Joint_LeftThigh, e_Joint_LeftKnee);
    computePairScale(e_Joint_RightThigh, e_Joint_RightKnee);
    computePairScale(e_Joint_LeftKnee, e_Joint_LeftAnkle);
    computePairScale(e_Joint_RightKnee, e_Joint_RightAnkle);
    segScale[e_Joint_LeftElbow] = segScale[e_Joint_LeftShoulder];
    segScale[e_Joint_RightElbow] = segScale[e_Joint_RightShoulder];

    // Collect mesh instances to emit
    std::vector<int> meshNodes;
    auto collectNodes = [&](auto &self, int nodeIdx) -> void {
      if (nodeIdx < 0 || nodeIdx >= static_cast<int>(doc.nodes.size())) return;
      if (doc.nodes[nodeIdx].mesh >= 0) meshNodes.push_back(nodeIdx);
      for (int c : doc.nodes[nodeIdx].children) self(self, c);
    };

    if (!doc.defaultSceneNodes.empty()) {
      for (int r : doc.defaultSceneNodes) collectNodes(collectNodes, r);
    } else {
      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        if (doc.nodes[i].mesh >= 0) meshNodes.push_back(static_cast<int>(i));
      }
    }

    int emittedPrimitives = 0;

    for (int nodeIdx : meshNodes) {
      const GLTFNode &node = doc.nodes[nodeIdx];
      if (node.mesh < 0 || node.mesh >= static_cast<int>(doc.meshes.size())) continue;
      const GLTFMesh &mesh = doc.meshes[node.mesh];

      const bool isSkinnedNode = (node.skin >= 0 && node.skin < static_cast<int>(doc.skins.size()));
      std::vector<ColumnMat4> restSkinMatsGltf;
      std::vector<int> skinJointToEngineJoint;

      if (isSkinnedNode) {
        const GLTFSkin &skin = doc.skins[node.skin];
        size_t jCount = skin.joints.size();
        restSkinMatsGltf.assign(jCount, ColumnMat4::Identity());
        skinJointToEngineJoint.assign(jCount, 0);

        std::vector<float> ibmFloats;
        bool hasIBM = (skin.inverseBindMatrices >= 0) && ReadAccessorFloats(doc, skin.inverseBindMatrices, ibmFloats) &&
                      (ibmFloats.size() >= jCount * 16);

        for (size_t k = 0; k < jCount; ++k) {
          int jNode = skin.joints[k];
          int mappedEngineJ = ResolveNodeToEngineJointIndex(doc, jNode);
          skinJointToEngineJoint[k] = (mappedEngineJ >= 0) ? mappedEngineJ : 0;

          if (hasIBM && jNode >= 0 && jNode < static_cast<int>(gltfGlobalMats.size())) {
            ColumnMat4 ibm;
            std::memcpy(ibm.m, &ibmFloats[k * 16], 16 * sizeof(float));
            restSkinMatsGltf[k] = gltfGlobalMats[jNode] * ibm;
          } else if (jNode >= 0 && jNode < static_cast<int>(gltfGlobalMats.size())) {
            restSkinMatsGltf[k] = gltfGlobalMats[jNode];
          }
        }
      }

      for (const GLTFPrimitive &prim : mesh.primitives) {
        if (prim.mode != 4) continue; // Only TRIANGLES
        if (prim.posAccessor < 0) continue;

        std::vector<float> rawPos;
        if (!ReadAccessorFloats(doc, prim.posAccessor, rawPos) || rawPos.size() < 9) continue;
        size_t vCount = rawPos.size() / 3;

        std::vector<unsigned int> indices;
        if (prim.indices >= 0) {
          if (!ReadAccessorUInts(doc, prim.indices, indices)) continue;
        } else {
          indices.resize(vCount);
          for (size_t i = 0; i < vCount; ++i) indices[i] = static_cast<unsigned int>(i);
        }
        // Trim incomplete trailing triangle if any
        indices.resize((indices.size() / 3) * 3);
        if (indices.empty()) continue;

        std::vector<float> rawNorm;
        bool hasNorm = (prim.normAccessor >= 0) && ReadAccessorFloats(doc, prim.normAccessor, rawNorm) &&
                       (rawNorm.size() >= vCount * 3);

        std::vector<float> rawUV;
        bool hasUV = (prim.uv0Accessor >= 0) && ReadAccessorFloats(doc, prim.uv0Accessor, rawUV) &&
                     (rawUV.size() >= vCount * 2);

        std::vector<float> rawTan;
        bool hasTan = (prim.tangentAccessor >= 0) && ReadAccessorFloats(doc, prim.tangentAccessor, rawTan) &&
                      (rawTan.size() >= vCount * 4);

        std::vector<unsigned int> rawJoints0, rawJoints1;
        std::vector<float> rawWeights0, rawWeights1;
        bool hasSkinAttrs = isSkinnedNode &&
                            (prim.joints0Accessor >= 0) && (prim.weights0Accessor >= 0) &&
                            ReadAccessorUInts(doc, prim.joints0Accessor, rawJoints0) &&
                            ReadAccessorFloats(doc, prim.weights0Accessor, rawWeights0) &&
                            (rawJoints0.size() >= vCount * 4) && (rawWeights0.size() >= vCount * 4);

        bool hasSkinAttrs1 = hasSkinAttrs &&
                             (prim.joints1Accessor >= 0) && (prim.weights1Accessor >= 0) &&
                             ReadAccessorUInts(doc, prim.joints1Accessor, rawJoints1) &&
                             ReadAccessorFloats(doc, prim.weights1Accessor, rawWeights1) &&
                             (rawJoints1.size() >= vCount * 4) && (rawWeights1.size() >= vCount * 4);

        std::vector<Vector3> positions(vCount);
        std::vector<Vector3> normals(vCount, Vector3(0.0f, 0.0f, 1.0f));
        std::vector<Vector3> uvs(vCount, Vector3(0.0f, 0.0f, 0.0f));
        std::vector<Vector3> tangents(vCount, Vector3(1.0f, 0.0f, 0.0f));
        std::vector<Vector3> bitangents(vCount, Vector3(0.0f, 1.0f, 0.0f));
        std::vector<float> skinData;

        if (hasUV) {
          for (size_t v = 0; v < vCount; ++v) {
            uvs[v] = Vector3(rawUV[v * 2 + 0], rawUV[v * 2 + 1], 0.0f);
          }
        }

        if (hasSkinAttrs) {
          skinData.assign(vCount * 8, 0.0f);

          for (size_t v = 0; v < vCount; ++v) {
            Vector3 pMesh(rawPos[v * 3 + 0], rawPos[v * 3 + 1], rawPos[v * 3 + 2]);
            Vector3 nMesh = hasNorm
              ? Vector3(rawNorm[v * 3 + 0], rawNorm[v * 3 + 1], rawNorm[v * 3 + 2])
              : Vector3(0.0f, 1.0f, 0.0f);

            // 1. Evaluate glTF rest-pose skinning (handles Armature scale & non-identity IBM cleanly)
            Vector3 pRestGltf(0.0f, 0.0f, 0.0f);
            Vector3 nRestGltf(0.0f, 0.0f, 0.0f);
            float accumEngineWeights[e_Joint_Count] = { 0.0f };
            float rawSum = 0.0f;

            auto accumulateInfluence = [&](unsigned int skinJ, float w) {
              if (w <= 1e-5f || skinJ >= restSkinMatsGltf.size()) return;
              pRestGltf += restSkinMatsGltf[skinJ].TransformPoint(pMesh) * w;
              nRestGltf += restSkinMatsGltf[skinJ].TransformVector(nMesh) * w;
              int engJ = skinJointToEngineJoint[skinJ];
              accumEngineWeights[engJ] += w;
              rawSum += w;
            };

            for (int c = 0; c < 4; ++c) {
              accumulateInfluence(rawJoints0[v * 4 + c], rawWeights0[v * 4 + c]);
            }
            if (hasSkinAttrs1) {
              for (int c = 0; c < 4; ++c) {
                accumulateInfluence(rawJoints1[v * 4 + c], rawWeights1[v * 4 + c]);
              }
            }

            Vector3 pRestEng;
            Vector3 nRestEng;
            if (rawSum > 1e-5f) {
              pRestGltf /= rawSum;
              pRestEng = GltfPosToEngine(pRestGltf) * rigUnitScale;
              nRestEng = GltfPosToEngine(nRestGltf).GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
            } else {
              pRestEng = GltfPosToEngine(pMesh) * rigUnitScale;
              nRestEng = GltfPosToEngine(nMesh).GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
              accumEngineWeights[0] = 1.0f;
            }

            // 2. Keep top 4 merged canonical engine joints (0..12) and normalize to 1.0
            struct PairJW { int j; float w; };
            PairJW sorted[e_Joint_Count];
            for (int j = 0; j < e_Joint_Count; ++j) sorted[j] = { j, accumEngineWeights[j] };
            std::sort(sorted, sorted + e_Joint_Count, [](const PairJW &a, const PairJW &b) {
              return a.w > b.w;
            });

            float top4Sum = sorted[0].w + sorted[1].w + sorted[2].w + sorted[3].w;
            if (top4Sum <= 1e-5f) {
              sorted[0] = { 0, 1.0f };
              top4Sum = 1.0f;
            }

            Vector3 pStraight(0.0f, 0.0f, 0.0f);
            Vector3 nStraight(0.0f, 0.0f, 0.0f);

            for (int b = 0; b < 4; ++b) {
              int ej = sorted[b].j;
              float nw = sorted[b].w / top4Sum;
              skinData[v * 8 + b] = static_cast<float>(ej);
              skinData[v * 8 + 4 + b] = nw;

              if (nw > 1e-5f) {
                if (hasValidJoint[ej]) {
                  Vector3 localRel = (pRestEng - srcJointPos[ej]) * segScale[ej];
                  Vector3 mappedPos = (bindAlignRot[ej] * localRel) + GetEngineStraightBindJointPos(ej);
                  pStraight += mappedPos * nw;
                  nStraight += (bindAlignRot[ej] * nRestEng) * nw;
                } else {
                  pStraight += pRestEng * nw;
                  nStraight += nRestEng * nw;
                }
              }
            }

            positions[v] = pStraight;
            normals[v] = nStraight.GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
          }

          if (!hasNorm) {
            ComputeVertexNormals(positions, indices, normals);
          }
          ComputeVertexTangents(positions, normals, uvs, indices, tangents, bitangents);
        } else {
          // Static / non-skinned node mesh: apply node's global transform in glTF space, then convert to Engine Z-up
          const ColumnMat4 &gm = gltfGlobalMats[nodeIdx];
          for (size_t v = 0; v < vCount; ++v) {
            Vector3 pLocal(rawPos[v * 3 + 0], rawPos[v * 3 + 1], rawPos[v * 3 + 2]);
            positions[v] = GltfPosToEngine(gm.TransformPoint(pLocal));
            if (hasNorm) {
              Vector3 nLocal(rawNorm[v * 3 + 0], rawNorm[v * 3 + 1], rawNorm[v * 3 + 2]);
              normals[v] = GltfPosToEngine(gm.TransformVector(nLocal)).GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
            }
          }

          if (!hasNorm) {
            ComputeVertexNormals(positions, indices, normals);
          }

          if (hasTan) {
            for (size_t v = 0; v < vCount; ++v) {
              Vector3 tLocal(rawTan[v * 4 + 0], rawTan[v * 4 + 1], rawTan[v * 4 + 2]);
              float handedness = (rawTan[v * 4 + 3] < 0.0f) ? -1.0f : 1.0f;
              Vector3 tEng = GltfPosToEngine(gm.TransformVector(tLocal));
              tEng = (tEng - normals[v] * normals[v].GetDotProduct(tEng)).GetNormalized(Vector3(1.0f, 0.0f, 0.0f));
              Vector3 bEng = (normals[v].GetCrossProduct(tEng) * handedness).GetNormalized(Vector3(0.0f, 1.0f, 0.0f));
              tangents[v] = tEng;
              bitangents[v] = bEng;
            }
          } else {
            ComputeVertexTangents(positions, normals, uvs, indices, tangents, bitangents);
          }
        }

        // Pack planar SoA vertex buffer (5 * 3 * vCount floats: Pos, Norm, UV, Tan, Bitan)
        int elementStrideFloats = static_cast<int>(vCount * 3);
        int totalFloats = elementStrideFloats * GetTriangleMeshElementCount();
        float *packedVertices = new float[totalFloats];

        for (size_t v = 0; v < vCount; ++v) {
          std::memcpy(&packedVertices[0 * elementStrideFloats + v * 3], positions[v].coords, 3 * sizeof(float));
          std::memcpy(&packedVertices[1 * elementStrideFloats + v * 3], normals[v].coords,   3 * sizeof(float));
          std::memcpy(&packedVertices[2 * elementStrideFloats + v * 3], uvs[v].coords,       3 * sizeof(float));
          std::memcpy(&packedVertices[3 * elementStrideFloats + v * 3], tangents[v].coords,  3 * sizeof(float));
          std::memcpy(&packedVertices[4 * elementStrideFloats + v * 3], bitangents[v].coords,3 * sizeof(float));
        }

        Material engineMat = BuildEngineMaterial(doc, prim.material);

        resource->resourceMutex.lock();
        resource->GetResource()->AddTriangleMesh(engineMat, packedVertices, totalFloats, indices);
        if (!skinData.empty()) {
          auto &meshesRef = resource->GetResource()->GetTriangleMeshesRef();
          meshesRef.back().skinData = std::move(skinData);
        }
        resource->resourceMutex.unlock();

        ++emittedPrimitives;
      }
    }

    if (emittedPrimitives == 0) {
      Log(e_Warning, "GLTFLoader", "Load", "No triangle primitives found in " + filename);
    } else {
      Log(e_Notice, "GLTFLoader", "Load",
          "Loaded " + int_to_str(emittedPrimitives) + " primitive(s) from " + filename);
    }
  }

}
