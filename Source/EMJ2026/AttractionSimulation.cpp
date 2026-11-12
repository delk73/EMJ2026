#include "AttractionSimulation.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RHI.h"

AAttractionSimulation::AAttractionSimulation()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("BoardRoot")));
    LiquidPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LiquidPlane"));
    LiquidPlane->SetupAttachment(RootComponent);
    LiquidPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LiquidPlane->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
    LiquidPlane->SetStaticMesh(Mesh.Object);
    LiquidMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_LiquidHeightfield.M_LiquidHeightfield")));
    BondRules.Add(FAttractionBondRule());
}

uint64 AAttractionSimulation::PairKey(int32 A, int32 B)
{
    return (uint64(uint32(FMath::Min(A,B))) << 32) | uint32(FMath::Max(A,B));
}

void AAttractionSimulation::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ResetSimulation();
}

void AAttractionSimulation::BeginPlay()
{
    Super::BeginPlay();
    ResetSimulation();
}

void AAttractionSimulation::ResetSimulation()
{
    Nodes = InitialNodes;
    if (bUseDemo)
    {
        Nodes.Reset();
        FRandomStream Random(Seed);
        for (int32 I = 0; I < 9; ++I)
        {
            const auto Type = static_cast<EAttractionArchetype>(I%3);
            FAttractionNode N = MakeArchetype(Type);
            const float Angle = FMath::DegreesToRadians((I/3)*120.f+(I%3)*25.f);
            const float Radius = I%3 == 0 ? 260.f : I%3 == 1 ? 155.f : 90.f;
            N.Position = FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);
            N.Velocity = FVector(-FMath::Sin(Angle),FMath::Cos(Angle),0)*(I%3 == 0 ? 75.f : 25.f);
            N.Velocity += FVector(Random.FRandRange(-8,8),Random.FRandRange(-8,8),0);
            N.bPinned = I == 2;
            if (Type == EAttractionArchetype::Buffer) N.InternalEnergy = Random.FRandRange(0.6f,0.75f);
            Nodes.Add(N);
        }
    }
    for (FAttractionNode& N : Nodes)
    {
        N.Position.Z = 0;
        N.Velocity.Z = 0;
        N.Mass = FMath::Max(N.Mass,0.01f);
        N.Valence = FMath::Clamp(N.Valence,0,N.Archetype == EAttractionArchetype::Anchor ? 4 : 3);
        N.InternalEnergy = FMath::Clamp(N.InternalEnergy,0.f,1.f);
        N.PulseTimeRemaining = N.PulseCooldown = 0;
        if (N.bPinned) N.Velocity = FVector::ZeroVector;
    }
    Tethers.Reset();
    PinchingFilaments.Reset();
    SuppressedPairs.Reset();
    Accumulator = 0;
    BuildVisuals();
}

void AAttractionSimulation::BuildVisuals()
{
    LiquidPlane->SetRelativeScale3D(FVector(PlaneHalfExtent*2/100.f,PlaneHalfExtent*2/100.f,1));
    // The GPU evaluates the field. This texture contains only float primitive records.
    if (!LiquidDataTexture && !IsRunningCommandlet())
    {
        LiquidDataTexture = UTexture2D::CreateTransient(256,3,PF_A32B32G32R32F);
        LiquidDataTexture->SRGB = false;
        LiquidDataTexture->Filter = TF_Nearest;
        LiquidDataTexture->AddressX = TA_Clamp;
        LiquidDataTexture->AddressY = TA_Clamp;
        LiquidDataTexture->NeverStream = true;
#if WITH_EDITORONLY_DATA
        LiquidDataTexture->MipGenSettings = TMGS_NoMipmaps;
#endif
        LiquidDataTexture->UpdateResource();
    }
    if (!LiquidMID)
        if (UMaterialInterface* Parent = LiquidMaterialAsset.LoadSynchronous())
            LiquidMID = UMaterialInstanceDynamic::Create(Parent,this);
    if (LiquidMID)
    {
        if (LiquidDataTexture) LiquidMID->SetTextureParameterValue(TEXT("DataTex"),LiquidDataTexture);
        LiquidMID->SetScalarParameterValue(TEXT("PlaneHalfExtent"),PlaneHalfExtent);
        LiquidPlane->SetMaterial(0,LiquidMID);
    }
    UpdateLiquidSurface();
}

void AAttractionSimulation::UpdateLiquidSurface()
{
    if (!LiquidDataTexture || !LiquidDataTexture->GetResource() || !LiquidMID || GUsingNullRHI) return;
    constexpr int32 Width = 256;
    // 12 KB backing allocation, but only occupied row spans are uploaded (16 bytes/record).
    // Each queued upload owns its storage until the RHI-thread cleanup callback.
    FVector4f* Records = new FVector4f[Width*3];
    const int32 NodeCount = FMath::Min(Nodes.Num(),Width);
    for (int32 I=0; I<NodeCount; ++I)
    {
        const float Energy = Nodes[I].PulseTimeRemaining>0 ? 1.f : FMath::Clamp(Nodes[I].InternalEnergy,0.f,1.f);
        Records[I] = FVector4f(float(Nodes[I].Position.X),float(Nodes[I].Position.Y),FMath::Max(20.f,DropletRadius),Energy);
    }
    int32 TetherCount = 0;
    auto AddTether = [&](const FVector& A,const FVector& B,float Radius,float PinchAlpha)
    {
        if (TetherCount >= Width) return;
        Records[Width+TetherCount] = FVector4f(float(A.X),float(A.Y),float(B.X),float(B.Y));
        Records[Width*2+TetherCount] = FVector4f(Radius,FMath::Clamp(PinchAlpha,0.f,1.f),0,0);
        ++TetherCount;
    };
    for (const FAttractionTether& T : Tethers)
    {
        if (!Nodes.IsValidIndex(T.A) || !Nodes.IsValidIndex(T.B)) continue;
        const FVector A = Nodes[T.A].Position, B = Nodes[T.B].Position;
        const float Strain = FMath::Max(0.f,float(FVector::Distance(A,B))/FMath::Max(1.f,T.Rule.RestLength)-1);
        const float Stretch = FMath::Clamp(Strain/FMath::Max(0.01f,T.Rule.BreakStrain),0.f,1.f);
        if (T.A<NodeCount) Records[T.A].W = FMath::Max(Records[T.A].W,Stretch);
        if (T.B<NodeCount) Records[T.B].W = FMath::Max(Records[T.B].W,Stretch);
        const float Pinch = Stretch*Stretch*(3-2*Stretch);
        AddTether(A,B,FMath::Max(10.f,FilamentRadius)*(1.f-0.88f*Pinch),Pinch);
    }
    // Live tethers take priority if an authored board exceeds the 256-record render capacity.
    for (const FPinchingFilament& Cut : PinchingFilaments)
    {
        const float Fade = 1.f-FMath::Clamp(Cut.Age/0.22f,0.f,1.f);
        AddTether(Cut.A,Cut.B,Cut.Radius,1.f-(Cut.Amplitude/0.65f)*Fade);
    }
    LiquidMID->SetScalarParameterValue(TEXT("NodeCount"),NodeCount);
    LiquidMID->SetScalarParameterValue(TEXT("TetherCount"),TetherCount);
    LiquidMID->SetScalarParameterValue(TEXT("PlaneHalfExtent"),PlaneHalfExtent);
    LiquidMID->SetScalarParameterValue(TEXT("IsoThreshold"),FMath::Max(0.01f,IsoThreshold));
    LiquidMID->SetScalarParameterValue(TEXT("SurfaceHeight"),FMath::Max(0.1f,SurfaceHeight));
    FUpdateTextureRegion2D* Regions = new FUpdateTextureRegion2D[3];
    uint32 RegionCount = 0;
    if (NodeCount>0) Regions[RegionCount++] = FUpdateTextureRegion2D(0,0,0,0,NodeCount,1);
    if (TetherCount>0)
    {
        Regions[RegionCount++] = FUpdateTextureRegion2D(0,1,0,1,TetherCount,1);
        Regions[RegionCount++] = FUpdateTextureRegion2D(0,2,0,2,TetherCount,1);
    }
    // Counts hide stale records after cuts/reset; zero-count rows need no upload.
    if (RegionCount == 0) { delete[] Records; delete[] Regions; return; }
    LiquidDataTexture->UpdateTextureRegions(0,RegionCount,Regions,Width*sizeof(FVector4f),sizeof(FVector4f),reinterpret_cast<uint8*>(Records),
        [](uint8* Data,const FUpdateTextureRegion2D* UploadedRegions)
        { delete[] reinterpret_cast<FVector4f*>(Data); delete[] UploadedRegions; });
}

bool AAttractionSimulation::SliceTether(int32 TetherIndex)
{
    if (!Tethers.IsValidIndex(TetherIndex)) return false;
    const FAttractionTether& T = Tethers[TetherIndex];
    if (Nodes.IsValidIndex(T.A) && Nodes.IsValidIndex(T.B))
    {
        if (PinchingFilaments.Num() >= 128) PinchingFilaments.RemoveAt(0);
        const float Strain = FMath::Max(0.f,float(FVector::Distance(Nodes[T.A].Position,Nodes[T.B].Position))/FMath::Max(1.f,T.Rule.RestLength)-1);
        const float Stretch = FMath::Clamp(Strain/FMath::Max(0.01f,T.Rule.BreakStrain),0.f,1.f);
        const float Neck = 1.f-Stretch*Stretch*(3-2*Stretch);
        PinchingFilaments.Add({Nodes[T.A].Position,Nodes[T.B].Position,
            FMath::Max(10.f,FilamentRadius)*(0.12f+0.88f*Neck),0.65f*Neck,0});
    }
    SuppressedPairs.Add(PairKey(T.A,T.B));
    Tethers.RemoveAt(TetherIndex);
    return true;
}

int32 AAttractionSimulation::DropNode(FAttractionNode Profile, FVector LocalPosition)
{
    if (LocalPosition.ContainsNaN() || Nodes.Num() >= 128 ||
        !IsInsidePlayfield(LocalPosition)) return INDEX_NONE;
    Profile.Position = FVector(LocalPosition.X,LocalPosition.Y,0);
    Profile.Velocity = FVector::ZeroVector;
    Profile.bPinned = false;
    Profile.Mass = FMath::Max(0.01f,Profile.Mass);
    Profile.Valence = FMath::Clamp(Profile.Valence,0,Profile.Archetype == EAttractionArchetype::Anchor ? 4 : 3);
    Profile.InternalEnergy = Profile.PulseTimeRemaining = Profile.PulseCooldown = 0;
    const int32 Index = Nodes.Add(Profile);
    return Index;
}

FAttractionNode AAttractionSimulation::MakeArchetype(EAttractionArchetype Type)
{
    FAttractionNode N;
    N.Archetype = Type;
    N.CaptureRadius = 320;
    if (Type == EAttractionArchetype::Driver) { N.Mass = 0.8f; N.Valence = 2; N.RepulsionScale = 0.8f; }
    if (Type == EAttractionArchetype::Buffer) { N.Mass = 1.2f; N.Valence = 3; N.RepulsionScale = 1.5f; }
    if (Type == EAttractionArchetype::Anchor) { N.Mass = 2.f; N.Valence = 4; N.RepulsionScale = 0.4f; }
    return N;
}

bool AAttractionSimulation::IsInsidePlayfield(const FVector& P) const
{
    return !P.ContainsNaN() && FMath::Abs(P.X)<=FMath::Max(400.f,PlayfieldHalfExtentX) && FMath::Abs(P.Y)<=FMath::Max(400.f,PlayfieldHalfExtentY);
}

bool AAttractionSimulation::ToggleNearestPin(FVector P)
{
    int32 Best = INDEX_NONE;
    double Distance2 = 140*140;
    for (int32 I=0; I<Nodes.Num(); ++I)
    {
        const double D = FVector::DistSquared2D(P,Nodes[I].Position);
        if (D<=Distance2) { Distance2=D; Best=I; }
    }
    if (Best==INDEX_NONE) return false;
    Nodes[Best].bPinned = !Nodes[Best].bPinned;
    Nodes[Best].Velocity = FVector::ZeroVector;
    return true;
}

void AAttractionSimulation::Stir(FVector Start,FVector End)
{
    if (Start.ContainsNaN() || End.ContainsNaN()) return;
    Start.Z = End.Z = 0;
    const FVector Impulse = ((End-Start)*2.f).GetClampedToMaxSize(240.f);
    for (FAttractionNode& N : Nodes)
    {
        if (N.bPinned) continue;
        const FVector Closest = FMath::ClosestPointOnSegment(N.Position,Start,End);
        const float Q = FMath::Max(0.f,1.f-float(FVector::DistSquared(N.Position,Closest))/(350.f*350.f));
        N.Velocity = (N.Velocity+Impulse*Q*Q/FMath::Max(0.01f,N.Mass)).GetClampedToMaxSize(FMath::Max(1.f,MaxSpeed));
    }
}

void AAttractionSimulation::AdvanceSimulation(float DeltaSeconds)
{
    if (!bRunning || !FMath::IsFinite(DeltaSeconds)) return;
    constexpr double FixedDt = 1.0/120.0;
    Accumulator = FMath::Min(Accumulator+FMath::Max(0.f,DeltaSeconds),16*FixedDt);
    while (Accumulator>=FixedDt) { Step(float(FixedDt)); Accumulator -= FixedDt; }
}

void AAttractionSimulation::Step(float Dt)
{
    TArray<FVector> Forces;
    Forces.Init(FVector::ZeroVector, Nodes.Num());
    // Density-driven relaxation oscillator: charge, 0.35 s pulse, then refractory cooldown.
    for (int32 I=0; I<Nodes.Num(); ++I)
    {
        FAttractionNode& N = Nodes[I];
        if (N.Archetype != EAttractionArchetype::Buffer) { N.InternalEnergy *= FMath::Exp(-Dt); continue; }
        N.PulseTimeRemaining = FMath::Max(0.f,N.PulseTimeRemaining-Dt);
        N.PulseCooldown = FMath::Max(0.f,N.PulseCooldown-Dt);
        if (N.PulseTimeRemaining>0) { N.InternalEnergy=1; continue; }
        if (N.PulseCooldown>0) { N.InternalEnergy=FMath::Max(0.f,N.InternalEnergy-1.5f*Dt); continue; }
        float Density = 0;
        for (int32 J=0; J<Nodes.Num(); ++J)
            if (I!=J) Density += FMath::Exp(-float(FVector::DistSquared(N.Position,Nodes[J].Position))/(300.f*300.f));
        N.InternalEnergy = FMath::Clamp(N.InternalEnergy+Dt*(Density>1.3f ? 0.9f*(Density-1.3f) : -0.5f),0.f,1.f);
        if (N.InternalEnergy>=1) { N.PulseTimeRemaining=0.35f; N.PulseCooldown=1.85f; }
    }
    TArray<int32> Degree;
    Degree.Init(0,Nodes.Num());
    TSet<uint64> Existing;
    for (int32 I = Tethers.Num()-1; I >= 0; --I)
    {
        const FAttractionTether& T = Tethers[I];
        if (!Nodes.IsValidIndex(T.A) || !Nodes.IsValidIndex(T.B)) { Tethers.RemoveAt(I); continue; }
        const float Distance = FVector::Distance(Nodes[T.A].Position,Nodes[T.B].Position);
        if (Distance > FMath::Max(1.f,T.Rule.RestLength) * (1.f + FMath::Max(0.f,T.Rule.BreakStrain)))
        {
            SuppressedPairs.Add(PairKey(T.A,T.B));
            Tethers.RemoveAt(I);
            continue;
        }
        ++Degree[T.A]; ++Degree[T.B]; Existing.Add(PairKey(T.A,T.B));
    }
    for (int32 A = 0; A < Nodes.Num(); ++A)
    for (int32 B = A+1; B < Nodes.Num(); ++B)
    {
        const FAttractionNode& NA = Nodes[A]; const FAttractionNode& NB = Nodes[B];
        const FVector Delta = NB.Position - NA.Position;
        const float Distance = Delta.Size();
        const FVector Direction = Delta.GetSafeNormal();
        auto Affinity = [this](const FAttractionNode& Receiver, const FAttractionNode& Emitter)
        {
            return FVector::DotProduct(Receiver.Receive,FVector(
                FVector::DotProduct(ChannelRow0,Emitter.Emit),
                FVector::DotProduct(ChannelRow1,Emitter.Emit),
                FVector::DotProduct(ChannelRow2,Emitter.Emit)));
        };
        // Difference of Gaussian potentials: analytic smooth radial gradients, zero at overlap.
        // The cushion is reciprocal; emit/receive affinity still permits directional field work.
        const float Range = FMath::Max(1.f,PullRange);
        const bool HasBuffer = NA.Archetype==EAttractionArchetype::Buffer || NB.Archetype==EAttractionArchetype::Buffer;
        const float CushionRange = FMath::Max(1.f,Softening)*(HasBuffer ? 1.6f : 1.f);
        const float Pull = 80.f*(Distance/Range)*FMath::Exp(-0.5f*Delta.SizeSquared()/(Range*Range));
        const float Cushion = FMath::Max(0.f,CushionStrength)*FMath::Max(0.f,(NA.RepulsionScale+NB.RepulsionScale)*0.5f)*(Distance/CushionRange)*FMath::Exp(-0.5f*Delta.SizeSquared()/(CushionRange*CushionRange));
        const float EnergyScale = FMath::Max(0.f,FieldStrength)/800000.f;
        // Rows receive, columns emit: deliberately nonreciprocal. No global orbit controller.
        static constexpr float Coupling[3][3] = {{-1.2f,1.5f,2.2f},{0.45f,0.4f,0.5f},{0.35f,0.25f,0.f}};
        auto Radial = [&](const FAttractionNode& Receiver,const FAttractionNode& Emitter)
        {
            float F = Pull*Coupling[int32(Receiver.Archetype)][int32(Emitter.Archetype)]*Affinity(Receiver,Emitter)-Cushion;
            if (Receiver.Archetype==EAttractionArchetype::Anchor)
                F += 210.f*(Distance/90.f)*FMath::Exp(-0.5f*Delta.SizeSquared()/(90.f*90.f));
            return F;
        };
        Forces[A] += Direction*EnergyScale*Radial(NA,NB);
        Forces[B] -= Direction*EnergyScale*Radial(NB,NA);
        const float Pulse = 900.f*(Distance/240.f)*FMath::Exp(-0.5f*Delta.SizeSquared()/(450.f*450.f));
        if (NB.PulseTimeRemaining>0) Forces[A] -= Direction*Pulse;
        if (NA.PulseTimeRemaining>0) Forces[B] += Direction*Pulse;
        const float ViscousWeight = FMath::Exp(-0.5f*Delta.SizeSquared()/(Range*Range));
        const float DampingGain = (NA.Archetype==EAttractionArchetype::Anchor || NB.Archetype==EAttractionArchetype::Anchor) ? 3.f : 1.f;
        const FVector ViscousForce = (NB.Velocity-NA.Velocity)*FMath::Max(0.f,Viscosity)*DampingGain*ViscousWeight/FMath::Max(1.f,Nodes.Num()/6.f);
        Forces[A] += ViscousForce; Forces[B] -= ViscousForce;
        const uint64 Key = PairKey(A,B);
        if (Existing.Contains(Key) || SuppressedPairs.Contains(Key) ||
            Degree[A] >= NA.Valence || Degree[B] >= NB.Valence ||
            Distance > FMath::Min(NA.CaptureRadius,NB.CaptureRadius)) continue;
        for (const FAttractionBondRule& Rule : BondRules)
        {
            if (!((NA.BondType == Rule.TypeA && NB.BondType == Rule.TypeB) ||
                  (NA.BondType == Rule.TypeB && NB.BondType == Rule.TypeA))) continue;
            if ((NB.Velocity-NA.Velocity).Size() > Rule.MaxRelativeSpeed ||
                Distance > FMath::Max(1.f,Rule.RestLength)*(1.f+FMath::Max(0.f,Rule.BreakStrain))) continue;
            FAttractionTether T; T.A = A; T.B = B; T.Rule = Rule;
            if (NA.Archetype==EAttractionArchetype::Driver || NB.Archetype==EAttractionArchetype::Driver)
            {
                T.Rule.Stiffness *= 0.55f;
                T.Rule.BreakStrain = FMath::Max(1.8f,T.Rule.BreakStrain);
            }
            Tethers.Add(T); Existing.Add(Key); ++Degree[A]; ++Degree[B]; break;
        }
    }
    for (const FAttractionTether& T : Tethers)
    {
        const FVector Delta = Nodes[T.B].Position-Nodes[T.A].Position;
        const FVector Direction = Delta.GetSafeNormal();
        const float Speed = FVector::DotProduct(Nodes[T.B].Velocity-Nodes[T.A].Velocity,Direction);
        const float Rest = FMath::Max(1.f,T.Rule.RestLength);
        const float Extension = Delta.Size()-Rest;
        const float SoftExtension = Extension/FMath::Sqrt(1.f+FMath::Square(Extension/Rest));
        const FVector Force = Direction * (FMath::Max(0.f,T.Rule.Stiffness)*SoftExtension + FMath::Max(0.f,T.Rule.Damping)*Speed);
        Forces[T.A] += Force; Forces[T.B] -= Force;
    }
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        FAttractionNode& N = Nodes[I];
        if (N.bPinned) { N.Velocity = FVector::ZeroVector; continue; }
        // Soft inward spring plus outward-velocity damping in a border band, without teleporting.
        auto Boundary = [&](float Position,float Velocity,float Extent)
        {
            const float Depth = FMath::Max(0.f,FMath::Abs(Position)-FMath::Max(100.f,Extent-FMath::Max(50.f,BoundarySoftness)));
            const float Sign = FMath::Sign(Position);
            return -Sign*(2.5f*Depth+3.f*FMath::Max(0.f,Sign*Velocity)*FMath::Clamp(Depth/FMath::Max(50.f,BoundarySoftness),0.f,1.f));
        };
        const FVector BoundaryAcceleration(Boundary(N.Position.X,N.Velocity.X,PlayfieldHalfExtentX),Boundary(N.Position.Y,N.Velocity.Y,PlayfieldHalfExtentY),0);
        const float KineticGain = N.Archetype==EAttractionArchetype::Driver ? 1.45f : 1.f;
        const FVector Acceleration = (Forces[I]*KineticGain/FMath::Max(0.01f,N.Mass)+BoundaryAcceleration).GetClampedToMaxSize(FMath::Max(1.f,MaxAcceleration));
        N.Velocity = ((N.Velocity + Acceleration*Dt)*FMath::Exp(-FMath::Max(0.f,Drag)*Dt)).GetClampedToMaxSize(FMath::Max(1.f,MaxSpeed));
        N.Position += N.Velocity*Dt;
    }
}

void AAttractionSimulation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    for (int32 I=PinchingFilaments.Num()-1; I>=0; --I)
    {
        PinchingFilaments[I].Age += FMath::Max(0.f,DeltaSeconds);
        if (PinchingFilaments[I].Age>=0.22f) PinchingFilaments.RemoveAt(I);
    }
    AdvanceSimulation(DeltaSeconds);
    UpdateLiquidSurface();
    const FTransform Transform = GetActorTransform();
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        if (!Nodes[I].bPinned) continue;
        const FVector P = Transform.TransformPosition(Nodes[I].Position+FVector(0,0,2));
        DrawDebugCircle(GetWorld(),P,22,32,FColor::White,false,-1,0,1,
            Transform.GetUnitAxis(EAxis::X),Transform.GetUnitAxis(EAxis::Y),false);
    }
}
