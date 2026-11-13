#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DirectOrScutHUD.generated.h"
UCLASS()
class EMJ2026_API ADirectOrScutHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void DrawHUD() override;
    TSharedPtr<class SDirectOrScutWorkstationWidget> GetWorkstation() const { return Workstation; }
private:
    TSharedPtr<class SDirectOrScutWorkstationWidget> Workstation;
};
