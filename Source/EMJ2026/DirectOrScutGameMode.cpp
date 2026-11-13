#include "DirectOrScutGameMode.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutCargoMarker.h"
#include "DirectOrScutHUD.h"
#include "DirectOrScutHullActor.h"
#include "DirectOrScutGeometry.h"
#include "Components/BoxComponent.h"
#include "DoorActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
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
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&ADirectOrScutPlayerController::ClickVent);
    InputComponent->BindKey(EKeys::F1,IE_Pressed,this,&ADirectOrScutPlayerController::DebugPanic);
}
void ADirectOrScutPlayerController::BeginPlay()
{
    Super::BeginPlay(); bShowMouseCursor=true; SetInputMode(FInputModeGameAndUI());
    bAutoManageActiveCameraTarget=false;
    for (TActorIterator<ACameraActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DirectOrScutCamera"))) { SetViewTarget(*It); break; }
}
bool ADirectOrScutPlayerController::ToggleDoorAtScreenPosition(const FVector2D& Pixel,bool ExteriorModifier)
{
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(Pixel,DirectOrScut::DoorClickChannel,true,Hit))
        if (auto* Door=Cast<ADoorActor>(Hit.GetActor()))
        {
            if (Door->bExterior && !ExteriorModifier) return false;
            Door->Toggle(); return true;
        }
    return false;
}
bool ADirectOrScutPlayerController::HandlePrimaryAtScreenPosition(const FVector2D& Pixel,bool Shift)
{
    auto* Sim=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    if (!Sim) return false;
    if (Shift && HandleVentAtScreenPosition(Pixel)) return true;
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(Pixel,DirectOrScut::CrewClickChannel,true,Hit))
        if (auto* Unit=Cast<ACrewUnitBlockout>(Hit.GetActor())) { SelectedUnit=Unit->SimUnitIndex; return true; }
    if (GetHitResultAtScreenPosition(Pixel,DirectOrScut::DoorClickChannel,true,Hit))
        return ToggleDoorAtScreenPosition(Pixel,Shift);
    int32 Room=-1;
    if (GetHitResultAtScreenPosition(Pixel,ECC_Visibility,true,Hit) && Hit.GetComponent() && Hit.GetComponent()->GetFName()==TEXT("ProceduralHull"))
        Room=DirectOrScut::RoomAtPoint(FVector2D(Hit.ImpactPoint));
    if (Room<0)
    {
        FVector Origin,Direction;
        if (DeprojectScreenPositionToWorld(Pixel.X,Pixel.Y,Origin,Direction) && Direction.Z<-.001)
            Room=DirectOrScut::RoomAtPoint(FVector2D(Origin+Direction*(-Origin.Z/Direction.Z)));
    }
    return SelectedUnit>=0 && Sim->AssignRoom(SelectedUnit,Room);
}
bool ADirectOrScutPlayerController::HandleVentAtScreenPosition(const FVector2D& Pixel)
{
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(Pixel,DirectOrScut::DoorClickChannel,true,Hit))
        if (auto* Door=Cast<ADoorActor>(Hit.GetActor())) if (Door->bExterior) { Door->Toggle(); return true; }
    return false;
}
void ADirectOrScutPlayerController::ClickDoor()
{
    FVector2D P;
    if (GetMousePosition(P.X,P.Y)) HandlePrimaryAtScreenPosition(P,IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift));
}
void ADirectOrScutPlayerController::ClickVent()
{
    FVector2D P; if (GetMousePosition(P.X,P.Y)) HandleVentAtScreenPosition(P);
}
void ADirectOrScutPlayerController::DebugPanic()
{
    if (auto* Sim=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>()) Sim->Simulation.ForceRandomHumanPanic();
}
ADirectOrScutGameMode::ADirectOrScutGameMode()
{
    PlayerControllerClass=ADirectOrScutPlayerController::StaticClass(); DefaultPawnClass=nullptr; HUDClass=ADirectOrScutHUD::StaticClass();
}
void ADirectOrScutGameMode::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->SpawnActor<ADirectOrScutHullActor>();
    for (int32 I=0;I<2;++I)
    {
        const float A=DirectOrScut::Stations()[4].Face*45.f;
        const FVector P=DirectOrScut::Radial(A,DirectOrScut::PodCenterRadius()-260)+DirectOrScut::Radial(A+90,I==0?-180.f:180.f);
        auto* Cargo=GetWorld()->SpawnActorDeferred<ADirectOrScutCargoMarker>(ADirectOrScutCargoMarker::StaticClass(),FTransform(P),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Cargo->SimBodyIndex=4+I; Cargo->FinishSpawning(FTransform(P));
    }
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
        GetWorldTimerManager().SetTimer(Capture,[this]()
        {
            const bool Chunk2Capture=FParse::Param(FCommandLine::Get(),TEXT("ScutChunk2Capture"));
            if (Chunk2Capture)
            {
                // Artifact-only presentation; regular PIE retains engine diagnostics.
                GAreScreenMessagesEnabled=false;
                if (auto* PC=Cast<ADirectOrScutPlayerController>(GetWorld()->GetFirstPlayerController())) PC->SelectedUnit=0;
            }
            const TCHAR* Name=Chunk2Capture?TEXT("DirectOrScut/Chunk2.png"):TEXT("DirectOrScut/Blockout.png");
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name,true,false);
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
