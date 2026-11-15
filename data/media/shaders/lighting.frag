#version 150

#pragma optimize(on)

uniform sampler2D map_albedo; // 0
uniform sampler2D map_normal; // 1
uniform sampler2D map_depth;  // was: 3, now: 2
//uniform sampler2D map_aux;    // was: 2, now: 3 (disabled though)
uniform sampler2DShadow map_shadow; // was: 7, now: 3

//uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 inverseProjectionViewMatrix;
uniform mat4 lightViewProjectionMatrix;

uniform float contextWidth;
uniform float contextHeight;
uniform float contextX;
uniform float contextY;

//uniform vec2 cameraClip;

uniform bool has_shadow;

uniform vec3 cameraPosition;
uniform vec3 lightPosition;
uniform vec3 lightColor;
uniform float lightRadius;

out vec4 stdout0;

const int poisson_size = 16;
const vec2 poisson[poisson_size] = vec2[poisson_size](
  vec2(-0.94201624, -0.39906216),
  vec2( 0.94558609, -0.76890725),
  vec2(-0.09418410, -0.92938870),
  vec2( 0.34495938,  0.29387760),
  vec2(-0.91588581,  0.45771432),
  vec2(-0.81544232, -0.87912464),
  vec2(-0.38277543,  0.27676845),
  vec2( 0.97484398,  0.75648379),
  vec2( 0.44323325, -0.97511554),
  vec2( 0.53742981, -0.47373420),
  vec2(-0.26496911, -0.41893023),
  vec2( 0.79197514,  0.19090188),
  vec2(-0.24188840,  0.99706507),
  vec2(-0.81409955,  0.91437590),
  vec2( 0.19984126,  0.78641367),
  vec2( 0.14383161, -0.14100790)
);

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

  float depth = texture2D(map_depth, texCoord).x; // non-linear (0 .. 1)
  if (depth > 0.9995f) {
    stdout0 = vec4(0.0f);
    return;
  }

  vec3 worldPosition = GetWorldPosition(texCoord, depth);

  float dist = distance(lightPosition, worldPosition.xyz);
  if (dist >= lightRadius) {
    stdout0 = vec4(0.0f);
    return;
  }

  vec3 lightDir = normalize(lightPosition - worldPosition.xyz);

  vec4 normalshine = texture2D(map_normal, texCoord);
  vec3 normal = normalize(normalshine.xyz);

  float nDotLD = dot(lightDir, normal);

  // scattering emulation ;)
  float scatter_nDotLD = nDotLD * 0.5f + 0.5f;
  scatter_nDotLD = pow(scatter_nDotLD, 1.7f);
  nDotLD = max(nDotLD, 0.0f);

  float resulting_nDotLD = nDotLD * 0.6f + scatter_nDotLD * 0.4f;

  float shininess = normalshine.w;

  float falloff = max(0.0, lightRadius - dist) / lightRadius;
  falloff = falloff * falloff;

  float brightness = 1.55f;
  vec3 lighted = resulting_nDotLD * falloff * lightColor * brightness;

  vec3 viewDir = normalize(cameraPosition - worldPosition.xyz);
  vec3 halfVec = normalize(lightDir + viewDir);

  vec4 base = texture2D(map_albedo, texCoord);

  // Blinn-Phong specular + subtle grazing rim sheen on kits/players
  float spec = base.w;
  float specPower = max(8.0f, shininess * 128.0f);
  vec3 specular = pow(max(0.0f, dot(normal, halfVec)), specPower) * spec * falloff * lightColor * 0.9f;
  float fresnelRim = pow(1.0f - max(0.0f, dot(normal, viewDir)), 4.0f) * max(0.0f, nDotLD) * spec * 0.22f;
  specular += fresnelRim * lightColor * falloff;

  float shaded = 1.0f;
  if (has_shadow) {
    vec4 projectedFrag = lightViewProjectionMatrix * vec4(worldPosition, 1.0f);
    vec3 projCoords = projectedFrag.xyz / projectedFrag.w;

    if (projCoords.x > 0.002f && projCoords.x < 0.998f &&
        projCoords.y > 0.002f && projCoords.y < 0.998f &&
        projCoords.z > 0.0f && projCoords.z < 0.9995f) {

      vec2 texelScale = (1.65f) / vec2(textureSize(map_shadow, 0));
      float slopeBias = clamp(0.00014f + 0.00038f * (1.0f - max(0.0f, dot(normal, lightDir))), 0.00012f, 0.00065f);
      float refZ = projCoords.z - slopeBias;

      float shadowAcc = 0.0f;
      for (int i = 0; i < poisson_size; i++) {
        shadowAcc += texture(map_shadow, vec3(projCoords.xy + poisson[i] * texelScale, refZ));
      }
      shadowAcc *= (1.0f / float(poisson_size));

      // Grounded stadium shadow density (ambient pass supplies natural sky fill)
      shaded = mix(0.18f, 1.0f, shadowAcc);
    }
  }

  vec3 fragColor = (base.rgb * lighted + specular) * shaded;

  stdout0 = vec4(fragColor, 0.0f);
}
