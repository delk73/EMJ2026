#include "DirectOrScutHUD.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGameMode.h"
#include "CompartmentCameraRig.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
void ADirectOrScutHUD::BeginPlay()
{
    Super::BeginPlay();
    if (!GetWorld()->GetGameViewport() || !FSlateApplication::IsInitialized()) return;
    ACompartmentCameraRig* Rig=nullptr;for(TActorIterator<ACompartmentCameraRig> It(GetWorld());It;++It){Rig=*It;break;}
    if(!Rig)Rig=GetWorld()->SpawnActor<ACompartmentCameraRig>();
    Rig->InitializeFeeds();
    SAssignNew(Workstation,SDirectOrScutWorkstationWidget).Sim(GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>()).Rig(Rig);
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(Workstation.ToSharedRef(),10);
    if (FParse::Param(FCommandLine::Get(),TEXT("ScutSlice9Capture")))
    {
        int32 Feed=1; FParse::Value(FCommandLine::Get(),TEXT("ScutFeed="),Feed);
        Workstation->SelectFeed(FMath::Clamp(Feed,1,8),false);

    }
    if (auto* PC=Cast<ADirectOrScutPlayerController>(PlayerOwner)) PC->FocusWorkstation();
    FSlateApplication::Get().SetKeyboardFocus(Workstation,EFocusCause::SetDirectly);
}
void ADirectOrScutHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Workstation.IsValid() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Workstation.ToSharedRef());
    Workstation.Reset(); Super::EndPlay(Reason);
}
void ADirectOrScutHUD::DrawHUD() { Super::DrawHUD(); }
