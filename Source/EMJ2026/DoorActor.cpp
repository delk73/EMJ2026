#include "DoorActor.h"
#include "DirectOrScutGeometry.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "Components/BoxComponent.h"

ADoorActor::ADoorActor()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Slab=CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("SlidingSlab"));
    Slab->SetupAttachment(RootComponent);
    Sill=CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("OpticalSill"));
    Sill->SetupAttachment(RootComponent);
    ClickTarget=CreateDefaultSubobject<UBoxComponent>(TEXT("DoorwayClickTarget"));
    ClickTarget->SetupAttachment(RootComponent);
    ClickTarget->SetRelativeLocation(FVector(0,0,130));
    // Local Y is doorway width; local X is depth: 240 x 180 x 260uu.
    ClickTarget->SetBoxExtent(FVector(90,120,130));
    ClickTarget->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ClickTarget->SetCollisionResponseToAllChannels(ECR_Ignore);
    ClickTarget->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    ClickTarget->SetCollisionResponseToChannel(DirectOrScut::DoorClickChannel,ECR_Block);
    ClickTarget->SetGenerateOverlapEvents(false);
}
void ADoorActor::BeginPlay()
{
    Super::BeginPlay();
    DirectOrScut::Materials(Slab);
    DirectOrScut::Materials(Sill);
    // Profile X becomes local Y; profile Y becomes world Z. Extrusion is local X.
    const FQuat Rotation=FMatrix(FPlane(0,1,0,0),FPlane(0,0,1,0),FPlane(1,0,0,0),FPlane(0,0,0,1)).ToQuat();
    DirectOrScut::Prism(Slab->GetDynamicMesh(),DirectOrScut::Octagon(200,240,30),30,
        FTransform(Rotation,FVector(-15,0,120)),1);
    DirectOrScut::Box(Slab->GetDynamicMesh(),FVector(-17,0,130),FVector(4,48,12),4);
    DirectOrScut::Box(Slab->GetDynamicMesh(),FVector(17,0,130),FVector(4,48,12),4);
    DirectOrScut::Box(Slab->GetDynamicMesh(),FVector(-17,0,45),FVector(4,110,5),bExterior?4:5);
    DirectOrScut::Box(Sill->GetDynamicMesh(),FVector(0,0,-132),FVector(86,250,260),7);
    DirectOrScut::Box(Sill->GetDynamicMesh(),FVector(0,0,1),FVector(86,250,2),5);
    DirectOrScut::Box(Sill->GetDynamicMesh(),FVector(0,0,2.5),FVector(16,180,1),4);
    DirectOrScut::Finish(Slab);
    DirectOrScut::Finish(Sill);
}
void ADoorActor::Toggle()
{
    bOpen=!bOpen;
    Sill->SetMaterial(4,bOpen?Sill->GetMaterial(8):Slab->GetMaterial(4));
}
void ADoorActor::Tick(float Dt)
{
    Super::Tick(Dt);
    Slide=FMath::FInterpConstantTo(Slide,bOpen ? 250.f : 0.f,Dt,180.f);
    Slab->SetRelativeLocation(FVector(0,0,-Slide));
}
