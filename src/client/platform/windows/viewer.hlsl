Texture2D inputTex : register(t0);
SamplerState inputTexSampler : register(s0);


cbuffer box : register(b0) {
    // [x, y, w, h]
    float4 boxInfo;
}

struct vs_in {
    float2 pos : POS;
};

struct ps_in {
    float4 pos : SV_Position;
    float2 texCoord : TEXCOORD;
};


ps_in vs_full(vs_in input) {
    float2 texCoord = input.pos * 0.5 + 0.5;
    texCoord.y = 1 - texCoord.y;

    ps_in output = (ps_in)0;
    output.pos = float4(input.pos, 0.0, 1.0);
    output.texCoord = texCoord;
    return output;
}


ps_in vs_box(vs_in input) {
    float2 texCoord = input.pos * 0.5 + 0.5;
    texCoord.y = 1 - texCoord.y;

    float2 outCoord = texCoord * boxInfo.zw + boxInfo.xy;
    outCoord.y = 1 - outCoord.y;
    outCoord = outCoord * 2 - 1;

    ps_in output = (ps_in)0;
    output.pos = float4(outCoord, 0.0, 1.0);
    output.texCoord = texCoord;
    return output;
}


float4 ps_main(ps_in input) : SV_Target0 {
    return inputTex.Sample(inputTexSampler, input.texCoord);
}