#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
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
    int32 GetDragArchetypeIndex() const { return INDEX_NONE; }
    bool CanPreviewDrop() const { FVector P; return CursorPoint(P); }
private:
    UPROPERTY() TObjectPtr<AAttractionSimulation> Board;
    UPROPERTY() TObjectPtr<class ACameraActor> BoardCamera;
    enum class ELeftMode : uint8 { None, RewardPaint, PatchDrag };
    ELeftMode LeftMode = ELeftMode::None;
    FVector LeftPoint = FVector::ZeroVector;
    bool bHaveLeftPoint = false;
    void DisableReward();
    void NextStage();
    static constexpr float MinZoomZ = 550.0f;
    static constexpr float MaxZoomZ = 18000.0f;
    static constexpr float OverviewZoomZ = 5200.0f;
    float TargetZoomZ = OverviewZoomZ;
    bool bRecenterOnFirstTick = false;
    bool bSwiping = false;
    bool bHaveSwipePoint = false;
    FVector PreviousPoint = FVector::ZeroVector;
    bool CursorPoint(FVector& LocalPoint,bool bAllowTray = false) const;
    bool MouseScreen(FVector2D& Point,int32& Width,int32& Height) const;
    void BeginLeft();
    void EndLeft();
    void CancelLeft();
    void UpdateLeft();
    void ZoomIn();
    void ZoomOut();
    void Zoom(float WheelAxis);
    void CenterCamera();
    void StartSwipe();
    void EndSwipe();
    void ResetBoard();
    void SliceSegment(const FVector& Start, const FVector& End);
};
