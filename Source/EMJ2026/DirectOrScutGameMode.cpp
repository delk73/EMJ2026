#include "DirectOrScutGameMode.h"
#include "DirectOrScutHullActor.h"
#include "DirectOrScutGeometry.h"
#include "Components/BoxComponent.h"
#include "DoorActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "CrewUnitBlockout.h"
#include "Components/DynamicMeshComponent.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

void ADirectOrScutPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent(); InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ADirectOrScutPlayerController::ClickDoor);
}
void ADirectOrScutPlayerController::BeginPlay()
{
    Super::BeginPlay(); bShowMouseCursor=true; SetInputMode(FInputModeGameAndUI());
    bAutoManageActiveCameraTarget=false;
    for (TActorIterator<ACameraActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DirectOrScutCamera"))) { SetViewTarget(*It); break; }
}
bool ADirectOrScutPlayerController::ToggleDoorAtScreenPosition(const FVector2D& ScreenPosition)
{
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(ScreenPosition,DirectOrScut::DoorClickChannel,true,Hit))
        if (auto* Door=Cast<ADoorActor>(Hit.GetActor())) { Door->Toggle(); return true; }
    return false;
}
void ADirectOrScutPlayerController::ClickDoor()
{
    FVector2D ScreenPosition;
    if (GetMousePosition(ScreenPosition.X,ScreenPosition.Y)) ToggleDoorAtScreenPosition(ScreenPosition);
}
ADirectOrScutGameMode::ADirectOrScutGameMode()
{
    PlayerControllerClass=ADirectOrScutPlayerController::StaticClass(); DefaultPawnClass=nullptr; HUDClass=nullptr;
}
void ADirectOrScutGameMode::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->SpawnActor<ADirectOrScutHullActor>();
    const FRotator View(-45,-135,0);
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>(-View.Vector()*4500,View);
    Camera->Tags.Add(TEXT("DirectOrScutCamera"));
    auto* C=Camera->GetCameraComponent(); C->ProjectionMode=ECameraProjectionMode::Orthographic; C->OrthoWidth=4200;
    // Fixed aspect preserves the complete ship at 4200uu, including on wide monitors.
    C->bConstrainAspectRatio=true; C->AspectRatio=1.15f;
    C->PostProcessSettings.bOverride_AutoExposureMethod=true; C->PostProcessSettings.AutoExposureMethod=AEM_Manual;
    C->PostProcessSettings.bOverride_AutoExposureBias=true; C->PostProcessSettings.AutoExposureBias=0;
    C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    C->PostProcessSettings.bOverride_BloomIntensity=true; C->PostProcessSettings.BloomIntensity=.2f;
    auto* Key=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-65,-35,0));
    Key->GetLightComponent()->SetIntensity(2.4f);
    Key->GetLightComponent()->bUseTemperature=true;
    Key->GetLightComponent()->SetTemperature(5800.f);
    CastChecked<UDirectionalLightComponent>(Key->GetLightComponent())->LightSourceAngle=2.f;
    Key->GetLightComponent()->SetLightColor(FLinearColor::White);
    auto* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-55,145,0));
    Fill->GetLightComponent()->SetIntensity(.8f);
    Fill->GetLightComponent()->SetCastShadows(false);
    Fill->GetLightComponent()->bUseTemperature=true;
    Fill->GetLightComponent()->SetTemperature(5800.f);
    for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It) if (auto* PC=It->Get()) PC->SetViewTarget(Camera);
    if (FParse::Param(FCommandLine::Get(),TEXT("ScutCapture")))
    {
        FTimerHandle Capture, Exit;
        GetWorldTimerManager().SetTimer(Capture,[]()
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Blockout.png"),false,false);
        },10.f,false);
        GetWorldTimerManager().SetTimer(Exit,[](){ FPlatformMisc::RequestExit(false); },15.f,false);
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("ScutValidate")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle,[this]()
        {
            bool Pass=true; int32 DoorCount=0, Humans=0, Androids=0;
            for (TActorIterator<ADoorActor> It(GetWorld());It;++It)
            {
                ++DoorCount;
                Pass &= It->ClickTarget->GetUnscaledBoxExtent().Equals(FVector(90,120,130));
                It->Toggle(); It->Tick(2.f);
                Pass &= FMath::IsNearlyEqual(It->Slab->GetRelativeLocation().Z,-250.f);
                It->Toggle(); It->Tick(2.f);
                Pass &= It->Slab->GetRelativeLocation().IsNearlyZero();
                FHitResult Hit; FVector P=It->GetActorLocation()+FVector(0,0,120);
                FVector N=It->GetActorForwardVector();
                GetWorld()->LineTraceSingleByChannel(Hit,P-N*24,P+N*24,DirectOrScut::DoorClickChannel,FCollisionQueryParams(SCENE_QUERY_STAT(ScutDoor),true));
                if (Hit.GetActor()!=*It) UE_LOG(LogTemp,Warning,TEXT("SCUT door trace %s instead hit %s"),*It->GetActorLocation().ToString(),*GetNameSafe(Hit.GetActor()));
                Pass &= Hit.GetActor()==*It;
            }
            for (TActorIterator<ACrewUnitBlockout> It(GetWorld());It;++It)
            {
                It->bAndroid ? ++Androids : ++Humans;
                FVector O,E; It->GetActorBounds(false,O,E);
                const float W=It->bAndroid?85.f:80.f, H=It->bAndroid?185.f:180.f;
                Pass &= FMath::IsNearlyEqual(E.X*2,W,.1f) && FMath::IsNearlyEqual(E.Y*2,W,.1f);
                Pass &= FMath::IsNearlyEqual(E.Z*2,H,.1f) && FMath::IsNearlyEqual(O.Z-E.Z,0.f,.1f);
                UE_LOG(LogTemp,Display,TEXT("SCUT unit dimensions %s: %s base %.2f"),It->bAndroid?TEXT("android"):TEXT("human"),*(E*2).ToString(),O.Z-E.Z);
            }
            FHitResult Floor;
            GetWorld()->LineTraceSingleByChannel(Floor,FVector(1053,0,100),FVector(1053,0,-100),ECC_Visibility,FCollisionQueryParams(SCENE_QUERY_STAT(ScutFloor),true));
            Pass &= Floor.bBlockingHit && FMath::IsNearlyZero(Floor.ImpactPoint.Z,.1f);
            for (const FVector& Sample : {DirectOrScut::Radial(135,700),DirectOrScut::Radial(315,700)})
            {
                FHitResult SpokeFloor;
                GetWorld()->LineTraceSingleByChannel(SpokeFloor,Sample+FVector(0,0,80),Sample-FVector(0,0,80),ECC_Visibility,FCollisionQueryParams(SCENE_QUERY_STAT(ScutSpokeFloor),true));
                Pass &= SpokeFloor.bBlockingHit && FMath::IsNearlyZero(SpokeFloor.ImpactPoint.Z,.1f);
            }
            Pass &= DoorCount==9 && Humans==3 && Androids==1;
            UE_LOG(LogTemp,Display,TEXT("SCUT VALIDATION %s: doors=%d humans=%d androids=%d floor=%.2f"),Pass?TEXT("PASS"):TEXT("FAIL"),DoorCount,Humans,Androids,Floor.ImpactPoint.Z);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        },3.f,false);
    }
}
