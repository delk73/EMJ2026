#include "AttractionPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "AttractionHUD.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

AAttractionPlayerController::AAttractionPlayerController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
}

void AAttractionPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController())
    {
        return;
    }
    for (TActorIterator<AAttractionSimulation> It(GetWorld()); It; ++It)
    {
        Board = *It;
        break;
    }
    // Greenfield convenience: empty maps are playable without an authored board.
    if (!Board)
    {
        Board = GetWorld()->SpawnActor<AAttractionSimulation>(FVector(0, 0, 150), FRotator::ZeroRotator);
    }
    if (!Board)
    {
        return;
    }
    BoardCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (BoardCamera)
    {
        BoardCamera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Perspective);
        // Perspective uses UE's infinite-far reversed-Z projection; no finite far clip
        // needs extending for the 18000-unit overview.
        BoardCamera->GetCameraComponent()->SetFieldOfView(60.f);
        BoardCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
        BoardCamera->GetCameraComponent()->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
        BoardCamera->GetCameraComponent()->bOverrideAspectRatioAxisConstraint = true;
        // Texture already contains the lighting; neutral exposure keeps its greyscale readable.
        auto &Post = BoardCamera->GetCameraComponent()->PostProcessSettings;
        Post.bOverride_AutoExposureMethod = true;
        Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        Post.bOverride_AutoExposureBias = true;
        Post.AutoExposureBias = 0;
        Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Post.AutoExposureApplyPhysicalCameraExposure = false;
        Post.bOverride_BloomIntensity = true;
        Post.BloomIntensity = 0;
        Post.bOverride_VignetteIntensity = true;
        Post.VignetteIntensity = 0;
        CenterCamera();
        // All actors have completed BeginPlay before the first world tick. Re-sample
        // once in case the authored board had not populated Nodes above.
        bRecenterOnFirstTick = true;
    }
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}

void AAttractionPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAttractionPlayerController::BeginLeft);
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAttractionPlayerController::EndLeft);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAttractionPlayerController::StartSwipe);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AAttractionPlayerController::EndSwipe);
    InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAttractionPlayerController::DisableReward);
    InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AAttractionPlayerController::NextStage);
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AAttractionPlayerController::ResetBoard);
    InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AAttractionPlayerController::ZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AAttractionPlayerController::ZoomOut);
}

bool AAttractionPlayerController::CursorPoint(FVector &LocalPoint, bool bAllowTray) const
{
    if (!IsValid(Board))
    {
        return false;
    }
    if (!IsValid(BoardCamera))
    {
        return false;
    }
    float MouseX, MouseY;
    int32 Width, Height;
    GetViewportSize(Width, Height);
    if (!GetMousePosition(MouseX, MouseY) || Width <= 0 || Height <= 0 || MouseX < 0 || MouseY < 0 || MouseX > Width ||
        MouseY > Height)
    {
        return false;
    }

    FVector Origin, Direction;
    if (!DeprojectScreenPositionToWorld(MouseX, MouseY, Origin, Direction))
    {
        return false;
    }
    const FTransform Transform = Board->GetActorTransform();
    const FVector Normal = Transform.GetRotation().GetAxisZ();
    const double Denominator = FVector::DotProduct(Direction, Normal);
    if (FMath::Abs(Denominator) < 1.e-6)
    {
        return false;
    }
    const double T = FVector::DotProduct(Transform.GetLocation() - Origin, Normal) / Denominator;
    if (T < 0)
    {
        return false;
    }
    LocalPoint = Transform.InverseTransformPosition(Origin + Direction * T);
    LocalPoint.Z = 0;
    return Board->IsInsidePlayfield(LocalPoint);
}

bool AAttractionPlayerController::MouseScreen(FVector2D &Point, int32 &Width, int32 &Height) const
{
    float X, Y;
    GetViewportSize(Width, Height);
    if (!GetMousePosition(X, Y) || X < 0 || Y < 0 || X > Width || Y > Height || Width <= 0 || Height <= 0)
    {
        return false;
    }
    Point = FVector2D(X, Y);
    return true;
}

void AAttractionPlayerController::BeginLeft()
{
    CancelLeft();
    if (bSwiping || !CursorPoint(LeftPoint))
    {
        return;
    }
    const int32 Hit = Board->PickLatticeComponent(LeftPoint);
    if (Hit != INDEX_NONE)
    {
        if (Board->BeginPatchDrag(Hit))
        {
            LeftMode = ELeftMode::PatchDrag;
            bHaveLeftPoint = true;
        }
        return; // Rooted lattice hits must not fall through to painting.
    }
    LeftMode = ELeftMode::RewardPaint;
    bHaveLeftPoint = true;
    Board->PaintReward(FVector2D(LeftPoint));
}
void AAttractionPlayerController::UpdateLeft()
{
    FVector P;
    if (LeftMode == ELeftMode::None || !CursorPoint(P))
    {
        bHaveLeftPoint = false;
        return;
    }
    if (LeftMode == ELeftMode::PatchDrag)
    {
        Board->UpdatePatchDrag(bHaveLeftPoint ? P - LeftPoint : FVector::ZeroVector, GetWorld()->GetDeltaSeconds());
        LeftPoint = P;
        bHaveLeftPoint = true;
        return;
    }
    // Spatial sampling keeps fast strokes connected. Losing the viewport breaks
    // the path, so re-entry never paints a bridge across a stale cursor location.
    const float Spacing = FMath::Max(25.0f, Board->EdgeRestLength * 1.75f * 0.25f);
    const int32 Steps = bHaveLeftPoint ? FMath::Max(1, FMath::CeilToInt(FVector::Distance(LeftPoint, P) / Spacing)) : 1;
    for (int32 I = 1; I <= Steps; ++I)
    {
        Board->PaintReward(FVector2D(bHaveLeftPoint ? FMath::Lerp(LeftPoint, P, float(I) / Steps) : P));
    }
    LeftPoint = P;
    bHaveLeftPoint = true;
}
void AAttractionPlayerController::EndLeft()
{
    if (IsValid(Board) && LeftMode != ELeftMode::None)
    {
        FVector P;
        const bool bValidRelease = CursorPoint(P);
        if (bValidRelease)
        {
            UpdateLeft();
        }
        if (LeftMode == ELeftMode::PatchDrag)
        {
            Board->EndPatchDrag(bValidRelease);
        }
    }
    CancelLeft();
}
void AAttractionPlayerController::CancelLeft()
{
    if (LeftMode == ELeftMode::PatchDrag && IsValid(Board))
    {
        Board->EndPatchDrag(false);
    }
    LeftMode = ELeftMode::None;
    bHaveLeftPoint = false;
}
void AAttractionPlayerController::DisableReward()
{
    CancelLeft();
    if (Board)
    {
        Board->ClearReward();
    }
}
void AAttractionPlayerController::NextStage()
{
    CancelLeft();
    bSwiping = false;
    bHaveSwipePoint = false;
    if (Board)
    {
        Board->NextStage();
    }
}

void AAttractionPlayerController::ZoomIn()
{
    Zoom(1.0f);
}
void AAttractionPlayerController::ZoomOut()
{
    Zoom(-1.0f);
}
void AAttractionPlayerController::Zoom(float WheelAxis)
{
    TargetZoomZ = FMath::Clamp(TargetZoomZ * (1.0f - WheelAxis * 0.16f), MinZoomZ, MaxZoomZ);
}

void AAttractionPlayerController::CenterCamera()
{
    if (!IsValid(Board) || !IsValid(BoardCamera))
    {
        return;
    }
    FVector Center = FVector::ZeroVector;
    for (const FAttractionNode &Node : Board->Nodes)
    {
        Center += Node.Position;
    }
    if (!Board->Nodes.IsEmpty())
    {
        Center /= Board->Nodes.Num();
    }
    Center = Board->GetActorTransform().TransformPosition(Center);
    TargetZoomZ = OverviewZoomZ;
    Center.Z = OverviewZoomZ;
    BoardCamera->SetActorLocationAndRotation(Center, FRotator(-90.0f, -90.0f, 0.0f));
    UCameraComponent *Camera = BoardCamera->GetCameraComponent();
    Camera->SetRelativeTransform(FTransform::Identity);
    Camera->ClearAdditiveOffset();
    Camera->bUsePawnControlRotation = false;
    Camera->bLockToHmd = false;
    // This camera actor is the view target; a pawn transform must not offset it.
    SetViewTarget(BoardCamera);
    if (PlayerCameraManager)
    {
        PlayerCameraManager->UpdateCamera(0.0f);
    }
}
void AAttractionPlayerController::StartSwipe()
{
    CancelLeft();
    bSwiping = true;
    bHaveSwipePoint = CursorPoint(PreviousPoint, true);
}
void AAttractionPlayerController::EndSwipe()
{
    FVector Point;
    if (bSwiping && bHaveSwipePoint && CursorPoint(Point, true))
    {
        SliceSegment(PreviousPoint, Point);
    }
    bSwiping = false;
    bHaveSwipePoint = false;
}
void AAttractionPlayerController::ResetBoard()
{
    bSwiping = false;
    bHaveSwipePoint = false;
    CancelLeft();
    if (IsValid(Board))
    {
        Board->ResetSimulation();
    }
    CenterCamera();
}

void AAttractionPlayerController::SliceSegment(const FVector &Start, const FVector &End)
{
    if (Start.Equals(End, 0.01))
    {
        return;
    }
    // Closest-segment distance includes collinear/end-point crossings, with a small swipe tolerance.
    for (int32 I = Board->Tethers.Num() - 1; I >= 0; --I)
    {
        const FAttractionTether &T = Board->Tethers[I];
        if (!Board->Nodes.IsValidIndex(T.A) || !Board->Nodes.IsValidIndex(T.B))
        {
            continue;
        }
        FVector OnSwipe, OnTether;
        FMath::SegmentDistToSegmentSafe(Start, End, Board->Nodes[T.A].Position, Board->Nodes[T.B].Position, OnSwipe,
                                        OnTether);
        if (FVector::DistSquared(OnSwipe, OnTether) <= FMath::Square(5.f))
        {
            Board->SliceTether(I);
        }
    }
    const FTransform Transform = Board->GetActorTransform();
    DrawDebugLine(GetWorld(), Transform.TransformPosition(Start + FVector(0, 0, 3)),
                  Transform.TransformPosition(End + FVector(0, 0, 3)), FColor::White, false, 0.35f, 0, 2);
}

void AAttractionPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsValid(Board))
    {
        return;
    }
    if (bRecenterOnFirstTick)
    {
        CenterCamera();
        bRecenterOnFirstTick = false;
    }
    if (BoardCamera)
    {
        FVector Location = BoardCamera->GetActorLocation();
        Location.Z = FMath::FInterpTo(Location.Z, TargetZoomZ, DeltaTime, 12.0f);
        BoardCamera->SetActorLocation(Location);
        // Deprojection must see this tick's camera, including the deferred re-center.
        if (PlayerCameraManager)
        {
            PlayerCameraManager->UpdateCamera(DeltaTime);
        }
    }
    FVector Point;
    const bool bValidPoint = CursorPoint(Point);
    if (LeftMode != ELeftMode::None)
    {
        // A lost release/focus cancels instead of spawning or toggling unexpectedly.
        if (!IsInputKeyDown(EKeys::LeftMouseButton))
        {
            CancelLeft();
        }
        else
        {
            UpdateLeft();
        }
    }
    FVector SlicePoint;
    const bool bValidSlicePoint = CursorPoint(SlicePoint, true);
    if (bSwiping)
    {
        // A release outside the viewport or lost focus must not leave a stuck swipe.
        if (!IsInputKeyDown(EKeys::RightMouseButton))
        {
            EndSwipe();
        }
        else
        {
            if (bHaveSwipePoint && bValidSlicePoint)
            {
                SliceSegment(PreviousPoint, SlicePoint);
            }
            bHaveSwipePoint = bValidSlicePoint;
            if (bValidSlicePoint)
            {
                PreviousPoint = SlicePoint;
            }
        }
    }
    if (bValidPoint)
    {
        DrawDebugCircle(GetWorld(), Board->GetActorTransform().TransformPosition(Point + FVector(0, 0, 3)), 18.f, 48,
                        FColor::White, false, -1, 0, 1, Board->GetActorTransform().GetUnitAxis(EAxis::X),
                        Board->GetActorTransform().GetUnitAxis(EAxis::Y), false);
    }
}
