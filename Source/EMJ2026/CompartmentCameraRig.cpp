#include "CompartmentCameraRig.h"
#include "DirectOrScutSimSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
ACompartmentCameraRig::ACompartmentCameraRig()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));
    for (int32 F = 0; F < 9; ++F)
    {
        auto *C = CreateDefaultSubobject<USceneCaptureComponent2D>(*FString::Printf(TEXT("Feed%d"), F));
        C->SetupAttachment(RootComponent);
        C->bCaptureEveryFrame = false;
        C->bCaptureOnMovement = false;
        C->FOVAngle = 73.7398f; // 36 mm sensor, 24 mm rectilinear lens.
        C->PostProcessBlendWeight = 1;
        C->ShowFlags.SetPostProcessing(true);
        auto &Post = C->PostProcessSettings;
        Post.bOverride_AutoExposureMethod = true;
        Post.AutoExposureMethod = AEM_Manual;
        Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Post.AutoExposureApplyPhysicalCameraExposure = false;
        Post.bOverride_AutoExposureBias = true;
        Post.AutoExposureBias = 1.f;
        Post.bOverride_VignetteIntensity = true;
        Post.VignetteIntensity = .38f;
        Post.bOverride_FilmGrainIntensity = true;
        Post.FilmGrainIntensity = .18f;
        Post.bOverride_SceneFringeIntensity = true;
        Post.SceneFringeIntensity = .3f;
        Post.bOverride_ColorSaturation = true;
        Post.ColorSaturation = FVector4(.68f, .68f, .68f, 1.f);
        Captures.Add(C);
    }
}
int32 ACompartmentCameraRig::RoomForFeed(int32 F)
{
    const int32 Map[] = {-1, 0, 2, 3, 4, 5, 6, 7, 1};
    return F >= 0 && F < 9 ? Map[F] : -1;
}
int32 ACompartmentCameraRig::FeedForRoom(int32 R)
{
    for (int32 F = 1; F < 9; ++F)
    {
        if (RoomForFeed(F) == R)
        {
            return F;
        }
    }
    return 0;
}
void ACompartmentCameraRig::OnConstruction(const FTransform &T)
{
    Super::OnConstruction(T);
    for (int32 F = 1; F < 9; ++F)
    {
        const int32 R = RoomForFeed(F);
        auto Stage = UDirectOrScutSimSubsystem::DefaultSetTransform(R);
        if (auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())
        {
            if (const auto *Registered = S->CompartmentSets.Find(static_cast<ERoomId>(R)))
            {
                Stage = *Registered;
            }
        }
        const FVector P =
            Stage.WorldOrigin + FRotator(0, Stage.Yaw, 0).RotateVector(FVector(R == 1 ? -2700 : -1000, 0, 210));
        Captures[F]->SetWorldLocationAndRotation(P, FRotator(0, Stage.Yaw, 0));
    }
}
void ACompartmentCameraRig::BeginPlay()
{
    Super::BeginPlay();
    InitializeFeeds();
}
void ACompartmentCameraRig::InitializeFeeds()
{
    if (!Targets.IsEmpty())
    {
        return;
    }
    OnConstruction(GetActorTransform());
    for (int32 F = 0; F < 9; ++F)
    {
        // Loaded map components can retain older serialized capture overrides.
        auto *Defaults = GetClass()->GetDefaultObject<ACompartmentCameraRig>()->Captures[F].Get();
        Captures[F]->PostProcessSettings = Defaults->PostProcessSettings;
        Captures[F]->PostProcessBlendWeight = 1;
        Captures[F]->ShowFlags.SetPostProcessing(true);
        Captures[F]->ShowFlags.SetTonemapper(true);
        Captures[F]->ShowFlags.SetEyeAdaptation(true);
        auto *RT = NewObject<UTextureRenderTarget2D>(this);
        RT->RenderTargetFormat = RTF_RGBA8;
        RT->InitAutoFormat(1024, 576);
        RT->UpdateResourceImmediate(true);
        Targets.Add(RT);
        Captures[F]->TextureTarget = RT;
        Captures[F]->CaptureSource = SCS_FinalColorLDR;
        FeedMaterials.Add(nullptr);
    }
}
void ACompartmentCameraRig::SetVisibleFeeds(int32 P, int32 A, bool Grid)
{
    Active = FMath::Clamp(P, 1, 8);
    PiP = A;
    bGrid = Grid;
}
void ACompartmentCameraRig::Tick(float D)
{
    Super::Tick(D);
    CaptureClock += D;
    if (CaptureClock < 1.f / 15)
    {
        return;
    }
    CaptureClock = 0;
    for (int32 F = 1; F < 9; ++F)
    {
        if (bGrid || F == Active || F == PiP)
        {
            Captures[F]->CaptureScene();
            ++CaptureCounts[F];
        }
    }
}
