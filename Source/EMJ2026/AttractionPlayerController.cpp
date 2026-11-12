#include "AttractionPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
        BoardCamera->SetActorRotation(FRotationMatrix::MakeFromXY(-Normal,Board->GetActorQuat().GetAxisX()).Rotator());
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
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&AAttractionPlayerController::BeginLeft);
    InputComponent->BindKey(EKeys::LeftMouseButton,IE_Released,this,&AAttractionPlayerController::EndLeft);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&AAttractionPlayerController::StartSwipe);
    InputComponent->BindKey(EKeys::RightMouseButton,IE_Released,this,&AAttractionPlayerController::EndSwipe);
    InputComponent->BindKey(EKeys::R,IE_Pressed,this,&AAttractionPlayerController::ResetBoard);
    InputComponent->BindKey(EKeys::MouseScrollUp,IE_Pressed,this,&AAttractionPlayerController::ZoomIn);
    InputComponent->BindKey(EKeys::MouseScrollDown,IE_Pressed,this,&AAttractionPlayerController::ZoomOut);
}

bool AAttractionPlayerController::CursorPoint(FVector& LocalPoint,bool bAllowTray) const
{
    if (!IsValid(Board)) return false;
    if (!IsValid(BoardCamera)) return false;
    float MouseX,MouseY;
    int32 Width,Height;
    GetViewportSize(Width,Height);
    if (!GetMousePosition(MouseX,MouseY) || Width<=0 || Height<=0 ||
        MouseX<0 || MouseY<0 || MouseX>Width || MouseY>Height) return false;
    if (!bAllowTray && MouseX<AAttractionHUD::TrayWidth(Width,Height)) return false;
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
    return Board->IsInsidePlayfield(LocalPoint);
}

bool AAttractionPlayerController::MouseScreen(FVector2D& Point,int32& Width,int32& Height) const
{
    float X,Y;
    GetViewportSize(Width,Height);
    if (!GetMousePosition(X,Y) || X<0 || Y<0 || X>Width || Y>Height || Width<=0 || Height<=0) return false;
    Point = FVector2D(X,Y);
    return true;
}

void AAttractionPlayerController::BeginLeft()
{
    CancelLeft();
    int32 Width,Height;
    FVector2D Mouse;
    if (!MouseScreen(Mouse,Width,Height)) return;
    const int32 Tile = AAttractionHUD::HitTray(Mouse,Width,Height);
    if (Tile!=INDEX_NONE) { LeftGesture=ELeftGesture::Tray; DragIndex=Tile; return; }
    if (CursorPoint(LeftPoint))
    {
        LeftGesture=ELeftGesture::Pending;
        PressScreen=Mouse;
        PressTime=GetWorld()->GetRealTimeSeconds();
        bHaveLeftPoint=true;
    }
}

void AAttractionPlayerController::UpdateLeft()
{
    if (LeftGesture!=ELeftGesture::Pending && LeftGesture!=ELeftGesture::Stir) return;
    FVector2D Mouse;
    int32 Width,Height;
    FVector P;
    if (!MouseScreen(Mouse,Width,Height) || !CursorPoint(P)) { bHaveLeftPoint=false; return; }
    if (FVector2D::DistSquared(Mouse,PressScreen)>36) LeftGesture=ELeftGesture::Stir;
    if (LeftGesture==ELeftGesture::Stir && bHaveLeftPoint) Board->Stir(LeftPoint,P);
    LeftPoint=P;
    bHaveLeftPoint=true;
}

void AAttractionPlayerController::EndLeft()
{
    if (!IsValid(Board)) { CancelLeft(); return; }
    FVector P;
    if (LeftGesture==ELeftGesture::Tray)
    {
        if (DragIndex>=0 && DragIndex<3 && CursorPoint(P))
            Board->DropNode(AAttractionSimulation::MakeArchetype(static_cast<EAttractionArchetype>(DragIndex)),P);
    }
    else if (LeftGesture!=ELeftGesture::None)
    {
        UpdateLeft();
        if (LeftGesture==ELeftGesture::Pending && GetWorld()->GetRealTimeSeconds()-PressTime<=0.35 && CursorPoint(P))
            Board->ToggleNearestPin(P);
    }
    CancelLeft();
}

void AAttractionPlayerController::CancelLeft()
{
    LeftGesture=ELeftGesture::None;
    DragIndex=INDEX_NONE;
    bHaveLeftPoint=false;
}

void AAttractionPlayerController::ZoomIn() { TargetAltitude = FMath::Clamp(TargetAltitude/1.18f,600.f,4200.f); }
void AAttractionPlayerController::ZoomOut() { TargetAltitude = FMath::Clamp(TargetAltitude*1.18f,600.f,4200.f); }
void AAttractionPlayerController::StartSwipe()
{
    bSwiping = true;
    bHaveSwipePoint = CursorPoint(PreviousPoint,true);
}
void AAttractionPlayerController::EndSwipe()
{
    FVector Point;
    if (bSwiping && bHaveSwipePoint && CursorPoint(Point,true)) SliceSegment(PreviousPoint,Point);
    bSwiping = false; bHaveSwipePoint = false;
}
void AAttractionPlayerController::ResetBoard()
{
    bSwiping = false; bHaveSwipePoint = false;
    CancelLeft();
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
    if (LeftGesture!=ELeftGesture::None)
    {
        // A lost release/focus cancels instead of spawning or toggling unexpectedly.
        if (!IsInputKeyDown(EKeys::LeftMouseButton)) CancelLeft();
        else UpdateLeft();
    }
    FVector SlicePoint;
    const bool bValidSlicePoint = CursorPoint(SlicePoint,true);
    if (bSwiping)
    {
        // A release outside the viewport or lost focus must not leave a stuck swipe.
        if (!IsInputKeyDown(EKeys::RightMouseButton)) EndSwipe();
        else
        {
            if (bHaveSwipePoint && bValidSlicePoint) SliceSegment(PreviousPoint,SlicePoint);
            bHaveSwipePoint = bValidSlicePoint;
            if (bValidSlicePoint) PreviousPoint = SlicePoint;
        }
    }
    if (bValidPoint) DrawDebugCircle(GetWorld(),Board->GetActorTransform().TransformPosition(Point+FVector(0,0,3)),
        LeftGesture==ELeftGesture::Tray ? Board->DropletRadius : LeftGesture==ELeftGesture::Stir ? 350.f : 18.f,
        48,FColor::White,false,-1,0,1,Board->GetActorTransform().GetUnitAxis(EAxis::X),Board->GetActorTransform().GetUnitAxis(EAxis::Y),false);
}
