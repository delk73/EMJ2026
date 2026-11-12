// Native-resolution UI silhouettes; no icon textures or external UMG assets.
struct GlyphSDF
{
    float Circle(float2 p,float2 center,float radius) { return length(p-center)-radius; }
    float Capsule(float2 p,float2 a,float2 b,float radius)
    {
        float2 ab=b-a;
        float t=saturate(dot(p-a,ab)/max(dot(ab,ab),0.00001));
        return length(p-a-ab*t)-radius;
    }
    float SMin(float a,float b,float k)
    {
        float h=saturate(0.5+0.5*(b-a)/k);
        return lerp(b,a,h)-k*h*(1-h);
    }
    float Shape(float2 p,float index)
    {
        if (index<0.5)
        {
            float d=SMin(Circle(p,float2(-0.19,0.10),0.34),Circle(p,float2(0.29,-0.18),0.21),0.19);
            d=SMin(d,Capsule(p,float2(-0.13,0.19),float2(0.48,-0.32),0.085),0.12);
            return SMin(d,abs(length(p-float2(0.02,-0.02))-0.53)-0.027,0.065);
        }
        if (index<1.5)
        {
            float d=Circle(p,float2(0,0),0.17);
            d=SMin(d,abs(length(p)-0.34)-0.055,0.065);
            return SMin(d,abs(length(p)-0.56)-0.045,0.065);
        }
        float d=SMin(Capsule(p,float2(-0.36,0),float2(0.36,0),0.10),
            Capsule(p,float2(0,-0.36),float2(0,0.36),0.10),0.12);
        d=SMin(d,Circle(p,float2(-0.36,0),0.19),0.10);
        d=SMin(d,Circle(p,float2(0.36,0),0.19),0.10);
        d=SMin(d,Circle(p,float2(0,-0.36),0.19),0.10);
        return SMin(d,Circle(p,float2(0,0.36),0.19),0.10);
    }
};
GlyphSDF SDF;
float Hover=saturate(HoverAlpha), Drag=saturate(DragAlpha);
float2 P=(UV-0.5)*2.0/(1.0+0.05*Hover+0.07*Drag);
float D=SDF.Shape(P,GlyphIndex);
float AA=max(fwidth(D)*0.5,0.0001);
float Coverage=1.0-smoothstep(-AA,AA,D);
float2 E=float2(0.001,0);
float2 G=float2(SDF.Shape(P+E.xy,GlyphIndex)-SDF.Shape(P-E.xy,GlyphIndex),
    SDF.Shape(P+E.yx,GlyphIndex)-SDF.Shape(P-E.yx,GlyphIndex))/0.002;
float Bevel=1.0-smoothstep(0.0,0.12,-D);
float3 N=normalize(float3(G*Bevel,0.8));
float Diffuse=saturate(dot(N,normalize(float3(-0.4,-0.6,0.8))));
float Grey=saturate(0.28+0.35*Diffuse+0.10*Hover+0.12*Drag+0.12*Bevel);
return float4(Grey,Grey,Grey,Coverage);
