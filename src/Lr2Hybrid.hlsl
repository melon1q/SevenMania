#define A_GPU 1
#define A_HLSL 1
#define FSR_EASU_F 1
#include "ffx_a.h"
#include "ffx_fsr1.h"

Texture2D<float4> Source : register(t0);
SamplerState ClampSampler : register(s0);
cbuffer Geometry : register(b0) {
    float2 inputSize;
    float2 outputOrigin;
    float2 outputSize;
    float2 padding;
};

AF4 FsrEasuRF(AF2 p) { return Source.GatherRed(ClampSampler, p); }
AF4 FsrEasuGF(AF2 p) { return Source.GatherGreen(ClampSampler, p); }
AF4 FsrEasuBF(AF2 p) { return Source.GatherBlue(ClampSampler, p); }

float4 vertexMain(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? -3 : 1, 0, 1);
}

float4 cubicWeights(float t) {
    return float4(-0.5*t + t*t - 0.5*t*t*t, 1 - 2.5*t*t + 1.5*t*t*t,
                  0.5*t + 2*t*t - 1.5*t*t*t, -0.5*t*t + 0.5*t*t*t);
}

float4 pixelMain(float4 screen : SV_Position) : SV_Target {
    float2 position = (screen.xy - outputOrigin) * inputSize / outputSize - 0.5;
    int2 origin = int2(floor(position));
    float4 wx = cubicWeights(frac(position.x)), wy = cubicWeights(frac(position.y));
    float3 cubic = 0, low = 1, high = 0;
    float luminance[16];
    [unroll] for (int y = 0; y < 4; ++y) {
        [unroll] for (int x = 0; x < 4; ++x) {
            int2 pixel = clamp(origin + int2(x-1, y-1), int2(0,0), int2(inputSize)-1);
            float3 sampleColor = Source.Load(int3(pixel,0)).rgb;
            cubic += sampleColor * wx[x] * wy[y];
            low = min(low,sampleColor);
            high = max(high,sampleColor);
            luminance[y*4+x] = dot(sampleColor, float3(0.2126,0.7152,0.0722));
        }
    }
    // Analyze the same source pixels as the cubic kernel. Compare neighboring
    // Scharr gradients to favor coherent edges over texture and isolated specks.
    float2 direction = 0;
    float totalStrength = 0;
    [unroll] for (int j = 0; j < 2; ++j) {
        [unroll] for (int i = 0; i < 2; ++i) {
            int k = j*4+i;
            float a = luminance[k], b = luminance[k+1], c = luminance[k+2];
            float d = luminance[k+4], f = luminance[k+6];
            float g = luminance[k+8], h = luminance[k+9], m = luminance[k+10];
            float2 gradient = float2(3*(c-a+m-g)+10*(f-d), 3*(g-a+m-c)+10*(h-b))/16;
            direction += gradient;
            totalStrength += length(gradient);
        }
    }
    float contrast = dot(high-low,float3(0.2126,0.7152,0.0722));
    float weight = smoothstep(0.045,0.25,totalStrength*0.25)
                 * smoothstep(0.45,0.85,length(direction)/max(totalStrength,1e-5))
                 * smoothstep(0.06,0.28,contrast);
    [branch] if (weight > 0.001) {
        uint4 con0, con1, con2, con3;
        FsrEasuCon(con0,con1,con2,con3,inputSize.x,inputSize.y,inputSize.x,inputSize.y,outputSize.x,outputSize.y);
        con0.zw = asuint(asfloat(con0.zw) - outputOrigin * inputSize / outputSize);
        float3 easu;
        FsrEasuF(easu,uint2(screen.xy),con0,con1,con2,con3);
        cubic = lerp(cubic,easu,weight);
    }
    return float4(clamp(cubic,low,high),1);
}
