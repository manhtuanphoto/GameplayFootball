#include "gltfdata.hpp"

#include "base/log.hpp"
#include "base/utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace blunted {

  namespace {

    class JsonParser {
    public:
      explicit JsonParser(const std::string &src) : s(src), pos(0), len(src.size()) {}

      bool Parse(GLTFJsonValue &out, std::string &err) {
        SkipWS();
        if (!ParseValue(out, err)) return false;
        SkipWS();
        return true;
      }

    private:
      const std::string &s;
      size_t pos;
      size_t len;

      void SkipWS() {
        while (pos < len) {
          unsigned char c = static_cast<unsigned char>(s[pos]);
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++pos;
          } else {
            break;
          }
        }
      }

      bool ParseValue(GLTFJsonValue &out, std::string &err) {
        SkipWS();
        if (pos >= len) {
          err = "Unexpected end of JSON";
          return false;
        }
        char c = s[pos];
        if (c == '{') return ParseObject(out, err);
        if (c == '[') return ParseArray(out, err);
        if (c == '"') {
          out.type = GLTFJsonValue::String;
          return ParseString(out.strVal, err);
        }
        if (c == 't' && pos + 4 <= len && s.compare(pos, 4, "true") == 0) {
          pos += 4;
          out.type = GLTFJsonValue::Bool;
          out.boolVal = true;
          return true;
        }
        if (c == 'f' && pos + 5 <= len && s.compare(pos, 5, "false") == 0) {
          pos += 5;
          out.type = GLTFJsonValue::Bool;
          out.boolVal = false;
          return true;
        }
        if (c == 'n' && pos + 4 <= len && s.compare(pos, 4, "null") == 0) {
          pos += 4;
          out.type = GLTFJsonValue::Null;
          return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
          return ParseNumber(out, err);
        }
        err = std::string("Unexpected character in JSON: '") + c + "'";
        return false;
      }

      bool ParseString(std::string &outStr, std::string &err) {
        if (pos >= len || s[pos] != '"') {
          err = "Expected '\"'";
          return false;
        }
        ++pos;
        outStr.clear();
        while (pos < len) {
          char c = s[pos++];
          if (c == '"') return true;
          if (c == '\\') {
            if (pos >= len) {
              err = "Unterminated escape in string";
              return false;
            }
            char esc = s[pos++];
            switch (esc) {
              case '"': outStr.push_back('"'); break;
              case '\\': outStr.push_back('\\'); break;
              case '/': outStr.push_back('/'); break;
              case 'b': outStr.push_back('\b'); break;
              case 'f': outStr.push_back('\f'); break;
              case 'n': outStr.push_back('\n'); break;
              case 'r': outStr.push_back('\r'); break;
              case 't': outStr.push_back('\t'); break;
              case 'u': {
                if (pos + 4 > len) {
                  err = "Invalid \\uXXXX escape";
                  return false;
                }
                unsigned int codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                  char hc = s[pos++];
                  codepoint <<= 4;
                  if (hc >= '0' && hc <= '9') codepoint |= (hc - '0');
                  else if (hc >= 'a' && hc <= 'f') codepoint |= (hc - 'a' + 10);
                  else if (hc >= 'A' && hc <= 'F') codepoint |= (hc - 'A' + 10);
                }
                if (codepoint <= 0x7F) {
                  outStr.push_back(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7FF) {
                  outStr.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                  outStr.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                } else {
                  outStr.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                  outStr.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                  outStr.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                }
                break;
              }
              default:
                outStr.push_back(esc);
                break;
            }
          } else {
            outStr.push_back(c);
          }
        }
        err = "Unterminated JSON string";
        return false;
      }

      bool ParseNumber(GLTFJsonValue &out, std::string &err) {
        const char *startPtr = s.c_str() + pos;
        char *endPtr = nullptr;
        double val = std::strtod(startPtr, &endPtr);
        if (endPtr == startPtr) {
          err = "Invalid JSON number";
          return false;
        }
        pos += static_cast<size_t>(endPtr - startPtr);
        out.type = GLTFJsonValue::Number;
        out.numVal = val;
        return true;
      }

      bool ParseArray(GLTFJsonValue &out, std::string &err) {
        ++pos; // skip '['
        out.type = GLTFJsonValue::Array;
        out.arrVal.clear();
        SkipWS();
        if (pos < len && s[pos] == ']') {
          ++pos;
          return true;
        }
        while (pos < len) {
          GLTFJsonValue elem;
          if (!ParseValue(elem, err)) return false;
          out.arrVal.push_back(std::move(elem));
          SkipWS();
          if (pos < len && s[pos] == ',') {
            ++pos;
            SkipWS();
          } else if (pos < len && s[pos] == ']') {
            ++pos;
            return true;
          } else {
            err = "Expected ',' or ']' in JSON array";
            return false;
          }
        }
        err = "Unterminated JSON array";
        return false;
      }

      bool ParseObject(GLTFJsonValue &out, std::string &err) {
        ++pos; // skip '{'
        out.type = GLTFJsonValue::Object;
        out.objVal.clear();
        SkipWS();
        if (pos < len && s[pos] == '}') {
          ++pos;
          return true;
        }
        while (pos < len) {
          SkipWS();
          std::string key;
          if (!ParseString(key, err)) return false;
          SkipWS();
          if (pos >= len || s[pos] != ':') {
            err = "Expected ':' after object key";
            return false;
          }
          ++pos;
          GLTFJsonValue val;
          if (!ParseValue(val, err)) return false;
          out.objVal[std::move(key)] = std::move(val);
          SkipWS();
          if (pos < len && s[pos] == ',') {
            ++pos;
            SkipWS();
          } else if (pos < len && s[pos] == '}') {
            ++pos;
            return true;
          } else {
            err = "Expected ',' or '}' in JSON object";
            return false;
          }
        }
        err = "Unterminated JSON object";
        return false;
      }
    };

    bool ReadBinaryFile(const std::string &path, std::vector<uint8_t> &outBytes) {
      std::ifstream f(path, std::ios::binary | std::ios::ate);
      if (!f.is_open()) return false;
      std::streamsize sz = f.tellg();
      if (sz < 0) return false;
      f.seekg(0, std::ios::beg);
      outBytes.resize(static_cast<size_t>(sz));
      if (sz > 0 && !f.read(reinterpret_cast<char *>(outBytes.data()), sz)) {
        return false;
      }
      return true;
    }

    bool DecodeBase64(const std::string &b64, std::vector<uint8_t> &out) {
      static const int8_t kTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
      };
      out.clear();
      out.reserve((b64.size() * 3) / 4);
      uint32_t val = 0;
      int valb = -8;
      for (unsigned char c : b64) {
        int8_t d = kTable[c];
        if (d == -1) continue;
        if (d == -2) break;
        val = (val << 6) + static_cast<uint32_t>(d);
        valb += 6;
        if (valb >= 0) {
          out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
          valb -= 8;
        }
      }
      return true;
    }

    bool TryDecodeDataURI(const std::string &uri, std::vector<uint8_t> &outBytes) {
      if (uri.compare(0, 5, "data:") != 0) return false;
      size_t comma = uri.find(',');
      if (comma == std::string::npos) return false;
      std::string header = uri.substr(0, comma);
      if (header.find(";base64") == std::string::npos) return false;
      return DecodeBase64(uri.substr(comma + 1), outBytes);
    }

    std::string UrlDecodePercent(const std::string &in) {
      std::string out;
      out.reserve(in.size());
      for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
          char hex[3] = { in[i + 1], in[i + 2], 0 };
          char *endp = nullptr;
          long v = std::strtol(hex, &endp, 16);
          if (endp == hex + 2) {
            out.push_back(static_cast<char>(v));
            i += 2;
            continue;
          }
        }
        out.push_back(in[i]);
      }
      return out;
    }

    Quaternion Matrix3ToQuat(float m00, float m01, float m02,
                             float m10, float m11, float m12,
                             float m20, float m21, float m22) {
      float trace = m00 + m11 + m22;
      float x, y, z, w;
      if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (m21 - m12) / s;
        y = (m02 - m20) / s;
        z = (m10 - m01) / s;
      } else if ((m00 > m11) && (m00 > m22)) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        w = (m21 - m12) / s;
        x = 0.25f * s;
        y = (m01 + m10) / s;
        z = (m02 + m20) / s;
      } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        w = (m02 - m20) / s;
        x = (m01 + m10) / s;
        y = 0.25f * s;
        z = (m12 + m21) / s;
      } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        w = (m10 - m01) / s;
        x = (m02 + m20) / s;
        y = (m12 + m21) / s;
        z = 0.25f * s;
      }
      Quaternion q(x, y, z, w);
      q.Normalize();
      return q;
    }

    // Decompose column-major glTF 4x4 matrix m[0..15]
    void DecomposeGLTFMatrix(const float m[16], Vector3 &outT, Quaternion &outR, Vector3 &outS) {
      outT = Vector3(m[12], m[13], m[14]);
      Vector3 col0(m[0], m[1], m[2]);
      Vector3 col1(m[4], m[5], m[6]);
      Vector3 col2(m[8], m[9], m[10]);

      float sx = col0.GetLength();
      float sy = col1.GetLength();
      float sz = col2.GetLength();
      if (sx < 1e-6f) sx = 1.0f;
      if (sy < 1e-6f) sy = 1.0f;
      if (sz < 1e-6f) sz = 1.0f;

      // Check handedness determinant
      float det = col0.GetCrossProduct(col1).GetDotProduct(col2);
      if (det < 0.0f) sx = -sx;

      outS = Vector3(sx, sy, sz);
      col0 /= sx;
      col1 /= sy;
      col2 /= sz;

      outR = Matrix3ToQuat(
        col0.coords[0], col1.coords[0], col2.coords[0],
        col0.coords[1], col1.coords[1], col2.coords[1],
        col0.coords[2], col1.coords[2], col2.coords[2]
      );
    }

    void ComputeNodeGlobalRecursive(GLTFDocument &doc, int nodeIdx, std::vector<uint8_t> &visited) {
      if (nodeIdx < 0 || nodeIdx >= static_cast<int>(doc.nodes.size())) return;
      if (visited[nodeIdx] == 2) return;
      if (visited[nodeIdx] == 1) return; // cycle guard
      visited[nodeIdx] = 1;

      GLTFNode &node = doc.nodes[nodeIdx];
      if (node.parent >= 0 && node.parent < static_cast<int>(doc.nodes.size())) {
        ComputeNodeGlobalRecursive(doc, node.parent, visited);
        const GLTFNode &p = doc.nodes[node.parent];
        Vector3 scaledLocal(
          node.engineLocalPos.coords[0] * p.engineGlobalScale.coords[0],
          node.engineLocalPos.coords[1] * p.engineGlobalScale.coords[1],
          node.engineLocalPos.coords[2] * p.engineGlobalScale.coords[2]
        );
        node.engineGlobalPos = p.engineGlobalPos + (p.engineGlobalRot * scaledLocal);
        node.engineGlobalRot = (p.engineGlobalRot * node.engineLocalRot).GetNormalized();
        node.engineGlobalScale = Vector3(
          p.engineGlobalScale.coords[0] * node.engineLocalScale.coords[0],
          p.engineGlobalScale.coords[1] * node.engineLocalScale.coords[1],
          p.engineGlobalScale.coords[2] * node.engineLocalScale.coords[2]
        );
      } else {
        node.engineGlobalPos = node.engineLocalPos;
        node.engineGlobalRot = node.engineLocalRot;
        node.engineGlobalScale = node.engineLocalScale;
      }

      visited[nodeIdx] = 2;
      for (int childIdx : node.children) {
        ComputeNodeGlobalRecursive(doc, childIdx, visited);
      }
    }

    size_t GetComponentByteSize(int componentType) {
      switch (componentType) {
        case 5120: // BYTE
        case 5121: // UNSIGNED_BYTE
          return 1;
        case 5122: // SHORT
        case 5123: // UNSIGNED_SHORT
          return 2;
        case 5125: // UNSIGNED_INT
        case 5126: // FLOAT
          return 4;
        default:
          return 0;
      }
    }

    float ReadComponentAsFloat(const uint8_t *ptr, int componentType, bool normalized) {
      switch (componentType) {
        case 5120: { // int8
          int8_t v = *reinterpret_cast<const int8_t *>(ptr);
          return normalized ? std::max(static_cast<float>(v) / 127.0f, -1.0f) : static_cast<float>(v);
        }
        case 5121: { // uint8
          uint8_t v = *ptr;
          return normalized ? (static_cast<float>(v) / 255.0f) : static_cast<float>(v);
        }
        case 5122: { // int16
          int16_t v = 0;
          std::memcpy(&v, ptr, sizeof(int16_t));
          return normalized ? std::max(static_cast<float>(v) / 32767.0f, -1.0f) : static_cast<float>(v);
        }
        case 5123: { // uint16
          uint16_t v = 0;
          std::memcpy(&v, ptr, sizeof(uint16_t));
          return normalized ? (static_cast<float>(v) / 65535.0f) : static_cast<float>(v);
        }
        case 5125: { // uint32
          uint32_t v = 0;
          std::memcpy(&v, ptr, sizeof(uint32_t));
          return static_cast<float>(v);
        }
        case 5126: { // float
          float v = 0.0f;
          std::memcpy(&v, ptr, sizeof(float));
          return v;
        }
        default:
          return 0.0f;
      }
    }

    uint32_t ReadComponentAsUInt(const uint8_t *ptr, int componentType) {
      switch (componentType) {
        case 5121:
          return static_cast<uint32_t>(*ptr);
        case 5123: {
          uint16_t v = 0;
          std::memcpy(&v, ptr, sizeof(uint16_t));
          return static_cast<uint32_t>(v);
        }
        case 5125: {
          uint32_t v = 0;
          std::memcpy(&v, ptr, sizeof(uint32_t));
          return v;
        }
        case 5120:
          return static_cast<uint32_t>(std::max<int>(0, *reinterpret_cast<const int8_t *>(ptr)));
        case 5122: {
          int16_t v = 0;
          std::memcpy(&v, ptr, sizeof(int16_t));
          return static_cast<uint32_t>(std::max<int>(0, v));
        }
        default:
          return 0;
      }
    }

    std::string NormalizeBoneIdentifier(const std::string &raw) {
      std::string s = raw;
      // Strip namespace / armature prefixes (e.g. "mixamorig:", "mixamorig1:", "Armature|")
      size_t colon = s.find_last_of(':');
      if (colon != std::string::npos) s = s.substr(colon + 1);
      size_t pipe = s.find_last_of('|');
      if (pipe != std::string::npos) s = s.substr(pipe + 1);

      std::string lower;
      lower.reserve(s.size());
      for (unsigned char c : s) {
        lower.push_back(static_cast<char>(std::tolower(c)));
      }

      // Strip common rig prefixes
      const char *prefixes[] = {
        "mixamorig_", "mixamorig", "cc_base_", "bip001_", "bip01_", "def-", "def_", "org-", "org_", "j_bip_c_", "j_bip_"
      };
      for (const char *pfx : prefixes) {
        size_t plen = std::strlen(pfx);
        if (lower.compare(0, plen, pfx) == 0 && lower.size() > plen) {
          lower = lower.substr(plen);
          break;
        }
      }

      // Trim leading spaces/underscores
      while (!lower.empty() && (lower.front() == '_' || lower.front() == ' ' || lower.front() == '-')) {
        lower.erase(lower.begin());
      }
      return lower;
    }

    // Detect side (-1 = Left, +1 = Right, 0 = Center/None) and return stripped core token
    int ExtractBoneSideAndCore(const std::string &norm, std::string &outCore) {
      int side = 0;
      std::string work = norm;

      // Prefix sides: left_, right_, l_, r_, left, right
      if (work.compare(0, 5, "left_") == 0) { side = -1; work = work.substr(5); }
      else if (work.compare(0, 6, "right_") == 0) { side = 1; work = work.substr(6); }
      else if (work.compare(0, 2, "l_") == 0) { side = -1; work = work.substr(2); }
      else if (work.compare(0, 2, "r_") == 0) { side = 1; work = work.substr(2); }
      else if (work.compare(0, 4, "left") == 0 && work.size() > 4) { side = -1; work = work.substr(4); }
      else if (work.compare(0, 5, "right") == 0 && work.size() > 5) { side = 1; work = work.substr(5); }

      // Suffix sides: .l, .r, _l, _r, -l, -r (including .l.001)
      size_t dotNum = work.rfind(".00");
      if (dotNum != std::string::npos) work = work.substr(0, dotNum);

      if (side == 0 && work.size() > 2) {
        std::string tail2 = work.substr(work.size() - 2);
        if (tail2 == ".l" || tail2 == "_l" || tail2 == "-l") {
          side = -1;
          work = work.substr(0, work.size() - 2);
        } else if (tail2 == ".r" || tail2 == "_r" || tail2 == "-r") {
          side = 1;
          work = work.substr(0, work.size() - 2);
        }
      }

      outCore.clear();
      for (char c : work) {
        if (c != '_' && c != '.' && c != '-' && c != ' ') {
          outCore.push_back(c);
        }
      }
      return side;
    }

  } // namespace

  int GetAccessorComponentCount(const std::string &typeStr) {
    if (typeStr == "SCALAR") return 1;
    if (typeStr == "VEC2") return 2;
    if (typeStr == "VEC3") return 3;
    if (typeStr == "VEC4") return 4;
    if (typeStr == "MAT2") return 4;
    if (typeStr == "MAT3") return 9;
    if (typeStr == "MAT4") return 16;
    return 0;
  }

  bool LoadGLTFDocument(const std::string &filename, GLTFDocument &outDoc, std::string &outError) {
    outDoc = GLTFDocument();
    outDoc.sourcePath = filename;
    fs::path p(filename);
    outDoc.baseDir = p.has_parent_path() ? (p.parent_path().generic_string() + "/") : "";

    std::vector<uint8_t> rawFile;
    if (!ReadBinaryFile(filename, rawFile) || rawFile.size() < 4) {
      outError = "Could not open glTF/GLB file: " + filename;
      return false;
    }

    std::string jsonText;
    std::vector<uint8_t> glbBinChunk;
    bool hasGlbBinChunk = false;

    uint32_t magic = 0;
    std::memcpy(&magic, rawFile.data(), sizeof(uint32_t));
    if (magic == 0x46546C67u) { // "glTF" binary .glb
      if (rawFile.size() < 12) {
        outError = "Truncated GLB header in " + filename;
        return false;
      }
      uint32_t version = 0;
      uint32_t totalLen = 0;
      std::memcpy(&version, rawFile.data() + 4, 4);
      std::memcpy(&totalLen, rawFile.data() + 8, 4);
      if (version != 2) {
        outError = "Unsupported GLB version (expected glTF 2.0) in " + filename;
        return false;
      }

      size_t offset = 12;
      size_t maxLen = std::min<size_t>(rawFile.size(), totalLen);
      while (offset + 8 <= maxLen) {
        uint32_t chunkLen = 0;
        uint32_t chunkType = 0;
        std::memcpy(&chunkLen, rawFile.data() + offset, 4);
        std::memcpy(&chunkType, rawFile.data() + offset + 4, 4);
        offset += 8;
        if (offset + chunkLen > rawFile.size()) {
          outError = "Truncated GLB chunk in " + filename;
          return false;
        }
        if (chunkType == 0x4E4F534Au) { // JSON
          jsonText.assign(reinterpret_cast<const char *>(rawFile.data() + offset), chunkLen);
        } else if (chunkType == 0x004E4942u && !hasGlbBinChunk) { // BIN
          glbBinChunk.assign(rawFile.data() + offset, rawFile.data() + offset + chunkLen);
          hasGlbBinChunk = true;
        }
        offset += chunkLen;
      }
      if (jsonText.empty()) {
        outError = "Missing JSON chunk in GLB: " + filename;
        return false;
      }
    } else {
      jsonText.assign(reinterpret_cast<const char *>(rawFile.data()), rawFile.size());
    }

    JsonParser parser(jsonText);
    if (!parser.Parse(outDoc.rootJson, outError) || !outDoc.rootJson.IsObject()) {
      outError = "JSON parse error in " + filename + ": " + outError;
      return false;
    }

    const GLTFJsonValue &root = outDoc.rootJson;
    if (root.Has("extras")) outDoc.extras = root.Get("extras");

    // 1. Buffers
    const GLTFJsonValue &buffersJson = root.Get("buffers");
    if (buffersJson.IsArray()) {
      outDoc.buffers.resize(buffersJson.arrVal.size());
      for (size_t i = 0; i < buffersJson.arrVal.size(); ++i) {
        const GLTFJsonValue &b = buffersJson.arrVal[i];
        std::string uri = b.GetString("uri");
        if (uri.empty()) {
          if (i == 0 && hasGlbBinChunk) {
            outDoc.buffers[i] = std::move(glbBinChunk);
          }
        } else if (!TryDecodeDataURI(uri, outDoc.buffers[i])) {
          std::string relPath = outDoc.baseDir + UrlDecodePercent(uri);
          if (!ReadBinaryFile(relPath, outDoc.buffers[i])) {
            outError = "Failed to load external glTF buffer: " + relPath;
            return false;
          }
        }
      }
    }

    // 2. BufferViews
    const GLTFJsonValue &bvJson = root.Get("bufferViews");
    if (bvJson.IsArray()) {
      outDoc.bufferViews.reserve(bvJson.arrVal.size());
      for (const auto &v : bvJson.arrVal) {
        GLTFBufferView bv;
        bv.buffer = v.GetInt("buffer", -1);
        bv.byteOffset = static_cast<size_t>(std::max(0, v.GetInt("byteOffset", 0)));
        bv.byteLength = static_cast<size_t>(std::max(0, v.GetInt("byteLength", 0)));
        bv.byteStride = static_cast<size_t>(std::max(0, v.GetInt("byteStride", 0)));
        outDoc.bufferViews.push_back(bv);
      }
    }

    // 3. Accessors
    const GLTFJsonValue &accJson = root.Get("accessors");
    if (accJson.IsArray()) {
      outDoc.accessors.reserve(accJson.arrVal.size());
      for (const auto &a : accJson.arrVal) {
        GLTFAccessor acc;
        acc.bufferView = a.GetInt("bufferView", -1);
        acc.byteOffset = static_cast<size_t>(std::max(0, a.GetInt("byteOffset", 0)));
        acc.componentType = a.GetInt("componentType", 0);
        acc.normalized = a.GetBool("normalized", false);
        acc.count = static_cast<size_t>(std::max(0, a.GetInt("count", 0)));
        acc.type = a.GetString("type", "SCALAR");
        outDoc.accessors.push_back(acc);
      }
    }

    // 4. Images
    const GLTFJsonValue &imgJson = root.Get("images");
    if (imgJson.IsArray()) {
      outDoc.images.reserve(imgJson.arrVal.size());
      for (const auto &im : imgJson.arrVal) {
        GLTFImage img;
        img.name = im.GetString("name");
        img.uri = im.GetString("uri");
        img.bufferView = im.GetInt("bufferView", -1);
        img.mimeType = im.GetString("mimeType");
        if (!img.uri.empty()) {
          TryDecodeDataURI(img.uri, img.decodedUriBytes);
          if (img.decodedUriBytes.empty()) {
            img.uri = UrlDecodePercent(img.uri);
          }
        }
        outDoc.images.push_back(std::move(img));
      }
    }

    // 5. Textures
    const GLTFJsonValue &texJson = root.Get("textures");
    if (texJson.IsArray()) {
      outDoc.textures.reserve(texJson.arrVal.size());
      for (const auto &t : texJson.arrVal) {
        GLTFTexture tex;
        tex.source = t.GetInt("source", -1);
        tex.sampler = t.GetInt("sampler", -1);
        tex.name = t.GetString("name");
        outDoc.textures.push_back(tex);
      }
    }

    // 6. Materials
    const GLTFJsonValue &matJson = root.Get("materials");
    if (matJson.IsArray()) {
      outDoc.materials.reserve(matJson.arrVal.size());
      for (const auto &m : matJson.arrVal) {
        GLTFMaterial mat;
        mat.name = m.GetString("name");
        mat.doubleSided = m.GetBool("doubleSided", false);
        const GLTFJsonValue &pbr = m.Get("pbrMetallicRoughness");
        if (pbr.IsObject()) {
          const GLTFJsonValue &bcf = pbr.Get("baseColorFactor");
          if (bcf.IsArray() && bcf.arrVal.size() >= 4) {
            for (int k = 0; k < 4; ++k) mat.baseColorFactor[k] = static_cast<float>(bcf.arrVal[k].numVal);
          }
          const GLTFJsonValue &bct = pbr.Get("baseColorTexture");
          if (bct.IsObject()) mat.baseColorTexture = bct.GetInt("index", -1);
          mat.metallicFactor = pbr.GetFloat("metallicFactor", 0.0f);
          mat.roughnessFactor = pbr.GetFloat("roughnessFactor", 0.8f);
          const GLTFJsonValue &mrt = pbr.Get("metallicRoughnessTexture");
          if (mrt.IsObject()) mat.metallicRoughnessTexture = mrt.GetInt("index", -1);
        }
        const GLTFJsonValue &nt = m.Get("normalTexture");
        if (nt.IsObject()) mat.normalTexture = nt.GetInt("index", -1);
        const GLTFJsonValue &et = m.Get("emissiveTexture");
        if (et.IsObject()) mat.emissiveTexture = et.GetInt("index", -1);
        const GLTFJsonValue &ef = m.Get("emissiveFactor");
        if (ef.IsArray() && ef.arrVal.size() >= 3) {
          for (int k = 0; k < 3; ++k) mat.emissiveFactor[k] = static_cast<float>(ef.arrVal[k].numVal);
        }
        const GLTFJsonValue &mext = m.Get("extras");
        if (mext.IsObject()) {
          mat.specularTexture = mext.GetInt("specularTexture", -1);
          mat.legacyShininess = mext.GetFloat("shininess", -1.0f);
          mat.legacySpecularAmount = mext.GetFloat("specularAmount", -1.0f);
        }
        outDoc.materials.push_back(mat);
      }
    }

    // 7. Meshes
    const GLTFJsonValue &meshJson = root.Get("meshes");
    if (meshJson.IsArray()) {
      outDoc.meshes.reserve(meshJson.arrVal.size());
      for (const auto &mj : meshJson.arrVal) {
        GLTFMesh mesh;
        mesh.name = mj.GetString("name");
        const GLTFJsonValue &prims = mj.Get("primitives");
        if (prims.IsArray()) {
          for (const auto &pj : prims.arrVal) {
            GLTFPrimitive prim;
            prim.mode = pj.GetInt("mode", 4);
            prim.indices = pj.GetInt("indices", -1);
            prim.material = pj.GetInt("material", -1);
            const GLTFJsonValue &attrs = pj.Get("attributes");
            if (attrs.IsObject()) {
              prim.posAccessor = attrs.GetInt("POSITION", -1);
              prim.normAccessor = attrs.GetInt("NORMAL", -1);
              prim.uv0Accessor = attrs.GetInt("TEXCOORD_0", -1);
              prim.tangentAccessor = attrs.GetInt("TANGENT", -1);
              prim.joints0Accessor = attrs.GetInt("JOINTS_0", -1);
              prim.weights0Accessor = attrs.GetInt("WEIGHTS_0", -1);
              prim.joints1Accessor = attrs.GetInt("JOINTS_1", -1);
              prim.weights1Accessor = attrs.GetInt("WEIGHTS_1", -1);
            }
            mesh.primitives.push_back(prim);
          }
        }
        outDoc.meshes.push_back(std::move(mesh));
      }
    }

    // 8. Skins
    const GLTFJsonValue &skinsJson = root.Get("skins");
    if (skinsJson.IsArray()) {
      outDoc.skins.reserve(skinsJson.arrVal.size());
      for (const auto &sj : skinsJson.arrVal) {
        GLTFSkin skin;
        skin.name = sj.GetString("name");
        skin.inverseBindMatrices = sj.GetInt("inverseBindMatrices", -1);
        skin.skeleton = sj.GetInt("skeleton", -1);
        const GLTFJsonValue &jj = sj.Get("joints");
        if (jj.IsArray()) {
          for (const auto &jv : jj.arrVal) {
            if (jv.IsNumber()) skin.joints.push_back(static_cast<int>(jv.numVal));
          }
        }
        outDoc.skins.push_back(std::move(skin));
      }
    }

    // 9. Nodes
    const GLTFJsonValue &nodesJson = root.Get("nodes");
    if (nodesJson.IsArray()) {
      outDoc.nodes.resize(nodesJson.arrVal.size());
      for (size_t i = 0; i < nodesJson.arrVal.size(); ++i) {
        const GLTFJsonValue &nj = nodesJson.arrVal[i];
        GLTFNode &node = outDoc.nodes[i];
        node.name = nj.GetString("name");
        node.mesh = nj.GetInt("mesh", -1);
        node.skin = nj.GetInt("skin", -1);

        const GLTFJsonValue &matArr = nj.Get("matrix");
        if (matArr.IsArray() && matArr.arrVal.size() == 16) {
          float m[16];
          for (int k = 0; k < 16; ++k) m[k] = static_cast<float>(matArr.arrVal[k].numVal);
          DecomposeGLTFMatrix(m, node.translation, node.rotation, node.scale);
        } else {
          const GLTFJsonValue &tr = nj.Get("translation");
          if (tr.IsArray() && tr.arrVal.size() >= 3) {
            node.translation = Vector3(
              static_cast<float>(tr.arrVal[0].numVal),
              static_cast<float>(tr.arrVal[1].numVal),
              static_cast<float>(tr.arrVal[2].numVal)
            );
          }
          const GLTFJsonValue &rot = nj.Get("rotation");
          if (rot.IsArray() && rot.arrVal.size() >= 4) {
            node.rotation = Quaternion(
              static_cast<float>(rot.arrVal[0].numVal),
              static_cast<float>(rot.arrVal[1].numVal),
              static_cast<float>(rot.arrVal[2].numVal),
              static_cast<float>(rot.arrVal[3].numVal)
            );
            node.rotation.Normalize();
          }
          const GLTFJsonValue &sc = nj.Get("scale");
          if (sc.IsArray() && sc.arrVal.size() >= 3) {
            node.scale = Vector3(
              static_cast<float>(sc.arrVal[0].numVal),
              static_cast<float>(sc.arrVal[1].numVal),
              static_cast<float>(sc.arrVal[2].numVal)
            );
          }
        }

        node.engineLocalPos = GltfPosToEngine(node.translation);
        node.engineLocalRot = GltfQuatToEngine(
          node.rotation.elements[0], node.rotation.elements[1],
          node.rotation.elements[2], node.rotation.elements[3]
        );
        node.engineLocalScale = GltfScaleToEngine(
          node.scale.coords[0], node.scale.coords[1], node.scale.coords[2]
        );

        const GLTFJsonValue &ch = nj.Get("children");
        if (ch.IsArray()) {
          for (const auto &cv : ch.arrVal) {
            if (cv.IsNumber()) {
              int cIdx = static_cast<int>(cv.numVal);
              if (cIdx >= 0 && cIdx < static_cast<int>(nodesJson.arrVal.size())) {
                node.children.push_back(cIdx);
              }
            }
          }
        }
      }

      // Wire parent indices
      for (size_t i = 0; i < outDoc.nodes.size(); ++i) {
        for (int cIdx : outDoc.nodes[i].children) {
          outDoc.nodes[cIdx].parent = static_cast<int>(i);
        }
      }

      // Compute rest-pose global transforms in Engine Z-up space
      std::vector<uint8_t> visited(outDoc.nodes.size(), 0);
      for (size_t i = 0; i < outDoc.nodes.size(); ++i) {
        ComputeNodeGlobalRecursive(outDoc, static_cast<int>(i), visited);
      }
    }

    // 10. Default scene roots
    const GLTFJsonValue &scenesJson = root.Get("scenes");
    int sceneIdx = root.GetInt("scene", 0);
    if (scenesJson.IsArray() && !scenesJson.arrVal.empty()) {
      if (sceneIdx < 0 || sceneIdx >= static_cast<int>(scenesJson.arrVal.size())) sceneIdx = 0;
      const GLTFJsonValue &sNodes = scenesJson.arrVal[sceneIdx].Get("nodes");
      if (sNodes.IsArray()) {
        for (const auto &nv : sNodes.arrVal) {
          if (nv.IsNumber()) outDoc.defaultSceneNodes.push_back(static_cast<int>(nv.numVal));
        }
      }
    }

    // 11. Animations
    const GLTFJsonValue &animsJson = root.Get("animations");
    if (animsJson.IsArray()) {
      outDoc.animations.reserve(animsJson.arrVal.size());
      for (const auto &aj : animsJson.arrVal) {
        GLTFAnimation anim;
        anim.name = aj.GetString("name");
        if (aj.Has("extras")) anim.extras = aj.Get("extras");

        const GLTFJsonValue &sampArr = aj.Get("samplers");
        if (sampArr.IsArray()) {
          for (const auto &sv : sampArr.arrVal) {
            GLTFAnimSampler samp;
            samp.input = sv.GetInt("input", -1);
            samp.output = sv.GetInt("output", -1);
            samp.interpolation = sv.GetString("interpolation", "LINEAR");
            anim.samplers.push_back(samp);
          }
        }

        const GLTFJsonValue &chanArr = aj.Get("channels");
        if (chanArr.IsArray()) {
          for (const auto &cv : chanArr.arrVal) {
            GLTFAnimChannel chan;
            chan.sampler = cv.GetInt("sampler", -1);
            const GLTFJsonValue &tgt = cv.Get("target");
            if (tgt.IsObject()) {
              chan.targetNode = tgt.GetInt("node", -1);
              chan.targetPath = tgt.GetString("path");
            }
            if (chan.sampler >= 0 && chan.targetNode >= 0 && !chan.targetPath.empty()) {
              anim.channels.push_back(chan);
            }
          }
        }
        outDoc.animations.push_back(std::move(anim));
      }
    }

    return true;
  }

  bool ReadAccessorFloats(const GLTFDocument &doc, int accessorIdx, std::vector<float> &outFloats) {
    outFloats.clear();
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(doc.accessors.size())) return false;
    const GLTFAccessor &acc = doc.accessors[accessorIdx];
    int numComp = GetAccessorComponentCount(acc.type);
    size_t compBytes = GetComponentByteSize(acc.componentType);
    if (numComp <= 0 || compBytes == 0) return false;

    outFloats.assign(acc.count * static_cast<size_t>(numComp), 0.0f);
    if (acc.count == 0 || acc.bufferView < 0) return true;
    if (acc.bufferView >= static_cast<int>(doc.bufferViews.size())) return false;

    const GLTFBufferView &bv = doc.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc.buffers.size())) return false;
    const std::vector<uint8_t> &buf = doc.buffers[bv.buffer];

    size_t elemSize = static_cast<size_t>(numComp) * compBytes;
    size_t stride = (bv.byteStride > 0) ? bv.byteStride : elemSize;
    size_t baseOffset = bv.byteOffset + acc.byteOffset;

    if (baseOffset + (acc.count - 1) * stride + elemSize > buf.size()) return false;

    for (size_t i = 0; i < acc.count; ++i) {
      const uint8_t *elemPtr = buf.data() + baseOffset + i * stride;
      for (int c = 0; c < numComp; ++c) {
        outFloats[i * numComp + c] = ReadComponentAsFloat(elemPtr + c * compBytes, acc.componentType, acc.normalized);
      }
    }
    return true;
  }

  bool ReadAccessorUInts(const GLTFDocument &doc, int accessorIdx, std::vector<unsigned int> &outUInts) {
    outUInts.clear();
    if (accessorIdx < 0 || accessorIdx >= static_cast<int>(doc.accessors.size())) return false;
    const GLTFAccessor &acc = doc.accessors[accessorIdx];
    int numComp = GetAccessorComponentCount(acc.type);
    size_t compBytes = GetComponentByteSize(acc.componentType);
    if (numComp <= 0 || compBytes == 0) return false;

    outUInts.assign(acc.count * static_cast<size_t>(numComp), 0u);
    if (acc.count == 0 || acc.bufferView < 0) return true;
    if (acc.bufferView >= static_cast<int>(doc.bufferViews.size())) return false;

    const GLTFBufferView &bv = doc.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(doc.buffers.size())) return false;
    const std::vector<uint8_t> &buf = doc.buffers[bv.buffer];

    size_t elemSize = static_cast<size_t>(numComp) * compBytes;
    size_t stride = (bv.byteStride > 0) ? bv.byteStride : elemSize;
    size_t baseOffset = bv.byteOffset + acc.byteOffset;

    if (baseOffset + (acc.count - 1) * stride + elemSize > buf.size()) return false;

    for (size_t i = 0; i < acc.count; ++i) {
      const uint8_t *elemPtr = buf.data() + baseOffset + i * stride;
      for (int c = 0; c < numComp; ++c) {
        if (acc.componentType == 5126) {
          float f = 0.0f;
          std::memcpy(&f, elemPtr + c * compBytes, sizeof(float));
          outUInts[i * numComp + c] = static_cast<unsigned int>(std::max(0.0f, std::round(f)));
        } else {
          outUInts[i * numComp + c] = ReadComponentAsUInt(elemPtr + c * compBytes, acc.componentType);
        }
      }
    }
    return true;
  }

  Quaternion GLTFShortestArcQuat(const Vector3 &fromUnit, const Vector3 &toUnit) {
    float d = clamp(fromUnit.GetDotProduct(toUnit), -1.0f, 1.0f);
    if (d > 0.99995f) {
      return Quaternion(QUATERNION_IDENTITY);
    }
    if (d < -0.9999f) {
      Vector3 fallback = (std::fabs(fromUnit.coords[0]) < 0.9f) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
      Vector3 ortho = fallback.GetCrossProduct(fromUnit).GetNormalized(Vector3(1, 0, 0));
      Quaternion q;
      q.SetAngleAxis(pi, ortho);
      return q.GetNormalized();
    }
    Vector3 axis = fromUnit.GetCrossProduct(toUnit);
    axis.Normalize(Vector3(1, 0, 0));
    Quaternion q;
    q.SetAngleAxis(std::acos(d), axis);
    return q.GetNormalized();
  }

  const char *GetEngineJointName(int jointIdx) {
    static const char *kNames[e_Joint_Count] = {
      "body",
      "middle",
      "neck",
      "left_shoulder",
      "left_elbow",
      "right_shoulder",
      "right_elbow",
      "left_thigh",
      "left_knee",
      "left_ankle",
      "right_thigh",
      "right_knee",
      "right_ankle"
    };
    if (jointIdx < 0 || jointIdx >= e_Joint_Count) return "";
    return kNames[jointIdx];
  }

  int GetEngineJointParent(int jointIdx) {
    static const int kParents[e_Joint_Count] = {
      -1,                  // 0 body
      e_Joint_Body,        // 1 middle
      e_Joint_Middle,      // 2 neck
      e_Joint_Middle,      // 3 left_shoulder
      e_Joint_LeftShoulder,// 4 left_elbow
      e_Joint_Middle,      // 5 right_shoulder
      e_Joint_RightShoulder,// 6 right_elbow
      e_Joint_Body,        // 7 left_thigh
      e_Joint_LeftThigh,   // 8 left_knee
      e_Joint_LeftKnee,    // 9 left_ankle
      e_Joint_Body,        // 10 right_thigh
      e_Joint_RightThigh,  // 11 right_knee
      e_Joint_RightKnee    // 12 right_ankle
    };
    if (jointIdx < 0 || jointIdx >= e_Joint_Count) return -1;
    return kParents[jointIdx];
  }

  Vector3 GetEngineStraightBindJointPos(int jointIdx) {
    // Exact derived world positions of player.object when straight.anim.util (Identity rotations) is applied
    static const Vector3 kPositions[e_Joint_Count] = {
      Vector3( 0.000f,  0.000f, 0.960f), // 0 body
      Vector3( 0.000f,  0.000f, 1.110f), // 1 middle
      Vector3( 0.000f, -0.030f, 1.610f), // 2 neck
      Vector3( 0.160f, -0.010f, 1.590f), // 3 left_shoulder
      Vector3( 0.150f, -0.010f, 1.260f), // 4 left_elbow
      Vector3(-0.160f, -0.010f, 1.590f), // 5 right_shoulder
      Vector3(-0.150f, -0.010f, 1.260f), // 6 right_elbow
      Vector3( 0.087f,  0.000f, 0.950f), // 7 left_thigh
      Vector3( 0.087f,  0.000f, 0.530f), // 8 left_knee
      Vector3( 0.087f, -0.040f, 0.090f), // 9 left_ankle
      Vector3(-0.087f,  0.000f, 0.950f), // 10 right_thigh
      Vector3(-0.087f,  0.000f, 0.530f), // 11 right_knee
      Vector3(-0.087f, -0.040f, 0.090f)  // 12 right_ankle
    };
    if (jointIdx < 0 || jointIdx >= e_Joint_Count) return Vector3(0);
    return kPositions[jointIdx];
  }

  Vector3 GetEngineStraightBindLimbDir(int jointIdx) {
    switch (jointIdx) {
      case e_Joint_Body:
      case e_Joint_Middle:
      case e_Joint_Neck:
        return Vector3(0.0f, 0.0f, 1.0f);
      case e_Joint_LeftShoulder:
        return (GetEngineStraightBindJointPos(e_Joint_LeftElbow) - GetEngineStraightBindJointPos(e_Joint_LeftShoulder)).GetNormalized(Vector3(0, 0, -1));
      case e_Joint_RightShoulder:
        return (GetEngineStraightBindJointPos(e_Joint_RightElbow) - GetEngineStraightBindJointPos(e_Joint_RightShoulder)).GetNormalized(Vector3(0, 0, -1));
      case e_Joint_LeftElbow:
      case e_Joint_RightElbow:
        return Vector3(0.0f, 0.0f, -1.0f);
      case e_Joint_LeftThigh:
        return (GetEngineStraightBindJointPos(e_Joint_LeftKnee) - GetEngineStraightBindJointPos(e_Joint_LeftThigh)).GetNormalized(Vector3(0, 0, -1));
      case e_Joint_RightThigh:
        return (GetEngineStraightBindJointPos(e_Joint_RightKnee) - GetEngineStraightBindJointPos(e_Joint_RightThigh)).GetNormalized(Vector3(0, 0, -1));
      case e_Joint_LeftKnee:
        return (GetEngineStraightBindJointPos(e_Joint_LeftAnkle) - GetEngineStraightBindJointPos(e_Joint_LeftKnee)).GetNormalized(Vector3(0, 0, -1));
      case e_Joint_RightKnee:
        return (GetEngineStraightBindJointPos(e_Joint_RightAnkle) - GetEngineStraightBindJointPos(e_Joint_RightKnee)).GetNormalized(Vector3(0, 0, -1));
      case e_Joint_LeftAnkle:
      case e_Joint_RightAnkle:
        return Vector3(0.0f, -1.0f, 0.0f);
      default:
        return Vector3(0.0f, 0.0f, 1.0f);
    }
  }

  bool IsRootMotionBoneName(const std::string &rawBoneName) {
    std::string norm = NormalizeBoneIdentifier(rawBoneName);
    return (norm == "player" || norm == "root" || norm == "hips" || norm == "pelvis" || norm == "body");
  }

  int MapBoneNameToEngineJointIndex(const std::string &rawBoneName, bool hasSeparateUpperArm) {
    if (rawBoneName.empty()) return -1;
    std::string norm = NormalizeBoneIdentifier(rawBoneName);

    // 1. Exact native GameplayFootball names first
    if (norm == "body") return e_Joint_Body;
    if (norm == "middle") return e_Joint_Middle;
    if (norm == "neck") return e_Joint_Neck;
    if (norm == "left_shoulder") return e_Joint_LeftShoulder;
    if (norm == "left_elbow") return e_Joint_LeftElbow;
    if (norm == "right_shoulder") return e_Joint_RightShoulder;
    if (norm == "right_elbow") return e_Joint_RightElbow;
    if (norm == "left_thigh") return e_Joint_LeftThigh;
    if (norm == "left_knee") return e_Joint_LeftKnee;
    if (norm == "left_ankle") return e_Joint_LeftAnkle;
    if (norm == "right_thigh") return e_Joint_RightThigh;
    if (norm == "right_knee") return e_Joint_RightKnee;
    if (norm == "right_ankle") return e_Joint_RightAnkle;

    // 2. Universal Humanoid / Mixamo / Blender Rigify / Unreal / Unity / CC4 mapping
    std::string core;
    int side = ExtractBoneSideAndCore(norm, core);

    if (side == 0) {
      if (core == "hips" || core == "pelvis" || core == "hip" || core == "waist" || core == "cog") {
        return e_Joint_Body;
      }
      if (core.find("spine") == 0 || core.find("chest") != std::string::npos ||
          core == "torso" || core == "abdomen" || core == "upperbody") {
        return e_Joint_Middle;
      }
      if (core.find("neck") == 0 || core.find("head") == 0 || core == "jaw" || core.find("eye") != std::string::npos) {
        return e_Joint_Neck;
      }
      return -1;
    }

    const bool isLeft = (side < 0);

    // Clavicle vs UpperArm distinction:
    // In Mixamo/Rigify, "LeftShoulder"/"clavicle.L" is the clavicle while "LeftArm"/"upper_arm.L" is the upper arm.
    if (core == "clavicle" || core == "collar") {
      return e_Joint_Middle;
    }
    if (core == "shoulder") {
      return hasSeparateUpperArm ? e_Joint_Middle : (isLeft ? e_Joint_LeftShoulder : e_Joint_RightShoulder);
    }
    if (core == "arm" || core == "upperarm" || core.find("armroll") != std::string::npos || core.find("upperarm") == 0) {
      return isLeft ? e_Joint_LeftShoulder : e_Joint_RightShoulder;
    }
    if (core == "forearm" || core == "lowerarm" || core == "elbow" ||
        core.find("forearm") == 0 || core.find("lowerarm") == 0 ||
        core == "hand" || core == "wrist" ||
        core.find("thumb") != std::string::npos || core.find("index") != std::string::npos ||
        core.find("middle") != std::string::npos || core.find("ring") != std::string::npos ||
        core.find("pinky") != std::string::npos || core.find("finger") != std::string::npos) {
      return isLeft ? e_Joint_LeftElbow : e_Joint_RightElbow;
    }
    if (core == "upleg" || core == "thigh" || core == "upperleg" || core == "hip" ||
        core.find("upleg") == 0 || core.find("thigh") == 0) {
      return isLeft ? e_Joint_LeftThigh : e_Joint_RightThigh;
    }
    if (core == "leg" || core == "lowerleg" || core == "calf" || core == "shin" || core == "knee" ||
        core.find("calf") == 0 || core.find("shin") == 0 || core.find("lowerleg") == 0) {
      return isLeft ? e_Joint_LeftKnee : e_Joint_RightKnee;
    }
    if (core == "foot" || core == "ankle" || core == "ball" || core.find("toe") != std::string::npos || core == "heel") {
      return isLeft ? e_Joint_LeftAnkle : e_Joint_RightAnkle;
    }

    return -1;
  }

  static bool DocumentHasSeparateUpperArmNodes(const GLTFDocument &doc) {
    for (const auto &n : doc.nodes) {
      std::string norm = NormalizeBoneIdentifier(n.name);
      if (norm == "left_shoulder" || norm == "right_shoulder") return false; // native GameplayFootball rig
      std::string core;
      int side = ExtractBoneSideAndCore(norm, core);
      if (side != 0 && (core == "arm" || core == "upperarm")) return true; // Mixamo/Humanoid rig
    }
    return false;
  }

  int ResolveNodeToEngineJointIndex(const GLTFDocument &doc, int nodeIdx) {
    if (nodeIdx < 0 || nodeIdx >= static_cast<int>(doc.nodes.size())) return -1;
    bool hasUpperArm = DocumentHasSeparateUpperArmNodes(doc);

    int cur = nodeIdx;
    int guard = 0;
    while (cur >= 0 && cur < static_cast<int>(doc.nodes.size()) && guard++ < 64) {
      int mapped = MapBoneNameToEngineJointIndex(doc.nodes[cur].name, hasUpperArm);
      if (mapped >= 0) return mapped;
      cur = doc.nodes[cur].parent;
    }
    return -1;
  }

  void ComputeBindPoseLimbAlignment(const GLTFDocument &doc,
                                    Quaternion outAlignRot[e_Joint_Count],
                                    int outPrimaryNodeForJoint[e_Joint_Count],
                                    float &outUnitScale) {
    for (int j = 0; j < e_Joint_Count; ++j) {
      outAlignRot[j] = Quaternion(QUATERNION_IDENTITY);
      outPrimaryNodeForJoint[j] = -1;
    }
    outUnitScale = 1.0f;
    if (doc.nodes.empty()) return;

    bool hasUpperArm = DocumentHasSeparateUpperArmNodes(doc);
    std::vector<int> nodeJoint(doc.nodes.size(), -1);
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
      nodeJoint[i] = MapBoneNameToEngineJointIndex(doc.nodes[i].name, hasUpperArm);
    }

    // Select proximal (root-most) node for each canonical joint 0..12
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
      int j = nodeJoint[i];
      if (j < 0) continue;
      int p = doc.nodes[i].parent;
      int pj = (p >= 0) ? ResolveNodeToEngineJointIndex(doc, p) : -1;
      if (pj != j && outPrimaryNodeForJoint[j] == -1) {
        outPrimaryNodeForJoint[j] = static_cast<int>(i);
      }
    }
    // Fallback if not yet set
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
      int j = nodeJoint[i];
      if (j >= 0 && outPrimaryNodeForJoint[j] == -1) {
        outPrimaryNodeForJoint[j] = static_cast<int>(i);
      }
    }

    // Estimate unit scale (e.g., Mixamo 175cm -> ~0.01f, standard glTF 1.75m -> ~1.0f)
    if (outPrimaryNodeForJoint[e_Joint_Body] >= 0 && outPrimaryNodeForJoint[e_Joint_Neck] >= 0) {
      float bodyZ = doc.nodes[outPrimaryNodeForJoint[e_Joint_Body]].engineGlobalPos.coords[2];
      float neckZ = doc.nodes[outPrimaryNodeForJoint[e_Joint_Neck]].engineGlobalPos.coords[2];
      float ankleZ = 0.0f;
      if (outPrimaryNodeForJoint[e_Joint_LeftAnkle] >= 0) {
        ankleZ = doc.nodes[outPrimaryNodeForJoint[e_Joint_LeftAnkle]].engineGlobalPos.coords[2];
      }
      float rigSpan = std::fabs(neckZ - ankleZ);
      if (rigSpan > 15.0f) {
        // Centimeter-scaled rig (Mixamo/FBX-to-glTF unscaled)
        outUnitScale = 1.52f / rigSpan;
      } else if (rigSpan > 0.25f && (rigSpan < 1.15f || rigSpan > 2.15f)) {
        outUnitScale = 1.52f / rigSpan;
      } else if (std::fabs(bodyZ) > 15.0f) {
        outUnitScale = 0.01f;
      }
    }

    // Helper lambda to align limb joint j using vector from jointA -> jointB
    auto alignLimbPair = [&](int jointIdx, int fromNodeIdx, int toNodeIdx) {
      if (fromNodeIdx < 0 || toNodeIdx < 0) return;
      Vector3 bindVec = doc.nodes[toNodeIdx].engineGlobalPos - doc.nodes[fromNodeIdx].engineGlobalPos;
      if (bindVec.GetLength() < 1e-4f) return;
      bindVec.Normalize();
      Vector3 targetDir = GetEngineStraightBindLimbDir(jointIdx);
      outAlignRot[jointIdx] = GLTFShortestArcQuat(bindVec, targetDir);
    };

    alignLimbPair(e_Joint_LeftShoulder, outPrimaryNodeForJoint[e_Joint_LeftShoulder], outPrimaryNodeForJoint[e_Joint_LeftElbow]);
    alignLimbPair(e_Joint_RightShoulder, outPrimaryNodeForJoint[e_Joint_RightShoulder], outPrimaryNodeForJoint[e_Joint_RightElbow]);
    alignLimbPair(e_Joint_LeftThigh, outPrimaryNodeForJoint[e_Joint_LeftThigh], outPrimaryNodeForJoint[e_Joint_LeftKnee]);
    alignLimbPair(e_Joint_RightThigh, outPrimaryNodeForJoint[e_Joint_RightThigh], outPrimaryNodeForJoint[e_Joint_RightKnee]);
    alignLimbPair(e_Joint_LeftKnee, outPrimaryNodeForJoint[e_Joint_LeftKnee], outPrimaryNodeForJoint[e_Joint_LeftAnkle]);
    alignLimbPair(e_Joint_RightKnee, outPrimaryNodeForJoint[e_Joint_RightKnee], outPrimaryNodeForJoint[e_Joint_RightAnkle]);

    // Forearm alignment: look for distal hand child node under LeftElbow / RightElbow, else inherit shoulder alignment
    auto findDistalChildNode = [&](int elbowNodeIdx, int elbowJointIdx) -> int {
      if (elbowNodeIdx < 0) return -1;
      for (int cIdx : doc.nodes[elbowNodeIdx].children) {
        if (ResolveNodeToEngineJointIndex(doc, cIdx) == elbowJointIdx) {
          return cIdx;
        }
      }
      return -1;
    };

    int leftHandNode = findDistalChildNode(outPrimaryNodeForJoint[e_Joint_LeftElbow], e_Joint_LeftElbow);
    if (leftHandNode >= 0) {
      alignLimbPair(e_Joint_LeftElbow, outPrimaryNodeForJoint[e_Joint_LeftElbow], leftHandNode);
    } else {
      outAlignRot[e_Joint_LeftElbow] = outAlignRot[e_Joint_LeftShoulder];
    }

    int rightHandNode = findDistalChildNode(outPrimaryNodeForJoint[e_Joint_RightElbow], e_Joint_RightElbow);
    if (rightHandNode >= 0) {
      alignLimbPair(e_Joint_RightElbow, outPrimaryNodeForJoint[e_Joint_RightElbow], rightHandNode);
    } else {
      outAlignRot[e_Joint_RightElbow] = outAlignRot[e_Joint_RightShoulder];
    }
  }

}
