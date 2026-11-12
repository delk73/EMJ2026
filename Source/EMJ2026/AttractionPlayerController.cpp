#include "AttractionPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

AAttractionGameMode::AAttractionGameMode()
{
    PlayerControllerClass = AAttractionPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
}

AAttractionPlayerController::AAttractionPlayerController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
}

void AAttractionPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    for (TActorIterator<AAttractionSimulation> It(GetWorld()); It; ++It) { Board = *It; break; }
    // Greenfield convenience: empty maps are playable without an authored board.
    if (!Board) Board = GetWorld()->SpawnActor<AAttractionSimulation>(FVector(0,0,150),FRotator::ZeroRotator);
    if (!Board) return;
    BoardCamera = GetWorld()->SpawnActor<ACameraActor>();
    if (BoardCamera)
    {
        const FVector Normal = Board->GetActorQuat().GetAxisZ();
        BoardCamera->SetActorLocation(Board->GetActorLocation() + Normal*1800);
        BoardCamera->SetActorRotation((-Normal).Rotation());
        BoardCamera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Orthographic);
        BoardCamera->GetCameraComponent()->OrthoWidth = 1800;
        SetViewTarget(BoardCamera);
    }
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}

void AAttractionPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&AAttractionPlayerController::Drop);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&AAttractionPlayerController::StartSwipe);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Released,this,&AAttractionPlayerController::EndSwipe);
    InputComponent->BindKey(EKeys::Tab,IE_Pressed,this,&AAttractionPlayerController::NextProfile);
    InputComponent->BindKey(EKeys::R,IE_Pressed,this,&AAttractionPlayerController::ResetBoard);
}

bool AAttractionPlayerController::CursorPoint(FVector& LocalPoint) const
{
    if (!IsValid(Board)) return false;
    FVector Origin,Direction;
    if (!DeprojectMousePositionToWorld(Origin,Direction)) return false;
    const FTransform Transform = Board->GetActorTransform();
    const FVector Normal = Transform.GetRotation().GetAxisZ();
    const double Denominator = FVector::DotProduct(Direction,Normal);
    if (FMath::Abs(Denominator) < 1.e-6) return false;
    const double T = FVector::DotProduct(Transform.GetLocation()-Origin,Normal)/Denominator;
    if (T < 0) return false;
    LocalPoint = Transform.InverseTransformPosition(Origin+Direction*T);
    LocalPoint.Z = 0;
    return !LocalPoint.ContainsNaN();
}

FAttractionNode AAttractionPlayerController::ActiveProfile() const
{
    FAttractionNode N;
    if (ProfileIndex == 1) { N.Receive = FVector(-1,0,0); N.Valence = 1; }
    if (ProfileIndex == 2) { N.Emit = FVector(1,1,0); N.Receive = FVector(0.5,1,0); N.Valence = 3; N.Mass = 2; }
    return N;
}

void AAttractionPlayerController::Drop()
{
    FVector Point;
    if (CursorPoint(Point)) Board->DropNode(ActiveProfile(),Point);
}
void AAttractionPlayerController::NextProfile() { ProfileIndex = (ProfileIndex+1)%3; }
void AAttractionPlayerController::StartSwipe()
{
    bSwiping = true;
    bHaveSwipePoint = CursorPoint(PreviousPoint);
}
void AAttractionPlayerController::EndSwipe()
{
    FVector Point;
    if (bSwiping && bHaveSwipePoint && CursorPoint(Point)) SliceSegment(PreviousPoint,Point);
    bSwiping = false; bHaveSwipePoint = false;
}
void AAttractionPlayerController::ResetBoard()
{
    bSwiping = false; bHaveSwipePoint = false;
    if (IsValid(Board)) Board->ResetSimulation();
}

void AAttractionPlayerController::SliceSegment(const FVector& Start, const FVector& End)
{
    if (Start.Equals(End,0.01)) return;
    // Closest-segment distance includes collinear/end-point crossings, with a small swipe tolerance.
    for (int32 I = Board->Tethers.Num()-1; I >= 0; --I)
    {
        const FAttractionTether& T = Board->Tethers[I];
        if (!Board->Nodes.IsValidIndex(T.A) || !Board->Nodes.IsValidIndex(T.B)) continue;
        FVector OnSwipe,OnTether;
        FMath::SegmentDistToSegmentSafe(Start,End,Board->Nodes[T.A].Position,Board->Nodes[T.B].Position,OnSwipe,OnTether);
        if (FVector::DistSquared(OnSwipe,OnTether) <= FMath::Square(5.f)) Board->SliceTether(I);
    }
    const FTransform Transform = Board->GetActorTransform();
    DrawDebugLine(GetWorld(),Transform.TransformPosition(Start),Transform.TransformPosition(End),FColor::Orange,false,0.35f,0,5);
}

void AAttractionPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsValid(Board)) return;
    FVector Point;
    const bool bValidPoint = CursorPoint(Point);
    if (bSwiping)
    {
        // A release outside the viewport or lost focus must not leave a stuck swipe.
        if (!IsInputKeyDown(EKeys::RightMouseButton)) EndSwipe();
        else
        {
            if (bHaveSwipePoint && bValidPoint) SliceSegment(PreviousPoint,Point);
            bHaveSwipePoint = bValidPoint;
            if (bValidPoint) PreviousPoint = Point;
        }
    }
    static const FColor Colors[] = {FColor::Green,FColor::Magenta,FColor::Yellow};
    static const TCHAR* Names[] = {TEXT("Attractor / valence 2"),TEXT("Chaser / valence 1"),TEXT("Bridge / valence 3")};
    if (bValidPoint) DrawDebugSphere(GetWorld(),Board->GetActorTransform().TransformPosition(Point),18,16,Colors[ProfileIndex],false,-1,0,2);
    if (GEngine) GEngine->AddOnScreenDebugMessage(48,0,FColor::White,
        FString::Printf(TEXT("LMB Drop | RMB drag Slice | Tab Profile | R Reset\n%s | Nodes %d/128 | Tethers %d"),Names[ProfileIndex],Board->Nodes.Num(),Board->Tethers.Num()));
}
