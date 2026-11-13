#include "DirectOrScutHUD.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "DirectOrScutGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void ADirectOrScutHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* Sim=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    if (!Canvas || !Sim) return;
    const auto& Model=Sim->Simulation;
    DrawRect(FLinearColor(.025f,.03f,.035f,.90f),12,12,300,478);
    float Y=22;
    auto Line=[&](const FString& Text,FLinearColor Color=FLinearColor(.86f,.91f,.95f))
    { DrawText(Text,Color,22,Y,GEngine->GetSmallFont(),1.05f); Y+=19; };
    Line(TEXT("DIRECT OR SCUT / SIMULATION"));
    Line(FString::Printf(TEXT("ORE REFINED   %.1f"),Model.OreRefined));
    Line(FString::Printf(TEXT("SHUTTLE ETA   %04.1fs"),Model.ShuttleETA()));
    Y+=5; Line(TEXT("COMPARTMENT PRESSURE"));
    for (int32 R=0;R<8;++R)
        Line(FString::Printf(TEXT("%-18s %s"),DirectOrScut::RoomName(R),Model.Rooms[R].bIsPressurized?TEXT("OK"):TEXT("VACUUM")),
            Model.Rooms[R].bIsPressurized?FLinearColor(.7f,.9f,.9f):FLinearColor(1,.14f,.12f));
    Y+=5;
    auto* PC=Cast<ADirectOrScutPlayerController>(PlayerOwner);
    const int32 Index=PC?PC->SelectedUnit:-1;
    if (Index>=0 && Index<4 && Model.Bodies[Index].bRegistered)
    {
        const auto& U=Model.Bodies[Index];
        Line(FString::Printf(TEXT("UNIT      %s %02d"),U.bAndroid?TEXT("ANDROID"):TEXT("HUMAN"),Index+1));
        Line(FString::Printf(TEXT("ROOM      %s"),DirectOrScut::RoomName(U.Room)));
        Line(FString::Printf(TEXT("ASSIGNED  %s"),DirectOrScut::RoomName(U.AssignedRoom)));
        Line(FString::Printf(TEXT("STATE     %s"),DirectOrScutStateName(U.State)));
        Line(FString::Printf(TEXT("STRESS    %.0f%%    INTEGRITY %.0f%%"),U.Stress,U.Integrity));
    }
    else { Line(TEXT("UNIT      Select a crew unit")); Y+=76; }
    Y+=5;
    Line(TEXT("LMB: crew / DIRECT / interior door"));
    Line(TEXT("RMB or Shift+LMB: SCUT hatch"));
    Line(TEXT("F1: force human panic"));
}
