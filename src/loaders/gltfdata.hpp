#ifndef _HPP_LOADERS_GLTFDATA
#define _HPP_LOADERS_GLTFDATA

#include "defines.hpp"
#include "base/math/vector3.hpp"
#include "base/math/quaternion.hpp"
#include "base/math/matrix4.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace blunted {

  // Minimal self-contained JSON DOM for glTF 2.0
  struct GLTFJsonValue {
    enum Type {
      Null,
      Bool,
      Number,
      String,
      Array,
      Object
    };

    Type type = Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<GLTFJsonValue> arrVal;
    std::map<std::string, GLTFJsonValue> objVal;

    bool IsNull() const { return type == Null; }
    bool IsBool() const { return type == Bool; }
    bool IsNumber() const { return type == Number; }
    bool IsString() const { return type == String; }
    bool IsArray() const { return type == Array; }
    bool IsObject() const { return type == Object; }

    bool Has(const std::string &key) const {
      return type == Object && objVal.find(key) != objVal.end();
    }

    const GLTFJsonValue &Get(const std::string &key) const {
      static const GLTFJsonValue kNull;
      if (type != Object) return kNull;
      auto it = objVal.find(key);
      return (it != objVal.end()) ? it->second : kNull;
    }

    int GetInt(const std::string &key, int defVal = -1) const {
      const GLTFJsonValue &v = Get(key);
      return v.IsNumber() ? static_cast<int>(v.numVal) : defVal;
    }

    float GetFloat(const std::string &key, float defVal = 0.0f) const {
      const GLTFJsonValue &v = Get(key);
      return v.IsNumber() ? static_cast<float>(v.numVal) : defVal;
    }

    bool GetBool(const std::string &key, bool defVal = false) const {
      const GLTFJsonValue &v = Get(key);
      return v.IsBool() ? v.boolVal : defVal;
    }

    std::string GetString(const std::string &key, const std::string &defVal = "") const {
      const GLTFJsonValue &v = Get(key);
      return v.IsString() ? v.strVal : defVal;
    }
  };

  struct GLTFBufferView {
    int buffer = -1;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0;
  };

  struct GLTFAccessor {
    int bufferView = -1;
    size_t byteOffset = 0;
    int componentType = 0; // 5120..5126
    bool normalized = false;
    size_t count = 0;
    std::string type;      // SCALAR, VEC2, VEC3, VEC4, MAT4
  };

  struct GLTFImage {
    std::string name;
    std::string uri;
    int bufferView = -1;
    std::string mimeType;
    std::vector<uint8_t> decodedUriBytes; // populated when uri is data:...;base64,...
  };

  struct GLTFTexture {
    int source = -1;
    int sampler = -1;
    std::string name;
  };

  struct GLTFMaterial {
    std::string name;
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int baseColorTexture = -1;
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.8f;
    int metallicRoughnessTexture = -1;
    int normalTexture = -1;
    int emissiveTexture = -1;
    int specularTexture = -1;
    float legacyShininess = -1.0f;
    float legacySpecularAmount = -1.0f;
    float emissiveFactor[3] = { 0.0f, 0.0f, 0.0f };
    bool doubleSided = false;
  };

  struct GLTFPrimitive {
    int mode = 4; // 4 == TRIANGLES
    int indices = -1;
    int material = -1;
    int posAccessor = -1;
    int normAccessor = -1;
    int uv0Accessor = -1;
    int tangentAccessor = -1;
    int joints0Accessor = -1;
    int weights0Accessor = -1;
    int joints1Accessor = -1;
    int weights1Accessor = -1;
  };

  struct GLTFMesh {
    std::string name;
    std::vector<GLTFPrimitive> primitives;
  };

  struct GLTFSkin {
    std::string name;
    int inverseBindMatrices = -1;
    int skeleton = -1;
    std::vector<int> joints; // node indices
  };

  struct GLTFNode {
    std::string name;
    int mesh = -1;
    int skin = -1;
    int parent = -1;
    std::vector<int> children;

    // Local TRS in glTF Y-up space
    Vector3 translation = Vector3(0.0f, 0.0f, 0.0f);
    Quaternion rotation = Quaternion(QUATERNION_IDENTITY);
    Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);

    // Rest-pose transforms converted into GameplayFootball Z-up space
    Vector3 engineLocalPos = Vector3(0.0f, 0.0f, 0.0f);
    Quaternion engineLocalRot = Quaternion(QUATERNION_IDENTITY);
    Vector3 engineLocalScale = Vector3(1.0f, 1.0f, 1.0f);

    Vector3 engineGlobalPos = Vector3(0.0f, 0.0f, 0.0f);
    Quaternion engineGlobalRot = Quaternion(QUATERNION_IDENTITY);
    Vector3 engineGlobalScale = Vector3(1.0f, 1.0f, 1.0f);
  };

  struct GLTFAnimSampler {
    int input = -1;
    int output = -1;
    std::string interpolation = "LINEAR"; // LINEAR, STEP, CUBICSPLINE
  };

  struct GLTFAnimChannel {
    int sampler = -1;
    int targetNode = -1;
    std::string targetPath; // "translation", "rotation", "scale", "weights"
  };

  struct GLTFAnimation {
    std::string name;
    std::vector<GLTFAnimSampler> samplers;
    std::vector<GLTFAnimChannel> channels;
    GLTFJsonValue extras;
  };

  struct GLTFDocument {
    std::string sourcePath;
    std::string baseDir;
    GLTFJsonValue rootJson;
    GLTFJsonValue extras;

    std::vector<std::vector<uint8_t>> buffers;
    std::vector<GLTFBufferView> bufferViews;
    std::vector<GLTFAccessor> accessors;
    std::vector<GLTFImage> images;
    std::vector<GLTFTexture> textures;
    std::vector<GLTFMaterial> materials;
    std::vector<GLTFMesh> meshes;
    std::vector<GLTFSkin> skins;
    std::vector<GLTFNode> nodes;
    std::vector<int> defaultSceneNodes;
    std::vector<GLTFAnimation> animations;
  };

  // Canonical 13 joints of GameplayFootball's humanoid rig (matches player.object DFS order)
  enum e_EngineHumanoidJoint {
    e_Joint_Body = 0,
    e_Joint_Middle = 1,
    e_Joint_Neck = 2,
    e_Joint_LeftShoulder = 3,
    e_Joint_LeftElbow = 4,
    e_Joint_RightShoulder = 5,
    e_Joint_RightElbow = 6,
    e_Joint_LeftThigh = 7,
    e_Joint_LeftKnee = 8,
    e_Joint_LeftAnkle = 9,
    e_Joint_RightThigh = 10,
    e_Joint_RightKnee = 11,
    e_Joint_RightAnkle = 12,
    e_Joint_Count = 13
  };

  // Parse .gltf or .glb file from disk into GLTFDocument
  bool LoadGLTFDocument(const std::string &filename, GLTFDocument &outDoc, std::string &outError);

  // Accessor readers
  int GetAccessorComponentCount(const std::string &typeStr);
  bool ReadAccessorFloats(const GLTFDocument &doc, int accessorIdx, std::vector<float> &outFloats);
  bool ReadAccessorUInts(const GLTFDocument &doc, int accessorIdx, std::vector<unsigned int> &outUInts);

  // Coordinate conversions: glTF 2.0 (RH, +X Right, +Y Up, +Z Forward) -> Engine (RH, +X Right, -Y Forward, +Z Up)
  inline Vector3 GltfPosToEngine(float x, float y, float z) {
    return Vector3(x, -z, y);
  }
  inline Vector3 GltfPosToEngine(const Vector3 &v) {
    return Vector3(v.coords[0], -v.coords[2], v.coords[1]);
  }
  inline Vector3 GltfScaleToEngine(float sx, float sy, float sz) {
    return Vector3(sx, sz, sy);
  }
  inline Quaternion GltfQuatToEngine(float x, float y, float z, float w) {
    Quaternion q(x, -z, y, w);
    q.Normalize();
    return q;
  }

  // Shortest-arc rotation between two unit vectors
  Quaternion GLTFShortestArcQuat(const Vector3 &fromUnit, const Vector3 &toUnit);

  // Canonical GameplayFootball joint queries
  const char *GetEngineJointName(int jointIdx);
  int GetEngineJointParent(int jointIdx);
  Vector3 GetEngineStraightBindJointPos(int jointIdx);
  Vector3 GetEngineStraightBindLimbDir(int jointIdx);

  // Maps native GameplayFootball, Mixamo, Unity/Unreal Humanoid, Blender Rigify, CC4 bone names -> [0..12] or -1
  int MapBoneNameToEngineJointIndex(const std::string &rawBoneName, bool hasSeparateUpperArm = true);
  // Resolves a glTF node index to [0..12] by checking the node name and walking parent chain if needed
  int ResolveNodeToEngineJointIndex(const GLTFDocument &doc, int nodeIdx);
  bool IsRootMotionBoneName(const std::string &rawBoneName);

  // Computes rest-pose alignment rotation R_align[j] (for each engine joint 0..12) that maps
  // the glTF skeleton's bind limb direction (e.g., Mixamo T-Pose) to GameplayFootball's straight.anim.util pose
  void ComputeBindPoseLimbAlignment(const GLTFDocument &doc,
                                    Quaternion outAlignRot[e_Joint_Count],
                                    int outPrimaryNodeForJoint[e_Joint_Count],
                                    float &outUnitScale);

}

#endif
