Texture2D inputTex : register(t0);
SamplerState inputTexSampler : register(s0);

cbuffer convMode : register(b0) {
    float4x4 colorMat;
};

struct vs_in {
    float2 pos : POS;
};

struct ps_in {
    float4 pos : SV_Position;
    float2 texCoord : TEXCOORD;
};


ps_in vs_main(vs_in input) {
    float2 texCoord = input.pos * 0.5 + 0.5;
    texCoord.y = 1 - texCoord.y;

    ps_in output = (ps_in) 0;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.texCoord = texCoord;
    return output;
}


static const float3 multiplier = float3(219, 224, 224) / 255;
static const float3 offset = float3(16, 128, 128) / 255;

float4 ps_yuv(ps_in input) : SV_Target0 {
    float4 color = inputTex.Sample(inputTexSampler, input.texCoord);

    float3 ypbpr = mul(colorMat, color).xyz;
    float3 yuv = ypbpr * multiplier + offset;
    yuv = clamp(yuv, 0.0f, 1.0f);

    return float4(yuv.bgr, 1.0f);  // AYUV (=VUYA)
}

float ps_y(ps_in input) : SV_Target0 {
    float4 color = inputTex.Sample(inputTexSampler, input.texCoord);

    float y = mul(colorMat[0], color);
    y = y * multiplier[0] + offset[0];
    y = clamp(y, 0.0f, 1.0f);

    return y;
}

float2 ps_uv(ps_in input) : SV_Target0 {
    float4 color = inputTex.Sample(inputTexSampler, input.texCoord);

    float u = mul(colorMat[1], color);
    float v = mul(colorMat[2], color);
    
    float2 uv = float2(u, v) * multiplier[1] + offset[1];
    uv = clamp(uv, 0.0f, 1.0f);

    return uv;
}

float4 ps_copy(ps_in input) : SV_Target0 {
    return inputTex.Sample(inputTexSampler, input.texCoord);
}