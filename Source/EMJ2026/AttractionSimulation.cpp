#include "AttractionSimulation.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"

AAttractionSimulation::AAttractionSimulation()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("BoardRoot")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    SphereMesh = Mesh.Object;
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
    SuppressedPairs.Reset();
    Accumulator = 0;
    BuildVisuals();
}

void AAttractionSimulation::BuildVisuals()
{
    for (UStaticMeshComponent* Visual : NodeVisuals) if (Visual) Visual->DestroyComponent();
    NodeVisuals.Reset();
    for (const FAttractionNode& N : Nodes)
    {
        UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(this);
        Visual->SetStaticMesh(SphereMesh);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetMobility(EComponentMobility::Movable);
        Visual->SetupAttachment(RootComponent);
        Visual->RegisterComponent();
        Visual->SetRelativeScale3D(FVector(N.bPinned ? 0.5f : 0.3f));
        NodeVisuals.Add(Visual);
    }
    UpdateVisuals();
}

void AAttractionSimulation::UpdateVisuals()
{
    for (int32 I = 0; I < Nodes.Num() && I < NodeVisuals.Num(); ++I)
        NodeVisuals[I]->SetRelativeLocation(Nodes[I].Position);
}

bool AAttractionSimulation::SliceTether(int32 TetherIndex)
{
    if (!Tethers.IsValidIndex(TetherIndex)) return false;
    const FAttractionTether& T = Tethers[TetherIndex];
    SuppressedPairs.Add(PairKey(T.A,T.B));
    Tethers.RemoveAt(TetherIndex);
    return true;
}

int32 AAttractionSimulation::DropNode(FAttractionNode Profile, FVector LocalPosition)
{
    if (LocalPosition.ContainsNaN() || Nodes.Num() >= 128) return INDEX_NONE;
    Profile.Position = FVector(LocalPosition.X,LocalPosition.Y,0);
    Profile.Velocity = FVector::ZeroVector;
    Profile.bPinned = false;
    Profile.Mass = FMath::Max(0.01f,Profile.Mass);
    const int32 Index = Nodes.Add(Profile);
    BuildVisuals();
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
        const float Strength = FMath::Max(0.f,FieldStrength) / (Delta.SizeSquared() + FMath::Square(FMath::Max(1.f,Softening)));
        Forces[A] += Direction * Strength * Affinity(NA,NB);
        Forces[B] -= Direction * Strength * Affinity(NB,NA);
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
        const FVector Force = Direction * (FMath::Max(0.f,T.Rule.Stiffness)*(Delta.Size()-FMath::Max(1.f,T.Rule.RestLength)) + FMath::Max(0.f,T.Rule.Damping)*Speed);
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
    constexpr double FixedDt = 1.0/120.0;
    if (bRunning)
    {
        // Bound hitch catch-up to 16 steps, intentionally dropping excess wall time.
        Accumulator = FMath::Min(Accumulator + FMath::Max(0.f,DeltaSeconds),16*FixedDt);
        while (Accumulator >= FixedDt) { Step(float(FixedDt)); Accumulator -= FixedDt; }
    }
    UpdateVisuals();
    const FTransform Transform = GetActorTransform();
    for (const FAttractionTether& T : Tethers)
        DrawDebugLine(GetWorld(),Transform.TransformPosition(Nodes[T.A].Position),Transform.TransformPosition(Nodes[T.B].Position),FColor::Cyan,false,-1,0,3);
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        const FVector P = Transform.TransformPosition(Nodes[I].Position);
        DrawDebugString(GetWorld(),P+FVector(0,0,30),FString::Printf(TEXT("%d%s"),I,Nodes[I].bPinned ? TEXT(" PIN") : TEXT("")),nullptr,FColor::White,0,true);
    }
}
