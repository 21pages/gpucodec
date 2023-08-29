#include "image_base.hlsli"
VertexImageOut main(VertexImageIn input) {
  VertexImageOut output;
  float4 pos = float4(input.Pos, 1.0f);
  pos = mul(pos, g_World);
  pos = mul(pos, g_View);
  pos = mul(pos, g_Proj);
  output.Pos = pos;
  output.Tex = input.Tex;

  return output;
}