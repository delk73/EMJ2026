#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "LevelEditorViewport.h"
#include "RoomSetBase.h"
#include "DirectOrScutSimSubsystem.h"
#include "DirectOrScutGeometry.h"
#include "CompartmentCameraRig.h"
#include "DirectOrScutHUD.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SWindow.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

class FSlice10aMapCheck : public IAutomationLatentCommand
{
public:
    explicit FSlice10aMapCheck(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(!Started)Started=FPlatformTime::Seconds();
        if(FPlatformTime::Seconds()-Started<1)return false;
        auto* W=GEditor->GetEditorWorldContext().World();
        TArray<ARoomSetBase*> Sets;
        for(TActorIterator<ARoomSetBase> It(W);It;++It)
        {
            Sets.Add(*It);const int32 Tile=ACompartmentCameraRig::FeedForRoom(It->RoomId)-1;
            Test->TestTrue(TEXT("Saved map mirrors UX feed order"),It->GetActorLocation().Equals(FVector((Tile%4)*3500,0,Tile<4?3500:0),1));
            Test->TestTrue(TEXT("Corridor fits its column by facing Y"),FMath::IsNearlyEqual(It->GetActorRotation().Yaw,It->RoomId==1?90.f:0.f));
        }
        Test->TestEqual(TEXT("Eight saved room sets"),Sets.Num(),8);
        for(int32 A=0;A<Sets.Num();++A)for(int32 B=0;B<Sets.Num();++B)if(A!=B)
        {
            const FBox Other=Sets[B]->SetMesh->Bounds.GetBox();
            Test->TestFalse(TEXT("Enclosed stage shells do not overlap"),Sets[A]->SetMesh->Bounds.GetBox().Intersect(Other));
            TInlineComponentArray<UPointLightComponent*> Lights(Sets[A]);
            for(auto* L:Lights)
            {
                Test->TestTrue(TEXT("Key radius cannot reach another stage shell"),FVector::Distance(L->GetComponentLocation(),Other.GetClosestPointTo(L->GetComponentLocation()))>L->AttenuationRadius);
                Test->TestEqual(TEXT("Clean neutral temperature retained"),L->Temperature,5800.f);
            }
        }
        for(TActorIterator<ACompartmentCameraRig> It(W);It;++It)for(int32 F=1;F<=8;++F)
        {
            const int32 R=ACompartmentCameraRig::RoomForFeed(F);const auto Stage=UDirectOrScutSimSubsystem::DefaultSetTransform(R);
            Test->TestTrue(TEXT("Saved camera matches translated and rotated stage"),It->Captures[F]->GetComponentLocation().Equals(Stage.WorldOrigin+FRotator(0,Stage.Yaw,0).RotateVector(FVector(R==1?-2700:-1000,0,210)),1));
        }
        Test->TestTrue(TEXT("Overview camera persisted"),W->EditorViews.Num()>0 && W->EditorViews[0].CamPosition.Y>9000);
        for(auto* Client:GEditor->GetLevelViewportClients())if(Client && Client->GetWorld()==W && Client->IsPerspective() && Client->Viewport)
        {
            Test->TestTrue(TEXT("Editor opens on overview framing"),Client->GetViewLocation().Equals(FVector(5250,12000,4700),1));
            TArray<FColor> Pixels;const FIntPoint Size=Client->Viewport->GetSizeXY();
            const bool Captured=Size.X>0 && Size.Y>0 && Client->Viewport->ReadPixels(Pixels);
            Test->TestTrue(TEXT("Editor overview screenshot captured"),Captured);
            if(Captured){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);Test->TestTrue(TEXT("Editor overview screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice10aMap.png"))));}
            break;
        }
        return true;
    }
private: FAutomationTestBase* Test;double Started=0;
};

class FSlice10aInputCheck : public IAutomationLatentCommand
{
public:
    explicit FSlice10aInputCheck(FAutomationTestBase* T):Test(T) {}
    static TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,FName Tag)
    {
        if(W->GetTag()==Tag)return W;
        auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=Find(Children->GetChildAt(I),Tag))return Found;
        return nullptr;
    }
    bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(Now-Started>100){Test->AddError(TEXT("Slice 10a input timed out"));return true;}
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W || !W->HasBegunPlay())return false;
        auto* S=W->GetSubsystem<UDirectOrScutSimSubsystem>();auto* PC=W->GetFirstPlayerController();auto* H=PC?Cast<ADirectOrScutHUD>(PC->GetHUD()):nullptr;
        if(!S || !S->Simulation.bReady || !H || !H->GetWorkstation())return false;
        auto UI=H->GetWorkstation();if(UI->GetCachedGeometry().GetLocalSize().X<100 || Now-Last<.22)return false;
        if(Stage==0 && S->Simulation.TickIndex<30)return false;
        Last=Now;auto& App=FSlateApplication::Get();
        auto Key=[&](FKey K){App.ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));App.ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));};
        auto Click=[&](FName Tag)
        {
            auto Button=Find(UI->GetContentRoot(),Tag);Test->TestTrue(TEXT("Clickable UI target exists"),Button.IsValid());if(!Button)return;
            const auto& G=Button->GetCachedGeometry();const FVector2D Pos=G.LocalToAbsolute(G.GetLocalSize()*.5f);auto Window=App.FindWidgetWindow(Button.ToSharedRef());if(!Window){Test->AddError(TEXT("Button window missing"));return;}
            App.ProcessMouseMoveEvent(FPointerEvent(0,Pos,Pos-FVector2D(1,0),TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()));
            App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,Pos,Pos,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
            App.ProcessMouseButtonUpEvent(FPointerEvent(0,Pos,Pos,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        };
        auto Selected=[&](int32 Index){Test->TestEqual(TEXT("Arrow/follow selected tile"),UI->SelectedCameraIndex,Index);Test->TestEqual(TEXT("Selected tile matches active feed"),UI->ActiveFeed,Index+1);};
        auto Full=[&](int32 Feed){Test->TestTrue(TEXT("Single feed fullscreen"),UI->bFullscreen && !UI->bGrid);Test->TestEqual(TEXT("Fullscreen feed"),UI->ActiveFeed,Feed);Test->TestFalse(TEXT("Inspection did not open console"),UI->bConsoleOpen);};
        auto Command=[&](const TCHAR* Text){S->QueueConsoleCommand(Text);S->AdvanceSteps(1);};
        UE_LOG(LogTemp,Display,TEXT("Slice10a stage %d selected=%d follow=%d"),Stage,UI->SelectedCameraIndex,S->FollowedCrew);
        switch(Stage)
        {
        case 0:Test->TestFalse(TEXT("Overview light excluded from PIE"),bool(TActorIterator<ADirectionalLight>(W)));S->bAutomaticAdvance=false;S->ResetSimulation();UI->SetConsoleOpen(false);UI->ShowGrid();UI->SelectFeed(1,false);App.SetKeyboardFocus(UI);break;
        case 1:Key(EKeys::Left);break;
        case 2:
        {
            Selected(3);
            auto Tile=Find(UI->GetContentRoot(),TEXT("ScutCamera4"));
            Test->TestTrue(TEXT("Selected border is bright phosphor"),Tile.IsValid() && StaticCastSharedPtr<SBorder>(Tile)->GetBorderBackgroundColor().GetSpecifiedColor().G>.9f);
            auto Other=Find(UI->GetContentRoot(),TEXT("ScutCamera1"));
            Test->TestTrue(TEXT("Unselected border remains subdued"),Other.IsValid() && StaticCastSharedPtr<SBorder>(Other)->GetBorderBackgroundColor().GetSpecifiedColor().G<.1f);
            Key(EKeys::Right);break;
        }
        case 3:Selected(0);Key(EKeys::Up);break;
        case 4:Selected(4);Key(EKeys::Down);break;
        case 5:Selected(0);Key(EKeys::Down);Key(EKeys::Right);Key(EKeys::Right);Key(EKeys::Right);break;
        case 6:Selected(7);Key(EKeys::Enter);break;
        case 7:Full(8);Test->TestTrue(TEXT("Fullscreen back action"),Find(UI->GetContentRoot(),TEXT("ScutBackToGrid")).IsValid());Key(EKeys::G);break;
        case 8:Test->TestTrue(TEXT("G restores grid"),UI->bGrid);Key(EKeys::F);break;
        case 9:Full(8);Key(EKeys::Escape);break;
        case 10:Test->TestTrue(TEXT("Escape restores grid"),UI->bGrid);Click(TEXT("ScutInspect3"));break;
        case 11:Full(3);Key(EKeys::Escape);break;
        case 12:Selected(2);Key(EKeys::Slash);break;
        case 13:
        {
            Test->TestTrue(TEXT("Slash opens console prompt"),UI->bConsoleOpen && UI->IsPromptFocused());Key(EKeys::Right);Selected(2);
            auto Prompt=StaticCastSharedPtr<SEditableTextBox>(Find(UI->GetContentRoot(),TEXT("ScutPrompt")));
            Test->TestTrue(TEXT("Console prompt exists"),Prompt.IsValid());if(Prompt)Prompt->SetText(FText::FromString(TEXT("follow vale")));
            Key(EKeys::Enter);Test->TestEqual(TEXT("Follow awaits fixed tick"),S->FollowedCrew,-1);break;
        }
        case 14:Test->TestEqual(TEXT("Console follow command executed"),S->FollowedCrew,0);Test->TestTrue(TEXT("Enter submits with console open"),UI->bConsoleOpen);Key(EKeys::Escape);break;
        case 15:Selected(1);Test->TestTrue(TEXT("Following does not expand grid"),UI->bGrid);Click(TEXT("ScutStopFollow"));break;
        case 16:Test->TestEqual(TEXT("Stop follow action"),S->FollowedCrew,-1);Click(TEXT("ScutFollowCrew0Feed2"));break;
        case 17:
            Test->TestEqual(TEXT("Tile badge activates follow"),S->FollowedCrew,0);UI->SelectFeed(6,false);Command(TEXT("follow vale"));UI->ExpandSelected();
            S->Simulation.bExperiment=false;for(int32 D=0;D<8;++D)S->SetDoorOpen(D,true);
            Test->TestTrue(TEXT("Assign real navigation route"),S->AssignRoom(0,3));break;
        case 18:
            Selected(1);Full(2);
            for(int32 I=0;I<3000 && S->Simulation.Bodies[0].Room==2;++I)S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Crew physically crossed into logical ring"),S->Simulation.Bodies[0].Room,1);break;
        case 19:
            Selected(7);Full(8);Test->TestTrue(TEXT("Live following room label"),S->FollowStatus().Contains(TEXT("RING_CORRIDOR")));
            for(int32 I=0;I<5000 && S->Simulation.Bodies[0].Room!=3;++I)S->AdvanceSteps(1);
            Test->TestEqual(TEXT("Crew physically crossed into med bay"),S->Simulation.Bodies[0].Room,3);break;
        case 20:Selected(2);Full(3);Key(EKeys::G);Command(TEXT("follow ash"));break;
        case 21:Selected(4);Test->TestTrue(TEXT("Follow tracks grid without expanding"),UI->bGrid);Test->TestEqual(TEXT("Android can be followed"),S->FollowedCrew,3);Command(TEXT("follow invalid"));break;
        case 22:Test->TestEqual(TEXT("Invalid follow preserves target"),S->FollowedCrew,3);Command(TEXT("follow off"));Key(EKeys::F3);break;
        case 23:Test->TestEqual(TEXT("Crew dossier opened"),int32(S->ActiveMenu),int32(EShipAIScreenMenu::Crew));Selected(4);Key(EKeys::Left);Selected(4);Click(TEXT("ScutDossierName2"));break;
        case 24:Test->TestEqual(TEXT("Dossier activates follow"),S->FollowedCrew,2);Selected(3);Full(4);UI->ShowGrid();break;
        case 25:
        {
            Test->TestTrue(TEXT("Follow indicator widget"),Find(UI->GetContentRoot(),TEXT("ScutFollowIndicator")).IsValid());
            TArray<FColor> Pixels;FIntVector Size;const bool Captured=App.TakeScreenshot(UI->GetContentRoot(),Pixels,Size);Test->TestTrue(TEXT("Verification screenshot captured"),Captured);
            if(Captured){TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);Test->TestTrue(TEXT("Verification screenshot saved"),FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("DirectOrScut/Slice10aPIE.png"))));}
            Command(TEXT("follow chen"));break;
        }
        case 26:Test->TestEqual(TEXT("Medic follow command"),S->FollowedCrew,1);S->Simulation.Bodies[1].Integrity=0;break;
        case 27:Test->TestEqual(TEXT("Lost crew stops follow cleanly"),S->FollowedCrew,-1);Command(TEXT("follow chen"));break;
        case 28:Test->TestEqual(TEXT("Unavailable target rejected"),S->FollowedCrew,-1);Test->TestTrue(TEXT("Follow completion"),S->CompleteConsoleCommand(TEXT("follow va")).Contains(TEXT("follow vale ")));S->ResetSimulation();return true;
        }
        S->AdvanceSteps(1);++Stage;return false;
    }
private: FAutomationTestBase* Test;double Started=0,Last=0;int32 Stage=0;
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlice10aTest,"DirectOrScut.Slice10a.GridFollowPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSlice10aTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/DirectOrScut_Soundstage")));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice10aMapCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSlice10aInputCheck>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
