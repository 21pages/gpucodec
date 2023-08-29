// Y
Texture2D g_txFrame0 : register(t0);
// U
Texture2D g_txFrame1 : register(t1);
// V
Texture2D g_txFrame2 : register(t2);

SamplerState g_Sam : register(s0);

SamplerComparisonState g_SamShadow : register(s1);

cbuffer CBChangesEveryInstanceDrawing : register(b0) {
  matrix g_World;
  matrix g_ColorCoef;
}

cbuffer CBDrawingStates : register(b1) {
  int g_TextureUsed;
  int g_EnableShadow;
  float2 g_Pad;
}

cbuffer CBChangesEveryFrame : register(b2) {
  // float2 u_ImgSize;//图像尺寸 (width, height)
  // float u_xOffset;//偏移量 1.0/width
  // float u_yOffset;//偏移量 1.0/height
  float4 g_ImageInfo;
  matrix g_View;
  // matrix g_ShadowTransform; // ShadowView * ShadowProj * T
  // float3 g_EyePosW;
  // float g_Pad2;
}

cbuffer CBChangesOnResize : register(b3) { matrix g_Proj; }

struct VertexImageIn {
  float3 Pos : POSITION;
  float2 Tex : TEXCOORD;
};

struct VertexPosNormalTex {
  float3 PosL : POSITION;
  float3 NormalL : NORMAL;
  float2 Tex : TEXCOORD;
};

struct VertexPosNormalTangentTex {
  float3 PosL : POSITION;
  float3 NormalL : NORMAL;
  float4 TangentL : TANGENT;
  float2 Tex : TEXCOORD;
};

struct InstancePosNormalTex {
  float3 PosL : POSITION;
  float3 NormalL : NORMAL;
  float2 Tex : TEXCOORD;
  matrix World : World;
  matrix WorldInvTranspose : WorldInvTranspose;
};

struct InstancePosNormalTangentTex {
  float3 PosL : POSITION;
  float3 NormalL : NORMAL;
  float4 TangentL : TANGENT;
  float2 Tex : TEXCOORD;
  matrix World : World;
  matrix WorldInvTranspose : WorldInvTranspose;
};

struct VertexImageOut {
  float4 Pos : SV_POSITION;
  float2 Tex : TEXCOORD0;
};

struct VertexOutBasic {
  float4 PosH : SV_POSITION;
  float3 PosW : POSITION;  // 在世界中的位置
  float3 NormalW : NORMAL; // 法向量在世界中的方向
  float2 Tex : TEXCOORD0;
  float4 ShadowPosH : TEXCOORD1;
};

struct VertexOutNormalMap {
  float4 PosH : SV_POSITION;
  float3 PosW : POSITION;    // 在世界中的位置
  float3 NormalW : NORMAL;   // 法向量在世界中的方向
  float4 TangentW : TANGENT; // 切线在世界中的方向
  float2 Tex : TEXCOORD0;
  float4 ShadowPosH : TEXCOORD1;
};

// Y =  0.299R + 0.587G + 0.114B
// U = -0.169R - 0.331G + 0.5B + 0.5
// V =  0.5R - 0.419G - 0.081B + 0.5
//
// static const float3 COEF_Y = float3(0.299, 0.587, 0.114);
// static const float3 COEF_U = float3(-0.169, -0.331, 0.5);       // + 0.5
// static const float3 COEF_V = float3(0.5, -0.419, -0.081);       // + 0.5
//
// R =  Y + 0.1.13983(V-0.5) + 0
// G = Y - 0.39465(U-0.5) -0.58060(V-0.5)
// B =  Y + 2.03211(U-0.5)
//
// static const float3 COEF_R = float3(1.0, -0.00093, 1.401687)    // Y
// static const float3 COEF_G = float3(1.0, -0.3437, -0.71417)     // U-128
// static const float3 COEF_B = float3(1.0, 1.77216, 0.00099)      // V-128

// YUV->RGB coefficients
// BT.601 (Y [16 .. 235], U/V [16 .. 240]) with linear, full-range RGB output.
// Input YUV must be first subtracted by (0.0625, 0.5, 0.5).
// static const float3x3 yuvCoef = {1.164f, 1.164f, 1.164f,  0.000f, -0.392f,
//				 2.017f, 1.596f, -0.813f, 0.000f};

// BT.709 (Y [16 .. 235], U/V [16 .. 240]) with linear, full-range RGB output.
// Input YUV must be first subtracted by (0.0625, 0.5, 0.5).
// static const float3x3 yuvCoef = {
//	1.164f,  1.164f, 1.164f,
//	0.000f, -0.213f, 2.112f,
//	1.793f, -0.533f, 0.000f};