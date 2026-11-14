#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DirectOrScutPlayerController.h"
#include "DirectOrScutGameMode.generated.h"
UCLASS()
class EMJ2026_API ADirectOrScutGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    ADirectOrScutGameMode();
    virtual void BeginPlay() override;
};
