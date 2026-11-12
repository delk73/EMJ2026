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
        BoardCamera->GetCameraComponent()->SetProjectionMode(ECameraProjectionMode::Perspective);
        BoardCamera->GetCameraComponent()->SetFieldOfView(60.f);
        BoardCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
        BoardCamera->GetCameraComponent()->SetAspectRatioAxisConstraint(AspectRatio_MaintainXFOV);
        BoardCamera->GetCameraComponent()->bOverrideAspectRatioAxisConstraint = true;
        // Texture already contains the lighting; neutral exposure keeps its greyscale readable.
        auto& Post = BoardCamera->GetCameraComponent()->PostProcessSettings;
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
    InputComponent->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&AAttractionPlayerController::ZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&AAttractionPlayerController::ZoomOut);
}

bool AAttractionPlayerController::CursorPoint(FVector& LocalPoint) const
{
    if (!IsValid(Board)) return false;
    if (!IsValid(BoardCamera)) return false;
    float MouseX,MouseY;
    int32 Width,Height;
    GetViewportSize(Width,Height);
    if (!GetMousePosition(MouseX,MouseY) || Width<=0 || Height<=0 ||
        MouseX<0 || MouseY<0 || MouseX>Width || MouseY>Height) return false;
    // Exact inverse of this camera's unconstrained, horizontal-FOV perspective view.
    // Read the camera transform directly, avoiding last-frame camera-cache lag during zoom.
    const float TanHalfFOV = FMath::Tan(FMath::DegreesToRadians(BoardCamera->GetCameraComponent()->FieldOfView*0.5f));
    const FVector Origin = BoardCamera->GetActorLocation();
    const FVector Direction = (BoardCamera->GetActorForwardVector()+
        BoardCamera->GetActorRightVector()*((2*MouseX/Width-1)*TanHalfFOV)+
        BoardCamera->GetActorUpVector()*((1-2*MouseY/Height)*TanHalfFOV*Height/Width)).GetSafeNormal();
    const FTransform Transform = Board->GetActorTransform();
    const FVector Normal = Transform.GetRotation().GetAxisZ();
    const double Denominator = FVector::DotProduct(Direction,Normal);
    if (FMath::Abs(Denominator) < 1.e-6) return false;
    const double T = FVector::DotProduct(Transform.GetLocation()-Origin,Normal)/Denominator;
    if (T < 0) return false;
    LocalPoint = Transform.InverseTransformPosition(Origin+Direction*T);
    LocalPoint.Z = 0;
    return !LocalPoint.ContainsNaN() && FMath::Abs(LocalPoint.X)<=Board->BoardHalfExtent && FMath::Abs(LocalPoint.Y)<=Board->BoardHalfExtent;
}

FAttractionNode AAttractionPlayerController::ActiveProfile() const
{
    FAttractionNode N;
    // Same mass, capture and valence: only coupling energy differentiates these profiles.
    if (ProfileIndex == 1) { N.Emit = FVector(2,0,0); N.Receive = FVector(2,0,0); }
    if (ProfileIndex == 2) { N.Emit = N.Receive = FVector::ZeroVector; N.RepulsionScale = 3.f; }
    return N;
}

void AAttractionPlayerController::Drop()
{
    FVector Point;
    if (CursorPoint(Point)) Board->DropNode(ActiveProfile(),Point);
}
void AAttractionPlayerController::NextProfile() { ProfileIndex = (ProfileIndex+1)%3; }
void AAttractionPlayerController::ZoomIn() { TargetAltitude = FMath::Clamp(TargetAltitude/1.18f,600.f,4200.f); }
void AAttractionPlayerController::ZoomOut() { TargetAltitude = FMath::Clamp(TargetAltitude*1.18f,600.f,4200.f); }
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
    DrawDebugLine(GetWorld(),Transform.TransformPosition(Start+FVector(0,0,3)),Transform.TransformPosition(End+FVector(0,0,3)),FColor::White,false,0.35f,0,2);
}

void AAttractionPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsValid(Board)) return;
    CameraAltitude = FMath::FInterpTo(CameraAltitude,TargetAltitude,DeltaTime,7.f);
    if (BoardCamera)
        BoardCamera->SetActorLocation(Board->GetActorLocation()+Board->GetActorQuat().GetAxisZ()*CameraAltitude);
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
    static const TCHAR* Names[] = {TEXT("Balanced"),TEXT("High-Pull"),TEXT("Repulsive Buffer")};
    if (bValidPoint) DrawDebugCircle(GetWorld(),Board->GetActorTransform().TransformPosition(Point+FVector(0,0,3)),18,32,FColor::White,false,-1,0,1,
        Board->GetActorTransform().GetUnitAxis(EAxis::X),Board->GetActorTransform().GetUnitAxis(EAxis::Y),false);
    if (GEngine) GEngine->AddOnScreenDebugMessage(48,0,FColor::White,
        FString::Printf(TEXT("LMB Drop | RMB drag Slice | Tab Profile | R Reset | Wheel Zoom\n%s | Nodes %d/128 | Tethers %d | Zoom %.0f"),Names[ProfileIndex],Board->Nodes.Num(),Board->Tethers.Num(),CameraAltitude));
}
