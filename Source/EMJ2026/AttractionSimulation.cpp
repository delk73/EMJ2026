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
        for (int32 I = 0; I < 6; ++I)
        {
            FAttractionNode N;
            N.Position = FVector((I % 3 - 1) * 145.f, (I / 3) * 150.f - 75.f, 0);
            N.bPinned = I == 0 || I == 2;
            N.Velocity = N.bPinned ? FVector::ZeroVector : FVector(Random.FRandRange(-30,30), Random.FRandRange(-40,40),0);
            // Node 5 responds oppositely to channel 0: nonreciprocal chase.
            if (I == 5) N.Receive = FVector(-0.6,0,0);
            Nodes.Add(N);
        }
    }
    for (FAttractionNode& N : Nodes)
    {
        N.Position.Z = 0;
        N.Velocity.Z = 0;
        N.Mass = FMath::Max(N.Mass,0.01f);
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
        Records[I] = FVector4f(float(Nodes[I].Position.X),float(Nodes[I].Position.Y),FMath::Max(20.f,DropletRadius),1.f);
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
        FMath::Abs(LocalPosition.X)>BoardHalfExtent || FMath::Abs(LocalPosition.Y)>BoardHalfExtent) return INDEX_NONE;
    Profile.Position = FVector(LocalPosition.X,LocalPosition.Y,0);
    Profile.Velocity = FVector::ZeroVector;
    Profile.bPinned = false;
    Profile.Mass = FMath::Max(0.01f,Profile.Mass);
    const int32 Index = Nodes.Add(Profile);
    return Index;
}

void AAttractionSimulation::Step(float Dt)
{
    TArray<FVector> Forces;
    Forces.Init(FVector::ZeroVector, Nodes.Num());
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
        const float CushionRange = FMath::Max(1.f,Softening);
        const float Pull = 80.f*(Distance/Range)*FMath::Exp(-0.5f*Delta.SizeSquared()/(Range*Range));
        const float Cushion = FMath::Max(0.f,CushionStrength)*FMath::Max(0.f,(NA.RepulsionScale+NB.RepulsionScale)*0.5f)*(Distance/CushionRange)*FMath::Exp(-0.5f*Delta.SizeSquared()/(CushionRange*CushionRange));
        const float EnergyScale = FMath::Max(0.f,FieldStrength)/800000.f;
        Forces[A] += Direction * EnergyScale*(Pull*Affinity(NA,NB)-Cushion);
        Forces[B] -= Direction * EnergyScale*(Pull*Affinity(NB,NA)-Cushion);
        const float ViscousWeight = FMath::Exp(-0.5f*Delta.SizeSquared()/(Range*Range));
        const FVector ViscousForce = (NB.Velocity-NA.Velocity)*FMath::Max(0.f,Viscosity)*ViscousWeight/FMath::Max(1.f,Nodes.Num()/6.f);
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
        const FVector Acceleration = (Forces[I]/FMath::Max(0.01f,N.Mass)).GetClampedToMaxSize(FMath::Max(1.f,MaxAcceleration));
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
    constexpr double FixedDt = 1.0/120.0;
    if (bRunning)
    {
        // Bound hitch catch-up to 16 steps, intentionally dropping excess wall time.
        Accumulator = FMath::Min(Accumulator + FMath::Max(0.f,DeltaSeconds),16*FixedDt);
        while (Accumulator >= FixedDt) { Step(float(FixedDt)); Accumulator -= FixedDt; }
    }
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
