#include "DirectOrScutGameMode.h"
#include "DirectOrScutSimSubsystem.h"
#include "GameFramework/SpectatorPawn.h"
#include "DirectOrScutHUD.h"
#include "Framework/Application/SlateApplication.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "DirectOrScutHullActor.h"
#include "DirectOrScutGeometry.h"
#include "Components/BoxComponent.h"
#include "DoorActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "CrewUnitBlockout.h"
#include "Components/DynamicMeshComponent.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Widgets/Input/SEditableTextBox.h"

ADirectOrScutGameMode::ADirectOrScutGameMode()
{
    PlayerControllerClass = ADirectOrScutPlayerController::StaticClass();
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    HUDClass = ADirectOrScutHUD::StaticClass();
}
void ADirectOrScutGameMode::BeginPlay()
{
    Super::BeginPlay();
    // Uncooked PIE can duplicate editor-only actors; remove the tagged overview helper before rendering.
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        if (It->ActorHasTag(TEXT("ScutEditorOverview")))
        {
            It->GetLightComponent()->SetVisibility(false);
            It->Destroy();
        }
    }
    if (!TActorIterator<ADirectOrScutHullActor>(GetWorld()))
    {
        GetWorld()->SpawnActor<ADirectOrScutHullActor>();
    }
    const FRotator View(-45, -135, 0);
    auto *Camera = GetWorld()->SpawnActor<ACameraActor>(-View.Vector() * 4500, View);
    Camera->Tags.Add(TEXT("DirectOrScutCamera"));
    auto *C = Camera->GetCameraComponent();
    C->ProjectionMode = ECameraProjectionMode::Perspective;
    C->FieldOfView = 73.7398f;
    // Fixed aspect preserves the complete ship at 4200uu, including on wide monitors.
    C->bConstrainAspectRatio = true;
    C->AspectRatio = 1.15f;
    C->PostProcessSettings.bOverride_AutoExposureMethod = true;
    C->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    C->PostProcessSettings.bOverride_AutoExposureBias = true;
    C->PostProcessSettings.AutoExposureBias = 0;
    C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    C->PostProcessSettings.bOverride_BloomIntensity = true;
    C->PostProcessSettings.BloomIntensity = .2f;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (auto *PC = It->Get())
        {
            PC->SetViewTarget(Camera);
        }
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ScutCapture")))
    {
        FTimerHandle Capture, Exit;
        GetWorldTimerManager().SetTimer(
            Capture,
            []()
            {
                FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("DirectOrScut/Workstation.png"),
                                                      true, false);
            },
            4.f, false);
        GetWorldTimerManager().SetTimer(Exit, []() { FPlatformMisc::RequestExit(false); }, 6.f, false);
    }
#if !UE_BUILD_SHIPPING
    // Opt-in capture of ordinary terminal play. No clock changes or scenario-state injection.
    if (FParse::Param(FCommandLine::Get(), TEXT("ScutTrailer")))
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Releases/TrailerCapture");
        IFileManager::Get().MakeDirectory(*Directory, true);
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 180.f);
        auto SubmitAt = [this](float Time, const TCHAR *Command)
        {
            FTimerHandle Handle;
            GetWorldTimerManager().SetTimer(
                Handle,
                [this, Input = FString(Command)]()
                {
                    if (auto *PC = GetWorld()->GetFirstPlayerController())
                    {
                        if (auto *HUD = Cast<ADirectOrScutHUD>(PC->GetHUD()))
                        {
                            if (auto UI = HUD->GetWorkstation())
                            {
                                UI->Prompt->SetText(FText::FromString(Input));
                                UI->Commit(FText::FromString(Input), ETextCommit::OnEnter);
                                UE_LOG(LogTemp, Display, TEXT("TRAILER INPUT %.3f : %s"), GetWorld()->GetTimeSeconds(),
                                       *Input);
                            }
                        }
                    }
                },
                Time, false);
        };
        SubmitAt(18, TEXT("show_cam bridge"));
        SubmitAt(27, TEXT("show_cam reactor_core"));
        SubmitAt(43, TEXT("trust dispatch"));
        SubmitAt(58, TEXT("show me the evidence"));
        SubmitAt(74, TEXT("investigate incident"));
        SubmitAt(94, TEXT("authorize relief"));
        SubmitAt(113, TEXT("show_cam quarters_mess"));
        SubmitAt(154, TEXT("file the optical finding"));
        SubmitAt(164, TEXT("report"));
        FTimerHandle Frames, Exit;
        GetWorldTimerManager().SetTimer(
            Frames,
            [this, Directory, Frame = 0]() mutable
            {
                const float Time = GetWorld()->GetTimeSeconds();
                FScreenshotRequest::RequestScreenshot(Directory / FString::Printf(TEXT("frame%05d.png"), Frame), true,
                                                      false);
                FFileHelper::SaveStringToFile(
                    FString::Printf(TEXT("%d,%.6f\n"), Frame++, Time), *(Directory / TEXT("frames.csv")),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
            },
            .1f, true, 1.f);
        GetWorldTimerManager().SetTimer(
            Exit,
            [this]()
            {
                UAudioMixerBlueprintLibrary::StopRecordingOutputToAnyPath(
                    this, EAudioRecordingExportType::WavFile, TEXT("Gameplay"),
                    FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Releases/TrailerCapture")));
                FTimerHandle Quit;
                GetWorldTimerManager().SetTimer(Quit, []() { FPlatformMisc::RequestExit(false); }, 2.f, false);
            },
            176.f, false);
    }
#endif
    if (FParse::Param(FCommandLine::Get(), TEXT("ScutValidate")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(
            Handle,
            [this]()
            {
                bool Pass = true;
                int32 DoorCount = 0, Humans = 0, Androids = 0;
                for (TActorIterator<ADoorActor> It(GetWorld()); It; ++It)
                {
                    ++DoorCount;
                    Pass &= It->ClickTarget->GetUnscaledBoxExtent().Equals(FVector(90, 120, 130));
                    It->Toggle();
                    It->Tick(2.f);
                    Pass &= FMath::IsNearlyEqual(It->Slab->GetRelativeLocation().Z, -250.f);
                    It->Toggle();
                    It->Tick(2.f);
                    Pass &= It->Slab->GetRelativeLocation().IsNearlyZero();
                    FHitResult Hit;
                    FVector P = It->GetActorLocation() + FVector(0, 0, 120);
                    FVector N = It->GetActorForwardVector();
                    GetWorld()->LineTraceSingleByChannel(Hit, P - N * 24, P + N * 24, DirectOrScut::DoorClickChannel,
                                                         FCollisionQueryParams(SCENE_QUERY_STAT(ScutDoor), true));
                    if (Hit.GetActor() != *It)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("SCUT door trace %s instead hit %s"),
                               *It->GetActorLocation().ToString(), *GetNameSafe(Hit.GetActor()));
                    }
                    Pass &= Hit.GetActor() == *It;
                }
                for (TActorIterator<ACrewUnitBlockout> It(GetWorld()); It; ++It)
                {
                    It->bAndroid ? ++Androids : ++Humans;
                    FVector O, E;
                    It->GetActorBounds(false, O, E);
                    const float W = It->bAndroid ? 85.f : 80.f, H = It->bAndroid ? 185.f : 180.f;
                    Pass &= FMath::IsNearlyEqual(E.X * 2, W, .1f) && FMath::IsNearlyEqual(E.Y * 2, W, .1f);
                    Pass &= FMath::IsNearlyEqual(E.Z * 2, H, .1f) && FMath::IsNearlyEqual(O.Z - E.Z, 0.f, .1f);
                    UE_LOG(LogTemp, Display, TEXT("SCUT unit dimensions %s: %s base %.2f"),
                           It->bAndroid ? TEXT("android") : TEXT("human"), *(E * 2).ToString(), O.Z - E.Z);
                }
                FHitResult Floor;
                GetWorld()->LineTraceSingleByChannel(Floor, FVector(1053, 0, 100), FVector(1053, 0, -100),
                                                     ECC_Visibility,
                                                     FCollisionQueryParams(SCENE_QUERY_STAT(ScutFloor), true));
                Pass &= Floor.bBlockingHit && FMath::IsNearlyZero(Floor.ImpactPoint.Z, .1f);
                for (const FVector &Sample : {DirectOrScut::Radial(135, 700), DirectOrScut::Radial(315, 700)})
                {
                    FHitResult SpokeFloor;
                    GetWorld()->LineTraceSingleByChannel(SpokeFloor, Sample + FVector(0, 0, 80),
                                                         Sample - FVector(0, 0, 80), ECC_Visibility,
                                                         FCollisionQueryParams(SCENE_QUERY_STAT(ScutSpokeFloor), true));
                    Pass &= SpokeFloor.bBlockingHit && FMath::IsNearlyZero(SpokeFloor.ImpactPoint.Z, .1f);
                }
                Pass &= DoorCount == 9 && Humans == 3 && Androids == 1;
                UE_LOG(LogTemp, Display, TEXT("SCUT VALIDATION %s: doors=%d humans=%d androids=%d floor=%.2f"),
                       Pass ? TEXT("PASS") : TEXT("FAIL"), DoorCount, Humans, Androids, Floor.ImpactPoint.Z);
                FPlatformMisc::RequestExitWithStatus(false, Pass ? 0 : 1);
            },
            3.f, false);
    }
}
