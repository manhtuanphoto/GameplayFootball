#version 150

#pragma optimize(on)

// Anti-Aliasing + Contrast-Adaptive Turf Clarity (CAS) Post-Pass
// Modes:
//   0 = Off
//   1 = FXAA Fast (Default - lightweight 5-tap directional edge AA)
//   2 = FXAA Quality (12-step FXAA 3.11 Quality + G-Buffer Silhouette Assist + CAS)

uniform sampler2D map_postprocess; // 0: .rgb = perceptual sRGB, .a = perceptual luma
uniform sampler2D map_modifier;    // 1: .r = G-buffer depth/normal silhouette factor

uniform float contextWidth;
uniform float contextHeight;
uniform float contextX;
uniform float contextY;
uniform int aaMode;

out vec4 stdout;

const float QUALITY_STEPS[12] = float[12](
  1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f,
  2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 8.0f
);

vec3 ApplyCAS(vec3 cM, vec3 cN, vec3 cS, vec3 cE, vec3 cW, float sharpness) {
  vec3 mnRGB = min(cM, min(min(cN, cS), min(cE, cW)));
  vec3 mxRGB = max(cM, max(max(cN, cS), max(cE, cW)));
  vec3 ampRGB = clamp(min(mnRGB, 2.0f - mxRGB) / max(mxRGB, vec3(1e-4f)), 0.0f, 1.0f);
  ampRGB = sqrt(ampRGB);
  vec3 wRGB = -ampRGB * (1.0f / mix(8.0f, 5.0f, sharpness));
  return clamp((cM + (cN + cS + cE + cW) * wRGB) / (1.0f + 4.0f * wRGB), 0.0f, 1.0f);
}

vec3 RunFXAAFast(vec2 uv, vec2 rcpFrame) {
  vec4 rgbyM = texture(map_postprocess, uv);
  float lumaM  = rgbyM.a;
  float lumaNW = texture(map_postprocess, uv + vec2(-0.5f, -0.5f) * rcpFrame).a;
  float lumaNE = texture(map_postprocess, uv + vec2( 0.5f, -0.5f) * rcpFrame).a;
  float lumaSW = texture(map_postprocess, uv + vec2(-0.5f,  0.5f) * rcpFrame).a;
  float lumaSE = texture(map_postprocess, uv + vec2( 0.5f,  0.5f) * rcpFrame).a;

  float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
  float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
  if ((lumaMax - lumaMin) < max(0.0312f, lumaMax * 0.125f)) {
    return rgbyM.rgb;
  }

  vec2 dir;
  dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
  dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

  float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * 0.125f), 1.0f / 128.0f);
  float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * rcpDirMin, vec2(-8.0f), vec2(8.0f)) * rcpFrame;

  vec4 rgbA = 0.5f * (
    texture(map_postprocess, uv + dir * (1.0f / 3.0f - 0.5f)) +
    texture(map_postprocess, uv + dir * (2.0f / 3.0f - 0.5f))
  );
  vec4 rgbB = rgbA * 0.5f + 0.25f * (
    texture(map_postprocess, uv + dir * -0.5f) +
    texture(map_postprocess, uv + dir *  0.5f)
  );

  if (rgbB.a < lumaMin || rgbB.a > lumaMax) {
    return rgbA.rgb;
  }
  return rgbB.rgb;
}

vec3 RunFXAAQuality(vec2 uv, vec2 rcpFrame) {
  vec4 rgbyM = texture(map_postprocess, uv);
  vec4 rgbyN = texture(map_postprocess, uv + vec2( 0.0f, -rcpFrame.y));
  vec4 rgbyS = texture(map_postprocess, uv + vec2( 0.0f,  rcpFrame.y));
  vec4 rgbyW = texture(map_postprocess, uv + vec2(-rcpFrame.x,  0.0f));
  vec4 rgbyE = texture(map_postprocess, uv + vec2( rcpFrame.x,  0.0f));

  float lumaM = rgbyM.a;
  float lumaN = rgbyN.a;
  float lumaS = rgbyS.a;
  float lumaW = rgbyW.a;
  float lumaE = rgbyE.a;

  float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
  float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
  float lumaRange = lumaMax - lumaMin;

  float geomEdge = texture(map_modifier, uv).r;
  float edgeThreshold = (geomEdge > 0.15f) ? 0.014f : max(0.028f, lumaMax * 0.085f);

  // Non-edge interior pixels (turf, kit fabric): keep razor-sharp detail via bounded CAS
  if (lumaRange < edgeThreshold) {
    return ApplyCAS(rgbyM.rgb, rgbyN.rgb, rgbyS.rgb, rgbyE.rgb, rgbyW.rgb, 0.15f);
  }

  float lumaNW = texture(map_postprocess, uv + vec2(-rcpFrame.x, -rcpFrame.y)).a;
  float lumaNE = texture(map_postprocess, uv + vec2( rcpFrame.x, -rcpFrame.y)).a;
  float lumaSW = texture(map_postprocess, uv + vec2(-rcpFrame.x,  rcpFrame.y)).a;
  float lumaSE = texture(map_postprocess, uv + vec2( rcpFrame.x,  rcpFrame.y)).a;

  float lumaNS = lumaN + lumaS;
  float lumaWE = lumaW + lumaE;
  float lumaNWSW = lumaNW + lumaSW;
  float lumaNESE = lumaNE + lumaSE;
  float lumaNWNE = lumaNW + lumaNE;
  float lumaSWSE = lumaSW + lumaSE;

  // Sub-pixel aliasing factor (3x3 tent filter)
  float lumaAvg = (2.0f * (lumaNS + lumaWE) + lumaNWSW + lumaNESE) * (1.0f / 12.0f);
  float subpixOffset1 = clamp(abs(lumaAvg - lumaM) / max(lumaRange, 1e-5f), 0.0f, 1.0f);
  float subpixOffset2 = (-2.0f * subpixOffset1 + 3.0f) * subpixOffset1 * subpixOffset1;
  float subpixQuality = (geomEdge > 0.15f) ? 0.75f : 0.62f;
  float subpixOffsetFinal = subpixOffset2 * subpixOffset2 * subpixQuality;

  // Local 3x3 horizontal vs vertical orientation classification
  float edgeHorz = abs(-2.0f * lumaW + lumaNWSW) + abs(-2.0f * lumaM + lumaNS) * 2.0f + abs(-2.0f * lumaE + lumaNESE);
  float edgeVert = abs(-2.0f * lumaN + lumaNWNE) + abs(-2.0f * lumaM + lumaWE) * 2.0f + abs(-2.0f * lumaS + lumaSWSE);
  bool isHorizontal = (edgeHorz >= edgeVert);

  float luma1 = isHorizontal ? lumaN : lumaW;
  float luma2 = isHorizontal ? lumaS : lumaE;
  float gradient1 = luma1 - lumaM;
  float gradient2 = luma2 - lumaM;
  bool is1Steepest = (abs(gradient1) >= abs(gradient2));
  float gradientScaled = 0.25f * max(abs(gradient1), abs(gradient2));

  float stepLength = isHorizontal ? rcpFrame.y : rcpFrame.x;
  float lumaLocalAverage = 0.0f;
  if (is1Steepest) {
    stepLength = -stepLength;
    lumaLocalAverage = 0.5f * (luma1 + lumaM);
  } else {
    lumaLocalAverage = 0.5f * (luma2 + lumaM);
  }

  vec2 currentUv = uv;
  if (isHorizontal) {
    currentUv.y += stepLength * 0.5f;
  } else {
    currentUv.x += stepLength * 0.5f;
  }

  // 12-iteration accelerating walk along the edge tangent (up to +/-28.5 pixels for shallow pitch lines)
  vec2 offset = isHorizontal ? vec2(rcpFrame.x, 0.0f) : vec2(0.0f, rcpFrame.y);
  vec2 uv1 = currentUv - offset * QUALITY_STEPS[0];
  vec2 uv2 = currentUv + offset * QUALITY_STEPS[0];

  float lumaEnd1 = texture(map_postprocess, uv1).a - lumaLocalAverage;
  float lumaEnd2 = texture(map_postprocess, uv2).a - lumaLocalAverage;
  bool reached1 = (abs(lumaEnd1) >= gradientScaled);
  bool reached2 = (abs(lumaEnd2) >= gradientScaled);
  bool reachedBoth = reached1 && reached2;

  if (!reached1) uv1 -= offset * QUALITY_STEPS[1];
  if (!reached2) uv2 += offset * QUALITY_STEPS[1];

  if (!reachedBoth) {
    for (int i = 2; i < 12; ++i) {
      if (!reached1) {
        lumaEnd1 = texture(map_postprocess, uv1).a - lumaLocalAverage;
        reached1 = (abs(lumaEnd1) >= gradientScaled);
      }
      if (!reached2) {
        lumaEnd2 = texture(map_postprocess, uv2).a - lumaLocalAverage;
        reached2 = (abs(lumaEnd2) >= gradientScaled);
      }
      reachedBoth = reached1 && reached2;
      if (reachedBoth) break;

      if (!reached1) uv1 -= offset * QUALITY_STEPS[i];
      if (!reached2) uv2 += offset * QUALITY_STEPS[i];
    }
    if (!reached1) {
      lumaEnd1 = texture(map_postprocess, uv1).a - lumaLocalAverage;
    }
    if (!reached2) {
      lumaEnd2 = texture(map_postprocess, uv2).a - lumaLocalAverage;
    }
  }

  float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
  float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

  bool isDirection1 = (distance1 < distance2);
  float distanceFinal = min(distance1, distance2);
  float edgeThickness = max(distance1 + distance2, 1e-5f);

  float pixelOffset = -distanceFinal / edgeThickness + 0.5f;
  bool isLumaCenterSmaller = (lumaM < lumaLocalAverage);
  bool correctVariation = (((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0f) != isLumaCenterSmaller);
  float finalOffset = correctVariation ? max(pixelOffset, subpixOffsetFinal) : subpixOffsetFinal;

  vec2 finalUv = uv;
  if (isHorizontal) {
    finalUv.y += finalOffset * stepLength;
  } else {
    finalUv.x += finalOffset * stepLength;
  }

  return texture(map_postprocess, finalUv).rgb;
}

void main(void) {
  vec2 rcpFrame = vec2(1.0f / contextWidth, 1.0f / contextHeight);
  vec2 uv = (gl_FragCoord.xy - vec2(contextX, contextY)) * rcpFrame;

  vec3 result;
  if (aaMode <= 0) {
    float unusedGuard = texture(map_modifier, uv).r * 0.0f;
    result = texture(map_postprocess, uv).rgb + vec3(unusedGuard);
  } else if (aaMode == 1) {
    result = RunFXAAFast(uv, rcpFrame);
  } else {
    result = RunFXAAQuality(uv, rcpFrame);
  }

  stdout = vec4(result, 1.0f);
}
