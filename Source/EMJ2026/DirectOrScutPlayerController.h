#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DirectOrScutPlayerController.generated.h"
UCLASS()
class EMJ2026_API ADirectOrScutPlayerController : public APlayerController
{
    GENERATED_BODY()
  public:
    virtual void SetupInputComponent() override;
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;
    void FocusWorkstation();
    void ToggleDevFX();
    void ToggleDevView();
    bool ToggleDoorAtScreenPosition(const FVector2D &ScreenPosition, bool ExteriorModifier = false);
    bool HandlePrimaryAtScreenPosition(const FVector2D &ScreenPosition, bool Shift = false);
    bool HandleVentAtScreenPosition(const FVector2D &ScreenPosition);
    UPROPERTY(VisibleAnywhere) int32 SelectedUnit = INDEX_NONE;

  private:
    bool bWasWorkstationVisible = true;
    void ClickDoor();
    void ClickVent();
    void DebugPanic();
};
