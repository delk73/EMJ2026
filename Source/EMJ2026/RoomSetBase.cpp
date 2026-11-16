#include "RoomSetBase.h"
#include "DirectOrScutGeometry.h"
#include "DirectOrScutSimSubsystem.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
using namespace DirectOrScut;
ARoomSetBase::ARoomSetBase()
{
    PrimaryActorTick.bCanEverTick = true;
    SetMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("SoundstageGeometry"));
    RootComponent = SetMesh;
    ContainmentBarrier = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ContainmentBarrier"));
    ContainmentBarrier->SetupAttachment(RootComponent);
    ContainmentBarrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ContainmentBarrier->SetVisibility(false);
    auto *Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("RoomLabel"));
    Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(1000, 0, 380));
    Label->SetRelativeRotation(FRotator(0, 180, 0));
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(38);
    auto *OverviewLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("OverviewLabel"));
    OverviewLabel->SetupAttachment(RootComponent);
    OverviewLabel->SetHorizontalAlignment(EHTA_Center);
    OverviewLabel->SetWorldSize(300);
    OverviewLabel->SetTextRenderColor(FColor(6, 40, 12));
    for (int32 I = 0; I < 3; ++I)
    {
        auto *L = CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("NeutralKey%d"), I));
        L->SetupAttachment(RootComponent);
        L->SetRelativeLocation(FVector(-500 + I * 550, 0, 350));
        L->bUseTemperature = true;
        L->SetTemperature(5800);
        L->SetIntensity(6500);
        L->SetAttenuationRadius(1900);
        L->SetSourceRadius(28);
        L->SetCastShadows(true);
    }
}
void ARoomSetBase::Block(FVector P, FVector S, int32 ID, float Yaw)
{
    Box(SetMesh->GetDynamicMesh(), P, S, ID, Yaw);
    if (P.Z + S.Z / 2 > 10 && P.Z - S.Z / 2 < 180 && S.Z > 15)
    {
        Solids.Add({P, S, Yaw});
    }
}
void ARoomSetBase::OnConstruction(const FTransform &T)
{
    Super::OnConstruction(T);
    AppliedLightingPower = 255;
    ApplyPowerLighting(EPowerState::Nominal);
    if (auto *Label = FindComponentByClass<UTextRenderComponent>())
    {
        Label->SetText(FText::FromString(RoomId == 7 ? TEXT("SCUT") : RoomName(RoomId)));
    }
    SetMesh->GetDynamicMesh()->Reset();
    Solids.Reset();
    Materials(SetMesh);
    TInlineComponentArray<UTextRenderComponent *> Labels(this);
    for (auto *Label : Labels)
    {
        if (Label->GetFName() == TEXT("OverviewLabel"))
        {
            const int32 FeedForRoom[] = {1, 8, 2, 3, 4, 5, 6, 7};
            Label->SetText(FText::FromString(FString::Printf(TEXT("[%d] %s"), FeedForRoom[RoomId], RoomName(RoomId))));
            Label->SetRelativeLocation(RoomId == 1 ? FVector(3025, 0, 300) : FVector(0, 940, 300));
            Label->SetRelativeRotation(FRotator(0, RoomId == 1 ? 0 : 90, 0));
        }
    }
    TInlineComponentArray<UPointLightComponent *> Keys(this);
    for (int32 I = 0; I < Keys.Num(); ++I)
    {
        Keys[I]->SetRelativeLocation(FVector(RoomId == 1 ? -2000 + I * 2000 : -500 + I * 550, 0, 300));
        Keys[I]->SetAttenuationRadius(RoomId == 1 ? 1900 : 1300);
    }
    const float Length = RoomId == 1 ? 6000 : 2200;
    Block({0, 0, -15}, {Length, 1800, 30}, 0);
    Block({0, -910, 220}, {Length, 30, 470});
    Block({0, 910, 220}, {Length, 30, 470});
    Block({-Length / 2, 0, 220}, {30, 1800, 470});
    Block({Length / 2, 0, 220}, {30, 1800, 470});
    Block({0, 0, 470}, {Length, 1800, 30});
    for (float Y : {-865.f, 865.f})
    {
        Block({0, Y, 30}, {Length, 12, 12}, 2);
    }
    switch (RoomId)
    {
    case 0:
        BuildReactor();
        break;
    case 1:
        BuildCorridor();
        break;
    case 2:
        BuildBridge();
        break;
    case 3:
        BuildMedical();
        break;
    case 4:
        BuildObservation();
        break;
    case 5:
        BuildQuarters();
        break;
    case 6:
        BuildArchive();
        break;
    case 7:
        BuildAirlock();
        break;
    default:
        break;
    }
    Finish(SetMesh);
    ContainmentBarrier->GetDynamicMesh()->Reset();
    Materials(ContainmentBarrier);
    // A thin containment field at the staged threshold, not articulated door art.
    Box(ContainmentBarrier->GetDynamicMesh(), {-150, 250, 135}, {6, 220, 270}, 4);
    for (int32 I = 0; I < 5; ++I)
    {
        Box(ContainmentBarrier->GetDynamicMesh(), {-155, 250, 30 + I * 50.f}, {4, 220, 12}, 3);
    }
    Finish(ContainmentBarrier);
    ContainmentBarrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARoomSetBase::BeginPlay()
{
    Super::BeginPlay();
    // Rebuild CPU-only footprint metadata after map load; the mesh reset is idempotent.
    OnConstruction(GetActorTransform());
    auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    if (!S)
    {
        return;
    }
    S->RegisterRoomSet(this);
    if (RoomId == 1)
    {
        return;
    }
    const float Yaw = RoomId >= 2 ? Stations()[RoomId - 2].Face * 45.f : 0;
    const FVector Center = RoomId >= 2 ? Radial(Yaw, PodCenterRadius()) : FVector::ZeroVector;
    const float Scale = RoomId >= 2 ? 1.4f : 1;
    for (const auto &Solid : Solids)
    {
        S->RecordObstacle(RoomId, Center + FRotator(0, Yaw, 0).RotateVector(Solid.Center / Scale), Solid.Size / Scale,
                          Yaw + Solid.Yaw);
    }
}
void ARoomSetBase::Tick(float D)
{
    Super::Tick(D);
    if (auto *Sim = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())
    {
        if (RoomId >= 0 && RoomId < 8)
        {
            ApplyPowerLighting(Sim->StationRooms[RoomId].Power);
        }
    }
    if (RoomId != 0)
    {
        return;
    }
    auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    const bool Alert = S && (S->Simulation.Rooms[0].bLockdown || S->Simulation.Rooms[0].AlertSeconds > 0 ||
                             !S->Simulation.Rooms[0].bIsPressurized);
    // Only the practical bars change; neutral key lights remain constant.
    if (auto *M = Cast<UMaterialInstanceDynamic>(SetMesh->GetMaterial(8)))
    {
        M->SetVectorParameterValue(TEXT("Tint"), Alert ? FLinearColor::Red : FLinearColor::White);
    }
}
void ARoomSetBase::BuildReactor()
{
    Block({500, 0, 210}, {160, 220, 420}, 7);
    for (float Y : {-430.f, -300.f, 300.f, 430.f})
    {
        Block({650, Y, 220}, {70, 70, 440}, 7);
        Block({610, Y, 220}, {8, 28, 390}, 8);
    }
}
void ARoomSetBase::BuildBridge()
{
    Block({1070, 0, 280}, {16, 1500, 270}, 7);
    for (int32 I = 0; I < 9; ++I)
    {
        const float Y = (I - 4) * 150.f;
        const float X = 600 - FMath::Abs(Y) * .3f;
        Block({X, Y, 65}, {170, 145, 130}, 7);
        Block({X, Y, 135}, {180, 145, 10});
        for (int32 J = 0; J < 4; ++J)
        {
            Block({X - 45 + J * 30, Y, 143}, {18, 25, 3}, J % 3 == 0 ? 3 : J % 3 == 1 ? 6 : 8);
        }
    }
}
void ARoomSetBase::BuildMedical()
{
    for (float Y : {-470.f, 470.f})
    {
        Block({250, Y, 60}, {530, 190, 120}, 1, 12);
        Block({270, Y, 125}, {450, 130, 10}, 0, 12);
        Block({550, Y, 170}, {45, 190, 260});
        Block({520, Y, 230}, {8, 150, 75}, 7);
        Block({514, Y, 235}, {4, 100, 8}, 3);
        Block({150, Y, 450}, {620, 300, 10}, 2);
    }
}
void ARoomSetBase::BuildObservation()
{
    Block({1070, 0, 240}, {12, 1500, 390}, 7);
    for (int32 I = 0; I < 16; ++I)
    {
        const float A = I * 22.5f;
        Block({850, FMath::Cos(FMath::DegreesToRadians(A)) * 490, 235 + FMath::Sin(FMath::DegreesToRadians(A)) * 210},
              {75, 95, 70}, 1);
    }
    Block({800, 0, 235}, {80, 120, 120}, 3);
    Block({750, 0, 235}, {16, 65, 65}, 4);
    FRandomStream Sky(1970);
    for (int32 I = 0; I < 70; ++I)
    {
        Block({1055, Sky.FRandRange(-700, 700), Sky.FRandRange(70, 410)}, {2, 3, 3}, 2);
    }
}
void ARoomSetBase::BuildQuarters()
{
    for (float Y : {-700.f, 700.f})
    {
        for (float X : {-200.f, 500.f})
        {
            Block({X, Y, 150}, {580, 280, 300});
            Block({X - 10, Y * .85f, 150}, {490, 100, 210}, 7);
            Block({X - 10, Y * .8f, 85}, {480, 170, 20}, 0);
        }
    }
    Block({300, 0, 95}, {550, 260, 20}, 0);
    Block({300, 0, 45}, {60, 120, 90}, 7);
}
void ARoomSetBase::BuildArchive()
{
    for (float Y : {-720.f, 720.f})
    {
        for (float X : {-300.f, 100.f, 500.f, 900.f})
        {
            Block({X, Y, 220}, {330, 220, 440}, 7);
            for (float Z : {100.f, 230.f, 360.f})
            {
                for (float DX : {-75.f, 75.f})
                {
                    Prism(SetMesh->GetDynamicMesh(), Octagon(95, 95, 28), 12,
                          FTransform(FRotator(90, 0, 0), FVector(X + DX, Y * .82f, Z)), 0);
                }
            }
            Block({X - 155, Y * .82f, 220}, {12, 12, 390}, 2);
        }
    }
}
void ARoomSetBase::BuildAirlock()
{
    Block({1060, 0, 220}, {40, 540, 440}, 4);
    Block({1020, 0, 215}, {20, 420, 360}, 7);
    for (int32 I = 0; I < 12; ++I)
    {
        Block({700, -300 + I * 50.f, 2}, {150, 30, 3}, I % 2 ? 3 : 7, 30);
    }
}
void ARoomSetBase::BuildCorridor()
{
    // Long beveled panels form an octagonal cross section in Y/Z.
    auto *M = SetMesh->GetDynamicMesh();
    const auto Cross = Octagon(1800, 470, 110);
    for (int32 I = 0; I < 8; ++I)
    {
        const FVector2D A = Cross[I], B = Cross[(I + 1) % 8];
        if (FMath::IsNearlyEqual(A.X, B.X) || FMath::IsNearlyEqual(A.Y, B.Y))
        {
            continue;
        }
        const FVector Edge(0, B.X - A.X, B.Y - A.Y);
        const FQuat Q = FQuat::FindBetweenNormals(FVector::YAxisVector, Edge.GetSafeNormal());
        FGeometryScriptPrimitiveOptions O;
        O.MaterialID = 1;
        UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
            M, O, FTransform(Q, FVector(0, (A.X + B.X) / 2, 235 + (A.Y + B.Y) / 2)), 6000, Edge.Size(), 24, 0, 0, 0,
            EGeometryScriptPrimitiveOriginMode::Center);
    }
    for (float X = -2600; X < 3000; X += 400)
    {
        for (float Y : {-800.f, 800.f})
        {
            Block({X, Y, 220}, {24, 30, 430}, 2);
            Block({X, Y * .95f, 40}, {270, 130, 12}, 7);
        }
        for (float Y : {-740.f, 740.f})
        {
            Block({X, Y, 395}, {35, 300, 40}, 1, 45);
            Block({X, Y, 60}, {35, 300, 40}, 1, 45);
        }
    }
}

void ARoomSetBase::ApplyContainmentBarrier(bool Sealed)
{
    ContainmentBarrier->SetVisibility(Sealed);
}

void ARoomSetBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (auto *S = GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>())
    {
        if (RoomId >= 0 && RoomId < 8 && S->RoomSetActors[RoomId].Get() == this)
        {
            S->RoomSetActors[RoomId].Reset();
        }
    }
    Super::EndPlay(EndPlayReason);
}

void ARoomSetBase::ApplyPowerLighting(EPowerState Power)
{
    if (AppliedLightingPower == uint8(Power))
    {
        return;
    }
    AppliedLightingPower = uint8(Power);
    TInlineComponentArray<UPointLightComponent *> Lights(this);
    for (int32 I = 0; I < Lights.Num(); ++I)
    {
        auto *Light = Lights[I];
        // Uneven practical pools, with a readable fill at the camera end.
        Light->SetVisibility(Power != EPowerState::Offline);
        const float Nominal = I == 0 ? 38000.f : I == 1 ? 17000.f : 29000.f;
        Light->SetIntensity(Power == EPowerState::Nominal ? Nominal : Power == EPowerState::Low ? 650.f : 0.f);
        Light->bUseTemperature = Power == EPowerState::Nominal;
        Light->SetTemperature(I == 1 ? 4300.f : 3400.f + RoomId * 130.f);
        Light->SetLightColor(Power == EPowerState::Low ? FLinearColor(1.f, .25f, .025f) : FLinearColor::White);
        Light->MarkRenderStateDirty();
    }
}
