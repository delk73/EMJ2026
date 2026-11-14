#include "CrewUnitBlockout.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
ACrewUnitBlockout::ACrewUnitBlockout()
{
    Body=CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Body")); RootComponent=Body;
    AutoPossessAI=EAutoPossessAI::Disabled;
}
void ACrewUnitBlockout::BeginPlay()
{
    Super::BeginPlay(); DirectOrScut::Materials(Body);
    if(!bAndroid)if(auto* Suit=Cast<UMaterialInstanceDynamic>(Body->GetMaterial(SuitSlot)))Suit->SetVectorParameterValue(TEXT("BaseTint"),SimUnitIndex==0?FLinearColor(.9f,.015f,.01f):SimUnitIndex==1?FLinearColor(.02f,.08f,.9f):FLinearColor(.95f,.7f,.015f));
    auto* M=Body->GetDynamicMesh();
    const float W=bAndroid ? 85.f : 80.f;
    const float H=bAndroid ? 185.f : 180.f;
    // Footprint is exactly W x W; base is Z=0. Faceted round human / square android.
    auto Profile=DirectOrScut::Octagon(W,W,bAndroid ? 8.f : 23.43146f);
    DirectOrScut::Prism(M,Profile,12,FTransform::Identity,7);
    DirectOrScut::Prism(M,DirectOrScut::Octagon(56,54,bAndroid ? 5 : 16),65,FTransform(FVector(0,0,12)),bAndroid ? 1 : SuitSlot);
    DirectOrScut::Prism(M,Profile,61,FTransform(FVector(0,0,77)),bAndroid ? 1 : SuitSlot);
    DirectOrScut::Prism(M,DirectOrScut::Octagon(56,56,bAndroid ? 4 : 16),H-138,FTransform(FVector(0,0,138)),1);
    DirectOrScut::Box(M,FVector(-29,0,H-22),FVector(4,46,bAndroid ? 7.f : 19.f),bAndroid ? 4 : 7);
    DirectOrScut::Box(M,FVector(-W/2+0.25f,0,111),FVector(0.5,42,9),3);
    if (bAndroid)
    {
        auto* Chrome=UMaterialInstanceDynamic::Create(Body->GetMaterial(1)->GetMaterial(),this);
        Chrome->SetScalarParameterValue(TEXT("Metallic"),1.f);
        Chrome->SetScalarParameterValue(TEXT("Roughness"),.18f);
        Body->SetMaterial(9,Chrome);
        for (float Y : {-28.f,28.f}) DirectOrScut::Box(M,FVector(0,Y,50),FVector(32,15,62),9);
        DirectOrScut::Box(M,FVector(0,0,145),FVector(52,52,12),9);
    }
    DirectOrScut::Finish(Body);
    Body->SetCollisionResponseToChannel(DirectOrScut::CrewClickChannel,ECR_Block);
    if (auto* Sim=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>()) Sim->RegisterCrew(this);
}

bool ACrewUnitBlockout::DirectToRoom(int32 Room)
{
    auto* Sim=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    return Sim && Sim->AssignRoom(SimUnitIndex,Room);
}
const FDirectOrScutUnitState* ACrewUnitBlockout::GetSimulationState() const
{
    auto* Sim=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    return Sim && SimUnitIndex>=0 && SimUnitIndex<12?&Sim->Simulation.Bodies[SimUnitIndex]:nullptr;
}

void ACrewUnitBlockout::ApplyStagingPose(const FCrewStagingState& Staging,uint64 Tick,float BPM)
{
    const float Wave=FMath::Sin(float(Tick)*BPM*2*PI/1800);
    float Pitch=Staging.Posture==ECrewStagingPosture::Incapacitated?90.f:Staging.Posture==ECrewStagingPosture::Panic?18.f:Staging.Posture==ECrewStagingPosture::Triage?35.f:0.f;
    if(Staging.Posture==ECrewStagingPosture::Panic || Staging.Posture==ECrewStagingPosture::Blocked)Pitch+=Wave*4;
    SetActorLocationAndRotation(Staging.Position,FRotator(Pitch,Staging.Yaw,0));
}
