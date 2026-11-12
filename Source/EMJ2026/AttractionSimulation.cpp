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
    Tethers.Reset();
    PinchingFilaments.Reset();
    SuppressedPairs.Reset();
    Accumulator = 0;
    bRewardActive = false;
    if (Stages.IsEmpty())
    {
        FAttractionStageConfig A; A.Lobes = {FVector(-900,0,1600),FVector(900,350,1450),FVector(300,-900,1100)}; A.RootAnchors = {FVector2D(-1300,0)};
        FAttractionStageConfig B; B.Lobes = {FVector(-1500,-550,1300),FVector(0,300,1000),FVector(1550,650,1400)}; B.RootAnchors = {FVector2D(-1700,-550)};
        FAttractionStageConfig C; C.Lobes = {FVector(-1300,650,1400),FVector(1250,650,1400),FVector(0,-1050,1500)}; C.RootAnchors = {FVector2D(0,-1300)};
        Stages = {A,B,C};
    }
    StageIndex = FMath::Clamp(StageIndex,0,Stages.Num()-1);
    if (bUseDemo)
    {
        Nodes.Reset();
        for (const FVector2D& Root : Stages[StageIndex].RootAnchors)
        {
            FAttractionNode N; N.Position = FVector(Root,0); N.bRootAnchor = true; N.bPinned = true;
            const int32 Base = Nodes.Add(N);
            for (int32 I=0; I<3; ++I)
            {
                N.bRootAnchor = N.bPinned = false;
                N.AgentRole = static_cast<EAttractionAgentRole>(I+1);
                N.Position = FVector(Root,0)+FVector(240, (I-1)*230,0);
                const int32 Child = Nodes.Add(N); AddStructuralTether(Base,Child);
                if (I>0) AddStructuralTether(Child-1,Child);
            }
        }
    }
    for (auto& N : Nodes) { N.bPinned=N.bRootAnchor; N.Position.Z=0; N.Velocity=FVector::ZeroVector; N.WorkCharge=0; }
    RefreshConnectivity();
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
    int32 NodeCount = FMath::Min(Nodes.Num(),Width);
    for (int32 I=0; I<NodeCount; ++I)
    {
        const auto& N=Nodes[I];
        const float RoleEnergy=N.AgentRole==EAttractionAgentRole::Builder?1.f:N.AgentRole==EAttractionAgentRole::Filler?0.72f:0.45f;
        const float Energy=!N.bConnectedToRoot?0.02f:N.AgentRole==EAttractionAgentRole::None?0.25f:RoleEnergy*(RewardWeight(N.Position)>0?1.f:0.32f);
        Records[I] = FVector4f(float(Nodes[I].Position.X),float(Nodes[I].Position.Y),FMath::Max(20.f,DropletRadius),Energy);
    }
    // Energy travels continuously along the selected edge while the carrier lattice stays intact.
    for(int32 I=0;I<NodeCount;++I) if(Nodes.IsValidIndex(Nodes[I].AgentSlideTarget)&&Nodes[I].AgentSlideTarget<NodeCount)
    {const int32 J=Nodes[I].AgentSlideTarget;const float A=FMath::Clamp(Nodes[I].AgentSlideProgress,0.f,1.f);const float Energy=Records[I].W;Records[I].W=FMath::Lerp(Energy,0.25f,A);Records[J].W=FMath::Lerp(0.25f,Energy,A);}
    const int32 CarrierCount=NodeCount;
    for(int32 I=0;I<CarrierCount && NodeCount<Width;++I)
    {
        const auto& N=Nodes[I];
        if(!Nodes.IsValidIndex(N.AgentSlideTarget))continue;
        const FVector P=FMath::Lerp(N.Position,Nodes[N.AgentSlideTarget].Position,FMath::Clamp(N.AgentSlideProgress,0.f,1.f));
        Records[NodeCount++]=FVector4f(P.X,P.Y,65.f,0.95f);
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

        const float Pinch = Stretch*Stretch*(3-2*Stretch);
        AddTether(A,B,FMath::Max(10.f,FilamentRadius)*(1.f-0.88f*Pinch),Pinch);
    }
    // Live tethers take priority if an authored board exceeds the 256-record render capacity.
    for (const FPinchingFilament& Cut : PinchingFilaments)
    {
        const float Fade = 1.f-FMath::Clamp(Cut.Age/0.22f,0.f,1.f);
        AddTether(Cut.A,Cut.B,Cut.Radius,1.f-(Cut.Amplitude/0.65f)*Fade);
    }
    for(int32 I=0;I<3;++I) {const FVector L=Stages.IsValidIndex(StageIndex)&&Stages[StageIndex].Lobes.IsValidIndex(I)?Stages[StageIndex].Lobes[I]:FVector(0,0,1);LiquidMID->SetVectorParameterValue(FName(*FString::Printf(TEXT("TargetLobe%d"),I)),FLinearColor(L.X,L.Y,L.Z,0));}
    LiquidMID->SetScalarParameterValue(TEXT("RewardActive"),bRewardActive?1.f:0.f);
    LiquidMID->SetVectorParameterValue(TEXT("RewardDisc"),FLinearColor(RewardCenter.X,RewardCenter.Y,RewardRadius,bRewardActive?1:0));
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
    RefreshConnectivity();
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

void AAttractionSimulation::AdvanceSimulation(float DeltaSeconds)
{
    if (!bRunning || !FMath::IsFinite(DeltaSeconds)) return;
    constexpr double FixedDt = 1.0/120.0;
    Accumulator = FMath::Min(Accumulator+FMath::Max(0.f,DeltaSeconds),16*FixedDt);
    while (Accumulator>=FixedDt) { Step(float(FixedDt)); Accumulator -= FixedDt; }
}

void AAttractionSimulation::Step(float Dt)
{
    RefreshConnectivity();
    WorkAgents(Dt);
    TArray<FVector> Forces; Forces.Init(FVector::ZeroVector,Nodes.Num());
    for (int32 A=0; A<Nodes.Num(); ++A)
    for (int32 B=A+1; B<Nodes.Num(); ++B)
    {
        const FVector D=Nodes[B].Position-Nodes[A].Position;
        const float L=D.Size();
        const FVector F=D.GetSafeNormal()*220.f*FMath::Square(FMath::Max(0.f,1.f-L/230.f));
        Forces[A]-=F; Forces[B]+=F;
    }
    for (int32 I=Tethers.Num()-1; I>=0; --I)
    {
        const auto T=Tethers[I];
        const FVector D=Nodes[T.B].Position-Nodes[T.A].Position;
        if (D.Size()>T.Rule.RestLength*2.8f) { SliceTether(I); continue; }
        const FVector F=D.GetSafeNormal()*((D.Size()-T.Rule.RestLength)*4.f+FVector::DotProduct(Nodes[T.B].Velocity-Nodes[T.A].Velocity,D.GetSafeNormal())*3.f);
        Forces[T.A]+=F; Forces[T.B]-=F;
    }
    // Incident edges repel angularly; forces remain local and reciprocal.
    for (int32 Center=0; Center<Nodes.Num(); ++Center)
    {
        TArray<int32> Neighbors;
        for (const auto& T:Tethers) { if (T.A==Center) Neighbors.Add(T.B); else if(T.B==Center) Neighbors.Add(T.A); }
        for(int32 A=0; A<Neighbors.Num(); ++A) for(int32 B=A+1; B<Neighbors.Num(); ++B)
        {
            const int32 U=Neighbors[A], V=Neighbors[B];
            const FVector DU=(Nodes[U].Position-Nodes[Center].Position).GetSafeNormal(), DV=(Nodes[V].Position-Nodes[Center].Position).GetSafeNormal();
            const float Pressure=FMath::Max(0.f,float(FVector::DotProduct(DU,DV))-0.4f)*65.f;
            const FVector FU=(DU-DV*FVector::DotProduct(DU,DV))*Pressure;
            const FVector FV=(DV-DU*FVector::DotProduct(DU,DV))*Pressure;
            Forces[U]+=FU; Forces[V]+=FV; Forces[Center]-=FU+FV;
        }
    }
    for(int32 I=0; I<Nodes.Num(); ++I)
    {
        auto& N=Nodes[I];
        if(N.bRootAnchor) { N.Velocity=FVector::ZeroVector; continue; }
        const float Exterior=SampleTargetHeight(FVector2D(N.Position))<TargetThreshold ? 2.f : 0.f;
        FVector F=Forces[I];
        F.X-=FMath::Sign(N.Position.X)*FMath::Max(0.f,float(FMath::Abs(N.Position.X))-PlayfieldHalfExtentX+150)*3;
        F.Y-=FMath::Sign(N.Position.Y)*FMath::Max(0.f,float(FMath::Abs(N.Position.Y))-PlayfieldHalfExtentY+150)*3;
        N.Velocity=((N.Velocity+F.GetClampedToMaxSize(MaxAcceleration)*Dt)*FMath::Exp(-(1.5f+Exterior)*Dt)).GetClampedToMaxSize(400);
        N.Position+=N.Velocity*Dt;
    }
    RefreshConnectivity();
}

float AAttractionSimulation::SampleTargetHeight(FVector2D P) const
{
    if(!Stages.IsValidIndex(StageIndex)) return 0;
    float Complement=1;
    for(const FVector& L:Stages[StageIndex].Lobes)
    {
        const float Q=FMath::Clamp(1.f-float(FVector2D::DistSquared(P,FVector2D(L)))/(L.Z*L.Z),0.f,1.f);
        Complement*=1.f-Q*Q*(3-2*Q);
    }
    return 1-Complement;
}
void AAttractionSimulation::SetReward(FVector2D Center,bool bActive)
{
    if(!FMath::IsFinite(Center.X)||!FMath::IsFinite(Center.Y))return;
    RewardCenter=Center;bRewardActive=bActive;
}
float AAttractionSimulation::RewardWeight(const FVector& P) const
{
    if(!bRewardActive) return 0;
    const float Q=FMath::Clamp(1.f-float(FVector2D::Distance(FVector2D(P),RewardCenter))/FMath::Max(1.f,RewardRadius),0.f,1.f);
    return Q*Q*(3-2*Q);
}
void AAttractionSimulation::RefreshConnectivity()
{
    TArray<int32> Queue;
    for(int32 I=0;I<Nodes.Num();++I) { Nodes[I].bConnectedToRoot=Nodes[I].bRootAnchor; if(Nodes[I].bRootAnchor) Queue.Add(I); }
    for(int32 Q=0;Q<Queue.Num();++Q) for(const auto& T:Tethers)
    {
        const int32 B=T.A==Queue[Q]?T.B:T.B==Queue[Q]?T.A:INDEX_NONE;
        if(Nodes.IsValidIndex(B) && !Nodes[B].bConnectedToRoot) {Nodes[B].bConnectedToRoot=true;Queue.Add(B);}
    }
}
bool AAttractionSimulation::AddStructuralTether(int32 A,int32 B)
{
    if(A==B || !Nodes.IsValidIndex(A)||!Nodes.IsValidIndex(B)||Tethers.Num()>=256) return false;
    for(const auto& T:Tethers) if(PairKey(A,B)==PairKey(T.A,T.B)) return false;
    if(SuppressedPairs.Contains(PairKey(A,B))) return false;
    FAttractionTether T; T.A=A;T.B=B;T.Rule.RestLength=FMath::Clamp(float(FVector::Distance(Nodes[A].Position,Nodes[B].Position)),180.f,300.f);T.Rule.BreakStrain=1.8f;
    Tethers.Add(T);return true;
}
void AAttractionSimulation::RemoveNode(int32 Index)
{
    if(!Nodes.IsValidIndex(Index)||Nodes[Index].bRootAnchor) return;
    for(int32 I=Tethers.Num()-1;I>=0;--I) if(Tethers[I].A==Index||Tethers[I].B==Index) Tethers.RemoveAt(I);
    for(auto& T:Tethers) {if(T.A>Index)--T.A;if(T.B>Index)--T.B;}
    TSet<uint64> Remapped;
    for(uint64 K:SuppressedPairs) {int32 A=int32(K>>32),B=int32(uint32(K));if(A==Index||B==Index)continue;if(A>Index)--A;if(B>Index)--B;Remapped.Add(PairKey(A,B));}
    SuppressedPairs=MoveTemp(Remapped);Nodes.RemoveAt(Index);RefreshConnectivity();
}
void AAttractionSimulation::WorkAgents(float Dt)
{
    if(!bRewardActive || bManipulatingAgents) return; // Hard authorization boundary: physics continues, topology work cannot.
    const int32 Count=Nodes.Num();
    for(int32 I=0;I<Count && I<Nodes.Num();++I)
    {
        auto& N=Nodes[I]; const float Weight=RewardWeight(N.Position);
        if(!N.bConnectedToRoot||N.AgentRole==EAttractionAgentRole::None||Weight<=0) continue;
        float Crowding=0,Strain=0;
        for(int32 J=0;J<Nodes.Num();++J) if(J!=I && FVector::DistSquared(N.Position,Nodes[J].Position)<230*230) ++Crowding;
        for(const auto& T:Tethers) if(T.A==I||T.B==I) Strain=FMath::Max(Strain,float(FVector::Distance(Nodes[T.A].Position,Nodes[T.B].Position))/T.Rule.RestLength-1);
        const float H=SampleTargetHeight(FVector2D(N.Position));
        N.WorkCharge+=Dt*Weight*FMath::Min(2.f,1.f+0.15f*(N.PotencyTier-1))/(1+Crowding*0.3f+FMath::Max(0.f,Strain)*4+(H<TargetThreshold?2:0));
        if(N.WorkCharge<0.9f)continue;
        N.WorkCharge=0;
        const FVector Origin=N.Position; const auto AgentRoleValue=N.AgentRole;
        if(AgentRoleValue==EAttractionAgentRole::Trimmer)
        {
            TSet<int32> Adjacent;for(const auto& T:Tethers){if(T.A==I)Adjacent.Add(T.B);else if(T.B==I)Adjacent.Add(T.A);}
            int32 Victim=INDEX_NONE;float Lowest=2;
            for(int32 J=0;J<Nodes.Num();++J) if(Adjacent.Contains(J)&&!Nodes[J].bRootAnchor && Nodes[J].AgentRole==EAttractionAgentRole::None && RewardWeight(Nodes[J].Position)>0 && FVector::DistSquared(Origin,Nodes[J].Position)<420*420)
            {const float V=SampleTargetHeight(FVector2D(Nodes[J].Position));if(V<Lowest){Lowest=V;Victim=J;}}
            if(Victim!=INDEX_NONE) {N.WorkCharge=Lowest<TargetThreshold?0:-3.f;RemoveNode(Victim);return;}
            // Inward creep transfers only over an existing edge.
            int32 Best=I;float BestHeight=H;
            for(const auto& T:Tethers) {const int32 J=T.A==I?T.B:T.B==I?T.A:INDEX_NONE;if(Nodes.IsValidIndex(J)&&Nodes[J].AgentRole==EAttractionAgentRole::None&&RewardWeight(Nodes[J].Position)>0&&SampleTargetHeight(FVector2D(Nodes[J].Position))>BestHeight){Best=J;BestHeight=SampleTargetHeight(FVector2D(Nodes[J].Position));}}
            if(Best!=I){Nodes[Best].AgentRole=AgentRoleValue;Nodes[Best].PotencyTier=N.PotencyTier;Nodes[Best].WorkCharge=-3;N.AgentRole=EAttractionAgentRole::None;}
            continue;
        }
        if(AgentRoleValue==EAttractionAgentRole::Filler)
        {
            if(H<TargetThreshold)continue;
            TArray<int32> Local;
            for(int32 J=0;J<Nodes.Num();++J) if(J!=I&&Nodes[J].bConnectedToRoot&&RewardWeight(Nodes[J].Position)>0&&FVector::DistSquared(Origin,Nodes[J].Position)<500*500)Local.Add(J);
            bool Done=false;
            for(int32 A=0;A<Local.Num()&&!Done;++A) for(int32 B=A+1;B<Local.Num()&&!Done;++B)
            {
                const FVector P=Nodes[Local[A]].Position,Q=Nodes[Local[B]].Position;
                if(FVector::Distance(P,Q)>390)continue;
                bool Interior=true;for(int32 K=0;K<=8;++K) if(SampleTargetHeight(FVector2D(FMath::Lerp(P,Q,K/8.f)))<TargetThreshold)Interior=false;
                if(Interior)Done=AddStructuralTether(Local[A],Local[B]);
            }
            if(!Done && Nodes.Num()<128 && Tethers.Num()<252)
            {
                for(int32 J:Local)
                {
                    const FVector Mid=(Origin+Nodes[J].Position)*0.5f;
                    const float Distance=FVector::Distance(Origin,Nodes[J].Position);
                    if(Distance<330||Distance>490||SampleTargetHeight(FVector2D(Mid))<TargetThreshold)continue;
                    bool Clear=true;for(const auto& Other:Nodes)if(FVector::DistSquared(Mid,Other.Position)<155*155)Clear=false;
                    for(int32 K=0;K<=8;++K)if(SampleTargetHeight(FVector2D(FMath::Lerp(Origin,Nodes[J].Position,K/8.f)))<TargetThreshold)Clear=false;
                    if(!Clear)continue;
                    FAttractionNode Fill;Fill.Position=Mid;const int32 New=Nodes.Add(Fill);AddStructuralTether(I,New);AddStructuralTether(J,New);RefreshConnectivity();Done=true;break;
                }
            }
            // Filler can ride an interior edge toward reward, but cannot traverse a saddle.
            if(!Done)
            {
                int32 Best=I;float Distance=FVector2D::DistSquared(FVector2D(Origin),RewardCenter);
                for(const auto& T:Tethers)
                {
                    const int32 J=T.A==I?T.B:T.B==I?T.A:INDEX_NONE;
                    if(!Nodes.IsValidIndex(J)||Nodes[J].AgentRole!=EAttractionAgentRole::None||!Nodes[J].bConnectedToRoot||RewardWeight(Nodes[J].Position)<=0)continue;
                    bool Interior=true;for(int32 K=0;K<=8;++K)if(SampleTargetHeight(FVector2D(FMath::Lerp(Origin,Nodes[J].Position,K/8.f)))<TargetThreshold)Interior=false;
                    const float D=FVector2D::DistSquared(FVector2D(Nodes[J].Position),RewardCenter);
                    if(Interior&&D<Distance){Best=J;Distance=D;}
                }
                if(Best!=I){Nodes[Best].AgentRole=AgentRoleValue;Nodes[Best].PotencyTier=Nodes[I].PotencyTier;Nodes[I].AgentRole=EAttractionAgentRole::None;}
            }
            continue;
        }
        if(Nodes.Num()>=128||Tethers.Num()>252)continue;
        const FVector2D P(Origin); const float E=60;
        const FVector Gradient(SampleTargetHeight(P+FVector2D(E,0))-SampleTargetHeight(P-FVector2D(E,0)),SampleTargetHeight(P+FVector2D(0,E))-SampleTargetHeight(P-FVector2D(0,E)),0);
        const FVector RewardDirection=(FVector(RewardCenter,0)-Origin).GetSafeNormal();
        N.GrowthDirection=(N.GrowthDirection*0.78f+Gradient.GetSafeNormal()*0.10f+RewardDirection*0.12f).GetSafeNormal();
        const FVector Direction=N.GrowthDirection;
        const FVector Side(-Direction.Y,Direction.X,0);
        const FVector P1=Origin+Direction*240+Side*115,P2=Origin+Direction*240-Side*115;
        bool Clear=IsInsidePlayfield(P1)&&IsInsidePlayfield(P2);
        for(int32 J=0;J<Nodes.Num();++J) if(J!=I&&(FVector::DistSquared(P1,Nodes[J].Position)<170*170||FVector::DistSquared(P2,Nodes[J].Position)<170*170))Clear=false;
        if(!Clear)continue;
        const int32 Tier=N.PotencyTier; N.AgentRole=EAttractionAgentRole::None;
        FAttractionNode Child;Child.Position=P1;Child.GrowthDirection=Direction;Child.PotencyTier=Tier;Child.AgentRole=AgentRoleValue;
        const int32 A=Nodes.Add(Child);Child.Position=P2;Child.AgentRole=EAttractionAgentRole::None;const int32 B=Nodes.Add(Child);
        AddStructuralTether(I,A);AddStructuralTether(I,B);AddStructuralTether(A,B);RefreshConnectivity();
    }
}
int32 AAttractionSimulation::PickNode(const FVector& P) const
{
    int32 Best=INDEX_NONE;float D=160*160;
    for(int32 I=0;I<Nodes.Num();++I) if(!Nodes[I].bConnectedToRoot||Nodes[I].AgentRole!=EAttractionAgentRole::None) {const float Q=FVector::DistSquared(P,Nodes[I].Position);if(Q<D){D=Q;Best=I;}}
    return Best;
}
void AAttractionSimulation::SlideAgent(int32& Index,const FVector& P)
{
    if(!Nodes.IsValidIndex(Index)||!Nodes[Index].bConnectedToRoot)return;
    int32 Best=Index;float D=FVector::DistSquared(Nodes[Index].Position,P);
    for(const auto& T:Tethers){const int32 J=T.A==Index?T.B:T.B==Index?T.A:INDEX_NONE;if(Nodes.IsValidIndex(J)&&Nodes[J].bConnectedToRoot&&Nodes[J].AgentRole==EAttractionAgentRole::None&&FVector::DistSquared(Nodes[J].Position,P)<D){Best=J;D=FVector::DistSquared(Nodes[J].Position,P);}}
    if(Best==Index){Nodes[Index].AgentSlideTarget=INDEX_NONE;Nodes[Index].AgentSlideProgress=0;return;}
    if(Nodes[Index].AgentSlideTarget!=Best){Nodes[Index].AgentSlideTarget=Best;Nodes[Index].AgentSlideProgress=0;}
    Nodes[Index].AgentSlideProgress+=GetWorld()->GetDeltaSeconds()*4;
    if(Nodes[Index].AgentSlideProgress<1)return;
    Nodes[Index].AgentSlideProgress=0;Nodes[Index].AgentSlideTarget=INDEX_NONE;
    if(Best!=Index){Nodes[Best].AgentRole=Nodes[Index].AgentRole;Nodes[Best].PotencyTier=Nodes[Index].PotencyTier;Nodes[Best].GrowthDirection=Nodes[Index].GrowthDirection;Nodes[Best].WorkCharge=0;Nodes[Index].AgentRole=EAttractionAgentRole::None;Index=Best;}
}
void AAttractionSimulation::DragPatch(int32 Index,const FVector& Delta)
{
    if(!Nodes.IsValidIndex(Index)||Nodes[Index].bConnectedToRoot)return;
    TArray<int32> Patch={Index};for(int32 Q=0;Q<Patch.Num();++Q)for(const auto& T:Tethers){const int32 J=T.A==Patch[Q]?T.B:T.B==Patch[Q]?T.A:INDEX_NONE;if(J!=INDEX_NONE&&!Patch.Contains(J))Patch.Add(J);}
    for(int32 J:Patch){Nodes[J].Position+=Delta.GetClampedToMaxSize(90);Nodes[J].Velocity=FVector::ZeroVector;}
}
void AAttractionSimulation::GraftPatch(int32 Index)
{
    if(!Nodes.IsValidIndex(Index)||Nodes[Index].bConnectedToRoot)return;
    TArray<int32> Patch={Index};for(int32 Q=0;Q<Patch.Num();++Q)for(const auto& T:Tethers){const int32 J=T.A==Patch[Q]?T.B:T.B==Patch[Q]?T.A:INDEX_NONE;if(J!=INDEX_NONE&&!Patch.Contains(J))Patch.Add(J);}
    for(int32 A:Patch)for(int32 B=0;B<Nodes.Num();++B)if(Nodes[B].bConnectedToRoot&&FVector::Distance(Nodes[A].Position,Nodes[B].Position)<=MaxAttachDist){SuppressedPairs.Remove(PairKey(A,B));AddStructuralTether(A,B);}
    RefreshConnectivity();
}
void AAttractionSimulation::NextStage()
{
    RefreshConnectivity();TArray<FAttractionNode> Survivors;
    for(const auto& N:Nodes)if(N.bConnectedToRoot&&N.AgentRole!=EAttractionAgentRole::None)Survivors.Add(N);
    StageIndex=(StageIndex+1)%FMath::Max(1,Stages.Num());ResetSimulation();
    if(Nodes.IsEmpty()) return;
    for(auto& N:Nodes)N.AgentRole=EAttractionAgentRole::None;
    for(int32 I=0;I<Survivors.Num()&&Nodes.Num()<128;++I){auto N=Survivors[I];N.PotencyTier++;N.bRootAnchor=N.bPinned=false;N.WorkCharge=0;N.Position=Nodes[0].Position+FVector(240+(I/5)*220,(I%5-2)*220,0);N.Velocity=FVector::ZeroVector;const int32 J=Nodes.Add(N);AddStructuralTether(0,J);}
    RefreshConnectivity();
}
float AAttractionSimulation::TargetCoverage() const
{
    int32 Total=0,Covered=0;
    for(float X=-PlayfieldHalfExtentX;X<=PlayfieldHalfExtentX;X+=160)for(float Y=-PlayfieldHalfExtentY;Y<=PlayfieldHalfExtentY;Y+=160)
    {const FVector P(X,Y,0);if(SampleTargetHeight(FVector2D(P))<TargetThreshold)continue;++Total;bool Hit=false;for(const auto& N:Nodes)if(N.bConnectedToRoot&&FVector::DistSquared(P,N.Position)<240*240){Hit=true;break;}if(Hit)++Covered;}
    return Total?100.f*Covered/Total:0;
}
int32 AAttractionSimulation::ExcessNodes() const {int32 C=0;for(const auto& N:Nodes)if(SampleTargetHeight(FVector2D(N.Position))<TargetThreshold)++C;return C;}
int32 AAttractionSimulation::ConnectedAgents() const {int32 C=0;for(const auto& N:Nodes)if(N.bConnectedToRoot&&N.AgentRole!=EAttractionAgentRole::None)++C;return C;}

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
