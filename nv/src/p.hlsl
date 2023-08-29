#include "image_base.hlsli"

float4 main(VertexImageOut input) : SV_TARGET {
  float y = g_txFrame0.Sample(g_Sam, input.Tex).r;
  float2 uv = g_txFrame1.Sample(g_Sam, input.Tex).rg - float2(0.5f, 0.5f);
  float u = uv.x;
  float v = uv.y;
  float r = y + 1.14f * v;
  float g = y - 0.394f * u - 0.581f * v;
  float b = y + 2.03f * u;
  return float4(r, g, b, 1.0f);
}