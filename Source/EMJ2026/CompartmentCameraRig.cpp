#include "CompartmentCameraRig.h"
#include "DirectOrScutSimSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
ACompartmentCameraRig::ACompartmentCameraRig()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));
    for(int32 F=0;F<9;++F)
    {
        auto* C=CreateDefaultSubobject<USceneCaptureComponent2D>(*FString::Printf(TEXT("Feed%d"),F));
        C->SetupAttachment(RootComponent);C->bCaptureEveryFrame=false;C->bCaptureOnMovement=false;
        C->FOVAngle=73.7398f; // 36 mm sensor, 24 mm rectilinear lens.
        C->PostProcessBlendWeight=0;C->ShowFlags.SetPostProcessing(false);Captures.Add(C);
    }
}
int32 ACompartmentCameraRig::RoomForFeed(int32 F) {const int32 Map[]={-1,0,2,3,4,5,6,7,1};return F>=0 && F<9?Map[F]:-1;}
int32 ACompartmentCameraRig::FeedForRoom(int32 R) {for(int32 F=1;F<9;++F)if(RoomForFeed(F)==R)return F;return 0;}
void ACompartmentCameraRig::OnConstruction(const FTransform& T)
{
    Super::OnConstruction(T);
    for(int32 F=1;F<9;++F)
    {
        const int32 R=RoomForFeed(F);auto Stage=UDirectOrScutSimSubsystem::DefaultSetTransform(R);
        if(auto* S=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())if(const auto* Registered=S->CompartmentSets.Find(static_cast<ERoomId>(R)))Stage=*Registered;
        const FVector P=Stage.WorldOrigin+FRotator(0,Stage.Yaw,0).RotateVector(FVector(R==1?-2700:-1000,0,210));
        Captures[F]->SetWorldLocationAndRotation(P,FRotator(0,Stage.Yaw,0));
    }
}
void ACompartmentCameraRig::BeginPlay()
{
    Super::BeginPlay();InitializeFeeds();
}
void ACompartmentCameraRig::InitializeFeeds()
{
    if(!Targets.IsEmpty())return;
    OnConstruction(GetActorTransform());
    for(int32 F=0;F<9;++F)
    {
        auto* RT=NewObject<UTextureRenderTarget2D>(this);RT->RenderTargetFormat=RTF_RGBA8;
        RT->InitAutoFormat(1024,576);RT->UpdateResourceImmediate(true);Targets.Add(RT);
        Captures[F]->TextureTarget=RT;Captures[F]->CaptureSource=SCS_FinalColorLDR;
        FeedMaterials.Add(nullptr);
    }
}
void ACompartmentCameraRig::SetVisibleFeeds(int32 P,int32 A,bool Grid) {Active=FMath::Clamp(P,1,8);PiP=A;bGrid=Grid;}
void ACompartmentCameraRig::Tick(float D)
{
    Super::Tick(D);CaptureClock+=D;if(CaptureClock<1.f/15)return;CaptureClock=0;
    for(int32 F=1;F<9;++F)if(bGrid || F==Active || F==PiP) {Captures[F]->CaptureScene();++CaptureCounts[F];}
}
