#include "DirectOrScutGameMode.h"
#include "DirectOrScutSimSubsystem.h"
#include "GameFramework/SpectatorPawn.h"
#include "DirectOrScutHUD.h"
#include "Framework/Application/SlateApplication.h"
#include "SDirectOrScutWorkstationWidget.h"
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
    // Slice 9 routes operator interaction through the covert workstation only.
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::F9,IE_Pressed,this,&ADirectOrScutPlayerController::ToggleDevFX);
    InputComponent->BindKey(EKeys::F10,IE_Pressed,this,&ADirectOrScutPlayerController::ToggleDevView);
}
void ADirectOrScutPlayerController::ToggleDevFX() { auto* S=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();S->bCleanDevRendering=true; }
void ADirectOrScutPlayerController::ToggleDevView() { auto* S=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();S->bWorkstationVisible=!S->bWorkstationVisible;if(S->bWorkstationVisible)FocusWorkstation(); }
void ADirectOrScutPlayerController::PlayerTick(float D)
{
    Super::PlayerTick(D);
    auto* S=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    if(!S || S->bWorkstationVisible==bWasWorkstationVisible)return;
    bWasWorkstationVisible=S->bWorkstationVisible;
    if(bWasWorkstationVisible) {FocusWorkstation();return;}
    if(!GetPawn()) {auto* P=GetWorld()->SpawnActor<ASpectatorPawn>();Possess(P);}
    if(APawn* P=GetPawn())
    {
        const int32 Room=S->RequestedFeed>0?S->RequestedFeed==8?1:S->RequestedFeed==1?0:S->RequestedFeed:0;
        P->SetActorLocation(UDirectOrScutSimSubsystem::DefaultSetTransform(Room).WorldOrigin+FVector(-950,0,210));
        SetControlRotation(FRotator::ZeroRotator);SetViewTarget(P);
    }
    bShowMouseCursor=false;SetInputMode(FInputModeGameOnly());
}
void ADirectOrScutPlayerController::BeginPlay()
{
    Super::BeginPlay(); bShowMouseCursor=true;
    FocusWorkstation();
    GetWorldTimerManager().SetTimerForNextTick(this,&ADirectOrScutPlayerController::FocusWorkstation);
    bAutoManageActiveCameraTarget=false;
    for (TActorIterator<ACameraActor> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("DirectOrScutCamera"))) { SetViewTarget(*It); break; }
}
void ADirectOrScutPlayerController::FocusWorkstation()
{
    if (!IsLocalController()) return;
    auto* HUD=Cast<ADirectOrScutHUD>(GetHUD());
    const auto Widget=HUD?HUD->GetWorkstation():nullptr;
    bShowMouseCursor=true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    if (Widget.IsValid()) Mode.SetWidgetToFocus(Widget);
    SetInputMode(Mode);
    if (Widget.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(Widget,EFocusCause::SetDirectly);
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
    PlayerControllerClass=ADirectOrScutPlayerController::StaticClass(); DefaultPawnClass=ASpectatorPawn::StaticClass(); HUDClass=ADirectOrScutHUD::StaticClass();
}
void ADirectOrScutGameMode::BeginPlay()
{
    Super::BeginPlay();
    // Uncooked PIE can duplicate editor-only actors; remove the tagged overview helper before rendering.
    for(TActorIterator<ADirectionalLight> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("ScutEditorOverview")))
    {
        It->GetLightComponent()->SetVisibility(false);It->Destroy();
    }
    if(!TActorIterator<ADirectOrScutHullActor>(GetWorld())) GetWorld()->SpawnActor<ADirectOrScutHullActor>();
    const FRotator View(-45,-135,0);
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>(-View.Vector()*4500,View);
    Camera->Tags.Add(TEXT("DirectOrScutCamera"));
    auto* C=Camera->GetCameraComponent(); C->ProjectionMode=ECameraProjectionMode::Perspective; C->FieldOfView=73.7398f;
    // Fixed aspect preserves the complete ship at 4200uu, including on wide monitors.
    C->bConstrainAspectRatio=true; C->AspectRatio=1.15f;
    C->PostProcessSettings.bOverride_AutoExposureMethod=true; C->PostProcessSettings.AutoExposureMethod=AEM_Manual;
    C->PostProcessSettings.bOverride_AutoExposureBias=true; C->PostProcessSettings.AutoExposureBias=0;
    C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;
    C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
    C->PostProcessSettings.bOverride_BloomIntensity=true; C->PostProcessSettings.BloomIntensity=.2f;
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
            const TCHAR* Name=FParse::Param(FCommandLine::Get(),TEXT("ScutSlice9bCapture"))?TEXT("DirectOrScut/Slice9b.png"):FParse::Param(FCommandLine::Get(),TEXT("ScutSlice9aCapture"))?TEXT("DirectOrScut/Slice9a.png"):FParse::Param(FCommandLine::Get(),TEXT("ScutSlice9Capture"))?TEXT("DirectOrScut/Slice9.png"):Chunk2Capture?TEXT("DirectOrScut/Chunk2.png"):TEXT("DirectOrScut/Blockout.png");
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/Name,true,false);
        },10.f,false);
        const bool Slice9Capture=FParse::Param(FCommandLine::Get(),TEXT("ScutSlice9Capture"));
        if (Slice9Capture)
        {
            auto Select=[this](int32 Feed,bool Grid,bool Full)
            {
                auto* PC=GetWorld()->GetFirstPlayerController(); auto* HUD=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
                if (!HUD || !HUD->GetWorkstation()) return;
                auto UI=HUD->GetWorkstation(); UI->SelectFeed(Feed,false);
                if (Grid) UI->ShowGrid(); else if (!UI->bFullscreen) UI->ToggleExpanded();
            };
            FTimerHandle Room,RoomShot,Grid,GridShot,Vacuum,VacuumShot;
            GetWorldTimerManager().SetTimer(Room,[Select](){Select(2,false,false);},11.f,false);
            GetWorldTimerManager().SetTimer(RoomShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9Room.png"),true,false);},12.f,false);
            GetWorldTimerManager().SetTimer(Grid,[Select](){Select(0,true,false);},13.f,false);
            GetWorldTimerManager().SetTimer(GridShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9Grid.png"),true,false);},14.f,false);
            GetWorldTimerManager().SetTimer(Vacuum,[this,Select](){Select(7,false,true); GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>()->SubmitDirective(TEXT("scut airlock"));},15.f,false);
            GetWorldTimerManager().SetTimer(VacuumShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9Vacuum.png"),true,false);},16.f,false);
        }
        const bool Slice9bCapture=FParse::Param(FCommandLine::Get(),TEXT("ScutSlice9bCapture"));
        if(Slice9bCapture)
        {
            auto Project=[this](const TCHAR* Command,bool Drawer)
            {
                auto* PC=GetWorld()->GetFirstPlayerController();auto* HUD=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
                if(HUD && HUD->GetWorkstation()) HUD->GetWorkstation()->SetConsoleOpen(Drawer,Drawer);
                GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>()->QueueConsoleCommand(Command);
            };
            FTimerHandle Help,HelpShot,AI,AIShot,Events,EventsShot,Crew,CrewShot;
            GetWorldTimerManager().SetTimer(Help,[Project](){Project(TEXT("help"),true);},11.f,false);
            GetWorldTimerManager().SetTimer(HelpShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9bConsole.png"),true,false);},12.f,false);
            GetWorldTimerManager().SetTimer(AI,[Project](){Project(TEXT("menu ai"),false);},13.f,false);
            GetWorldTimerManager().SetTimer(AIShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9bAI.png"),true,false);},14.f,false);
            GetWorldTimerManager().SetTimer(Events,[Project](){Project(TEXT("menu events"),false);},15.f,false);
            GetWorldTimerManager().SetTimer(EventsShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9bEvents.png"),true,false);},16.f,false);
            GetWorldTimerManager().SetTimer(Crew,[Project](){Project(TEXT("menu crew"),false);},17.f,false);
            GetWorldTimerManager().SetTimer(CrewShot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice9bCrew.png"),true,false);},18.f,false);
        }
        GetWorldTimerManager().SetTimer(Exit,[](){ FPlatformMisc::RequestExit(false); },Slice9bCapture?20.f:Slice9Capture?18.f:15.f,false);
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
