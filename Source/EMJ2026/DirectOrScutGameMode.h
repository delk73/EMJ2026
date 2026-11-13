#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "DirectOrScutGameMode.generated.h"
UCLASS()
class EMJ2026_API ADirectOrScutPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void SetupInputComponent() override;
    virtual void BeginPlay() override;
    void FocusWorkstation();
    void ToggleDevFX();
    void ToggleDevView();
    bool ToggleDoorAtScreenPosition(const FVector2D& ScreenPosition,bool ExteriorModifier=false);
    bool HandlePrimaryAtScreenPosition(const FVector2D& ScreenPosition,bool Shift=false);
    bool HandleVentAtScreenPosition(const FVector2D& ScreenPosition);
    UPROPERTY(VisibleAnywhere) int32 SelectedUnit=INDEX_NONE;
private:
    void ClickDoor();
    void ClickVent();
    void DebugPanic();
};
UCLASS()
class EMJ2026_API ADirectOrScutGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ADirectOrScutGameMode();
    virtual void BeginPlay() override;
};
