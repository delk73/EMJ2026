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
    bool ToggleDoorAtScreenPosition(const FVector2D& ScreenPosition);
private:
    void ClickDoor();
};
UCLASS()
class EMJ2026_API ADirectOrScutGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ADirectOrScutGameMode();
    virtual void BeginPlay() override;
};
