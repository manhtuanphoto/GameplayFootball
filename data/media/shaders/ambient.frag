#version 150

#pragma optimize(on)

// http://www.gamedev.net/community/forums/topic.asp?topic_id=463075&PageSize=25&WhichPage=4
// by ArKano22

uniform sampler2D map_albedo; // 0
uniform sampler2D map_normal; // 1
uniform sampler2D map_depth;  // was: 3, now: 2
uniform sampler2D map_aux;    // was: 2, now: 3
uniform sampler2D map_noise;  // 4

uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 inverseProjectionViewMatrix;

uniform float contextWidth;
uniform float contextHeight;
uniform float contextX;
uniform float contextY;

uniform vec2 cameraClip;

const int SSAO_kernelSize = 32; // don't make larger than this: 32 is hardcoded in source, so no more sample positions exist
//uniform int SSAO_kernelSize; // won't work until glsl 4.3 or so.
uniform vec3 SSAO_kernel[SSAO_kernelSize];

out vec4 stdout0;
out vec4 stdout1;

// For all settings: 1.0 = 100% 0.5=50% 1.5 = 150% etc
vec3 ContrastSaturationBrightness(vec3 color, float brt, float con, float sat) {
  // Increase or decrease theese values to adjust r, g and b color channels separately
  const float AvgLumR = 0.0;
  const float AvgLumG = 0.0;
  const float AvgLumB = 0.0;

  const vec3 LumCoeff = vec3(0.2125, 0.7154, 0.0721);

  vec3 AvgLumin = vec3(AvgLumR, AvgLumG, AvgLumB);
  vec3 brtColor = color * brt;
  vec3 intensity = vec3(dot(brtColor, LumCoeff));
  vec3 satColor = mix(intensity, brtColor, sat);
  vec3 conColor = mix(AvgLumin, satColor, con);

  return conColor;
}

float GetEdge(vec2 pos, float centerRawDepth, vec3 centerRawNormal) {
  vec2 invRes = vec2(1.0 / contextWidth, 1.0 / contextHeight);

  float centerLinearDepth = cameraClip.y / (centerRawDepth - cameraClip.x);

  vec2 uvN = pos + vec2(0.0, -invRes.y);
  vec2 uvE = pos + vec2(invRes.x, 0.0);
  vec2 uvS = pos + vec2(0.0, invRes.y);
  vec2 uvW = pos + vec2(-invRes.x, 0.0);

  float dN = cameraClip.y / (texture2D(map_depth, uvN).x - cameraClip.x);
  float dE = cameraClip.y / (texture2D(map_depth, uvE).x - cameraClip.x);
  float dS = cameraClip.y / (texture2D(map_depth, uvS).x - cameraClip.x);
  float dW = cameraClip.y / (texture2D(map_depth, uvW).x - cameraClip.x);

  vec2 delta1 = abs(vec2(dN, dE) - centerLinearDepth);
  vec2 delta2 = abs(centerLinearDepth - vec2(dS, dW));
  vec2 minDeltas = max(min(delta1, delta2), vec2(0.00001));
  vec2 maxDeltas = max(delta1, delta2);
  vec2 depthResults = step(minDeltas * 30.0, maxDeltas);

  vec2 n1 = vec2(dot(texture2D(map_normal, uvN).xyz, centerRawNormal),
                 dot(texture2D(map_normal, uvE).xyz, centerRawNormal));
  vec2 n2 = vec2(dot(texture2D(map_normal, uvS).xyz, centerRawNormal),
                 dot(texture2D(map_normal, uvW).xyz, centerRawNormal));
  vec2 normalResults = step(vec2(0.45), abs(n1 - n2));

  vec2 results = max(normalResults, depthResults);
  return (results.x + results.y) * 0.5;
}

vec3 GetWorldPosition(vec2 texCoord, float depth) {
  vec4 projectedPos = vec4(texCoord.x * 2 - 1, texCoord.y * 2 - 1, depth * 2 - 1, 1.0f);
  vec4 worldPosition = inverseProjectionViewMatrix * projectedPos;
  worldPosition.xyz /= worldPosition.w;
  return worldPosition.xyz;
}

void main(void) {

  vec2 texCoord = gl_FragCoord.xy;
  texCoord.x -= contextX;
  texCoord.y -= contextY;
  texCoord.x /= contextWidth;
  texCoord.y /= contextHeight;

  float depth = texture2D(map_depth, texCoord).x;
  vec3 rawNormal = texture2D(map_normal, texCoord).xyz;

  if (depth >= 0.9995f) {
    stdout0 = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    stdout1 = vec4(GetEdge(texCoord, depth, rawNormal), 1.0f, 0.0f, 0.0f);
    return;
  }

  vec3 worldPosition = GetWorldPosition(texCoord, depth);

  vec3 base = texture2D(map_albedo, texCoord).xyz;
  vec3 normal = normalize(rawNormal);

  // Natural stadium hemisphere ambient: cool sky dome from above + subtle warm emerald turf bounce from below
  float hemi = clamp(normal.z * 0.5f + 0.5f, 0.0f, 1.0f);
  vec3 skyAmbient = vec3(0.84f, 0.94f, 1.14f);
  vec3 groundBounce = vec3(0.56f, 0.72f, 0.52f);
  vec3 hemiLight = mix(groundBounce, skyAmbient, hemi);

  // Retain 85% natural albedo saturation in shadow instead of washing out to grey
  float luma = dot(base, vec3(0.2126f, 0.7152f, 0.0722f));
  vec3 richBase = mix(vec3(luma), base, 0.85f);
  float ambientStrength = 0.22f;
  vec3 ambientColor = richBase * hemiLight * ambientStrength;

  // screen space ambient occlusion
  // normal-oriented hemisphere method, (c) john chapman
  // http://john-chapman-graphics.blogspot.nl/2013/01/ssao-tutorial.html

  vec2 noiseScale = vec2(contextWidth / 4.0f, contextHeight / 4.0f);
  float SSAO_radius = 0.22f;
  float SSAO_bias = 0.025f;

  float SSAO = 0.0;

  vec3 viewPosition = vec3(viewMatrix * vec4(worldPosition, 1.0f));
  vec3 viewNormal = normalize(vec3(viewMatrix * vec4(normal, 0.0f)));

  vec3 randomVec = -texture2D(map_noise, texCoord * noiseScale).xyz * 2.0f - 1.0f;
  vec3 tangent = normalize(randomVec - viewNormal * dot(randomVec, viewNormal));
  vec3 bitangent = cross(viewNormal, tangent);
  mat3 tbn = mat3(tangent, bitangent, viewNormal);

  for (int i = 0; i < SSAO_kernelSize; ++i) {
    vec3 samplePos = tbn * SSAO_kernel[i];
    samplePos = viewPosition + samplePos * SSAO_radius;

    vec4 offset = projectionMatrix * vec4(samplePos, 1.0f);
    offset.xy /= offset.w;
    offset.xy = offset.xy * 0.5f + 0.5f;

    float sampleDepth = texture(map_depth, offset.xy).x;
    sampleDepth = cameraClip.y / (sampleDepth - cameraClip.x);

    float rangeCheckFalloff = clamp((SSAO_radius - abs(-viewPosition.z - sampleDepth) * 0.5f) / SSAO_radius, 0.0f, 1.0f);
    float rangeCheckInside = (sampleDepth < (-samplePos.z - SSAO_bias)) ? 1.0f : 0.0f;

    SSAO += rangeCheckFalloff * rangeCheckInside;
  }
  SSAO /= float(SSAO_kernelSize);
  SSAO = clamp(1.0f - SSAO * 1.15f, 0.0f, 1.0f);

  // self-illumination (stored in R8 channel)
  float self_illumination = texture2D(map_aux, texCoord.st).r;

  vec3 fragColor = clamp(ambientColor + base * self_illumination, 0.0f, 1.0f);

  // Output clean un-occluded ambient to accumulation buffer; blurred SSAO is applied once in postprocess.frag
  stdout0 = vec4(fragColor, 1.0f);
  stdout1 = vec4(GetEdge(texCoord, depth, rawNormal), SSAO, 0.0f, 0.0f);
}
