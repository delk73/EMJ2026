#include "DirectOrScutSetAnchor.h"
#include "Components/ArrowComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
ADirectOrScutSetAnchor::ADirectOrScutSetAnchor()
{
    bIsEditorOnlyActor = false;
    auto *Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Facing"));
    RootComponent = Arrow;
    Arrow->SetHiddenInGame(true);
    Arrow->ArrowSize = 1.5f;
    auto *Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("SlotLabel"));
    Label->SetupAttachment(RootComponent);
    Label->SetHiddenInGame(true);
    Label->SetWorldSize(20);
    Label->SetRelativeLocation(FVector(0, 0, 35));
}
void ADirectOrScutSetAnchor::OnConstruction(const FTransform &T)
{
    Super::OnConstruction(T);
    FindComponentByClass<UTextRenderComponent>()->SetText(FText::FromString(FString::Printf(
        TEXT("%s [%d]"), *StaticEnum<EStagingSlotRole>()->GetNameStringByValue(int64(StagingRole)), SlotIndex)));
}
void ADirectOrScutSetAnchor::BeginPlay()
{
    Super::BeginPlay();
    if (auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())
    {
        S->RegisterSetAnchor(this);
    }
}
void ADirectOrScutSetAnchor::PopulateDefaults(UWorld *World)
{
    for (int32 R = 0; R < 8; ++R)
    {
        for (int32 RoleId = 0; RoleId < 7; ++RoleId)
        {
            for (int32 Slot = 0; Slot < (RoleId == 4 || RoleId == 5 ? 4 : 1); ++Slot)
            {
                const FVector Positions[] = {FVector(-100, -220, 0),
                                             FVector(-100, 220, 0),
                                             FVector(-180, 350, 0),
                                             FVector(180, 350, 0),
                                             FVector(-800, 500 + Slot * 100, 0),
                                             FVector(-450 + Slot * 220, 100, 45),
                                             FVector(-350, 250, 0)};
                const auto Stage = UDirectOrScutSimSubsystem::DefaultSetTransform(R);
                FVector Local = Positions[RoleId];
                float Facing = RoleId == 0 || RoleId == 1 ? 0.f : -90.f;
                if (R == 3 && RoleId == 5)
                {
                    Local = FVector(150, Slot % 2 ? 470 : -470, 130);
                    if (Slot >= 2)
                    {
                        Local.Y += Slot % 2 ? -220 : 220;
                        Local.Z = 0;
                    }
                    Facing = 0;
                }
                if (RoleId == 4)
                {
                    Local = FVector(-800, 400 + Slot * 105, 0);
                }
                const FVector Location = Stage.WorldOrigin + FRotator(0, Stage.Yaw, 0).RotateVector(Local);
                auto *A = World->SpawnActor<ADirectOrScutSetAnchor>(Location, FRotator(0, Stage.Yaw + Facing, 0));
                A->RoomId = ERoomId(R);
                A->StagingRole = EStagingSlotRole(RoleId);
                A->SlotIndex = Slot;
                A->OnConstruction(A->GetActorTransform());
#if WITH_EDITOR
                A->SetActorLabel(FString::Printf(TEXT("Stage_%d_%s_%d"), R,
                                                 *StaticEnum<EStagingSlotRole>()->GetNameStringByValue(RoleId), Slot));
#endif
            }
        }
    }
}

void ADirectOrScutSetAnchor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())
    {
        for (auto &Anchor : S->SetAnchors)
        {
            if (Anchor.Get() == this)
            {
                Anchor.Reset();
            }
        }
    }
    Super::EndPlay(EndPlayReason);
}
