// Custom expression body. DataTex is a 256x3 nearest-filtered RGBA32F texture.
// UV is derived from mesh-local XY, independent of the Engine Plane's UV winding.
float2 P = (UV - 0.5) * (2.0 * PlaneHalfExtent);
float Phi = 0.0;
float2 Gradient = float2(0.0, 0.0);
float NodeWeight = 0.0;
float EnergyField = 0.0;
int Nodes = clamp((int)NodeCount, 0, 256);
int Tethers = clamp((int)TetherCount, 0, 256);

[loop]
for (int i = 0; i < Nodes; ++i)
{
    float4 Node = Texture2DSampleLevel(DataTex, DataTexSampler,
        float2((i + 0.5) / 256.0, 0.5 / 3.0), 0);
    float2 D = P - Node.xy;
    float R2 = max(Node.z * Node.z, 0.0001);
    float Q = saturate(1.0 - dot(D, D) / R2);
    // Geometry stays visible even at zero internal energy. Row 0.w now carries
    // normalized charge/pulse/strain, used only for lighting contrast.
    float Kernel = Q * Q * Q;
    NodeWeight += Kernel;
    EnergyField += saturate(Node.w) * Kernel;
    // Phi = (1 - r^2/R^2)^3 inside support; value and first two
    // derivatives vanish at the boundary. No finite differences or raster grid.
    Phi += Kernel;
    Gradient += (-6.0 * Q * Q / R2) * D;
}

[loop]
for (int i = 0; i < Tethers; ++i)
{
    float X = (i + 0.5) / 256.0;
    float4 Ends = Texture2DSampleLevel(DataTex, DataTexSampler, float2(X, 1.5 / 3.0), 0);
    float4 Shape = Texture2DSampleLevel(DataTex, DataTexSampler, float2(X, 2.5 / 3.0), 0);
    float2 AB = Ends.zw - Ends.xy;
    float Length2 = dot(AB, AB);
    float RawT = dot(P - Ends.xy, AB) / max(Length2, 0.000001);
    float T = saturate(RawT);
    float2 GradT = (RawT > 0.0 && RawT < 1.0 && Length2 > 0.000001)
        ? AB / Length2 : float2(0.0, 0.0);
    float2 D = P - (Ends.xy + T * AB);
    float D2 = dot(D, D);
    float Pinch = saturate(Shape.y);
    // C(t)=16*t^2*(1-t)^2 pinches the neck center, with zero slope at caps.
    float Center = 16.0 * T * T * (1.0 - T) * (1.0 - T);
    float CenterDerivative = 32.0 * T * (1.0 - T) * (1.0 - 2.0 * T);
    float BaseRadius = max(Shape.x, 0.01);
    float RadiusFactor = 1.0 - Pinch * Center;
    float Radius = BaseRadius * max(0.02, RadiusFactor);
    float2 GradRadius = RadiusFactor > 0.02
        ? (-BaseRadius * Pinch * CenterDerivative) * GradT : float2(0.0, 0.0);
    float InvR2 = rcp(Radius * Radius);
    float Q = saturate(1.0 - D2 * InvR2);
    float Energy = 0.65 * (1.0 - Pinch);
    Phi += Energy * Q * Q * Q;
    // Includes dR/dx,dR/dy for the variable-width neck, not just capsule distance.
    float2 GradQ = -2.0 * D * InvR2 + (2.0 * D2 * InvR2 / Radius) * GradRadius;
    Gradient += 3.0 * Energy * Q * Q * GradQ;
}

float3 N = normalize(float3(-Gradient * SurfaceHeight, 1.0));
float3 Light = normalize(float3(-0.4, -0.5, 0.77));
float3 HalfVector = normalize(Light + float3(0.0, 0.0, 1.0));
float Diffuse = saturate(dot(N, Light));
float Specular = pow(saturate(dot(N, HalfVector)), 64.0);
float Rim = (1.0 - saturate(N.z)) * (1.0 - saturate(N.z));
float Meniscus = 1.0 - smoothstep(IsoThreshold, IsoThreshold + 0.3, Phi);
float CoreEnergy = saturate(EnergyField / max(NodeWeight, 0.00001));
float Liquid = saturate(0.10 + 0.33 * Diffuse + lerp(0.3, 0.8, CoreEnergy) * Specular
    + 0.18 * Rim - 0.10 * Meniscus + 0.18 * CoreEnergy);
// Derivatives are evaluated after both uniform-count loops, at native pixel resolution.
float AA = max(0.5 * fwidth(Phi), 0.00001);
float Coverage = smoothstep(IsoThreshold - AA, IsoThreshold + AA, Phi);
float TargetComplement = 1.0;
float3 Lobes[3] = { TargetLobe0, TargetLobe1, TargetLobe2 };
[unroll] for(int l=0;l<3;++l)
{
    float2 D=P-Lobes[l].xy;
    float Q=saturate(1-dot(D,D)/max(1,Lobes[l].z*Lobes[l].z));
    TargetComplement *= 1-Q*Q*(3-2*Q);
}
float Target=1-TargetComplement;
float Recess=0.22-0.065*Target;
float DiscDistance=length(P-RewardDisc.xy)/max(1,RewardDisc.z);
float Reward=RewardActive * (1-smoothstep(0.65,1.0,DiscDistance));
float DiscEdge=RewardActive * (1-smoothstep(0.008,0.025,abs(DiscDistance-1)));
float Backdrop=Recess+0.035*Reward+0.045*DiscEdge;
float Grey = lerp(Backdrop, Liquid, Coverage);
return float3(Grey, Grey, Grey);
