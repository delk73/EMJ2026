#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "DirectOrScutGameMode.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/BoxComponent.h"

// Uses the actual PIE viewport/camera and the same screen-picking function as LMB.
// Each open/close transition runs through real world ticks, including the open sill.
class FScutPIEDoorTraceCommand : public IAutomationLatentCommand
{
public:
    explicit FScutPIEDoorTraceCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        const double Now=FPlatformTime::Seconds();
        if (Started==0) Started=Now;
        if (Now-Started>90) { Test->AddError(TEXT("PIE doorway trace timed out")); return true; }
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        if (!World || !World->HasBegunPlay()) return false;
        auto* PC=Cast<ADirectOrScutPlayerController>(World->GetFirstPlayerController());
        if (!PC) return false;
        int32 X=0,Y=0; PC->GetViewportSize(X,Y);
        if (X<100 || Y<100) return false;
        if (!bCollected)
        {
            int32 Exterior=0;
            for (TActorIterator<ADoorActor> It(World);It;++It) { Doors.Add(*It); Exterior+=It->bExterior?1:0; }
            Test->TestEqual(TEXT("Nine chokepoints"),Doors.Num(),9);
            Test->TestEqual(TEXT("One exterior SCUT hatch"),Exterior,1);
            bCollected=true; Last=Now;
            return false;
        }
        if (Index>=Doors.Num())
        {
            UE_LOG(LogTemp,Display,TEXT("SCUT PIE DOOR TRACE COMPLETE: %d doors; closed/open/closed from orthographic viewport"),Index);
            return true;
        }
        auto* Door=Doors[Index].Get();
        if (!Door) { Test->AddError(TEXT("PIE door disappeared")); return true; }
        if (Now-Last<2.0) return false;
        FVector2D Pixel;
        Test->TestTrue(TEXT("Doorway projects inside viewport"),PC->ProjectWorldLocationToScreen(Door->GetActorLocation()+FVector(0,0,130),Pixel));
        Test->TestTrue(TEXT("Doorway pixel within frame"),Pixel.X>=0 && Pixel.Y>=0 && Pixel.X<X && Pixel.Y<Y);
        if (Phase==0 || Phase==1)
        {
            const bool Open=Phase==1;
            Test->TestTrue(TEXT("Vertical slab reached endpoint"),Door->Slab->GetRelativeLocation().Equals(FVector(0,0,Open?-250:0),.1));
            Test->TestTrue(TEXT("Exact 240x180x260 target"),Door->ClickTarget->GetUnscaledBoxExtent().Equals(FVector(90,120,130)));
            FHitResult Hit;
            PC->GetHitResultAtScreenPosition(Pixel,DirectOrScut::DoorClickChannel,true,Hit);
            Test->TestTrue(TEXT("Orthographic mouse trace selects intended door"),Hit.GetActor()==Door);
            if (Open) Test->TestTrue(TEXT("Open optical sill is green"),Door->Sill->GetMaterial(4)==Door->Sill->GetMaterial(8));
            Test->TestTrue(TEXT("First screen click dispatches"),PC->ToggleDoorAtScreenPosition(Pixel));
            Test->TestEqual(TEXT("Single click toggles once"),Door->bOpen,!Open);

            ++Phase;
        }
        else
        {
            Test->TestFalse(TEXT("Door closed again"),Door->bOpen);
            Test->TestTrue(TEXT("Slab returned to threshold"),Door->Slab->GetRelativeLocation().IsNearlyZero(.1));
            UE_LOG(LogTemp,Display,TEXT("SCUT PIE door %d/%d passed closed/open mouse traces: %s"),Index+1,Doors.Num(),*Door->GetActorLocation().ToString());
            ++Index; Phase=0;
        }
        Last=Now;
        return false;
    }
private:
    FAutomationTestBase* Test;
    TArray<TWeakObjectPtr<ADoorActor>> Doors;
    double Started=0,Last=0;
    int32 Index=0,Phase=0;
    bool bCollected=false;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScutPIEDoorTraceTest,"DirectOrScut.Chunk1.PIEDoorMouseTrace",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FScutPIEDoorTraceTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FScutPIEDoorTraceCommand>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
