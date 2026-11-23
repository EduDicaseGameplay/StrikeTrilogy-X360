// Presentation shaders of the Xbox 360 host layer (raylib_x360.cpp): one textured, tinted quad per draw call
// (DrawTexturePro, DrawRectangle with a white 1x1 texture). Positions arrive in clip space, prepared on the CPU.
struct VsIn
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VsOut
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VsOut VsMain(VsIn i)
{
    VsOut o;
    o.pos = i.pos;
    o.uv = i.uv;
    o.color = i.color;
    return o;
}

sampler2D g_tex : register(s0);

float4 PsMain(VsOut i) : COLOR0
{
    return tex2D(g_tex, i.uv) * i.color;
}
