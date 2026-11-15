#include "assetupgrader.hpp"

#include "base/log.hpp"
#include "base/sdl_surface.hpp"
#include "base/utils.hpp"
#include "base/geometry/trianglemeshutils.hpp"
#include "loaders/aseloader.hpp"
#include "loaders/gltfdata.hpp"
#include "loaders/gltfloader.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "utils/animation.hpp"
#include "utils/animationextensions/footballanimationextension.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace blunted {

  namespace {

    // Inverse of GltfPosToEngine(gx, gy, gz) = (gx, -gz, gy)
    // Engine (RH, +X Right, -Y Forward, +Z Up) -> glTF 2.0 (RH, +X Right, +Y Up, +Z Forward)
    inline Vector3 EnginePosToGltf(const Vector3 &e) {
      return Vector3(e.coords[0], e.coords[2], -e.coords[1]);
    }

    inline std::array<float, 4> EngineQuatToGltfXYZW(const Quaternion &q) {
      return { q.elements[0], q.elements[2], -q.elements[1], q.elements[3] };
    }

    std::string EscapeJson(const std::string &s) {
      std::ostringstream oss;
      for (unsigned char c : s) {
        switch (c) {
          case '\"': oss << "\\\""; break;
          case '\\': oss << "\\\\"; break;
          case '\b': oss << "\\b"; break;
          case '\f': oss << "\\f"; break;
          case '\n': oss << "\\n"; break;
          case '\r': oss << "\\r"; break;
          case '\t': oss << "\\t"; break;
          default:
            if (c < 0x20) {
              oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            } else {
              oss << static_cast<char>(c);
            }
            break;
        }
      }
      return oss.str();
    }

    std::string FmtFloat(float v) {
      if (!std::isfinite(v) || std::fabs(v) < 1e-8f) return "0";
      std::ostringstream oss;
      oss << std::setprecision(8) << v;
      return oss.str();
    }

    struct BinaryGlbBuilder {
      struct BufferViewRec {
        size_t byteOffset = 0;
        size_t byteLength = 0;
        int target = 0; // 0 = omitted, 34962 = ARRAY_BUFFER, 34963 = ELEMENT_ARRAY_BUFFER
      };

      struct AccessorRec {
        int bufferView = -1;
        int componentType = 5126;
        size_t count = 0;
        std::string type = "SCALAR";
        bool hasMinMax = false;
        std::vector<float> minVals;
        std::vector<float> maxVals;
      };

      std::vector<uint8_t> bin;
      std::vector<BufferViewRec> bufferViews;
      std::vector<AccessorRec> accessors;

      void Align4() {
        while (bin.size() % 4 != 0) bin.push_back(0);
      }

      int AddBufferView(const void *data, size_t byteLength, int target = 0) {
        Align4();
        size_t offset = bin.size();
        if (byteLength > 0 && data != nullptr) {
          const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
          bin.insert(bin.end(), bytes, bytes + byteLength);
        }
        Align4();
        BufferViewRec bv;
        bv.byteOffset = offset;
        bv.byteLength = byteLength;
        bv.target = target;
        bufferViews.push_back(bv);
        return static_cast<int>(bufferViews.size()) - 1;
      }

      int AddFloatAccessor(const std::vector<float> &vals, int numComp, const std::string &typeStr, int target = 0, bool withMinMax = false) {
        size_t count = (numComp > 0) ? (vals.size() / static_cast<size_t>(numComp)) : 0;
        int bv = AddBufferView(vals.data(), vals.size() * sizeof(float), target);
        AccessorRec acc;
        acc.bufferView = bv;
        acc.componentType = 5126; // FLOAT
        acc.count = count;
        acc.type = typeStr;
        if (withMinMax && count > 0) {
          acc.hasMinMax = true;
          acc.minVals.assign(numComp,  1e30f);
          acc.maxVals.assign(numComp, -1e30f);
          for (size_t i = 0; i < count; ++i) {
            for (int c = 0; c < numComp; ++c) {
              float v = vals[i * numComp + c];
              if (v < acc.minVals[c]) acc.minVals[c] = v;
              if (v > acc.maxVals[c]) acc.maxVals[c] = v;
            }
          }
        }
        accessors.push_back(std::move(acc));
        return static_cast<int>(accessors.size()) - 1;
      }

      int AddUInt32Accessor(const std::vector<uint32_t> &vals, int target = 34963) {
        int bv = AddBufferView(vals.data(), vals.size() * sizeof(uint32_t), target);
        AccessorRec acc;
        acc.bufferView = bv;
        acc.componentType = 5125; // UNSIGNED_INT
        acc.count = vals.size();
        acc.type = "SCALAR";
        accessors.push_back(std::move(acc));
        return static_cast<int>(accessors.size()) - 1;
      }

      int AddUShortVec4Accessor(const std::vector<uint16_t> &vals, int target = 34962) {
        size_t count = vals.size() / 4;
        int bv = AddBufferView(vals.data(), vals.size() * sizeof(uint16_t), target);
        AccessorRec acc;
        acc.bufferView = bv;
        acc.componentType = 5123; // UNSIGNED_SHORT
        acc.count = count;
        acc.type = "VEC4";
        accessors.push_back(std::move(acc));
        return static_cast<int>(accessors.size()) - 1;
      }

      void EmitBufferAndAccessorJson(std::ostringstream &json) const {
        json << "\"buffers\":[{\"byteLength\":" << bin.size() << "}],";
        json << "\"bufferViews\":[";
        for (size_t i = 0; i < bufferViews.size(); ++i) {
          if (i > 0) json << ",";
          json << "{\"buffer\":0,\"byteOffset\":" << bufferViews[i].byteOffset
               << ",\"byteLength\":" << bufferViews[i].byteLength;
          if (bufferViews[i].target != 0) {
            json << ",\"target\":" << bufferViews[i].target;
          }
          json << "}";
        }
        json << "],\"accessors\":[";
        for (size_t i = 0; i < accessors.size(); ++i) {
          if (i > 0) json << ",";
          const auto &a = accessors[i];
          json << "{\"bufferView\":" << a.bufferView
               << ",\"componentType\":" << a.componentType
               << ",\"count\":" << a.count
               << ",\"type\":\"" << a.type << "\"";
          if (a.hasMinMax) {
            json << ",\"min\":[";
            for (size_t c = 0; c < a.minVals.size(); ++c) {
              if (c > 0) json << ",";
              json << FmtFloat(a.minVals[c]);
            }
            json << "],\"max\":[";
            for (size_t c = 0; c < a.maxVals.size(); ++c) {
              if (c > 0) json << ",";
              json << FmtFloat(a.maxVals[c]);
            }
            json << "]";
          }
          json << "}";
        }
        json << "]";
      }

      bool WriteGlbFile(const std::string &outPath, const std::string &jsonStr) {
        std::string paddedJson = jsonStr;
        while (paddedJson.size() % 4 != 0) paddedJson.push_back(' ');
        Align4();

        uint32_t magic = 0x46546C67u; // "glTF"
        uint32_t version = 2u;
        uint32_t jsonChunkLen = static_cast<uint32_t>(paddedJson.size());
        uint32_t jsonChunkType = 0x4E4F534Au; // "JSON"
        uint32_t binChunkLen = static_cast<uint32_t>(bin.size());
        uint32_t binChunkType = 0x004E4942u; // "BIN\0"
        uint32_t totalLen = 12u + 8u + jsonChunkLen + (binChunkLen > 0 ? (8u + binChunkLen) : 0u);

        std::error_code ec;
        fs::create_directories(fs::path(outPath).parent_path(), ec);
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;

        out.write(reinterpret_cast<const char *>(&magic), 4);
        out.write(reinterpret_cast<const char *>(&version), 4);
        out.write(reinterpret_cast<const char *>(&totalLen), 4);

        out.write(reinterpret_cast<const char *>(&jsonChunkLen), 4);
        out.write(reinterpret_cast<const char *>(&jsonChunkType), 4);
        out.write(paddedJson.data(), paddedJson.size());

        if (binChunkLen > 0) {
          out.write(reinterpret_cast<const char *>(&binChunkLen), 4);
          out.write(reinterpret_cast<const char *>(&binChunkType), 4);
          out.write(reinterpret_cast<const char *>(bin.data()), bin.size());
        }

        return out.good();
      }
    };

    struct TextureCatalog {
      std::map<std::string, std::string> byLowerFilename; // e.g. "skin.jpg" -> "media/objects/players/textures/skin.jpg"

      void BuildAndPrewarm(const std::string &mediaRoot) {
        auto surfMgr = ResourceManagerPool::GetInstance().GetManager<Surface>(e_ResourceType_Surface);

        // Register a tiny fallback surface for "orange.jpg" (ASELoader's default when MAP_DIFFUSE is omitted)
        {
          bool alreadyThere = false;
          auto res = surfMgr->Fetch("orange.jpg", false, alreadyThere, true);
          if (!alreadyThere) {
            SDL_Surface *surf = CreateSDLSurface(4, 4);
            res->GetResource()->SetData(surf);
          }
        }

        std::error_code ec;
        for (const auto &entry : fs::recursive_directory_iterator(mediaRoot, ec)) {
          if (!entry.is_regular_file(ec)) continue;
          std::string ext = entry.path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            std::string rel = entry.path().generic_string();
            std::string fname = entry.path().filename().string();
            std::string lowerName = fname;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (byLowerFilename.find(lowerName) == byLowerFilename.end()) {
              byLowerFilename[lowerName] = rel;
            }
            // Prewarm Surface manager so ASELoader finds every image by basename even if an .ase has a stale subfolder path
            surfMgr->Fetch(rel, true, true);
          }
        }
      }

      std::string ResolveRelativeUri(const std::string &identOrPath, const fs::path &glbDir) const {
        if (identOrPath.empty()) return "";
        std::string lowerBase = get_file_name(identOrPath);
        std::transform(lowerBase.begin(), lowerBase.end(), lowerBase.begin(), ::tolower);
        if (lowerBase == "orange.jpg") return "";

        std::error_code ec;
        fs::path targetPath;
        if (fs::exists(identOrPath, ec)) {
          targetPath = fs::path(identOrPath);
        } else {
          auto it = byLowerFilename.find(lowerBase);
          if (it != byLowerFilename.end()) {
            targetPath = fs::path(it->second);
          } else {
            return "";
          }
        }

        fs::path rel = fs::relative(targetPath, glbDir, ec);
        if (ec || rel.empty()) return targetPath.generic_string();
        return rel.generic_string();
      }
    };

    struct QuantizedVertKey {
      int32_t px, py, pz;
      int32_t nx, ny, nz;
      int32_t u, v;
      bool operator<(const QuantizedVertKey &o) const {
        return std::tie(px, py, pz, nx, ny, nz, u, v) <
               std::tie(o.px, o.py, o.pz, o.nx, o.ny, o.nz, o.u, o.v);
      }
    };

    struct ExportSubMesh {
      Material engineMat;
      std::string customMatName;
      std::vector<Vector3> posEng;   // Engine Z-up positions
      std::vector<Vector3> normEng;  // Engine Z-up unit normals
      std::vector<Vector3> uvGltf;   // glTF top-left UVs: (u_eng, 1.0f + v_eng, 0)
      std::vector<uint32_t> indices;
      std::vector<uint16_t> joints0; // 4 * vCount (skinned meshes only)
      std::vector<float> weights0;   // 4 * vCount (skinned meshes only)
    };

    // Extract welded indexed submesh from a raw ASELoader MaterializedTriangleMesh
    ExportSubMesh WeldASESubMesh(const MaterializedTriangleMesh &src) {
      ExportSubMesh dst;
      dst.engineMat = src.material;

      int elemFloats = src.verticesDataSize / GetTriangleMeshElementCount();
      int rawVertCount = elemFloats / 3;

      std::vector<unsigned int> rawIndices = src.indices;
      if (rawIndices.empty()) {
        rawIndices.resize(rawVertCount);
        for (int i = 0; i < rawVertCount; ++i) rawIndices[i] = static_cast<unsigned int>(i);
      }

      std::map<QuantizedVertKey, uint32_t> vertMap;
      dst.indices.reserve(rawIndices.size());

      for (unsigned int rv : rawIndices) {
        if (static_cast<int>(rv) >= rawVertCount) continue;
        Vector3 p(
          src.vertices[0 * elemFloats + rv * 3 + 0],
          src.vertices[0 * elemFloats + rv * 3 + 1],
          src.vertices[0 * elemFloats + rv * 3 + 2]
        );
        Vector3 n(
          src.vertices[1 * elemFloats + rv * 3 + 0],
          src.vertices[1 * elemFloats + rv * 3 + 1],
          src.vertices[1 * elemFloats + rv * 3 + 2]
        );
        n = n.GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
        Vector3 uvEng(
          src.vertices[2 * elemFloats + rv * 3 + 0],
          src.vertices[2 * elemFloats + rv * 3 + 1],
          0.0f
        );
        // Convert ASELoader's v_eng = -v_ase into glTF 2.0's standard top-left UV v_gltf = 1.0f - v_ase = 1.0f + v_eng
        Vector3 uvG(uvEng.coords[0], 1.0f + uvEng.coords[1], 0.0f);

        QuantizedVertKey key{
          static_cast<int32_t>(std::llround(p.coords[0] * 10000.0)),
          static_cast<int32_t>(std::llround(p.coords[1] * 10000.0)),
          static_cast<int32_t>(std::llround(p.coords[2] * 10000.0)),
          static_cast<int32_t>(std::llround(n.coords[0] * 2000.0)),
          static_cast<int32_t>(std::llround(n.coords[1] * 2000.0)),
          static_cast<int32_t>(std::llround(n.coords[2] * 2000.0)),
          static_cast<int32_t>(std::llround(uvG.coords[0] * 10000.0)),
          static_cast<int32_t>(std::llround(uvG.coords[1] * 10000.0))
        };

        auto it = vertMap.find(key);
        if (it != vertMap.end()) {
          dst.indices.push_back(it->second);
        } else {
          uint32_t newIdx = static_cast<uint32_t>(dst.posEng.size());
          vertMap[key] = newIdx;
          dst.posEng.push_back(p);
          dst.normEng.push_back(n);
          dst.uvGltf.push_back(uvG);
          dst.indices.push_back(newIdx);
        }
      }

      return dst;
    }

    // Upgrade soccer ball mesh (2x spherical midpoint subdivision: 200 -> 3,200 crisp spherical triangles at radius 0.11m)
    void SubdivideSoccerBallMesh(ExportSubMesh &ballMesh, int subdivisions = 2, float radius = 0.11f) {
      for (int pass = 0; pass < subdivisions; ++pass) {
        std::vector<uint32_t> nextIndices;
        nextIndices.reserve(ballMesh.indices.size() * 4);
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> edgeMidCache;

        auto getMidpoint = [&](uint32_t a, uint32_t b) -> uint32_t {
          auto edgeKey = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
          auto it = edgeMidCache.find(edgeKey);
          if (it != edgeMidCache.end()) return it->second;

          Vector3 midDir = (ballMesh.posEng[a] + ballMesh.posEng[b]).GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
          Vector3 midPos = midDir * radius;
          Vector3 midUV = (ballMesh.uvGltf[a] + ballMesh.uvGltf[b]) * 0.5f;

          uint32_t idx = static_cast<uint32_t>(ballMesh.posEng.size());
          ballMesh.posEng.push_back(midPos);
          ballMesh.normEng.push_back(midDir);
          ballMesh.uvGltf.push_back(midUV);
          edgeMidCache[edgeKey] = idx;
          return idx;
        };

        for (size_t t = 0; t + 2 < ballMesh.indices.size(); t += 3) {
          uint32_t i0 = ballMesh.indices[t + 0];
          uint32_t i1 = ballMesh.indices[t + 1];
          uint32_t i2 = ballMesh.indices[t + 2];

          uint32_t m01 = getMidpoint(i0, i1);
          uint32_t m12 = getMidpoint(i1, i2);
          uint32_t m20 = getMidpoint(i2, i0);

          nextIndices.insert(nextIndices.end(), { i0, m01, m20 });
          nextIndices.insert(nextIndices.end(), { i1, m12, m01 });
          nextIndices.insert(nextIndices.end(), { i2, m20, m12 });
          nextIndices.insert(nextIndices.end(), { m01, m12, m20 });
        }

        ballMesh.indices = std::move(nextIndices);
      }

      // Ensure all original vertices also sit on the exact analytical sphere with smooth analytical normals
      for (size_t v = 0; v < ballMesh.posEng.size(); ++v) {
        Vector3 n = ballMesh.posEng[v].GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
        ballMesh.posEng[v] = n * radius;
        ballMesh.normEng[v] = n;
      }
    }

    // Parse legacy fullbody.ase vertex colors directly (independent of GetVertexColors early-return)
    void ParseLegacyFullbodyVertexColors(const std::string &aseFilename, std::map<Vector3, Vector3> &colorCoords) {
      struct Idx3 { int index[3]; };
      std::vector<Vector3> vertices;
      std::vector<Vector3> colors;
      std::vector<Idx3> faces;
      std::vector<Idx3> colorFaces;

      std::ifstream file(aseFilename);
      if (!file.is_open()) return;

      std::string lineStr;
      while (std::getline(file, lineStr)) {
        std::vector<std::string> tokens;
        tokenize(lineStr, tokens, " \t\r\n");
        if (tokens.empty()) continue;

        if (tokens[0] == "*MESH_NORMALS") {
          for (size_t i = 0; i < colorFaces.size() && i < faces.size(); ++i) {
            for (int v = 0; v < 3; ++v) {
              const Vector3 &coord = vertices.at(faces[i].index[v]);
              const Vector3 &color = colors.at(colorFaces[i].index[v]);
              if (colorCoords.find(coord) == colorCoords.end()) {
                colorCoords.insert(std::make_pair(coord, color));
              }
            }
          }
          vertices.clear();
          colors.clear();
          faces.clear();
          colorFaces.clear();
        } else if (tokens[0] == "*MESH_VERTEX" && tokens.size() > 4) {
          vertices.emplace_back(
            static_cast<float>(std::atof(tokens[2].c_str())),
            static_cast<float>(std::atof(tokens[3].c_str())),
            static_cast<float>(std::atof(tokens[4].c_str()))
          );
        } else if (tokens[0] == "*MESH_FACE" && tokens.size() > 7) {
          Idx3 f{ std::atoi(tokens[3].c_str()), std::atoi(tokens[5].c_str()), std::atoi(tokens[7].c_str()) };
          faces.push_back(f);
        } else if (tokens[0] == "*MESH_VERTCOL" && tokens.size() > 4) {
          Vector3 c(
            static_cast<float>(std::atof(tokens[2].c_str())) * 255.0f,
            static_cast<float>(std::atof(tokens[3].c_str())) * 255.0f,
            static_cast<float>(std::atof(tokens[4].c_str())) * 255.0f
          );
          c.coords[0] = std::round(c.coords[0]);
          c.coords[1] = std::round(c.coords[1]);
          c.coords[2] = std::round(c.coords[2]);
          colors.push_back(c);
        } else if (tokens[0] == "*MESH_CFACE" && tokens.size() > 4) {
          Idx3 cf{ std::atoi(tokens[2].c_str()), std::atoi(tokens[3].c_str()), std::atoi(tokens[4].c_str()) };
          colorFaces.push_back(cf);
        }
      }
    }

    bool ExportSubMeshesToGlb(const std::string &outGlbPath,
                              const std::string &meshName,
                              const std::vector<ExportSubMesh> &subMeshes,
                              const TextureCatalog &texCatalog,
                              bool withHumanoidSkeleton) {
      BinaryGlbBuilder glb;
      fs::path glbDir = fs::path(outGlbPath).parent_path();

      std::map<std::string, int> uriToTextureIdx;
      std::vector<std::string> imageUris;

      auto getOrAddTexture = [&](const blunted::intrusive_ptr<Resource<Surface>> &surfRes) -> int {
        if (!surfRes) return -1;
        std::string relUri = texCatalog.ResolveRelativeUri(surfRes->GetIdentString(), glbDir);
        if (relUri.empty()) return -1;
        auto it = uriToTextureIdx.find(relUri);
        if (it != uriToTextureIdx.end()) return it->second;
        int idx = static_cast<int>(imageUris.size());
        imageUris.push_back(relUri);
        uriToTextureIdx[relUri] = idx;
        return idx;
      };

      struct PrimJsonInfo {
        int posAcc = -1;
        int normAcc = -1;
        int uvAcc = -1;
        int jointsAcc = -1;
        int weightsAcc = -1;
        int indicesAcc = -1;
        int matIdx = -1;
      };

      std::vector<PrimJsonInfo> primInfos;
      std::ostringstream materialsJson;
      materialsJson << "[";
      int matCounter = 0;

      for (const ExportSubMesh &sm : subMeshes) {
        if (sm.posEng.empty() || sm.indices.empty()) continue;
        size_t vCount = sm.posEng.size();

        std::vector<float> posFloats(vCount * 3);
        std::vector<float> normFloats(vCount * 3);
        std::vector<float> uvFloats(vCount * 2);

        for (size_t v = 0; v < vCount; ++v) {
          Vector3 pg = EnginePosToGltf(sm.posEng[v]);
          Vector3 ng = EnginePosToGltf(sm.normEng[v]).GetNormalized(Vector3(0.0f, 1.0f, 0.0f));
          posFloats[v * 3 + 0] = pg.coords[0];
          posFloats[v * 3 + 1] = pg.coords[1];
          posFloats[v * 3 + 2] = pg.coords[2];
          normFloats[v * 3 + 0] = ng.coords[0];
          normFloats[v * 3 + 1] = ng.coords[1];
          normFloats[v * 3 + 2] = ng.coords[2];
          uvFloats[v * 2 + 0] = sm.uvGltf[v].coords[0];
          uvFloats[v * 2 + 1] = sm.uvGltf[v].coords[1];
        }

        PrimJsonInfo pi;
        pi.posAcc = glb.AddFloatAccessor(posFloats, 3, "VEC3", 34962, true);
        pi.normAcc = glb.AddFloatAccessor(normFloats, 3, "VEC3", 34962, false);
        pi.uvAcc = glb.AddFloatAccessor(uvFloats, 2, "VEC2", 34962, false);
        if (withHumanoidSkeleton && sm.joints0.size() == vCount * 4 && sm.weights0.size() == vCount * 4) {
          pi.jointsAcc = glb.AddUShortVec4Accessor(sm.joints0, 34962);
          pi.weightsAcc = glb.AddFloatAccessor(sm.weights0, 4, "VEC4", 34962, false);
        }
        pi.indicesAcc = glb.AddUInt32Accessor(sm.indices, 34963);

        // Material
        int baseTex = getOrAddTexture(sm.engineMat.diffuseTexture);
        int normTex = getOrAddTexture(sm.engineMat.normalTexture);
        int specTex = getOrAddTexture(sm.engineMat.specularTexture);
        int emisTex = getOrAddTexture(sm.engineMat.illuminationTexture);

        std::string mName = sm.customMatName;
        if (mName.empty() && sm.engineMat.diffuseTexture) {
          mName = fs::path(sm.engineMat.diffuseTexture->GetIdentString()).stem().string();
        }
        if (mName.empty()) mName = "mat_" + int_to_str(matCounter);

        float smoothness = clamp(std::sqrt(clamp(sm.engineMat.shininess, 0.0f, 1.0f)), 0.02f, 0.98f);
        float roughness = clamp(1.0f - smoothness, 0.04f, 0.98f);

        if (matCounter > 0) materialsJson << ",";
        materialsJson << "{\"name\":\"" << EscapeJson(mName) << "\",\"pbrMetallicRoughness\":{";
        materialsJson << "\"baseColorFactor\":[1,1,1,1],";
        if (baseTex >= 0) {
          materialsJson << "\"baseColorTexture\":{\"index\":" << baseTex << "},";
        }
        materialsJson << "\"metallicFactor\":0,\"roughnessFactor\":" << FmtFloat(roughness) << "}";
        if (normTex >= 0) {
          materialsJson << ",\"normalTexture\":{\"index\":" << normTex << "}";
        }
        if (emisTex >= 0) {
          materialsJson << ",\"emissiveTexture\":{\"index\":" << emisTex << "}";
        }
        materialsJson << ",\"emissiveFactor\":["
                      << FmtFloat(sm.engineMat.self_illumination.coords[0]) << ","
                      << FmtFloat(sm.engineMat.self_illumination.coords[1]) << ","
                      << FmtFloat(sm.engineMat.self_illumination.coords[2]) << "]";
        materialsJson << ",\"extras\":{\"shininess\":" << FmtFloat(sm.engineMat.shininess)
                      << ",\"specularAmount\":" << FmtFloat(sm.engineMat.specular_amount);
        if (specTex >= 0) {
          materialsJson << ",\"specularTexture\":" << specTex;
        }
        materialsJson << "}}";

        pi.matIdx = matCounter++;
        primInfos.push_back(pi);
      }
      materialsJson << "]";

      if (primInfos.empty()) return false;

      // Optional 13-joint glTF Skin + InverseBindMatrices for fullbody.glb
      int ibmAccessor = -1;
      if (withHumanoidSkeleton) {
        std::vector<float> ibmFloats(e_Joint_Count * 16, 0.0f);
        for (int j = 0; j < e_Joint_Count; ++j) {
          Vector3 jPosGltf = EnginePosToGltf(GetEngineStraightBindJointPos(j));
          // Column-major 4x4 translation(-jPosGltf)
          float *m = &ibmFloats[j * 16];
          m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
          m[12] = -jPosGltf.coords[0];
          m[13] = -jPosGltf.coords[1];
          m[14] = -jPosGltf.coords[2];
        }
        ibmAccessor = glb.AddFloatAccessor(ibmFloats, 16, "MAT4", 0, false);
      }

      std::ostringstream json;
      json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"GameplayFootball AssetUpgrader 2.0\"},";
      json << "\"scene\":0,";

      if (withHumanoidSkeleton) {
        // Node 0 = Armature root ("player"), Nodes 1..13 = 13 canonical joints, Node 14 = Skinned Mesh
        json << "\"scenes\":[{\"name\":\"Scene\",\"nodes\":[0,14]}],";
        json << "\"nodes\":[";
        // Node 0: player root
        json << "{\"name\":\"player\",\"children\":[1]},";
        for (int j = 0; j < e_Joint_Count; ++j) {
          int pJoint = GetEngineJointParent(j);
          Vector3 localEng = (pJoint >= 0)
            ? (GetEngineStraightBindJointPos(j) - GetEngineStraightBindJointPos(pJoint))
            : GetEngineStraightBindJointPos(j);
          Vector3 localGltf = EnginePosToGltf(localEng);

          std::vector<int> childNodeIds;
          for (int cj = 0; cj < e_Joint_Count; ++cj) {
            if (GetEngineJointParent(cj) == j) childNodeIds.push_back(1 + cj);
          }

          json << "{\"name\":\"" << GetEngineJointName(j) << "\",\"translation\":["
               << FmtFloat(localGltf.coords[0]) << ","
               << FmtFloat(localGltf.coords[1]) << ","
               << FmtFloat(localGltf.coords[2]) << "]";
          if (!childNodeIds.empty()) {
            json << ",\"children\":[";
            for (size_t c = 0; c < childNodeIds.size(); ++c) {
              if (c > 0) json << ",";
              json << childNodeIds[c];
            }
            json << "]";
          }
          json << "},";
        }
        // Node 14: Skinned Mesh node
        json << "{\"name\":\"" << EscapeJson(meshName) << "\",\"mesh\":0,\"skin\":0}],";
        json << "\"skins\":[{\"name\":\"HumanoidRig\",\"skeleton\":1,\"inverseBindMatrices\":" << ibmAccessor
             << ",\"joints\":[1,2,3,4,5,6,7,8,9,10,11,12,13]}],";
      } else {
        json << "\"scenes\":[{\"name\":\"Scene\",\"nodes\":[0]}],";
        json << "\"nodes\":[{\"name\":\"" << EscapeJson(meshName) << "\",\"mesh\":0}],";
      }

      if (!imageUris.empty()) {
        json << "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],";
        json << "\"images\":[";
        for (size_t i = 0; i < imageUris.size(); ++i) {
          if (i > 0) json << ",";
          json << "{\"uri\":\"" << EscapeJson(imageUris[i]) << "\"}";
        }
        json << "],\"textures\":[";
        for (size_t i = 0; i < imageUris.size(); ++i) {
          if (i > 0) json << ",";
          json << "{\"sampler\":0,\"source\":" << i << "}";
        }
        json << "],";
      }

      json << "\"materials\":" << materialsJson.str() << ",";
      json << "\"meshes\":[{\"name\":\"" << EscapeJson(meshName) << "\",\"primitives\":[";
      for (size_t i = 0; i < primInfos.size(); ++i) {
        if (i > 0) json << ",";
        const auto &pi = primInfos[i];
        json << "{\"attributes\":{\"POSITION\":" << pi.posAcc
             << ",\"NORMAL\":" << pi.normAcc
             << ",\"TEXCOORD_0\":" << pi.uvAcc;
        if (pi.jointsAcc >= 0 && pi.weightsAcc >= 0) {
          json << ",\"JOINTS_0\":" << pi.jointsAcc
               << ",\"WEIGHTS_0\":" << pi.weightsAcc;
        }
        json << "},\"indices\":" << pi.indicesAcc
             << ",\"material\":" << pi.matIdx
             << ",\"mode\":4}";
      }
      json << "]}],";

      glb.EmitBufferAndAccessorJson(json);
      json << "}";

      return glb.WriteGlbFile(outGlbPath, json.str());
    }

    bool UpgradeSkinnedFullbodyModel(const std::string &asePath,
                                     const std::string &outGlbPath,
                                     const TextureCatalog &texCatalog) {
      std::map<Vector3, Vector3> colorCoords;
      ParseLegacyFullbodyVertexColors(asePath, colorCoords);
      if (colorCoords.empty()) return false;

      // Compute exact base.anim.util derived positions and inverse rotations for all 13 joints
      Animation baseAnim;
      baseAnim.Load("media/animations/base.anim.util");

      Vector3 baseDerivedPos[e_Joint_Count];
      Quaternion baseDerivedRot[e_Joint_Count];
      Quaternion baseInvRot[e_Joint_Count];

      for (int j = 0; j < e_Joint_Count; ++j) {
        Quaternion localRot(QUATERNION_IDENTITY);
        Vector3 dummyPos(0);
        baseAnim.GetKeyFrame(GetEngineJointName(j), 0, localRot, dummyPos, true, false);

        int p = GetEngineJointParent(j);
        Vector3 localOffset = (p >= 0)
          ? (GetEngineStraightBindJointPos(j) - GetEngineStraightBindJointPos(p))
          : GetEngineStraightBindJointPos(j);

        if (p >= 0) {
          baseDerivedPos[j] = baseDerivedPos[p] + (baseDerivedRot[p] * localOffset);
          baseDerivedRot[j] = (baseDerivedRot[p] * localRot).GetNormalized();
        } else {
          baseDerivedPos[j] = localOffset;
          baseDerivedRot[j] = localRot.GetNormalized();
        }
        baseInvRot[j] = baseDerivedRot[j].GetInverse().GetNormalized();
      }

      // Load raw fullbody.ase directly via ASELoader
      blunted::intrusive_ptr<Resource<GeometryData>> rawGeomRes(new Resource<GeometryData>("tmp_fullbody_ase"));
      ASELoader aseLoader;
      aseLoader.Load(asePath, rawGeomRes);

      const auto &srcMeshes = rawGeomRes->GetResource()->GetTriangleMeshes();
      std::vector<ExportSubMesh> outSubMeshes;
      outSubMeshes.reserve(srcMeshes.size());

      for (const auto &srcTm : srcMeshes) {
        ExportSubMesh sm = WeldASESubMesh(srcTm);
        size_t vCount = sm.posEng.size();
        sm.joints0.assign(vCount * 4, 0);
        sm.weights0.assign(vCount * 4, 0.0f);

        // Preserve kit/skin semantic material names while keeping exact normal/specular maps
        if (sm.engineMat.diffuseTexture) {
          std::string id = sm.engineMat.diffuseTexture->GetIdentString();
          if (id == "kit_template.png") sm.customMatName = "kit";
          else if (id == "skin.jpg" && sm.engineMat.normalTexture) {
            sm.customMatName = fs::path(sm.engineMat.normalTexture->GetIdentString()).stem().string();
          }
        }

        for (size_t v = 0; v < vCount; ++v) {
          const Vector3 &origPos = sm.posEng[v];
          const Vector3 &origNorm = sm.normEng[v];

          // Match exact vertex position in colorCoords (with nearest-neighbor fallback for float tolerance)
          Vector3 color(0.0f, 0.0f, 0.0f);
          auto exactIt = colorCoords.find(origPos);
          if (exactIt != colorCoords.end()) {
            color = exactIt->second;
          } else {
            float bestDist2 = 1e9f;
            for (const auto &kv : colorCoords) {
              float d2 = (kv.first - origPos).GetSquaredLength();
              if (d2 < bestDist2) {
                bestDist2 = d2;
                color = kv.second;
              }
            }
          }

          int jIds[4] = { 0, 0, 0, 0 };
          float jWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
          float totalW = 0.0f;
          int outCount = 0;

          for (int c = 0; c < 3; ++c) {
            int jointID = clamp(static_cast<int>(std::floor(color.coords[c] * 0.1f)), 0, e_Joint_Count - 1);
            float weight = (color.coords[c] - jointID * 10.0f) / 9.0f;
            if (weight > 0.01f) {
              jIds[outCount] = jointID;
              jWeights[outCount] = weight;
              totalW += weight;
              ++outCount;
            }
          }

          if (outCount == 0 || totalW <= 1e-5f) {
            jIds[0] = 0;
            jWeights[0] = 1.0f;
            outCount = 1;
            totalW = 1.0f;
          }

          Vector3 straightPos(0.0f, 0.0f, 0.0f);
          Vector3 straightNorm(0.0f, 0.0f, 0.0f);

          for (int b = 0; b < outCount; ++b) {
            jWeights[b] /= totalW;
            int j = jIds[b];
            float w = jWeights[b];

            Vector3 pPart = origPos - baseDerivedPos[j];
            pPart.Rotate(baseInvRot[j]);
            pPart += GetEngineStraightBindJointPos(j);

            Vector3 nPart = origNorm;
            nPart.Rotate(baseInvRot[j]);

            straightPos += pPart * w;
            straightNorm += nPart * w;

            sm.joints0[v * 4 + b] = static_cast<uint16_t>(j);
            sm.weights0[v * 4 + b] = w;
          }

          sm.posEng[v] = straightPos;
          sm.normEng[v] = straightNorm.GetNormalized(Vector3(0.0f, 0.0f, 1.0f));
        }

        outSubMeshes.push_back(std::move(sm));
      }

      return ExportSubMeshesToGlb(outGlbPath, "fullbody", outSubMeshes, texCatalog, true);
    }

    bool UpgradeStaticOrDynamicASEModel(const std::string &asePath,
                                        const std::string &outGlbPath,
                                        const TextureCatalog &texCatalog) {
      blunted::intrusive_ptr<Resource<GeometryData>> rawGeomRes(new Resource<GeometryData>("tmp_ase_" + get_file_name(asePath)));
      ASELoader aseLoader;
      aseLoader.Load(asePath, rawGeomRes);

      const auto &srcMeshes = rawGeomRes->GetResource()->GetTriangleMeshes();
      std::vector<ExportSubMesh> outSubMeshes;
      outSubMeshes.reserve(srcMeshes.size());

      std::string lowerPath = asePath;
      std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
      bool isSoccerBall = (lowerPath.find("balls/generic.ase") != std::string::npos);

      for (const auto &srcTm : srcMeshes) {
        ExportSubMesh sm = WeldASESubMesh(srcTm);
        if (isSoccerBall) {
          SubdivideSoccerBallMesh(sm, 2, 0.11f);
        }
        outSubMeshes.push_back(std::move(sm));
      }

      std::string meshName = fs::path(asePath).stem().string();
      return ExportSubMeshesToGlb(outGlbPath, meshName, outSubMeshes, texCatalog, false);
    }

    // Extract raw trailing XML block from a legacy .anim file for 100% lossless XMLTree reconstruction
    std::string ExtractRawXmlFromAnimFile(const std::string &animPath) {
      std::vector<std::string> lines;
      file_to_vector(animPath, lines);
      std::string xml;
      bool inXml = false;
      for (const std::string &line : lines) {
        if (!inXml) {
          std::string trimmed = line;
          size_t firstNonSpace = trimmed.find_first_not_of(" \t\r\n");
          if (firstNonSpace != std::string::npos && trimmed[firstNonSpace] == '<') {
            inXml = true;
          }
        }
        if (inXml) {
          xml.append(line);
        }
      }
      return xml;
    }

    bool UpgradeAnimFileToGlb(const std::string &animPath, const std::string &outGlbPath) {
      Animation anim;
      auto fbExt = std::make_shared<FootballAnimationExtension>(&anim);
      anim.AddExtension("football", fbExt);
      anim.Load(animPath);

      const auto &nodeAnims = anim.GetNodeAnimations();
      if (nodeAnims.empty()) return false;

      BinaryGlbBuilder glb;

      struct ChannelEntry {
        int samplerIdx = -1;
        int targetNode = -1;
        std::string targetPath;
      };
      struct SamplerEntry {
        int inputAcc = -1;
        int outputAcc = -1;
      };

      std::vector<SamplerEntry> samplers;
      std::vector<ChannelEntry> channels;

      auto findNodeAnim = [&](const std::string &trackName) -> const NodeAnimation * {
        for (const NodeAnimation *na : nodeAnims) {
          if (na && na->nodeName == trackName) return na;
        }
        return nullptr;
      };

      // Node 0: "player" translation track
      if (const NodeAnimation *playerTrack = findNodeAnim("player")) {
        if (!playerTrack->animation.empty()) {
          std::vector<float> times;
          std::vector<float> transVals;
          times.reserve(playerTrack->animation.size());
          transVals.reserve(playerTrack->animation.size() * 3);

          for (const auto &kf : playerTrack->animation) {
            times.push_back(static_cast<float>(kf.first) * 0.01f);
            Vector3 pg = EnginePosToGltf(kf.second.position);
            transVals.push_back(pg.coords[0]);
            transVals.push_back(pg.coords[1]);
            transVals.push_back(pg.coords[2]);
          }

          SamplerEntry se;
          se.inputAcc = glb.AddFloatAccessor(times, 1, "SCALAR", 0, true);
          se.outputAcc = glb.AddFloatAccessor(transVals, 3, "VEC3", 0, false);
          int sIdx = static_cast<int>(samplers.size());
          samplers.push_back(se);
          channels.push_back({ sIdx, 0, "translation" });
        }
      }

      // Nodes 1..13: canonical 13 humanoid joints ("body".."right_ankle") rotation tracks
      for (int j = 0; j < e_Joint_Count; ++j) {
        const NodeAnimation *jointTrack = findNodeAnim(GetEngineJointName(j));
        if (!jointTrack || jointTrack->animation.empty()) continue;

        std::vector<float> times;
        std::vector<float> rotVals;
        times.reserve(jointTrack->animation.size());
        rotVals.reserve(jointTrack->animation.size() * 4);

        for (const auto &kf : jointTrack->animation) {
          times.push_back(static_cast<float>(kf.first) * 0.01f);
          auto qg = EngineQuatToGltfXYZW(kf.second.orientation);
          rotVals.push_back(qg[0]);
          rotVals.push_back(qg[1]);
          rotVals.push_back(qg[2]);
          rotVals.push_back(qg[3]);
        }

        SamplerEntry se;
        se.inputAcc = glb.AddFloatAccessor(times, 1, "SCALAR", 0, true);
        se.outputAcc = glb.AddFloatAccessor(rotVals, 4, "VEC4", 0, false);
        int sIdx = static_cast<int>(samplers.size());
        samplers.push_back(se);
        channels.push_back({ sIdx, 1 + j, "rotation" });
      }

      if (channels.empty()) return false;

      std::string clipName = fs::path(animPath).stem().string();
      std::string rawXml = ExtractRawXmlFromAnimFile(animPath);

      std::ostringstream json;
      json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"GameplayFootball AssetUpgrader 2.0\"},";
      json << "\"scene\":0,\"scenes\":[{\"name\":\"Scene\",\"nodes\":[0]}],";
      json << "\"nodes\":[";
      // Node 0: player root
      json << "{\"name\":\"player\",\"children\":[1]}";
      for (int j = 0; j < e_Joint_Count; ++j) {
        int pJoint = GetEngineJointParent(j);
        Vector3 localEng = (pJoint >= 0)
          ? (GetEngineStraightBindJointPos(j) - GetEngineStraightBindJointPos(pJoint))
          : GetEngineStraightBindJointPos(j);
        Vector3 localGltf = EnginePosToGltf(localEng);

        std::vector<int> childNodeIds;
        for (int cj = 0; cj < e_Joint_Count; ++cj) {
          if (GetEngineJointParent(cj) == j) childNodeIds.push_back(1 + cj);
        }

        json << ",{\"name\":\"" << GetEngineJointName(j) << "\",\"translation\":["
             << FmtFloat(localGltf.coords[0]) << ","
             << FmtFloat(localGltf.coords[1]) << ","
             << FmtFloat(localGltf.coords[2]) << "]";
        if (!childNodeIds.empty()) {
          json << ",\"children\":[";
          for (size_t c = 0; c < childNodeIds.size(); ++c) {
            if (c > 0) json << ",";
            json << childNodeIds[c];
          }
          json << "]";
        }
        json << "}";
      }
      json << "],";

      json << "\"animations\":[{\"name\":\"" << EscapeJson(clipName) << "\",\"samplers\":[";
      for (size_t i = 0; i < samplers.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"input\":" << samplers[i].inputAcc
             << ",\"interpolation\":\"LINEAR\",\"output\":" << samplers[i].outputAcc << "}";
      }
      json << "],\"channels\":[";
      for (size_t i = 0; i < channels.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"sampler\":" << channels[i].samplerIdx
             << ",\"target\":{\"node\":" << channels[i].targetNode
             << ",\"path\":\"" << channels[i].targetPath << "\"}}";
      }
      json << "],\"extras\":{";
      json << "\"agy_native_space\":true,";
      json << "\"agy_frame_count\":" << anim.GetFrameCount() << ",";
      json << "\"agy_current_foot\":\"" << ((anim.GetCurrentFoot() == e_Foot_Left) ? "left" : "right") << "\",";
      json << "\"type\":\"" << EscapeJson(anim.GetAnimType()) << "\",";
      json << "\"agy_xml_data\":\"" << EscapeJson(rawXml) << "\",";
      json << "\"agy_football_touches\":[";
      bool firstTouch = true;
      for (const auto &tk : fbExt->GetAnimation()) {
        if (!firstTouch) json << ",";
        firstTouch = false;
        const FootballKeyFrame &fk = tk.second;
        json << "[" << tk.first << ","
             << FmtFloat(fk.position.coords[0]) << ","
             << FmtFloat(fk.position.coords[1]) << ","
             << FmtFloat(fk.position.coords[2]) << ","
             << FmtFloat(fk.orientation.elements[0]) << ","
             << FmtFloat(fk.orientation.elements[1]) << ","
             << FmtFloat(fk.orientation.elements[2]) << ","
             << FmtFloat(fk.orientation.elements[3]) << ","
             << FmtFloat(fk.power) << "]";
      }
      json << "]}}],";

      glb.EmitBufferAndAccessorJson(json);
      json << "}";

      return glb.WriteGlbFile(outGlbPath, json.str());
    }

  } // namespace

  bool RunAssetLibraryUpgrade(const std::string &mediaRoot, AssetUpgradeSummary &outSummary) {
    outSummary = AssetUpgradeSummary{};
    outSummary.reportPath = "asset_upgrade_report.txt";

    std::ofstream report(outSummary.reportPath, std::ios::trunc);
    auto logLine = [&](const std::string &msg) {
      if (report.is_open()) {
        report << msg << "\n";
        report.flush();
      }
      std::printf("%s\n", msg.c_str());
    };

    logLine("=== GameplayFootball Automated Legacy Asset Library Upgrader ===");
    logLine("Media Root: " + mediaRoot);

    TextureCatalog texCatalog;
    texCatalog.BuildAndPrewarm(mediaRoot);
    logLine("Indexed & pre-warmed " + int_to_str(static_cast<int>(texCatalog.byLowerFilename.size())) + " texture files.");

    // 1. Collect all .ase 3D models
    std::vector<std::string> aseFiles;
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(mediaRoot + "/objects", ec)) {
      if (!entry.is_regular_file(ec)) continue;
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".ase") {
        aseFiles.push_back(entry.path().generic_string());
      }
    }
    std::sort(aseFiles.begin(), aseFiles.end());

    GLTFLoader gltfVerifier;

    for (const std::string &asePath : aseFiles) {
      fs::path glbPath = fs::path(asePath);
      glbPath.replace_extension(".glb");
      std::string glbStr = glbPath.generic_string();

      std::string lower = asePath;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      bool isFullbody = (lower.find("players/models/fullbody.ase") != std::string::npos);

      bool ok = isFullbody
        ? UpgradeSkinnedFullbodyModel(asePath, glbStr, texCatalog)
        : UpgradeStaticOrDynamicASEModel(asePath, glbStr, texCatalog);

      if (!ok) {
        logLine("[ERROR] Failed converting model: " + asePath);
        ++outSummary.errors;
        continue;
      }
      ++outSummary.modelsConverted;

      // Verify round-trip load through GLTFLoader
      blunted::intrusive_ptr<Resource<GeometryData>> verifyRes(new Resource<GeometryData>("verify_" + get_file_name(glbStr)));
      gltfVerifier.Load(glbStr, verifyRes);
      const auto &loadedMeshes = verifyRes->GetResource()->GetTriangleMeshes();
      bool verifyOk = !loadedMeshes.empty() && !loadedMeshes[0].indices.empty();
      if (isFullbody) {
        bool hasKitMat = false;
        bool hasSkinMat = false;
        for (const auto &lm : loadedMeshes) {
          if (lm.skinData.empty()) verifyOk = false;
          if (lm.material.diffuseTexture) {
            if (lm.material.diffuseTexture->GetIdentString() == "kit_template.png") hasKitMat = true;
            if (lm.material.diffuseTexture->GetIdentString() == "skin.jpg") hasSkinMat = true;
          }
        }
        if (!hasKitMat || !hasSkinMat) verifyOk = false;
      }
      if (verifyOk) {
        ++outSummary.modelsVerified;
        size_t totalTris = 0;
        for (const auto &tm : loadedMeshes) totalTris += tm.indices.size() / 3;
        logLine("[OK Model] " + glbStr + " (" + int_to_str(static_cast<int>(loadedMeshes.size())) +
                " submeshes, " + int_to_str(static_cast<int>(totalTris)) + " tris" +
                (isFullbody ? ", 13-joint GPU skinned)" : ")"));
      } else {
        logLine("[ERROR] Verification failed for model: " + glbStr);
        ++outSummary.errors;
      }
    }

    // 2. Collect all .anim skeletal animations (including templates/)
    std::vector<std::string> animFiles;
    for (const auto &entry : fs::recursive_directory_iterator(mediaRoot + "/animations", ec)) {
      if (!entry.is_regular_file(ec)) continue;
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".anim") {
        animFiles.push_back(entry.path().generic_string());
      }
    }
    std::sort(animFiles.begin(), animFiles.end());

    for (const std::string &animPath : animFiles) {
      fs::path glbPath = fs::path(animPath);
      glbPath.replace_extension(".glb");
      std::string glbStr = glbPath.generic_string();

      if (!UpgradeAnimFileToGlb(animPath, glbStr)) {
        logLine("[ERROR] Failed converting animation: " + animPath);
        ++outSummary.errors;
        continue;
      }
      ++outSummary.animationsConverted;

      // Round-trip parity verification: compare .anim vs .glb across all engine queries
      Animation origAnim, glbAnim;
      auto origFb = std::make_shared<FootballAnimationExtension>(&origAnim);
      auto glbFb = std::make_shared<FootballAnimationExtension>(&glbAnim);
      origAnim.AddExtension("football", origFb);
      glbAnim.AddExtension("football", glbFb);
      origAnim.Load(animPath);
      glbAnim.Load(glbStr);

      bool parityOk =
        (origAnim.GetFrameCount() == glbAnim.GetFrameCount()) &&
        (origAnim.GetAnimType() == glbAnim.GetAnimType()) &&
        (origAnim.GetCurrentFoot() == glbAnim.GetCurrentFoot()) &&
        (std::fabs(origAnim.GetIncomingVelocity() - glbAnim.GetIncomingVelocity()) < 1e-3f) &&
        (std::fabs(origAnim.GetOutgoingVelocity() - glbAnim.GetOutgoingVelocity()) < 1e-3f) &&
        (std::fabs(origAnim.GetIncomingBodyAngle() - glbAnim.GetIncomingBodyAngle()) < 1e-3f) &&
        (std::fabs(origAnim.GetOutgoingBodyAngle() - glbAnim.GetOutgoingBodyAngle()) < 1e-3f) &&
        (std::fabs(origAnim.GetOutgoingAngle() - glbAnim.GetOutgoingAngle()) < 1e-3f) &&
        (origFb->GetTouchCount() == glbFb->GetTouchCount());

      if (parityOk) {
        ++outSummary.animationsVerified;
      } else {
        logLine("[ERROR] Parity mismatch on animation: " + glbStr +
                " (frames " + int_to_str(origAnim.GetFrameCount()) + " vs " + int_to_str(glbAnim.GetFrameCount()) +
                ", inVel " + real_to_str(origAnim.GetIncomingVelocity()) + " vs " + real_to_str(glbAnim.GetIncomingVelocity()) +
                ", outVel " + real_to_str(origAnim.GetOutgoingVelocity()) + " vs " + real_to_str(glbAnim.GetOutgoingVelocity()) + ")");
        ++outSummary.errors;
      }
    }

    logLine("=== Upgrade Summary ===");
    logLine("Models Converted & Verified:     " + int_to_str(outSummary.modelsVerified) + " / " + int_to_str(static_cast<int>(aseFiles.size())));
    logLine("Animations Converted & Verified: " + int_to_str(outSummary.animationsVerified) + " / " + int_to_str(static_cast<int>(animFiles.size())));
    logLine("Errors:                          " + int_to_str(outSummary.errors));

    return (outSummary.errors == 0);
  }

} // namespace blunted
