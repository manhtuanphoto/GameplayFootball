#version 150

/* matrix spaces primer :

  http://stackoverflow.com/questions/8207420/desktop-glsl-without-ftransform

    A model matrix transforms an object from object coordinates to world coordinates.
    A view matrix transforms the world coordinates to eye coordinates.
    A projection matrix converts eye coordinates to clip coordinates.

  Based on standard naming conventions, the mvpMatrix is projection * view * model, in that order.
  There is no other matrices that you need to multiply by.

  Projection is your projection matrix (either ortho or perspective),
  view is the camera transform matrix (NOT the modelview), and
  model is the position, scale, and rotation of your object.
*/

#pragma optimize(on)

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform int has_skinning;
uniform mat4 boneMatrices[16];

in vec4 position;
in vec3 normal;
in vec3 texcoord;
in vec3 tangent;
in vec3 bitangent;
in vec4 boneIndices;
in vec4 boneWeights;

// view space
out vec4 frag_position;
out vec3 frag_normal;
out vec3 frag_texcoord;
out vec3 frag_tangent;
out vec3 frag_bitangent;

vec3 safeNormalize(vec3 v) {
  float lenSq = dot(v, v);
  return (lenSq > 1e-8) ? (v * inversesqrt(lenSq)) : v;
}

void main(void) {
  vec4 localPos = position;
  vec3 localNormal = normal;
  vec3 localTangent = tangent;
  vec3 localBitangent = bitangent;

  if (has_skinning == 1) {
    mat4 skinMat = boneWeights.x * boneMatrices[int(boneIndices.x + 0.5)]
                 + boneWeights.y * boneMatrices[int(boneIndices.y + 0.5)]
                 + boneWeights.z * boneMatrices[int(boneIndices.z + 0.5)]
                 + boneWeights.w * boneMatrices[int(boneIndices.w + 0.5)];
    localPos = skinMat * vec4(position.xyz, 1.0);
    mat3 skinRot = mat3(skinMat);
    localNormal = safeNormalize(skinRot * normal);
    localTangent = safeNormalize(skinRot * tangent);
    localBitangent = safeNormalize(skinRot * bitangent);
  }

  mat4 modelViewMatrix = viewMatrix * modelMatrix;
  frag_position = modelViewMatrix * localPos;

  // modelMatrix is always constructed with unit scale (1, 1, 1) and pure rotation quaternion, so (R^-1)^T == R
  mat3 normalMatrix = mat3(modelMatrix);
  frag_normal = normalMatrix * localNormal;
  frag_tangent = normalMatrix * localTangent;
  frag_bitangent = normalMatrix * localBitangent;
  frag_texcoord.st = texcoord.st;

  gl_Position = projectionMatrix * frag_position;
}
