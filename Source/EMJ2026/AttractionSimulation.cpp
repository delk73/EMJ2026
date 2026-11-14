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
    LiquidMaterialAsset = TSoftObjectPtr<UMaterialInterface>(
        FSoftObjectPath(TEXT("/Game/Materials/M_LiquidHeightfield.M_LiquidHeightfield")));
    BondRules.Add(FAttractionBondRule());
}

uint64 AAttractionSimulation::PairKey(int32 A, int32 B)
{
    return (uint64(uint32(FMath::Min(A, B))) << 32) | uint32(FMath::Max(A, B));
}

void AAttractionSimulation::OnConstruction(const FTransform &Transform)
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
    Tethers.Reset();
    PinchingFilaments.Reset();
    SuppressedPairs.Reset();
    Accumulator = 0;
    DraggedPatch.Reset();
    PatchStartPositions.Reset();
    PatchTargetOffset = PatchCurrentOffset = FVector::ZeroVector;
    bRewardActive = false;
    if (Stages.IsEmpty())
    {
        FAttractionStageConfig A;
        A.Lobes = {FVector(-900, 0, 1600), FVector(900, 350, 1450), FVector(300, -900, 1100)};
        A.RootAnchors = {FVector2D(-1300, 0)};
        FAttractionStageConfig B;
        B.Lobes = {FVector(-1500, -550, 1300), FVector(0, 300, 1000), FVector(1550, 650, 1400)};
        B.RootAnchors = {FVector2D(-1700, -550)};
        FAttractionStageConfig C;
        C.Lobes = {FVector(-1300, 650, 1400), FVector(1250, 650, 1400), FVector(0, -1050, 1500)};
        C.RootAnchors = {FVector2D(0, -1300)};
        Stages = {A, B, C};
    }
    StageIndex = FMath::Clamp(StageIndex, 0, Stages.Num() - 1);
    Nodes.SetNum(FMath::Min(Nodes.Num(), MaxSimulationNodes));
    Triangles.Reset();
    CancelBuild();
    AgentNode = INDEX_NONE;
    PreviousPatrolNode = INDEX_NONE;
    ClearReward();
    if (bUseDemo)
    {
        Nodes.Reset();
        const FVector2D Root =
            Stages[StageIndex].RootAnchors.IsEmpty() ? FVector2D::ZeroVector : Stages[StageIndex].RootAnchors[0];
        const float Rest = FMath::Max(100.0f, EdgeRestLength);
        FAttractionNode N;
        N.Position = FVector(Root, 0);
        N.bRootAnchor = true;
        Nodes.Add(N);
        N.bRootAnchor = false;
        N.Position += FVector(Rest, 0, 0);
        Nodes.Add(N);
        N.Position = FVector(Root, 0) + FVector(Rest * 0.5f, Rest * 0.8660254f, 0);
        Nodes.Add(N);
    }
    for (auto &N : Nodes)
    {
        N.bPinned = N.bRootAnchor;
        N.Position.Z = 0;
        N.Velocity = FVector::ZeroVector;
        N.AgentRole = EAttractionAgentRole::None;
        N.AgentSlideTarget = INDEX_NONE;
    }
    if (Nodes.Num() >= 3 &&
        FMath::Abs((Nodes[1].Position.X - Nodes[0].Position.X) * (Nodes[2].Position.Y - Nodes[0].Position.Y) -
                   (Nodes[1].Position.Y - Nodes[0].Position.Y) * (Nodes[2].Position.X - Nodes[0].Position.X)) > 0.01)
    {
        Nodes[0].bRootAnchor = Nodes[0].bPinned = true;
        AddStructuralTether(0, 1);
        AddStructuralTether(1, 2);
        AddStructuralTether(2, 0);
        FAttractionTriangle T;
        T.A = 0;
        T.B = 1;
        T.C = 2;
        Triangles.Add(T);
        AgentNode = 1;
        Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
    }
    RefreshConnectivity();
    BuildVisuals();
}

void AAttractionSimulation::BuildVisuals()
{
    LiquidPlane->SetRelativeScale3D(FVector(PlaneHalfExtent * 2 / 100.f, PlaneHalfExtent * 2 / 100.f, 1));
    // The GPU evaluates the field. This texture contains only float primitive records.
    if (!LiquidDataTexture && !IsRunningCommandlet())
    {
        LiquidDataTexture = UTexture2D::CreateTransient(256, 3, PF_A32B32G32R32F);
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
    {
        if (UMaterialInterface *Parent = LiquidMaterialAsset.LoadSynchronous())
        {
            LiquidMID = UMaterialInstanceDynamic::Create(Parent, this);
        }
    }
    if (!RewardTexture && !IsRunningCommandlet())
    {
        RewardTexture = UTexture2D::CreateTransient(RewardGridSize, RewardGridSize, PF_R32_FLOAT);
        // Keep the continuous field independent of the nearest-filtered primitive table.
        RewardTexture->SRGB = false;
        RewardTexture->Filter = TF_Bilinear;
        RewardTexture->AddressX = TA_Clamp;
        RewardTexture->AddressY = TA_Clamp;
        RewardTexture->NeverStream = true;
#if WITH_EDITORONLY_DATA
        RewardTexture->MipGenSettings = TMGS_NoMipmaps;
#endif
        RewardTexture->UpdateResource();
    }
    if (LiquidMID)
    {
        if (LiquidDataTexture)
        {
            LiquidMID->SetTextureParameterValue(TEXT("DataTex"), LiquidDataTexture);
        }
        if (RewardTexture)
        {
            LiquidMID->SetTextureParameterValue(TEXT("RewardTex"), RewardTexture);
        }
        LiquidMID->SetScalarParameterValue(TEXT("PlaneHalfExtent"), PlaneHalfExtent);
        LiquidPlane->SetMaterial(0, LiquidMID);
    }
    UpdateLiquidSurface();
}

void AAttractionSimulation::UpdateLiquidSurface()
{
    if (!LiquidDataTexture || !LiquidDataTexture->GetResource() || !LiquidMID || GUsingNullRHI)
    {
        return;
    }
    constexpr int32 Width = 256;
    // 12 KB backing allocation, but only occupied row spans are uploaded (16 bytes/record).
    // Each queued upload owns its storage until the RHI-thread cleanup callback.
    FVector4f *Records = new FVector4f[Width * 3];
    int32 NodeCount = FMath::Min(Nodes.Num(), MaxSimulationNodes);
    const bool bVisibleAgent = Nodes.IsValidIndex(AgentNode);
    const FVector Carrier = AgentPosition();
    const auto PreviewBoundary = DraggedPatch.IsEmpty() ? TArray<int32>() : BoundaryVertices();
    for (int32 I = 0; I < NodeCount; ++I)
    {
        const auto &N = Nodes[I];
        const float Q =
            bVisibleAgent ? FMath::Clamp(1.0f - float(FVector::Distance(Carrier, N.Position)) / (EdgeRestLength * 0.8f),
                                         0.0f, 1.0f)
                          : 0.0f;
        const float Swell = Q * Q * (3 - 2 * Q);
        bool bSnapPreview = false;
        if (!DraggedPatch.IsEmpty() && !DraggedPatch.Contains(I) && PreviewBoundary.Contains(I))
        {
            for (int32 J : DraggedPatch)
            {
                if (PreviewBoundary.Contains(J) && FVector::DistSquared(N.Position, Nodes[J].Position) <=
                                                       FMath::Square(0.85f * FMath::Max(100.0f, EdgeRestLength)))
                {
                    bSnapPreview = true;
                    break;
                }
            }
        }
        const float Energy = FMath::Clamp((N.bConnectedToRoot ? 0.25f : 0.06f) + 0.7f * Swell +
                                              (DraggedPatch.Contains(I) ? 0.22f
                                               : bSnapPreview           ? 0.32f
                                                                        : 0.0f),
                                          0.0f, 1.0f);
        // [0,1] is ordinary energy; [2,3] carries the root flag plus that energy.
        // Keep the root's support stable so its material core ring does not breathe.
        Records[I] = FVector4f(float(N.Position.X), float(N.Position.Y),
                               FMath::Max(20.f, DropletRadius) * (N.bRootAnchor ? 1.0f : 1.0f + 0.12f * Swell),
                               Energy + (N.bRootAnchor ? 2.0f : 0.0f));
    }
    // Exactly one additional record: perimeter swell or animated outward bud.
    // The 220-node simulation cap leaves headroom; still guard the actual write.
    if (bVisibleAgent && NodeCount < Width)
    {
        const float Radius = bBuilding ? FMath::Lerp(55.0f, FMath::Max(20.0f, DropletRadius), BuildProgress) : 65.0f;
        Records[NodeCount++] =
            FVector4f(Carrier.X, Carrier.Y, Radius, bBuilding ? FMath::Lerp(0.55f, 1.0f, BuildProgress) : 0.95f);
    }
    int32 TetherCount = 0;
    auto AddTether = [&](const FVector &A, const FVector &B, float Radius, float PinchAlpha)
    {
        if (TetherCount >= Width)
        {
            return;
        }
        Records[Width + TetherCount] = FVector4f(float(A.X), float(A.Y), float(B.X), float(B.Y));
        Records[Width * 2 + TetherCount] = FVector4f(Radius, FMath::Clamp(PinchAlpha, 0.f, 1.f), 0, 0);
        ++TetherCount;
    };
    if (bBuilding && Nodes.IsValidIndex(BuildA) && Nodes.IsValidIndex(BuildB))
    {
        AddTether((Nodes[BuildA].Position + Nodes[BuildB].Position) * 0.5, Carrier, FilamentRadius, 0);
    }
    for (const FAttractionTether &T : Tethers)
    {
        if (!Nodes.IsValidIndex(T.A) || !Nodes.IsValidIndex(T.B))
        {
            continue;
        }
        const FVector A = Nodes[T.A].Position, B = Nodes[T.B].Position;
        const float Strain = FMath::Max(0.f, float(FVector::Distance(A, B)) / FMath::Max(1.f, T.Rule.RestLength) - 1);
        const float Stretch = FMath::Clamp(Strain / FMath::Max(0.01f, T.Rule.BreakStrain), 0.f, 1.f);

        const float Pinch = Stretch * Stretch * (3 - 2 * Stretch);
        AddTether(A, B, FMath::Max(10.f, FilamentRadius) * (1.f - 0.88f * Pinch), Pinch);
    }
    // Live tethers take priority if an authored board exceeds the 256-record render capacity.
    for (const FPinchingFilament &Cut : PinchingFilaments)
    {
        const float Fade = 1.f - FMath::Clamp(Cut.Age / 0.22f, 0.f, 1.f);
        AddTether(Cut.A, Cut.B, Cut.Radius, 1.f - (Cut.Amplitude / 0.65f) * Fade);
    }
    for (int32 I = 0; I < 3; ++I)
    {
        const FVector L = Stages.IsValidIndex(StageIndex) && Stages[StageIndex].Lobes.IsValidIndex(I)
                              ? Stages[StageIndex].Lobes[I]
                              : FVector(0, 0, 1);
        LiquidMID->SetVectorParameterValue(FName(*FString::Printf(TEXT("TargetLobe%d"), I)),
                                           FLinearColor(L.X, L.Y, L.Z, 0));
    }
    LiquidMID->SetScalarParameterValue(TEXT("RewardActive"), bRewardActive ? 1.f : 0.f);
    LiquidMID->SetVectorParameterValue(
        TEXT("BoardExtent"),
        FLinearColor(FMath::Max(400.0f, PlayfieldHalfExtentX), FMath::Max(400.0f, PlayfieldHalfExtentY), 0, 0));
    if (RewardTexture && RewardTexture->GetResource() && RewardGrid.Num() == RewardGridSize * RewardGridSize)
    {
        float *Values = new float[RewardGrid.Num()];
        FMemory::Memcpy(Values, RewardGrid.GetData(), RewardGrid.Num() * sizeof(float));
        auto *Region = new FUpdateTextureRegion2D(0, 0, 0, 0, RewardGridSize, RewardGridSize);
        RewardTexture->UpdateTextureRegions(0, 1, Region, RewardGridSize * sizeof(float), sizeof(float),
                                            reinterpret_cast<uint8 *>(Values),
                                            [](uint8 *Data, const FUpdateTextureRegion2D *Uploaded)
                                            {
                                                delete[] reinterpret_cast<float *>(Data);
                                                delete Uploaded;
                                            });
    }
    LiquidMID->SetScalarParameterValue(TEXT("NodeCount"), NodeCount);
    LiquidMID->SetScalarParameterValue(TEXT("TetherCount"), TetherCount);
    LiquidMID->SetScalarParameterValue(TEXT("PlaneHalfExtent"), PlaneHalfExtent);
    LiquidMID->SetScalarParameterValue(TEXT("IsoThreshold"), FMath::Max(0.01f, IsoThreshold));
    LiquidMID->SetScalarParameterValue(TEXT("SurfaceHeight"), FMath::Max(0.1f, SurfaceHeight));
    FUpdateTextureRegion2D *Regions = new FUpdateTextureRegion2D[3];
    uint32 RegionCount = 0;
    if (NodeCount > 0)
    {
        Regions[RegionCount++] = FUpdateTextureRegion2D(0, 0, 0, 0, NodeCount, 1);
    }
    if (TetherCount > 0)
    {
        Regions[RegionCount++] = FUpdateTextureRegion2D(0, 1, 0, 1, TetherCount, 1);
        Regions[RegionCount++] = FUpdateTextureRegion2D(0, 2, 0, 2, TetherCount, 1);
    }
    // Counts hide stale records after cuts/reset; zero-count rows need no upload.
    if (RegionCount == 0)
    {
        delete[] Records;
        delete[] Regions;
        return;
    }
    LiquidDataTexture->UpdateTextureRegions(0, RegionCount, Regions, Width * sizeof(FVector4f), sizeof(FVector4f),
                                            reinterpret_cast<uint8 *>(Records),
                                            [](uint8 *Data, const FUpdateTextureRegion2D *UploadedRegions)
                                            {
                                                delete[] reinterpret_cast<FVector4f *>(Data);
                                                delete[] UploadedRegions;
                                            });
}

bool AAttractionSimulation::SliceTether(int32 TetherIndex)
{
    if (!Tethers.IsValidIndex(TetherIndex))
    {
        return false;
    }
    const FVector PreviousCarrier = AgentPosition();
    const FAttractionTether &T = Tethers[TetherIndex];
    if (Nodes.IsValidIndex(T.A) && Nodes.IsValidIndex(T.B))
    {
        if (PinchingFilaments.Num() >= 128)
        {
            PinchingFilaments.RemoveAt(0);
        }
        const float Strain = FMath::Max(0.f, float(FVector::Distance(Nodes[T.A].Position, Nodes[T.B].Position)) /
                                                     FMath::Max(1.f, T.Rule.RestLength) -
                                                 1);
        const float Stretch = FMath::Clamp(Strain / FMath::Max(0.01f, T.Rule.BreakStrain), 0.f, 1.f);
        const float Neck = 1.f - Stretch * Stretch * (3 - 2 * Stretch);
        PinchingFilaments.Add({Nodes[T.A].Position, Nodes[T.B].Position,
                               FMath::Max(10.f, FilamentRadius) * (0.12f + 0.88f * Neck), 0.65f * Neck, 0});
    }
    const int32 CutA = T.A, CutB = T.B;
    Triangles.RemoveAll(
        [&](const FAttractionTriangle &Face)
        {
            return (Face.A == CutA || Face.B == CutA || Face.C == CutA) &&
                   (Face.A == CutB || Face.B == CutB || Face.C == CutB);
        });
    CancelBuild();
    SuppressedPairs.Add(PairKey(T.A, T.B));
    Tethers.RemoveAll([&](const FAttractionTether &E) { return PairKey(E.A, E.B) == PairKey(CutA, CutB); });
    RemoveCutOrphans(CutA, CutB, PreviousCarrier);
    RefreshConnectivity();
    RepairAgentCarrier(PreviousCarrier);
    return true;
}

int32 AAttractionSimulation::DropNode(FAttractionNode Profile, FVector LocalPosition)
{
    if (LocalPosition.ContainsNaN() || Nodes.Num() >= MaxSimulationNodes || !IsInsidePlayfield(LocalPosition))
    {
        return INDEX_NONE;
    }
    Profile.Position = FVector(LocalPosition.X, LocalPosition.Y, 0);
    Profile.Velocity = FVector::ZeroVector;
    Profile.bPinned = false;
    Profile.Mass = FMath::Max(0.01f, Profile.Mass);
    Profile.Valence = FMath::Clamp(Profile.Valence, 0, Profile.Archetype == EAttractionArchetype::Anchor ? 4 : 3);
    Profile.InternalEnergy = Profile.PulseTimeRemaining = Profile.PulseCooldown = 0;
    const int32 Index = Nodes.Add(Profile);
    return Index;
}

FAttractionNode AAttractionSimulation::MakeArchetype(EAttractionArchetype Type)
{
    FAttractionNode N;
    N.Archetype = Type;
    N.CaptureRadius = 320;
    if (Type == EAttractionArchetype::Driver)
    {
        N.Mass = 0.8f;
        N.Valence = 2;
        N.RepulsionScale = 0.8f;
    }
    if (Type == EAttractionArchetype::Buffer)
    {
        N.Mass = 1.2f;
        N.Valence = 3;
        N.RepulsionScale = 1.5f;
    }
    if (Type == EAttractionArchetype::Anchor)
    {
        N.Mass = 2.f;
        N.Valence = 4;
        N.RepulsionScale = 0.4f;
    }
    return N;
}

bool AAttractionSimulation::IsInsidePlayfield(const FVector &P) const
{
    return !P.ContainsNaN() && FMath::Abs(P.X) <= FMath::Max(400.f, PlayfieldHalfExtentX) &&
           FMath::Abs(P.Y) <= FMath::Max(400.f, PlayfieldHalfExtentY);
}

void AAttractionSimulation::AdvanceSimulation(float DeltaSeconds)
{
    if (!bRunning || !FMath::IsFinite(DeltaSeconds))
    {
        return;
    }
    constexpr double FixedDt = 1.0 / 120.0;
    Accumulator = FMath::Min(Accumulator + FMath::Max(0.f, DeltaSeconds), 16 * FixedDt);
    while (Accumulator >= FixedDt)
    {
        Step(float(FixedDt));
        Accumulator -= FixedDt;
    }
}

void AAttractionSimulation::Step(float Dt)
{
    DecayReward(Dt);
    RefreshConnectivity();
    WorkAgents(Dt);
    // Breaks can compact node indices. Finish them before allocating/indexing forces.
    for (int32 I = Tethers.Num() - 1; I >= 0; --I)
    {
        const auto &T = Tethers[I];
        if (FVector::Distance(Nodes[T.A].Position, Nodes[T.B].Position) > T.Rule.RestLength * 2.8f)
        {
            SliceTether(I);
        }
    }
    TArray<FVector> Forces;
    Forces.Init(FVector::ZeroVector, Nodes.Num());
    for (int32 A = 0; A < Nodes.Num(); ++A)
    {
        for (int32 B = A + 1; B < Nodes.Num(); ++B)
        {
            const FVector D = Nodes[B].Position - Nodes[A].Position;
            const float L = D.Size();
            const FVector F = D.GetSafeNormal() * 220.f * FMath::Square(FMath::Max(0.f, 1.f - L / 230.f));
            Forces[A] -= F;
            Forces[B] += F;
        }
    }
    for (int32 I = Tethers.Num() - 1; I >= 0; --I)
    {
        const auto T = Tethers[I];
        const FVector D = Nodes[T.B].Position - Nodes[T.A].Position;
        const FVector F = D.GetSafeNormal() *
                          ((D.Size() - T.Rule.RestLength) * 4.f +
                           FVector::DotProduct(Nodes[T.B].Velocity - Nodes[T.A].Velocity, D.GetSafeNormal()) * 3.f);
        Forces[T.A] += F;
        Forces[T.B] -= F;
    }
    // Incident edges repel angularly; forces remain local and reciprocal.
    for (int32 Center = 0; Center < Nodes.Num(); ++Center)
    {
        TArray<int32> Neighbors;
        for (const auto &T : Tethers)
        {
            if (T.A == Center)
            {
                Neighbors.Add(T.B);
            }
            else if (T.B == Center)
            {
                Neighbors.Add(T.A);
            }
        }
        for (int32 A = 0; A < Neighbors.Num(); ++A)
        {
            for (int32 B = A + 1; B < Neighbors.Num(); ++B)
            {
                const int32 U = Neighbors[A], V = Neighbors[B];
                const FVector DU = (Nodes[U].Position - Nodes[Center].Position).GetSafeNormal(),
                              DV = (Nodes[V].Position - Nodes[Center].Position).GetSafeNormal();
                const float Pressure = FMath::Max(0.f, float(FVector::DotProduct(DU, DV)) - 0.4f) * 65.f;
                const FVector FU = (DU - DV * FVector::DotProduct(DU, DV)) * Pressure;
                const FVector FV = (DV - DU * FVector::DotProduct(DU, DV)) * Pressure;
                Forces[U] += FU;
                Forces[V] += FV;
                Forces[Center] -= FU + FV;
            }
        }
    }
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        auto &N = Nodes[I];
        if (N.bRootAnchor || DraggedPatch.Contains(I))
        {
            N.Velocity = FVector::ZeroVector;
            continue;
        }
        const float Exterior = SampleTargetHeight(FVector2D(N.Position)) < TargetThreshold ? 2.f : 0.f;
        FVector F = Forces[I];
        F.X -= FMath::Sign(N.Position.X) *
               FMath::Max(0.f, float(FMath::Abs(N.Position.X)) - PlayfieldHalfExtentX + 150) * 3;
        F.Y -= FMath::Sign(N.Position.Y) *
               FMath::Max(0.f, float(FMath::Abs(N.Position.Y)) - PlayfieldHalfExtentY + 150) * 3;
        N.Velocity = ((N.Velocity + F.GetClampedToMaxSize(MaxAcceleration) * Dt) * FMath::Exp(-(1.5f + Exterior) * Dt))
                         .GetClampedToMaxSize(400);
        N.Position += N.Velocity * Dt;
    }
    RefreshConnectivity();
}

float AAttractionSimulation::SampleTargetHeight(FVector2D P) const
{
    if (!Stages.IsValidIndex(StageIndex))
    {
        return 0;
    }
    float Complement = 1;
    for (const FVector &L : Stages[StageIndex].Lobes)
    {
        const float Q = FMath::Clamp(1.f - float(FVector2D::DistSquared(P, FVector2D(L))) / (L.Z * L.Z), 0.f, 1.f);
        Complement *= 1.f - Q * Q * (3 - 2 * Q);
    }
    return 1 - Complement;
}
void AAttractionSimulation::RefreshConnectivity()
{
    TArray<int32> Queue;
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        Nodes[I].bConnectedToRoot = Nodes[I].bRootAnchor;
        if (Nodes[I].bRootAnchor)
        {
            Queue.Add(I);
        }
    }
    for (int32 Q = 0; Q < Queue.Num(); ++Q)
    {
        for (const auto &T : Tethers)
        {
            const int32 B = T.A == Queue[Q] ? T.B : T.B == Queue[Q] ? T.A : INDEX_NONE;
            if (Nodes.IsValidIndex(B) && !Nodes[B].bConnectedToRoot)
            {
                Nodes[B].bConnectedToRoot = true;
                Queue.Add(B);
            }
        }
    }
}
bool AAttractionSimulation::AddStructuralTether(int32 A, int32 B)
{
    if (A == B || !Nodes.IsValidIndex(A) || !Nodes.IsValidIndex(B) || Tethers.Num() >= 256)
    {
        return false;
    }
    for (const auto &T : Tethers)
    {
        if (PairKey(A, B) == PairKey(T.A, T.B))
        {
            return false;
        }
    }
    if (SuppressedPairs.Contains(PairKey(A, B)))
    {
        return false;
    }
    FAttractionTether T;
    T.A = A;
    T.B = B;
    T.Rule.RestLength = FMath::Max(100.0f, EdgeRestLength);
    T.Rule.BreakStrain = 1.8f;
    Tethers.Add(T);
    return true;
}
namespace
{
double CrossXY(const FVector &A, const FVector &B)
{
    return A.X * B.Y - A.Y * B.X;
}
bool InsideTriangle(const FVector &P, const FVector &A, const FVector &B, const FVector &C)
{
    const double X = CrossXY(B - A, P - A), Y = CrossXY(C - B, P - B), Z = CrossXY(A - C, P - C);
    return (X > 0.01 && Y > 0.01 && Z > 0.01) || (X < -0.01 && Y < -0.01 && Z < -0.01);
}
bool OnSegment(const FVector &P, const FVector &A, const FVector &B)
{
    return FMath::Abs(CrossXY(B - A, P - A)) < 0.01 && FVector::DotProduct(P - A, P - B) <= 0.01;
}
bool SegmentsMeet(const FVector &A, const FVector &B, const FVector &C, const FVector &D)
{
    const double X = CrossXY(B - A, C - A), Y = CrossXY(B - A, D - A), Z = CrossXY(D - C, A - C),
                 W = CrossXY(D - C, B - C);
    return ((X > 0 && Y < 0) || (X < 0 && Y > 0)) && ((Z > 0 && W < 0) || (Z < 0 && W > 0)) || OnSegment(A, C, D) ||
           OnSegment(B, C, D) || OnSegment(C, A, B) || OnSegment(D, A, B);
}
} // namespace

void AAttractionSimulation::ClearReward()
{
    ++RewardRevision;
    RewardGrid.Init(0.0f, RewardGridSize * RewardGridSize);
    bRewardActive = false;
}

void AAttractionSimulation::PaintReward(FVector2D Center)
{
    PaintRewardStroke(Center, Center);
}

void AAttractionSimulation::PaintRewardStroke(FVector2D Start, FVector2D End)
{
    if (!IsInsidePlayfield(FVector(Start, 0)) || !IsInsidePlayfield(FVector(End, 0)))
    {
        return;
    }
    if (RewardGrid.Num() != RewardGridSize * RewardGridSize)
    {
        ClearReward();
    }
    ++RewardRevision;
    const float Radius = 1.75f * FMath::Max(100.0f, EdgeRestLength);
    const float XExtent = FMath::Max(400.0f, PlayfieldHalfExtentX), YExtent = FMath::Max(400.0f, PlayfieldHalfExtentY);
    const FVector2D Segment = End - Start;
    const double LengthSquared = Segment.SizeSquared();
    // Continuous interpolation of radial stamps: distance to the swept segment
    // yields a soft ribbon even for a single board-wide cursor delta. The controller's
    // existing quarter-radius stamp spacing remains compatible and gesture-local.
    for (int32 Y = 0; Y < RewardGridSize; ++Y)
    {
        for (int32 X = 0; X < RewardGridSize; ++X)
        {
            const FVector2D P((2.0 * X / (RewardGridSize - 1) - 1) * XExtent,
                              (2.0 * Y / (RewardGridSize - 1) - 1) * YExtent);
            const double Alpha = LengthSquared > UE_SMALL_NUMBER
                                     ? FMath::Clamp(FVector2D::DotProduct(P - Start, Segment) / LengthSquared, 0.0, 1.0)
                                     : 0.0;
            const float Q =
                FMath::Clamp(1.0f - float(FVector2D::Distance(P, Start + Segment * Alpha)) / Radius, 0.0f, 1.0f);
            float &Value = RewardGrid[Y * RewardGridSize + X];
            Value = FMath::Clamp(FMath::Max(Value, Q * Q * (3 - 2 * Q)), 0.0f, 1.0f);
            bRewardActive |= Value > 0.0001f;
        }
    }
}

float AAttractionSimulation::SampleReward(FVector2D P) const
{
    if (!IsInsidePlayfield(FVector(P, 0)) || RewardGrid.Num() != RewardGridSize * RewardGridSize)
    {
        return 0;
    }
    const double X = (P.X / FMath::Max(400.0f, PlayfieldHalfExtentX) * 0.5 + 0.5) * (RewardGridSize - 1);
    const double Y = (P.Y / FMath::Max(400.0f, PlayfieldHalfExtentY) * 0.5 + 0.5) * (RewardGridSize - 1);
    const int32 X0 = FMath::Clamp(FMath::FloorToInt(X), 0, RewardGridSize - 1),
                Y0 = FMath::Clamp(FMath::FloorToInt(Y), 0, RewardGridSize - 1);
    const int32 X1 = FMath::Min(X0 + 1, RewardGridSize - 1), Y1 = FMath::Min(Y0 + 1, RewardGridSize - 1);
    return FMath::Lerp(
        FMath::Lerp(RewardGrid[Y0 * RewardGridSize + X0], RewardGrid[Y0 * RewardGridSize + X1], float(X - X0)),
        FMath::Lerp(RewardGrid[Y1 * RewardGridSize + X0], RewardGrid[Y1 * RewardGridSize + X1], float(X - X0)),
        float(Y - Y0));
}

void AAttractionSimulation::DecayReward(float Dt)
{
    bRewardActive = false;
    for (float &Value : RewardGrid)
    {
        Value = FMath::Max(0.0f, Value - Dt / 17.0f);
        bRewardActive |= Value > 0.0001f;
    }
}

bool AAttractionSimulation::HasTether(int32 A, int32 B) const
{
    for (const auto &T : Tethers)
    {
        if (PairKey(A, B) == PairKey(T.A, T.B))
        {
            return true;
        }
    }
    return false;
}

int32 AAttractionSimulation::EdgeIncidence(int32 A, int32 B) const
{
    int32 Count = 0;
    for (const auto &T : Triangles)
    {
        if ((T.A == A || T.B == A || T.C == A) && (T.A == B || T.B == B || T.C == B))
        {
            ++Count;
        }
    }
    return Count;
}

TArray<AAttractionSimulation::FBoundaryEdge> AAttractionSimulation::BoundaryEdges() const
{
    TArray<FBoundaryEdge> Result;
    for (const auto &T : Triangles)
    {
        const int32 V[3] = {T.A, T.B, T.C};
        for (int32 I = 0; I < 3; ++I)
        {
            const int32 A = FMath::Min(V[I], V[(I + 1) % 3]), B = FMath::Max(V[I], V[(I + 1) % 3]);
            if (EdgeIncidence(A, B) == 1 && HasTether(A, B))
            {
                Result.Add({A, B, V[(I + 2) % 3]});
            }
        }
    }
    Result.Sort([](const FBoundaryEdge &A, const FBoundaryEdge &B) { return A.A == B.A ? A.B < B.B : A.A < B.A; });
    return Result;
}

bool AAttractionSimulation::EvaluateFrontierCandidate(int32 A, int32 B, FVector &Apex, int32 &Weld, float &Score) const
{
    return EvaluateFrontierCandidateInternal(A, B, Apex, Weld, Score, false);
}

bool AAttractionSimulation::EvaluateFrontierCandidateInternal(int32 A, int32 B, FVector &Apex, int32 &Weld,
                                                              float &Score, bool bGeometryOnly) const
{
    Apex = FVector::ZeroVector;
    Weld = INDEX_NONE;
    Score = -FLT_MAX;
    if (!Nodes.IsValidIndex(A) || !Nodes.IsValidIndex(B))
    {
        return false;
    }
    const TArray<FBoundaryEdge> Boundary = BoundaryEdges();
    const FBoundaryEdge *Edge =
        Boundary.FindByPredicate([&](const FBoundaryEdge &E) { return PairKey(E.A, E.B) == PairKey(A, B); });
    if (!Edge || !Nodes.IsValidIndex(Edge->Opposite))
    {
        return false;
    }
    const float Rest = FMath::Max(100.0f, EdgeRestLength);
    const FVector PA = Nodes[A].Position, PB = Nodes[B].Position, Mid = (PA + PB) * 0.5;
    const FVector AB = PB - PA;
    FVector Normal = FVector(-AB.Y, AB.X, 0).GetSafeNormal();
    if (Normal.IsNearlyZero() || FMath::Abs(CrossXY(AB, Nodes[Edge->Opposite].Position - PA)) < 0.01)
    {
        return false;
    }
    if (FVector::DotProduct(Normal, Nodes[Edge->Opposite].Position - Mid) > 0)
    {
        Normal = -Normal;
    }
    Apex = Mid + Normal * (Rest * 0.866f);
    if (!IsInsidePlayfield(Apex))
    {
        return false;
    }
    double Nearest = FMath::Square(0.65f * Rest);
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        if (I == A || I == B ||
            !Boundary.ContainsByPredicate([&](const FBoundaryEdge &E) { return E.A == I || E.B == I; }))
        {
            continue;
        }
        const double D = FVector::DistSquared(Apex, Nodes[I].Position);
        if (D < Nearest)
        {
            Nearest = D;
            Weld = I;
        }
    }
    if (Weld != INDEX_NONE)
    {
        Apex = Nodes[Weld].Position;
    }
    if (Weld == Edge->Opposite || (!Nodes.IsValidIndex(Weld) && Nodes.Num() >= MaxSimulationNodes))
    {
        return false;
    }
    const int32 Needed = (!HasTether(A, Weld) ? 1 : 0) + (!HasTether(B, Weld) ? 1 : 0);
    if (Tethers.Num() + Needed > 255)
    {
        return false; // reserve one rendered bud filament
    }
    if (FVector::DotProduct(Apex - Mid, Normal) <= 0.01)
    {
        return false;
    }
    const FVector Points[3] = {PA, PB, Apex};
    for (int32 I = 0; I < 3; ++I)
    {
        const FVector U = Points[(I + 1) % 3] - Points[I], V = Points[(I + 2) % 3] - Points[I];
        if (U.Size() < 0.5f * Rest || U.Size() > 1.6f * Rest ||
            FVector::DotProduct(U.GetSafeNormal(), V.GetSafeNormal()) > FMath::Cos(FMath::DegreesToRadians(25.0f)))
        {
            return false;
        }
    }
    if (Weld != INDEX_NONE && (EdgeIncidence(A, Weld) >= 2 || EdgeIncidence(B, Weld) >= 2))
    {
        return false;
    }
    if (SuppressedPairs.Contains(PairKey(A, Weld)) || SuppressedPairs.Contains(PairKey(B, Weld)))
    {
        return false;
    }
    // Check all spring edges (a superset of boundary edges), including collinear
    // overlap/non-shared touches. Shared endpoints and an existing weld edge are legal.
    for (int32 Endpoint : {A, B})
    {
        for (const auto &T : Tethers)
        {
            if (Weld != INDEX_NONE && PairKey(Endpoint, Weld) == PairKey(T.A, T.B))
            {
                continue;
            }
            const FVector Start = Nodes[Endpoint].Position;
            if (T.A == Endpoint || T.B == Endpoint || T.A == Weld || T.B == Weld)
            {
                const int32 Shared = (T.A == Endpoint || T.B == Endpoint) ? Endpoint : Weld;
                const int32 Other = T.A == Shared ? T.B : T.A;
                const FVector NewOther = Shared == Endpoint ? Apex : Start;
                if (OnSegment(Nodes[Other].Position, Start, Apex) ||
                    OnSegment(NewOther, Nodes[T.A].Position, Nodes[T.B].Position))
                {
                    return false;
                }
                continue;
            }
            if (SegmentsMeet(Start, Apex, Nodes[T.A].Position, Nodes[T.B].Position))
            {
                return false;
            }
        }
    }
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        if (I != A && I != B && I != Weld &&
            (FVector::DistSquared(Nodes[I].Position, Apex) < FMath::Square(0.5f * Rest) ||
             InsideTriangle(Nodes[I].Position, PA, PB, Apex) || OnSegment(Nodes[I].Position, PA, Apex) ||
             OnSegment(Nodes[I].Position, PB, Apex)))
        {
            return false;
        }
    }
    for (const auto &T : Triangles)
    {
        if (InsideTriangle(Apex, Nodes[T.A].Position, Nodes[T.B].Position, Nodes[T.C].Position))
        {
            return false;
        }
    }

    // A committed visual extrusion retains its edge through reward/target changes.
    // All topology, crossing, capacity, length and angle checks above still apply.
    if (bGeometryOnly)
    {
        Score = 0;
        return true;
    }
    const FVector2D P(Apex), Half((Mid + Apex) * 0.5);
    const float H = SampleTargetHeight(P), R = SampleReward(P), HM = SampleTargetHeight(Half), RM = SampleReward(Half);
    bool bEntirelyOutside = true;
    for (const auto &N : Nodes)
    {
        if (SampleTargetHeight(FVector2D(N.Position)) >= TargetThreshold)
        {
            bEntirelyOutside = false;
            break;
        }
    }
    // Every candidate shares a target sensed from the carrier, so comparisons
    // cannot switch paint peaks simply because the candidate lies on another side.
    FVector Attractant;
    float MaximumReward = 0;
    const bool bPursuingReward = FindAttractant(AgentPosition(), Attractant, MaximumReward);
    const bool bApproach = bEntirelyOutside && !bPursuingReward;
    if (H < TargetThreshold && R < 0.05f && !bApproach && !bPursuingReward)
    {
        return false;
    }
    const float E = Rest * 0.25f;
    auto Field = [&](FVector2D Q) { return SampleTargetHeight(Q) + 1.5f * SampleReward(Q); };
    FVector Gradient(Field(FVector2D(Mid) + FVector2D(E, 0)) - Field(FVector2D(Mid) - FVector2D(E, 0)),
                     Field(FVector2D(Mid) + FVector2D(0, E)) - Field(FVector2D(Mid) - FVector2D(0, E)), 0);
    // Compact-support target gradients vanish far away. Use the nearest lobe's
    // signed distance direction to make the explicitly permitted approach useful.
    if (bApproach && Gradient.IsNearlyZero() && Stages.IsValidIndex(StageIndex))
    {
        double Closest = DBL_MAX;
        for (const FVector &L : Stages[StageIndex].Lobes)
        {
            const FVector D = FVector(L.X, L.Y, 0) - Mid;
            const double Distance = D.Size() - FMath::Max(1.0, L.Z);
            if (Distance < Closest)
            {
                Closest = Distance;
                Gradient = D;
            }
        }
    }
    const float Alignment = FVector::DotProduct((Apex - Mid).GetSafeNormal(), Gradient.GetSafeNormal());
    if (bApproach && H < TargetThreshold && Alignment <= 0.0f)
    {
        return false;
    }
    Score = H + 1.5f * R + 0.35f * (HM + 1.5f * RM);
    if (H < TargetThreshold || SampleTargetHeight(FVector2D(Mid)) < TargetThreshold)
    {
        Score += 0.35f * Alignment;
    }
    if (bPursuingReward)
    {
        const float Direction = FVector::DotProduct((Attractant - Mid).GetSafeNormal(), Normal);
        const float Progress = float(FVector::Distance(Mid, Attractant) - FVector::Distance(Apex, Attractant)) / Rest;
        // Paint explicitly authorizes growth across empty target-field gaps, but
        // never an outward step away from the chosen attractant to do ambient work.
        if (Progress <= 0.0f && R < 0.05f)
        {
            return false;
        }
        const float Proximity = 1.0f / (1.0f + float(FVector::Distance(Apex, Attractant)) / (Rest * 6.0f));
        Score = 0.2f * (H + 0.35f * HM) + 4.5f * (2.0f * Proximity + Direction + Progress + R + 0.35f * RM);
    }
    return true;
}

void AAttractionSimulation::CancelBuild()
{
    AgentPath.Reset();
    PathIndex = 0;
    TravelProgress = 0;
    BuildA = BuildB = BuildWeld = INDEX_NONE;
    BuildProgress = 0;
    bBuilding = false;
    AgentTravelDirection = FVector::ZeroVector;
    bRouteDirty = true;
}

FVector AAttractionSimulation::AgentPosition() const
{
    if (!Nodes.IsValidIndex(AgentNode))
    {
        return FVector::ZeroVector;
    }
    if (bBuilding && Nodes.IsValidIndex(BuildA) && Nodes.IsValidIndex(BuildB))
    {
        return FMath::Lerp((Nodes[BuildA].Position + Nodes[BuildB].Position) * 0.5, BuildApex,
                           FMath::SmoothStep(0.0f, 1.0f, BuildProgress));
    }
    if (AgentPath.IsValidIndex(PathIndex) && Nodes.IsValidIndex(AgentPath[PathIndex]))
    {
        return FMath::Lerp(Nodes[AgentNode].Position, Nodes[AgentPath[PathIndex]].Position, TravelProgress);
    }
    return Nodes[AgentNode].Position;
}

void AAttractionSimulation::WorkAgents(float Dt)
{
    if (DraggedPatch.Contains(AgentNode))
    {
        return; // Freeze extrusion while its rigid component is held.
    }
    if (!Nodes.IsValidIndex(AgentNode))
    {
        CancelBuild();
        return;
    }
    const auto Boundary = CarrierBoundaryEdges();
    if (Boundary.IsEmpty())
    {
        CancelBuild();
        return;
    }
    FVector Attractant;
    float MaximumReward = 0;
    const bool bPursuingReward = FindAttractant(AgentPosition(), Attractant, MaximumReward);
    bRouteDirty |= PlannedRewardRevision != RewardRevision || bPursuingReward != bWasPursuingReward;
    PlannedRewardRevision = RewardRevision;
    bWasPursuingReward = bPursuingReward;
    if (bRouteDirty && !(bBuilding && BuildProgress > 0.0f))
    {
        bRouteDirty = false;
        PlanAgentRoute(Boundary, bPursuingReward, Attractant);
    }
    if (!AgentPath.IsEmpty())
    {
        const int32 Next = AgentPath[PathIndex];
        if (!Nodes.IsValidIndex(Next) ||
            !Boundary.ContainsByPredicate([&](const FBoundaryEdge &E)
                                          { return PairKey(E.A, E.B) == PairKey(AgentNode, Next); }))
        {
            CancelBuild();
            return;
        }
        AgentTravelDirection = (Nodes[Next].Position - Nodes[AgentNode].Position).GetSafeNormal();
        const bool Last = PathIndex == AgentPath.Num() - 1 && BuildA != INDEX_NONE;
        const float End = Last ? 0.5f : 1.0f;
        TravelProgress = FMath::Min(
            End, TravelProgress + Dt * (bPursuingReward ? 2.5f : 1.0f) * FMath::Max(100.0f, EdgeRestLength) /
                                      (0.35f * FMath::Max(1.0, float(FVector::Distance(Nodes[AgentNode].Position,
                                                                                       Nodes[Next].Position)))));
        if (TravelProgress < End)
        {
            return;
        }
        if (Last)
        {
            AgentPath.Reset();
            TravelProgress = 0;
            bBuilding = true;
            BuildProgress = 0;
        }
        else
        {
            Nodes[AgentNode].AgentRole = EAttractionAgentRole::None;
            AgentNode = Next;
            Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
            TravelProgress = 0;
            if (++PathIndex >= AgentPath.Num())
            {
                AgentPath.Reset();
                PathIndex = 0;
                bRouteDirty = true;
            }
        }
        return;
    }
    if (bBuilding)
    {
        FVector Apex;
        int32 Weld;
        float Score;
        if (!EvaluateFrontierCandidateInternal(BuildA, BuildB, Apex, Weld, Score, BuildProgress > 0.0f) ||
            Weld != BuildWeld)
        {
            CancelBuild();
            return;
        }
        BuildApex = Apex;
        BuildProgress = FMath::Min(1.0f, BuildProgress + Dt * (bPursuingReward ? 1.75f : 1.0f) / 0.45f);
        if (BuildProgress < 1.0f)
        {
            return;
        }
        // Candidate was revalidated immediately above. Reserve all resources before
        // committing, so neither failed capacity nor cuts can leave half a triangle.
        int32 Vertex = Weld;
        if (Vertex == INDEX_NONE)
        {
            if (Nodes.Num() >= MaxSimulationNodes)
            {
                CancelBuild();
                return;
            }
            FAttractionNode Child;
            Child.Position = Apex;
            Vertex = Nodes.Add(Child);
        }
        if (!HasTether(BuildA, Vertex))
        {
            AddStructuralTether(BuildA, Vertex);
        }
        if (!HasTether(BuildB, Vertex))
        {
            AddStructuralTether(BuildB, Vertex);
        }
        FAttractionTriangle Triangle;
        Triangle.A = BuildA;
        Triangle.B = BuildB;
        Triangle.C = Vertex;
        Triangles.Add(Triangle);
        Nodes[AgentNode].AgentRole = EAttractionAgentRole::None;
        AgentNode = Vertex;
        Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
        CancelBuild();
        RefreshConnectivity();
        return;
    }
}

bool AAttractionSimulation::FindAttractant(const FVector &Origin, FVector &Position, float &Maximum) const
{
    Position = FVector::ZeroVector;
    Maximum = 0;
    float StrongestPull = -1;
    const float SensingScale = 6.0f * FMath::Max(100.0f, EdgeRestLength);
    for (int32 I = 0; I < RewardGrid.Num(); ++I)
    {
        Maximum = FMath::Max(Maximum, RewardGrid[I]);
        if (RewardGrid[I] <= 0)
        {
            continue;
        }
        // Sense actual local peaks, never a centroid between separate strokes.
        const int32 X = I % RewardGridSize, Y = I / RewardGridSize;
        bool bPeak = true;
        for (int32 DY = -1; DY <= 1 && bPeak; ++DY)
        {
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const int32 NX = X + DX, NY = Y + DY;
                if (NX >= 0 && NX < RewardGridSize && NY >= 0 && NY < RewardGridSize &&
                    RewardGrid[NY * RewardGridSize + NX] > RewardGrid[I])
                {
                    bPeak = false;
                    break;
                }
            }
        }
        if (!bPeak)
        {
            continue;
        }
        const FVector Cell(
            (2.0 * (I % RewardGridSize) / (RewardGridSize - 1) - 1) * FMath::Max(400.0f, PlayfieldHalfExtentX),
            (2.0 * (I / RewardGridSize) / (RewardGridSize - 1) - 1) * FMath::Max(400.0f, PlayfieldHalfExtentY), 0);
        const float Pull = RewardGrid[I] / (1.0f + float(FVector::Distance(Origin, Cell)) / SensingScale);
        if (Pull > StrongestPull)
        {
            StrongestPull = Pull;
            Position = Cell;
        }
    }
    return Maximum > 0.05f; // Row-major scan resolves exact peak/pull ties.
}

void AAttractionSimulation::PlanAgentRoute(const TArray<FBoundaryEdge> &Boundary, bool bPursuingReward,
                                           const FVector &Attractant)
{
    if (bBuilding && BuildProgress > 0.0f)
    {
        return; // Valid active extrusions are locked until commit or cut.
    }
    // Virtual source on the current carrier edge preserves position when fresh
    // paint reverses pursuit mid-edge. Both endpoint costs include partial travel.
    int32 SourceA = AgentNode, SourceB = INDEX_NONE;
    float SourceAlpha = 0;
    if (bBuilding)
    {
        SourceA = BuildA;
        SourceB = BuildB;
        SourceAlpha = 0.5f;
    }
    else if (AgentPath.IsValidIndex(PathIndex))
    {
        SourceB = AgentPath[PathIndex];
        SourceAlpha = TravelProgress;
    }
    TArray<double> Distance;
    Distance.Init(DBL_MAX, Nodes.Num());
    TArray<int32> Parent;
    Parent.Init(INDEX_NONE, Nodes.Num());
    TArray<bool> Visited;
    Visited.Init(false, Nodes.Num());
    Distance[SourceA] = 0;
    if (Nodes.IsValidIndex(SourceB))
    {
        const double Length = FVector::Distance(Nodes[SourceA].Position, Nodes[SourceB].Position);
        Distance[SourceA] = SourceAlpha * Length;
        Distance[SourceB] = (1.0f - SourceAlpha) * Length;
    }
    // O(V^2 + VE) is bounded by 220 nodes. Ascending node IDs and sorted boundary
    // edges resolve ties without unordered containers or floating heap ordering.
    for (int32 Iteration = 0; Iteration < Nodes.Num(); ++Iteration)
    {
        int32 Current = INDEX_NONE;
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            if (!Visited[I] && Distance[I] < DBL_MAX && (Current == INDEX_NONE || Distance[I] < Distance[Current]))
            {
                Current = I;
            }
        }
        if (Current == INDEX_NONE)
        {
            break;
        }
        Visited[Current] = true;
        for (const auto &E : Boundary)
        {
            const int32 J = E.A == Current ? E.B : E.B == Current ? E.A : INDEX_NONE;
            if (J == INDEX_NONE || Visited[J])
            {
                continue;
            }
            const double D = Distance[Current] + FVector::Distance(Nodes[Current].Position, Nodes[J].Position);
            if (D < Distance[J])
            {
                Distance[J] = D;
                Parent[J] = Current;
            }
        }
    }
    int32 TargetA = INDEX_NONE, TargetB = INDEX_NONE, TargetWeld = INDEX_NONE;
    FVector TargetApex = FVector::ZeroVector;
    float Best = -FLT_MAX;
    double BestTravel = DBL_MAX;
    for (const auto &E : Boundary)
    {
        if (Distance[E.A] == DBL_MAX && Distance[E.B] == DBL_MAX)
        {
            continue;
        }
        FVector Apex;
        int32 Weld;
        float Score;
        if (!EvaluateFrontierCandidate(E.A, E.B, Apex, Weld, Score))
        {
            continue;
        }
        if (bPursuingReward)
        {
            if (BuildA != INDEX_NONE && BuildB != INDEX_NONE && PairKey(E.A, E.B) == PairKey(BuildA, BuildB))
            {
                Score += 0.35f;
            }
            // Bias the first step of this candidate's shortest route, rather than
            // its straight-line chord through the lattice interior.
            int32 First = Distance[E.A] <= Distance[E.B] ? E.A : E.B;
            int32 Next = First;
            while (Parent[First] != INDEX_NONE)
            {
                Next = First;
                First = Parent[First];
            }
            FVector Direction = FVector::ZeroVector;
            if (Nodes.IsValidIndex(SourceB) && (First == SourceB || SourceAlpha > UE_SMALL_NUMBER))
            {
                Direction = (Nodes[SourceB].Position - Nodes[SourceA].Position).GetSafeNormal();
                if (First == SourceA)
                {
                    Direction = -Direction;
                }
            }
            else if (Next != First)
            {
                Direction = (Nodes[Next].Position - Nodes[First].Position).GetSafeNormal();
            }
            else
            {
                Direction = ((Nodes[E.A].Position + Nodes[E.B].Position) * 0.5 - Nodes[First].Position).GetSafeNormal();
            }
            if (Nodes.IsValidIndex(SourceB) && PairKey(SourceA, SourceB) == PairKey(E.A, E.B))
            {
                Direction = (Nodes[SourceB].Position - Nodes[SourceA].Position).GetSafeNormal() *
                            (SourceAlpha <= 0.5f ? 1.0f : -1.0f);
            }
            Score += 0.10f * FVector::DotProduct(AgentTravelDirection, Direction);
        }
        const double Length = FVector::Distance(Nodes[E.A].Position, Nodes[E.B].Position);
        const double Travel = Nodes.IsValidIndex(SourceB) && PairKey(SourceA, SourceB) == PairKey(E.A, E.B)
                                  ? FMath::Abs(0.5f - SourceAlpha) * Length
                                  : FMath::Min(Distance[E.A], Distance[E.B]) + 0.5 * Length;
        if (Score > Best + 0.000001f || (FMath::Abs(Score - Best) <= 0.000001f && Travel < BestTravel))
        {
            Best = Score;
            BestTravel = Travel;
            TargetA = E.A;
            TargetB = E.B;
            TargetWeld = Weld;
            TargetApex = Apex;
        }
    }
    if (bBuilding)
    {
        if (TargetA != INDEX_NONE && PairKey(TargetA, TargetB) == PairKey(BuildA, BuildB))
        {
            return;
        }
        // No progress yet: preserve the midpoint as the new route's virtual source.
        bBuilding = false;
    }
    AgentPath.Reset();
    PathIndex = 0;
    BuildA = BuildB = BuildWeld = INDEX_NONE;
    int32 Destination = INDEX_NONE, Across = INDEX_NONE;
    if (TargetA != INDEX_NONE)
    {
        const bool bUseA = Distance[TargetA] <= Distance[TargetB];
        Destination = bUseA ? TargetA : TargetB;
        Across = bUseA ? TargetB : TargetA;
        BuildA = Destination;
        BuildB = Across;
        BuildWeld = TargetWeld;
        BuildApex = TargetApex;
        if (PairKey(SourceA, SourceB) == PairKey(TargetA, TargetB) && Nodes.IsValidIndex(SourceB))
        {
            // Already on the chosen edge: walk directly to its midpoint.
            const bool bForward = SourceAlpha <= 0.5f;
            Nodes[AgentNode].AgentRole = EAttractionAgentRole::None;
            AgentNode = bForward ? SourceA : SourceB;
            Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
            BuildA = AgentNode;
            BuildB = bForward ? SourceB : SourceA;
            AgentPath.Add(BuildB);
            TravelProgress = bForward ? SourceAlpha : 1.0f - SourceAlpha;
            return;
        }
    }
    else if (bPursuingReward)
    {
        // No legal extrusion (wire scraps/blocked geometry): pursue the nearest
        // reachable boundary vertex, then wait there instead of ambient wandering.
        double Nearest = DBL_MAX;
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            if (Distance[I] < DBL_MAX)
            {
                const double D = FVector::DistSquared(Nodes[I].Position, Attractant);
                if (D < Nearest)
                {
                    Nearest = D;
                    Destination = I;
                }
            }
        }
    }
    else
    {
        for (const auto &E : Boundary)
        {
            const int32 J = E.A == AgentNode ? E.B : E.B == AgentNode ? E.A : INDEX_NONE;
            if (J != INDEX_NONE &&
                (Destination == INDEX_NONE || (J != PreviousPatrolNode && Destination == PreviousPatrolNode)))
            {
                Destination = J;
            }
        }
        PreviousPatrolNode = AgentNode;
    }
    if (Destination == INDEX_NONE)
    {
        TravelProgress = 0;
        return;
    }
    TArray<int32> Reverse;
    int32 Root = Destination;
    while (Parent[Root] != INDEX_NONE)
    {
        Reverse.Add(Root);
        Root = Parent[Root];
    }
    // Start by completing the appropriate fraction of the source edge.
    Nodes[AgentNode].AgentRole = EAttractionAgentRole::None;
    if (Root == SourceB)
    {
        AgentNode = SourceA;
        AgentPath.Add(SourceB);
        TravelProgress = SourceAlpha;
    }
    else if (Nodes.IsValidIndex(SourceB) && SourceAlpha > 0)
    {
        AgentNode = SourceB;
        AgentPath.Add(SourceA);
        TravelProgress = 1.0f - SourceAlpha;
    }
    else
    {
        AgentNode = SourceA;
        TravelProgress = 0;
    }
    Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
    for (int32 I = Reverse.Num() - 1; I >= 0; --I)
    {
        AgentPath.Add(Reverse[I]);
    }
    if (Across != INDEX_NONE)
    {
        AgentPath.Add(Across);
    }
}

TArray<int32> AAttractionSimulation::ComponentNodes(int32 NodeIndex) const
{
    TArray<int32> Result;
    if (!Nodes.IsValidIndex(NodeIndex))
    {
        return Result;
    }
    Result.Add(NodeIndex);
    for (int32 Q = 0; Q < Result.Num(); ++Q)
    {
        for (const auto &T : Tethers)
        {
            const int32 J = T.A == Result[Q] ? T.B : T.B == Result[Q] ? T.A : INDEX_NONE;
            if (Nodes.IsValidIndex(J) && !Result.Contains(J))
            {
                Result.Add(J);
            }
        }
    }
    Result.Sort();
    return Result;
}

int32 AAttractionSimulation::PickLatticeNode(FVector Position) const
{
    if (!IsInsidePlayfield(Position))
    {
        return INDEX_NONE;
    }
    int32 Best = INDEX_NONE;
    double Distance = FMath::Square(0.65f * FMath::Max(100.0f, EdgeRestLength));
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        const double D = FVector::DistSquaredXY(Position, Nodes[I].Position);
        if (D <= Distance && (Best == INDEX_NONE || D < Distance))
        {
            Best = I;
            Distance = D;
        }
    }
    return Best; // Ascending index wins an exact distance tie.
}

int32 AAttractionSimulation::PickLatticeComponent(FVector Position) const
{
    if (!IsInsidePlayfield(Position))
    {
        return INDEX_NONE;
    }
    int32 Best = PickLatticeNode(Position);
    double BestDistance = Best == INDEX_NONE ? FMath::Square(0.65f * FMath::Max(100.0f, EdgeRestLength))
                                             : FVector::DistSquaredXY(Position, Nodes[Best].Position);
    bool bEdgeBest = false;
    uint64 BestKey = MAX_uint64;
    for (const auto &T : Tethers)
    {
        if (!Nodes.IsValidIndex(T.A) || !Nodes.IsValidIndex(T.B))
        {
            continue;
        }
        const FVector A = Nodes[T.A].Position, B = Nodes[T.B].Position, AB = B - A;
        const float Alpha =
            AB.SizeSquared() > UE_SMALL_NUMBER
                ? FMath::Clamp(float(FVector::DotProduct(Position - A, AB) / AB.SizeSquared()), 0.0f, 1.0f)
                : 0.0f;
        const double Distance = FVector::DistSquaredXY(Position, A + AB * Alpha);
        const uint64 Key = PairKey(T.A, T.B);
        if (Distance < BestDistance || (Best == INDEX_NONE && Distance <= BestDistance) ||
            (bEdgeBest && Distance == BestDistance && Key < BestKey))
        {
            Best = FMath::Min(T.A, T.B);
            BestDistance = Distance;
            BestKey = Key;
            bEdgeBest = true;
        }
    }
    // A node wins a node/segment distance tie; tied segments use canonical endpoint
    // indices, independent of tether insertion order. Return a component member.
    return Best;
}

void AAttractionSimulation::RemoveCutOrphans(int32 CutA, int32 CutB, const FVector &PreviousCarrier)
{
    TArray<int32> Remap;
    Remap.Init(INDEX_NONE, Nodes.Num());
    TArray<FAttractionNode> Survivors;
    Survivors.Reserve(Nodes.Num());
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        const bool bCutEndpoint = I == CutA || I == CutB;
        bool bIncident = false;
        if (bCutEndpoint)
        {
            for (const auto &T : Tethers)
            {
                if (T.A == I || T.B == I)
                {
                    bIncident = true;
                    break;
                }
            }
        }
        // Only vertices orphaned by this cut dissolve. Roots and deliberately
        // authored/diagnostic isolated nodes are not removed by unrelated cuts.
        if (bCutEndpoint && !Nodes[I].bRootAnchor && !bIncident)
        {
            continue;
        }
        Remap[I] = Survivors.Add(Nodes[I]);
    }
    if (Survivors.Num() == Nodes.Num())
    {
        return;
    }
    auto Map = [&](int32 I) { return Remap.IsValidIndex(I) ? Remap[I] : INDEX_NONE; };
    const bool bHadBuilder = Nodes.IsValidIndex(AgentNode);
    AgentNode = Map(AgentNode);
    for (auto &T : Tethers)
    {
        T.A = Map(T.A);
        T.B = Map(T.B);
    }
    Triangles.RemoveAll([&](const FAttractionTriangle &F)
                        { return Map(F.A) == INDEX_NONE || Map(F.B) == INDEX_NONE || Map(F.C) == INDEX_NONE; });
    for (auto &F : Triangles)
    {
        F.A = Map(F.A);
        F.B = Map(F.B);
        F.C = Map(F.C);
    }
    for (auto &N : Survivors)
    {
        N.AgentSlideTarget = Map(N.AgentSlideTarget);
    }
    TSet<uint64> RemappedPairs;
    for (uint64 K : SuppressedPairs)
    {
        const int32 A = Map(int32(K >> 32)), B = Map(int32(uint32(K)));
        if (A != INDEX_NONE && B != INDEX_NONE && A != B)
        {
            RemappedPairs.Add(PairKey(A, B));
        }
    }
    SuppressedPairs = MoveTemp(RemappedPairs);
    // Usually RMB has already cancelled the LMB hold. Automatic strain breaks
    // must also preserve valid snapshots if a different component is being held.
    for (int32 I = DraggedPatch.Num() - 1; I >= 0; --I)
    {
        const int32 J = Map(DraggedPatch[I]);
        if (J == INDEX_NONE)
        {
            DraggedPatch.RemoveAt(I);
            PatchStartPositions.RemoveAt(I);
        }
        else
        {
            DraggedPatch[I] = J;
        }
    }
    if (DraggedPatch.IsEmpty())
    {
        PatchStartPositions.Reset();
        PatchTargetOffset = PatchCurrentOffset = FVector::ZeroVector;
    }
    Nodes = MoveTemp(Survivors);
    if (!bHadBuilder || AgentNode != INDEX_NONE)
    {
        return;
    }
    // The old carrier itself dissolved, so its former component has no edge to
    // keep it on. Rebind to the closest surviving exposed edge, then node/root if
    // no edge remains. Canonical edge/node order resolves all exact ties.
    double Best = DBL_MAX;
    for (const auto &E : CarrierBoundaryEdges())
    {
        const FVector A = Nodes[E.A].Position, AB = Nodes[E.B].Position - A;
        const float Alpha =
            AB.SizeSquared() > UE_SMALL_NUMBER
                ? FMath::Clamp(float(FVector::DotProduct(PreviousCarrier - A, AB) / AB.SizeSquared()), 0.0f, 1.0f)
                : 0.0f;
        const double Distance = FVector::DistSquared(PreviousCarrier, A + AB * Alpha);
        if (Distance < Best)
        {
            Best = Distance;
            AgentNode = E.A;
        }
    }
    if (AgentNode == INDEX_NONE)
    {
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            const double Distance = FVector::DistSquared(PreviousCarrier, Nodes[I].Position);
            if (Distance < Best)
            {
                Best = Distance;
                AgentNode = I;
            }
        }
    }
    if (Nodes.IsValidIndex(AgentNode))
    {
        Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
    }
}

bool AAttractionSimulation::BeginPatchDrag(int32 NodeIndex)
{
    if (!DraggedPatch.IsEmpty())
    {
        return false;
    }
    const auto Patch = ComponentNodes(NodeIndex);
    if (Patch.IsEmpty())
    {
        return false;
    }
    for (int32 I : Patch)
    {
        if (Nodes[I].bRootAnchor)
        {
            return false;
        }
    }
    FVector Min = Nodes[Patch[0]].Position, Max = Min;
    for (int32 I : Patch)
    {
        Min = Min.ComponentMin(Nodes[I].Position);
        Max = Max.ComponentMax(Nodes[I].Position);
    }
    if (Max.X - Min.X > 2 * FMath::Max(400.0f, PlayfieldHalfExtentX) ||
        Max.Y - Min.Y > 2 * FMath::Max(400.0f, PlayfieldHalfExtentY))
    {
        return false;
    }
    // A bud's temporary apex is presentation, not part of the rigid patch snapshot.
    DraggedPatch = Patch;
    PatchStartPositions.Reset();
    for (int32 I : DraggedPatch)
    {
        PatchStartPositions.Add(Nodes[I].Position);
        Nodes[I].Velocity = FVector::ZeroVector;
    }
    PatchTargetOffset = PatchCurrentOffset = FVector::ZeroVector;
    UpdatePatchDrag(FVector::ZeroVector, 0); // Correct pre-existing soft-boundary overshoot rigidly.
    return true;
}

void AAttractionSimulation::UpdatePatchDrag(FVector CursorDelta, float DeltaSeconds)
{
    if (DraggedPatch.IsEmpty() || CursorDelta.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0)
    {
        return;
    }
    CursorDelta.Z = 0;
    FVector Lower(-DBL_MAX, -DBL_MAX, 0), Upper(DBL_MAX, DBL_MAX, 0);
    const double X = FMath::Max(400.0f, PlayfieldHalfExtentX), Y = FMath::Max(400.0f, PlayfieldHalfExtentY);
    for (const FVector &P : PatchStartPositions)
    {
        Lower.X = FMath::Max(Lower.X, -X - P.X);
        Upper.X = FMath::Min(Upper.X, X - P.X);
        Lower.Y = FMath::Max(Lower.Y, -Y - P.Y);
        Upper.Y = FMath::Min(Upper.Y, Y - P.Y);
    }
    if (Lower.X > Upper.X || Lower.Y > Upper.Y)
    {
        return;
    }
    PatchTargetOffset += CursorDelta;
    PatchTargetOffset.X = FMath::Clamp(PatchTargetOffset.X, Lower.X, Upper.X);
    PatchTargetOffset.Y = FMath::Clamp(PatchTargetOffset.Y, Lower.Y, Upper.Y);
    const FVector PreviousOffset = PatchCurrentOffset;
    PatchCurrentOffset = FMath::VInterpTo(PatchCurrentOffset, PatchTargetOffset, DeltaSeconds, 18.0f);
    PatchCurrentOffset.X = FMath::Clamp(PatchCurrentOffset.X, Lower.X, Upper.X);
    PatchCurrentOffset.Y = FMath::Clamp(PatchCurrentOffset.Y, Lower.Y, Upper.Y);
    if (bBuilding && DraggedPatch.Contains(AgentNode))
    {
        BuildApex += PatchCurrentOffset - PreviousOffset;
    }
    for (int32 I = 0; I < DraggedPatch.Num(); ++I)
    {
        if (!Nodes.IsValidIndex(DraggedPatch[I]))
        {
            continue;
        }
        Nodes[DraggedPatch[I]].Position = PatchStartPositions[I] + PatchCurrentOffset;
        Nodes[DraggedPatch[I]].Velocity = FVector::ZeroVector;
    }
}

bool AAttractionSimulation::EndPatchDrag(bool bTryGraft)
{
    const auto Patch = DraggedPatch;
    DraggedPatch.Reset();
    PatchStartPositions.Reset();
    PatchTargetOffset = PatchCurrentOffset = FVector::ZeroVector;
    if (!bTryGraft || Patch.IsEmpty())
    {
        return false;
    }
    // A release acts on the visibly translated patch, not a hidden interpolation target.
    for (int32 I : Patch)
    {
        if (!Nodes.IsValidIndex(I) || Nodes[I].bRootAnchor)
        {
            return false;
        }
    }
    return GraftComponent(Patch);
}

TArray<AAttractionSimulation::FBoundaryEdge> AAttractionSimulation::CarrierBoundaryEdges() const
{
    auto Result = BoundaryEdges();
    // Cuts can leave a wire or isolated node after removing incident faces. Wires
    // are exposed carrier paths, but never growth bases (growth requires one face).
    for (const auto &T : Tethers)
    {
        if (EdgeIncidence(T.A, T.B) == 0)
        {
            Result.Add({FMath::Min(T.A, T.B), FMath::Max(T.A, T.B), INDEX_NONE});
        }
    }
    Result.Sort([](const FBoundaryEdge &A, const FBoundaryEdge &B) { return A.A == B.A ? A.B < B.B : A.A < B.A; });
    return Result;
}

TArray<int32> AAttractionSimulation::BoundaryVertices() const
{
    TArray<int32> Result;
    const auto Boundary = CarrierBoundaryEdges();
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        bool bIncident = false;
        for (const auto &T : Tethers)
        {
            if (T.A == I || T.B == I)
            {
                bIncident = true;
                break;
            }
        }
        if (!bIncident || Boundary.ContainsByPredicate([&](const FBoundaryEdge &E) { return E.A == I || E.B == I; }))
        {
            Result.Add(I);
        }
    }
    return Result;
}

void AAttractionSimulation::RepairAgentCarrier(const FVector &PreviousPosition)
{
    if (!Nodes.IsValidIndex(AgentNode))
    {
        CancelBuild();
        return;
    }
    const auto Component = ComponentNodes(AgentNode);
    const auto Boundary = CarrierBoundaryEdges();
    int32 BestA = INDEX_NONE, BestB = INDEX_NONE;
    float BestAlpha = 0;
    double BestDistance = DBL_MAX;
    for (const auto &E : Boundary)
    {
        if (!Component.Contains(E.A) || !Component.Contains(E.B))
        {
            continue;
        }
        const FVector AB = Nodes[E.B].Position - Nodes[E.A].Position;
        if (AB.IsNearlyZero())
        {
            continue;
        }
        const float Alpha = FMath::Clamp(
            float(FVector::DotProduct(PreviousPosition - Nodes[E.A].Position, AB) / AB.SizeSquared()), 0.0f, 1.0f);
        const double Distance = FVector::DistSquared(PreviousPosition, Nodes[E.A].Position + AB * Alpha);
        if (Distance < BestDistance - 0.000001)
        {
            BestA = E.A;
            BestB = E.B;
            BestAlpha = Alpha;
            BestDistance = Distance;
        }
    }
    CancelBuild();
    PreviousPatrolNode = INDEX_NONE;
    if (BestA == INDEX_NONE)
    {
        return; // An isolated component has no edge: keep its sole carrier dormant.
    }
    Nodes[AgentNode].AgentRole = EAttractionAgentRole::None;
    AgentNode = BestA;
    Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
    AgentPath.Add(BestB);
    PathIndex = 0;
    TravelProgress = BestAlpha;
}

bool AAttractionSimulation::IsGraftGeometryValid(const TArray<FAttractionNode> &P,
                                                 const TArray<FAttractionTether> &Edges,
                                                 const TArray<FAttractionTriangle> &Faces) const
{
    if (P.Num() > MaxSimulationNodes || Edges.Num() > 255)
    {
        return false;
    }
    const double Rest = FMath::Max(100.0f, EdgeRestLength), CosMinimum = FMath::Cos(FMath::DegreesToRadians(25.0));
    for (const auto &N : P)
    {
        if (!IsInsidePlayfield(N.Position))
        {
            return false;
        }
    }
    TSet<uint64> Keys;
    for (const auto &E : Edges)
    {
        if (!P.IsValidIndex(E.A) || !P.IsValidIndex(E.B) || E.A == E.B || Keys.Contains(PairKey(E.A, E.B)))
        {
            return false;
        }
        Keys.Add(PairKey(E.A, E.B));
        const double Length = FVector::Distance(P[E.A].Position, P[E.B].Position);
        if (Length < 0.5 * Rest || Length > 1.6 * Rest)
        {
            return false;
        }
        int32 Incidence = 0;
        for (const auto &F : Faces)
        {
            if ((F.A == E.A || F.B == E.A || F.C == E.A) && (F.A == E.B || F.B == E.B || F.C == E.B))
            {
                ++Incidence;
            }
        }
        if (Incidence > 2)
        {
            return false;
        }
    }
    for (int32 I = 0; I < Edges.Num(); ++I)
    {
        for (int32 J = I + 1; J < Edges.Num(); ++J)
        {
            const auto &E = Edges[I];
            const auto &F = Edges[J];
            int32 Shared = INDEX_NONE;
            if (E.A == F.A || E.A == F.B)
            {
                Shared = E.A;
            }
            else if (E.B == F.A || E.B == F.B)
            {
                Shared = E.B;
            }
            if (Shared != INDEX_NONE)
            {
                const FVector U = P[E.A == Shared ? E.B : E.A].Position - P[Shared].Position;
                const FVector V = P[F.A == Shared ? F.B : F.A].Position - P[Shared].Position;
                if (FVector::DotProduct(U.GetSafeNormal(), V.GetSafeNormal()) > CosMinimum)
                {
                    return false;
                }
            }
            else if (SegmentsMeet(P[E.A].Position, P[E.B].Position, P[F.A].Position, P[F.B].Position))
            {
                return false;
            }
        }
    }
    for (const auto &F : Faces)
    {
        if (!P.IsValidIndex(F.A) || !P.IsValidIndex(F.B) || !P.IsValidIndex(F.C) || F.A == F.B || F.B == F.C ||
            F.C == F.A)
        {
            return false;
        }
        if (!Keys.Contains(PairKey(F.A, F.B)) || !Keys.Contains(PairKey(F.B, F.C)) || !Keys.Contains(PairKey(F.C, F.A)))
        {
            return false;
        }
        if (FMath::Abs(CrossXY(P[F.B].Position - P[F.A].Position, P[F.C].Position - P[F.A].Position)) < 0.01)
        {
            return false;
        }
        const int32 V[3] = {F.A, F.B, F.C};
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector U = P[V[(I + 1) % 3]].Position - P[V[I]].Position,
                          W = P[V[(I + 2) % 3]].Position - P[V[I]].Position;
            if (FVector::DotProduct(U.GetSafeNormal(), W.GetSafeNormal()) > CosMinimum)
            {
                return false;
            }
        }
        for (int32 I = 0; I < P.Num(); ++I)
        {
            if (I != F.A && I != F.B && I != F.C &&
                InsideTriangle(P[I].Position, P[F.A].Position, P[F.B].Position, P[F.C].Position))
            {
                return false;
            }
        }
    }
    for (const auto &E : Edges)
    {
        for (int32 I = 0; I < P.Num(); ++I)
        {
            if (I != E.A && I != E.B && OnSegment(P[I].Position, P[E.A].Position, P[E.B].Position))
            {
                return false;
            }
        }
    }
    return true;
}

bool AAttractionSimulation::GraftComponent(const TArray<int32> &Patch)
{
    const auto Vertices = BoundaryVertices();
    const double SnapSquared = FMath::Square(0.85f * FMath::Max(100.0f, EdgeRestLength));
    struct FWeld
    {
        int32 Source, Target;
        double Distance;
    };
    TArray<FWeld> Welds;
    for (int32 A : Vertices)
    {
        if (Patch.Contains(A))
        {
            for (int32 B : Vertices)
            {
                if (!Patch.Contains(B))
                {
                    const double Distance = FVector::DistSquared(Nodes[A].Position, Nodes[B].Position);
                    if (Distance <= SnapSquared)
                    {
                        Welds.Add({A, B, Distance});
                    }
                }
            }
        }
    }
    Welds.Sort(
        [](const FWeld &A, const FWeld &B)
        {
            return A.Distance != B.Distance ? A.Distance < B.Distance
                   : A.Source != B.Source   ? A.Source < B.Source
                                            : A.Target < B.Target;
        });
    TArray<int32> Current;
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        Current.Add(I);
    }
    TSet<int32> UsedSources, UsedTargets;
    bool bWelded = false;
    // Validate the complete seam before individual welds: coincident seam edges
    // may only become legal when both endpoints collapse in the same transaction.
    TArray<FWeld> Matching;
    TSet<int32> MatchedSources, MatchedTargets;
    for (const auto &W : Welds)
    {
        if (!MatchedSources.Contains(W.Source) && !MatchedTargets.Contains(W.Target))
        {
            Matching.Add(W);
            MatchedSources.Add(W.Source);
            MatchedTargets.Add(W.Target);
        }
    }
    for (int32 Count = Matching.Num(); Count >= 2; --Count)
    {
        TArray<int32> Representative, Compact;
        Compact.Init(INDEX_NONE, Nodes.Num());
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            Representative.Add(I);
        }
        for (int32 I = 0; I < Count; ++I)
        {
            Representative[Matching[I].Source] = Matching[I].Target;
        }
        TArray<FAttractionNode> P;
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            if (Representative[I] == I)
            {
                Compact[I] = P.Add(Nodes[I]);
            }
        }
        auto Map = [&](int32 I) { return Compact[Representative[I]]; };
        TArray<FAttractionTether> E;
        TSet<uint64> Keys;
        for (auto Edge : Tethers)
        {
            Edge.A = Map(Edge.A);
            Edge.B = Map(Edge.B);
            if (Edge.A == Edge.B || Keys.Contains(PairKey(Edge.A, Edge.B)))
            {
                continue;
            }
            Keys.Add(PairKey(Edge.A, Edge.B));
            E.Add(Edge);
        }
        auto F = Triangles;
        for (auto &Face : F)
        {
            Face.A = Map(Face.A);
            Face.B = Map(Face.B);
            Face.C = Map(Face.C);
        }
        if (!IsGraftGeometryValid(P, E, F))
        {
            continue;
        }
        const FVector PreviousCarrier = AgentPosition();
        const int32 NewAgent = Nodes.IsValidIndex(AgentNode) ? Map(AgentNode) : INDEX_NONE;
        TSet<uint64> Suppression;
        for (uint64 K : SuppressedPairs)
        {
            const int32 A = Map(int32(K >> 32)), B = Map(int32(uint32(K)));
            if (A != B)
            {
                Suppression.Add(PairKey(A, B));
            }
        }
        Nodes = MoveTemp(P);
        Tethers = MoveTemp(E);
        Triangles = MoveTemp(F);
        SuppressedPairs = MoveTemp(Suppression);
        for (const auto &Edge : Tethers)
        {
            SuppressedPairs.Remove(PairKey(Edge.A, Edge.B));
        }
        for (auto &N : Nodes)
        {
            N.AgentRole = EAttractionAgentRole::None;
            N.AgentSlideTarget = INDEX_NONE;
        }
        AgentNode = NewAgent;
        if (Nodes.IsValidIndex(AgentNode))
        {
            Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
        }
        RefreshConnectivity();
        RepairAgentCarrier(PreviousCarrier);
        for (int32 I = 0; I < Current.Num(); ++I)
        {
            Current[I] = Map(I);
        }
        for (int32 I = 0; I < Count; ++I)
        {
            UsedSources.Add(Matching[I].Source);
            UsedTargets.Add(Matching[I].Target);
        }
        bWelded = true;
        break;
    }
    // Original IDs survive candidate ordering. Compact each validated transaction,
    // then update the original-to-current map before considering the next pair.
    for (const auto &W : Welds)
    {
        if (UsedSources.Contains(W.Source) || UsedTargets.Contains(W.Target))
        {
            continue;
        }
        const int32 Source = Current[W.Source], Target = Current[W.Target];
        if (Source == Target)
        {
            continue;
        }
        const auto SourceComponent = ComponentNodes(Source), TargetComponent = ComponentNodes(Target);
        if (!Triangles.ContainsByPredicate([&](const FAttractionTriangle &F)
                                           { return SourceComponent.Contains(F.A) || TargetComponent.Contains(F.A); }))
        {
            continue;
        }
        auto ProposedNodes = Nodes;
        auto ProposedEdges = Tethers;
        auto ProposedFaces = Triangles;
        auto Remap = [&](int32 I)
        {
            const int32 J = I == Source ? Target : I;
            return J > Source ? J - 1 : J;
        };
        TSet<uint64> EdgeKeys;
        for (int32 I = ProposedEdges.Num() - 1; I >= 0; --I)
        {
            auto &E = ProposedEdges[I];
            E.A = Remap(E.A);
            E.B = Remap(E.B);
            if (E.A == E.B || EdgeKeys.Contains(PairKey(E.A, E.B)))
            {
                ProposedEdges.RemoveAt(I);
            }
            else
            {
                EdgeKeys.Add(PairKey(E.A, E.B));
            }
        }
        for (auto &F : ProposedFaces)
        {
            F.A = Remap(F.A);
            F.B = Remap(F.B);
            F.C = Remap(F.C);
        }
        ProposedNodes.RemoveAt(Source);
        for (auto &N : ProposedNodes)
        {
            N.AgentSlideTarget = INDEX_NONE;
            N.AgentRole = EAttractionAgentRole::None;
        }
        if (!IsGraftGeometryValid(ProposedNodes, ProposedEdges, ProposedFaces))
        {
            continue;
        }
        const FVector PreviousCarrier = AgentPosition();
        const int32 RemappedAgent = Nodes.IsValidIndex(AgentNode) ? Remap(AgentNode) : INDEX_NONE;
        TSet<uint64> RemappedSuppression;
        for (uint64 K : SuppressedPairs)
        {
            const int32 A = Remap(int32(K >> 32)), B = Remap(int32(uint32(K)));
            if (A != B)
            {
                RemappedSuppression.Add(PairKey(A, B));
            }
        }
        Nodes = MoveTemp(ProposedNodes);
        Tethers = MoveTemp(ProposedEdges);
        Triangles = MoveTemp(ProposedFaces);
        SuppressedPairs = MoveTemp(RemappedSuppression);
        for (const auto &E : Tethers)
        {
            SuppressedPairs.Remove(PairKey(E.A, E.B));
        }
        for (int32 &I : Current)
        {
            I = Remap(I);
        }
        UsedSources.Add(W.Source);
        UsedTargets.Add(W.Target);
        bWelded = true;
        AgentNode = RemappedAgent;
        if (Nodes.IsValidIndex(AgentNode))
        {
            Nodes[AgentNode].AgentRole = EAttractionAgentRole::Builder;
        }
        RefreshConnectivity();
        RepairAgentCarrier(PreviousCarrier);
    }
    if (bWelded)
    {
        return true;
    }
    struct FSeam
    {
        int32 A, B, C;
        double Distance;
    };
    TArray<FSeam> Seams;
    for (const auto &E : CarrierBoundaryEdges())
    {
        for (int32 C : Vertices)
        {
            const bool bBaseInPatch = Patch.Contains(E.A) && Patch.Contains(E.B);
            if ((!bBaseInPatch && (Patch.Contains(E.A) || Patch.Contains(E.B))) || Patch.Contains(C) == bBaseInPatch)
            {
                continue;
            }
            const double D = FMath::Min(FVector::DistSquared(Nodes[E.A].Position, Nodes[C].Position),
                                        FVector::DistSquared(Nodes[E.B].Position, Nodes[C].Position));
            if (D <= SnapSquared)
            {
                Seams.Add({E.A, E.B, C, D});
            }
        }
    }
    Seams.Sort(
        [](const FSeam &A, const FSeam &B)
        {
            return A.Distance != B.Distance ? A.Distance < B.Distance
                   : A.A != B.A             ? A.A < B.A
                   : A.B != B.B             ? A.B < B.B
                                            : A.C < B.C;
        });
    for (const auto &S : Seams)
    {
        auto ProposedEdges = Tethers;
        auto ProposedFaces = Triangles;
        for (int32 A : {S.A, S.B})
        {
            if (!HasTether(A, S.C))
            {
                FAttractionTether E;
                E.A = A;
                E.B = S.C;
                E.Rule.RestLength = FMath::Max(100.0f, EdgeRestLength);
                E.Rule.BreakStrain = 1.8f;
                ProposedEdges.Add(E);
            }
        }
        FAttractionTriangle Face;
        Face.A = S.A;
        Face.B = S.B;
        Face.C = S.C;
        ProposedFaces.Add(Face);
        if (!IsGraftGeometryValid(Nodes, ProposedEdges, ProposedFaces))
        {
            continue;
        }
        const FVector PreviousCarrier = AgentPosition();
        Tethers = MoveTemp(ProposedEdges);
        Triangles = MoveTemp(ProposedFaces);
        // Only this explicit, validated graft may restore these previously cut pairs.
        SuppressedPairs.Remove(PairKey(S.A, S.C));
        SuppressedPairs.Remove(PairKey(S.B, S.C));
        RefreshConnectivity();
        RepairAgentCarrier(PreviousCarrier);
        return true;
    }
    return false;
}

void AAttractionSimulation::NextStage()
{
    StageIndex = (StageIndex + 1) % FMath::Max(1, Stages.Num());
    ResetSimulation();
}
float AAttractionSimulation::TargetCoverage() const
{
    int32 Total = 0, Covered = 0;
    for (float X = -PlayfieldHalfExtentX; X <= PlayfieldHalfExtentX; X += 160)
    {
        for (float Y = -PlayfieldHalfExtentY; Y <= PlayfieldHalfExtentY; Y += 160)
        {
            const FVector P(X, Y, 0);
            if (SampleTargetHeight(FVector2D(P)) < TargetThreshold)
            {
                continue;
            }
            ++Total;
            bool Hit = false;
            for (const auto &N : Nodes)
            {
                if (N.bConnectedToRoot && FVector::DistSquared(P, N.Position) < 240 * 240)
                {
                    Hit = true;
                    break;
                }
            }
            if (Hit)
            {
                ++Covered;
            }
        }
    }
    return Total ? 100.f * Covered / Total : 0;
}
int32 AAttractionSimulation::ExcessNodes() const
{
    int32 C = 0;
    for (const auto &N : Nodes)
    {
        if (SampleTargetHeight(FVector2D(N.Position)) < TargetThreshold)
        {
            ++C;
        }
    }
    return C;
}
int32 AAttractionSimulation::ConnectedAgents() const
{
    int32 C = 0;
    for (const auto &N : Nodes)
    {
        if (N.bConnectedToRoot && N.AgentRole != EAttractionAgentRole::None)
        {
            ++C;
        }
    }
    return C;
}

void AAttractionSimulation::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    for (int32 I = PinchingFilaments.Num() - 1; I >= 0; --I)
    {
        PinchingFilaments[I].Age += FMath::Max(0.f, DeltaSeconds);
        if (PinchingFilaments[I].Age >= 0.22f)
        {
            PinchingFilaments.RemoveAt(I);
        }
    }
    AdvanceSimulation(DeltaSeconds);
    UpdateLiquidSurface();
}
