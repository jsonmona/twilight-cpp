Texture2D desktopTex : register(t0);
Texture2D cursorTex : register(t1);

SamplerState DesktopTextureSampler : register(s0);
SamplerState CursorTextureSampler : register(s1);


cbuffer CursorInfo : register(b0) {
    float2 CursorPos;
    float2 CursorSize;
    uint FlagCursorVisible;
    uint FlagCursorXOR;
}

struct ps_in {
    float4 pos : SV_Position;
    float2 desktopTexCoord : TEXCOORD0;
    float2 cursorTexCoord : TEXCOORD1;
};


ps_in vs_fullscreen(uint id : SV_VertexID) {
    float2 texcoord;
    texcoord.x = (id == 1) ? 2.0 : 0.0;
    texcoord.y = (id == 2) ? 2.0 : 0.0;

    float2 cursorCoord = texcoord;
    if (FlagCursorVisible != 0) {
        cursorCoord -= CursorPos;
        cursorCoord /= CursorSize;
    }

    ps_in output;
    output.pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
    output.desktopTexCoord = texcoord;
    output.cursorTexCoord = cursorCoord;
    return output;
}


float4 ps_desktop(ps_in input) : SV_Target0 {
    float4 color = desktopTex.Sample(DesktopTextureSampler, input.desktopTexCoord);

    if (FlagCursorVisible != 0) {
        float4 cursor = cursorTex.Sample(CursorTextureSampler, input.cursorTexCoord);

        if (FlagCursorXOR != 0) {
            uint3 intColor = color.rgb * 255.5f;
            uint3 intCursor = cursor.rgb * 255.5f;
            cursor.rgb = (intColor ^ intCursor) / 255.0f;
        }

        float srcAlpha = cursor.a;
        float dstAlpha = color.a - color.a * cursor.a;  // FMA optimize color.a * (1 - cursor.a)
        color.a = srcAlpha + dstAlpha;
        color.rgb = (cursor.rgb * srcAlpha + color.rgb * dstAlpha) / color.a;
    }

    return color;
}
