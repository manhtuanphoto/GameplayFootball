#version 150

#pragma optimize(on)

uniform sampler2D map_accumulation; // 0
uniform sampler2D map_modifier;     // 1
uniform sampler2D map_depth;        // 2

uniform float contextWidth;
uniform float contextHeight;
uniform float contextX;
uniform float contextY;

uniform vec2 cameraClip;

uniform float fogScale;

out vec4 stdout;

// http://mouaif.wordpress.com/2009/01/05/photoshop-math-with-glsl-shaders/

#define GammaCorrection(color, gamma) pow(color, 1.0 / gamma)

// For all settings: 1.0 = 100% 0.5=50% 1.5 = 150%
vec3 ContrastSaturationBrightness(vec3 color, float brt, float con, float sat) {
  const vec3 LumCoeff = vec3(0.2125, 0.7154, 0.0721);
  // Increase or decrease these values to adjust r, g and b color channels seperately
  vec3 AvgLumin = vec3(0.5, 0.5, 0.5);
  vec3 brtColor = color * brt;
  vec3 intensity = vec3(dot(brtColor, LumCoeff));
  vec3 satColor = mix(intensity, brtColor, sat);
  vec3 conColor = mix(AvgLumin, satColor, con);

  return conColor;
}

vec3 AlternateContrast(vec3 color, float bias) {
  return color * (1.0f - bias) + (-cos(clamp(color, 0.0f, 1.0f) * 3.14159265f) * 0.5f + 0.5f) * bias;
}

vec3 LinearToSRGB(vec3 linearCol) {
  vec3 c = clamp(linearCol, 0.0f, 1.0f);
  vec3 lo = c * 12.92f;
  vec3 hi = 1.055f * pow(c, vec3(1.0f / 2.4f)) - 0.055f;
  return mix(lo, hi, step(vec3(0.0031308f), c));
}

vec3 Compress(vec3 color, float startThreshold, float endThreshold) {
  float range = endThreshold - startThreshold;
  float compressedRange = 1.0f - startThreshold;
  for (int c = 0; c < 3; c++) {
    if (color[c] > startThreshold) {
      color[c] = (color[c] - startThreshold) / range; // 0 to 1
      color[c] = pow(color[c], 0.5f);
      color[c] = startThreshold + color[c] * compressedRange;
    }
  }
  return color;
}


void main(void) {
  vec2 texCoord = gl_FragCoord.xy;
  texCoord.x -= contextX;
  texCoord.y -= contextY;
  texCoord.x /= contextWidth;
  texCoord.y /= contextHeight;

  float depth = texture2D(map_depth, texCoord).x;
  vec3 skyHorizonColor = vec3(0.78f, 0.85f, 0.94f);

  // SSAO 4x4 box blur (applied cleanly once, preserving direct sunlight highlights)
  int SSAO_blurSize = 4;
  float SSAO = 0.0f;

  vec2 texelSize = 1.0f / vec2(textureSize(map_modifier, 0));
  vec2 hlim = vec2(float(-SSAO_blurSize) * 0.5f + 0.5f);
  for (int x = 0; x < SSAO_blurSize; ++x) {
    for (int y = 0; y < SSAO_blurSize; ++y) {
      vec2 offset = (hlim + vec2(float(x), float(y))) * texelSize;
      SSAO += texture(map_modifier, texCoord + offset).g;
    }
  }
  SSAO = clamp(SSAO * (1.0f / 16.0f), 0.0f, 1.0f);

  vec3 fragColor;
  if (depth > 0.9995f) {
    fragColor = skyHorizonColor;
  } else {
    vec3 base = texture2D(map_accumulation, texCoord).rgb;

    float aoFactor = mix(0.64f, 1.0f, SSAO);
    fragColor = base * aoFactor;

    float fragDepth = cameraClip.y / (depth - cameraClip.x);
    float hazeStart = 135.0f;
    float fogFactor = clamp((fragDepth - hazeStart) * 0.0022f * (1.0f - fogScale * 0.5f), 0.0f, 0.18f);
    fragColor = mix(fragColor, skyHorizonColor, fogFactor);
  }

  // Smooth filmic highlight rolloff so white pitch lines & sunlit kits never clip harshly
  fragColor = fragColor / (1.0f + fragColor * 0.16f) * 1.12f;

  float saturation = 1.04f * (0.86f + SSAO * 0.14f);
  fragColor = ContrastSaturationBrightness(fragColor, 1.0f, 1.03f, saturation);
  fragColor = AlternateContrast(fragColor, 0.12f);
  fragColor = clamp(fragColor, 0.0f, 1.0f);

  vec3 srgbColor = LinearToSRGB(fragColor);
  float perceptualLuma = dot(srgbColor, vec3(0.299f, 0.587f, 0.114f));
  stdout = vec4(srgbColor, perceptualLuma);
}
