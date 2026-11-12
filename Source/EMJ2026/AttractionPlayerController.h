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
    int32 GetDragArchetypeIndex() const { return LeftGesture==ELeftGesture::Tray ? DragIndex : INDEX_NONE; }
    bool CanPreviewDrop() const { FVector P; return CursorPoint(P); }
private:
    UPROPERTY() TObjectPtr<AAttractionSimulation> Board;
    UPROPERTY() TObjectPtr<class ACameraActor> BoardCamera;
    enum class ELeftGesture : uint8 { None, Tray, Pending, Stir };
    ELeftGesture LeftGesture = ELeftGesture::None;
    int32 DragIndex = INDEX_NONE;
    FVector2D PressScreen = FVector2D::ZeroVector;
    FVector LeftPoint = FVector::ZeroVector;
    bool bHaveLeftPoint = false;
    double PressTime = 0;
    float CameraAltitude = 1800.f;
    float TargetAltitude = 1800.f;
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
    void StartSwipe();
    void EndSwipe();
    void ResetBoard();
    void SliceSegment(const FVector& Start, const FVector& End);
};
