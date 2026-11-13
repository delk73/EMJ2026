#include "DirectOrScutHullActor.h"
#include "RoomSetBase.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "Components/ChildActorComponent.h"
#include "CrewUnitBlockout.h"
#include "DoorActor.h"
ADirectOrScutHullActor::ADirectOrScutHullActor()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("SoundstageRoot"));
    const TSubclassOf<ARoomSetBase> Classes[]={AReactorCoreSet::StaticClass(),ARingCorridorSet::StaticClass(),ABridgeSet::StaticClass(),AMedBaySet::StaticClass(),AObservationDeckSet::StaticClass(),AQuartersMessSet::StaticClass(),AServerArchiveSet::StaticClass(),AAirlockChamberSet::StaticClass()};
    for(int32 R=0;R<8;++R)
    {
        auto* C=CreateDefaultSubobject<UChildActorComponent>(*FString::Printf(TEXT("RoomSet%d"),R));
        C->SetupAttachment(RootComponent); C->SetChildActorClass(Classes[R]); RoomSets.Add(C);
    }
}
void ADirectOrScutHullActor::OnConstruction(const FTransform& T)
{
    Super::OnConstruction(T);
    for(int32 R=0;R<8;++R)
    {
        const auto Stage=UDirectOrScutSimSubsystem::DefaultSetTransform(R);
        RoomSets[R]->SetRelativeLocationAndRotation(Stage.WorldOrigin,FRotator(0,Stage.Yaw,0));
        if(auto* Set=Cast<ARoomSetBase>(RoomSets[R]->GetChildActor())) {Set->RoomId=R;Set->OnConstruction(Set->GetActorTransform());}
    }
}
void ADirectOrScutHullActor::BeginPlay()
{
    Super::BeginPlay();
    auto* S=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    const auto Sites=DirectOrScut::DoorSites();
    for(int32 D=0;D<Sites.Num();++D)
    {
        const auto& Site=Sites[D];
        // Bulkheads dress the side walls; the logical door graph retains its original coordinates.
        const FVector Local=D<6?FVector(-1800+D*600,875,0):D<8?FVector(-500,D==6?-875:875,0):FVector(1000,0,0);
        const auto Stage=UDirectOrScutSimSubsystem::DefaultSetTransform(Site.RoomA);
        const FTransform T(FRotator(0,Stage.Yaw+(D==8?0:90),0),Stage.WorldOrigin+FRotator(0,Stage.Yaw,0).RotateVector(Local),D==8?FVector(1.7):FVector::OneVector);
        auto* Door=GetWorld()->SpawnActorDeferred<ADoorActor>(ADoorActor::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Door->SimDoorIndex=D;Door->bExterior=Site.bExterior;Door->FinishSpawning(T);
    }
    for(int32 I=0;I<4;++I)
    {
        const FTransform T(UDirectOrScutSimSubsystem::MapSimulationToSet(I+2,FVector2D(DirectOrScut::CrewSpawnPoint(I))));
        auto* Unit=GetWorld()->SpawnActorDeferred<ACrewUnitBlockout>(ACrewUnitBlockout::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Unit->SimUnitIndex=I; Unit->bAndroid=I==3;Unit->SuitSlot=I==0?4:I==1?6:3;Unit->FinishSpawning(T);
    }
    if(S)S->FinishLayout();
}
