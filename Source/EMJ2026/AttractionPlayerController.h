#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "AttractionSimulation.h"
#include "AttractionPlayerController.generated.h"

UCLASS()
class EMJ2026_API AAttractionPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AAttractionPlayerController();
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;
private:
    UPROPERTY() TObjectPtr<AAttractionSimulation> Board;
    UPROPERTY() TObjectPtr<class ACameraActor> BoardCamera;
    int32 ProfileIndex = 0;
    float CameraAltitude = 1800.f;
    float TargetAltitude = 1800.f;
    bool bSwiping = false;
    bool bHaveSwipePoint = false;
    FVector PreviousPoint = FVector::ZeroVector;
    bool CursorPoint(FVector& LocalPoint) const;
    FAttractionNode ActiveProfile() const;
    void Drop();
    void NextProfile();
    void ZoomIn();
    void ZoomOut();
    void StartSwipe();
    void EndSwipe();
    void ResetBoard();
    void SliceSegment(const FVector& Start, const FVector& End);
};

UCLASS()
class EMJ2026_API AAttractionGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAttractionGameMode();
};
