#version 150

#pragma optimize(on)

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform int has_skinning;
uniform mat4 boneMatrices[16];

in vec4 position;
in vec4 boneIndices;
in vec4 boneWeights;

void main(void) {
  vec4 localPos = position;
  if (has_skinning == 1) {
    mat4 skinMat = boneWeights.x * boneMatrices[int(boneIndices.x + 0.5)]
                 + boneWeights.y * boneMatrices[int(boneIndices.y + 0.5)]
                 + boneWeights.z * boneMatrices[int(boneIndices.z + 0.5)]
                 + boneWeights.w * boneMatrices[int(boneIndices.w + 0.5)];
    localPos = skinMat * vec4(position.xyz, 1.0);
  }
  gl_Position = projectionMatrix * viewMatrix * modelMatrix * localPos;
}
