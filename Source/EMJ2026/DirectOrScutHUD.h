#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DirectOrScutHUD.generated.h"
UCLASS()
class EMJ2026_API ADirectOrScutHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
