#include "DirectOrScutPlayerController.h"
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
    // Route operator interaction through the covert workstation.
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ADirectOrScutPlayerController::ToggleDevFX);
    InputComponent->BindKey(EKeys::F10, IE_Pressed, this, &ADirectOrScutPlayerController::ToggleDevView);
}
void ADirectOrScutPlayerController::ToggleDevFX()
{
    auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    S->bCleanDevRendering = true;
}
void ADirectOrScutPlayerController::ToggleDevView()
{
    auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    S->bWorkstationVisible = !S->bWorkstationVisible;
    if (S->bWorkstationVisible)
    {
        FocusWorkstation();
    }
}
void ADirectOrScutPlayerController::PlayerTick(float D)
{
    Super::PlayerTick(D);
    auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    if (!S || S->bWorkstationVisible == bWasWorkstationVisible)
    {
        return;
    }
    bWasWorkstationVisible = S->bWorkstationVisible;
    if (bWasWorkstationVisible)
    {
        FocusWorkstation();
        return;
    }
    if (!GetPawn())
    {
        auto *P = GetWorld()->SpawnActor<ASpectatorPawn>();
        Possess(P);
    }
    if (APawn *P = GetPawn())
    {
        const int32 Room = S->RequestedFeed > 0 ? S->RequestedFeed == 8   ? 1
                                                  : S->RequestedFeed == 1 ? 0
                                                                          : S->RequestedFeed
                                                : 0;
        P->SetActorLocation(UDirectOrScutSimSubsystem::DefaultSetTransform(Room).WorldOrigin + FVector(-950, 0, 210));
        SetControlRotation(FRotator::ZeroRotator);
        SetViewTarget(P);
    }
    bShowMouseCursor = false;
    SetInputMode(FInputModeGameOnly());
}
void ADirectOrScutPlayerController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    FocusWorkstation();
    GetWorldTimerManager().SetTimerForNextTick(this, &ADirectOrScutPlayerController::FocusWorkstation);
    bAutoManageActiveCameraTarget = false;
    for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
    {
        if (It->ActorHasTag(TEXT("DirectOrScutCamera")))
        {
            SetViewTarget(*It);
            break;
        }
    }
}
void ADirectOrScutPlayerController::FocusWorkstation()
{
    if (!IsLocalController())
    {
        return;
    }
    auto *HUD = Cast<ADirectOrScutHUD>(GetHUD());
    const auto Widget = HUD ? HUD->GetWorkstation() : nullptr;
    bShowMouseCursor = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    if (Widget.IsValid())
    {
        Mode.SetWidgetToFocus(Widget);
    }
    SetInputMode(Mode);
    if (Widget.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetKeyboardFocus(Widget, EFocusCause::SetDirectly);
    }
}
bool ADirectOrScutPlayerController::ToggleDoorAtScreenPosition(const FVector2D &Pixel, bool ExteriorModifier)
{
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(Pixel, DirectOrScut::DoorClickChannel, true, Hit))
    {
        if (auto *Door = Cast<ADoorActor>(Hit.GetActor()))
        {
            if (Door->bExterior && !ExteriorModifier)
            {
                return false;
            }
            Door->Toggle();
            return true;
        }
    }
    return false;
}
bool ADirectOrScutPlayerController::HandlePrimaryAtScreenPosition(const FVector2D &Pixel, bool Shift)
{
    auto *Sim = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    if (!Sim)
    {
        return false;
    }
    if (Shift && HandleVentAtScreenPosition(Pixel))
    {
        return true;
    }
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(Pixel, DirectOrScut::CrewClickChannel, true, Hit))
    {
        if (auto *Unit = Cast<ACrewUnitBlockout>(Hit.GetActor()))
        {
            SelectedUnit = Unit->SimUnitIndex;
            return true;
        }
    }
    if (GetHitResultAtScreenPosition(Pixel, DirectOrScut::DoorClickChannel, true, Hit))
    {
        return ToggleDoorAtScreenPosition(Pixel, Shift);
    }
    int32 Room = -1;
    if (GetHitResultAtScreenPosition(Pixel, ECC_Visibility, true, Hit) && Hit.GetComponent() &&
        Hit.GetComponent()->GetFName() == TEXT("ProceduralHull"))
    {
        Room = DirectOrScut::RoomAtPoint(FVector2D(Hit.ImpactPoint));
    }
    if (Room < 0)
    {
        FVector Origin, Direction;
        if (DeprojectScreenPositionToWorld(Pixel.X, Pixel.Y, Origin, Direction) && Direction.Z < -.001)
        {
            Room = DirectOrScut::RoomAtPoint(FVector2D(Origin + Direction * (-Origin.Z / Direction.Z)));
        }
    }
    return SelectedUnit >= 0 && Sim->AssignRoom(SelectedUnit, Room);
}
bool ADirectOrScutPlayerController::HandleVentAtScreenPosition(const FVector2D &Pixel)
{
    FHitResult Hit;
    if (GetHitResultAtScreenPosition(Pixel, DirectOrScut::DoorClickChannel, true, Hit))
    {
        if (auto *Door = Cast<ADoorActor>(Hit.GetActor()))
        {
            if (Door->bExterior)
            {
                Door->Toggle();
                return true;
            }
        }
    }
    return false;
}
void ADirectOrScutPlayerController::ClickDoor()
{
    FVector2D P;
    if (GetMousePosition(P.X, P.Y))
    {
        HandlePrimaryAtScreenPosition(P, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
    }
}
void ADirectOrScutPlayerController::ClickVent()
{
    FVector2D P;
    if (GetMousePosition(P.X, P.Y))
    {
        HandleVentAtScreenPosition(P);
    }
}
void ADirectOrScutPlayerController::DebugPanic()
{
    if (auto *Sim = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())
    {
        Sim->Simulation.ForceRandomHumanPanic();
    }
}
