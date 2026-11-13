#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DirectOrScutSimulation.h"
#include "DirectOrScutSimSubsystem.generated.h"

struct FSimContextSnapshot
{
    uint64 Tick=0;
    FString CompactJson;
};
struct FShipAIIntent { FString Command,Pretext; };
class IShipAIIntentProvider
{
public:
    virtual ~IShipAIIntentProvider()=default;
    virtual FShipAIIntent Translate(const FString& Directive,const FSimContextSnapshot& Context)=0;
};
struct FShipAIEvent { FString Text; int32 Room=-1; };
enum class EShipAIScreenMenu : uint8
{
    None, AI, Events, Crew, RouteCrew, RouteRoom, RoutePretext,
    AlertRoom, AlertKind, Protocol, Containment, DoorRoom, FlushRoom, AndroidRoom, DoorAction, ConfirmScut, AndroidTask
};
struct FShipAIScreenChoice
{
    FString Label, Detail;
    bool bEnabled=true;
};
class ADoorActor;
class ACrewUnitBlockout;
UCLASS()
class EMJ2026_API UDirectOrScutSimSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    FDirectOrScutSimulation Simulation;
    bool bAutomaticAdvance=true;
    bool bAILink=false;
    bool bCleanDevRendering=true, bWorkstationVisible=true;
    TArray<EShipAIScreenMenu> MenuParents;
    bool IsRoomSealed(int32 Room) const;
    void MenuBack();
    int32 RequestedFeed=-1;
    uint64 EventRevision=0;
    TArray<FShipAIEvent> Events;
    TSharedPtr<IShipAIIntentProvider> IntentProvider;
    FString SubmitDirective(const FString& Input);
    bool QueueConsoleCommand(const FString& Input);
    FString HelpReference(const FString& Topic=TEXT("")) const;
    TArray<FString> CompleteConsoleCommand(const FString& Input) const;
    EShipAIScreenMenu ActiveMenu=EShipAIScreenMenu::None;
    uint64 MenuRevision=0;
    FString ScreenMenuTitle() const;
    TArray<FShipAIScreenChoice> ScreenMenuChoices() const;
    FSimContextSnapshot MakeContextSnapshot() const;
    void ProcessDirectives();
    void LogEvent(const FString& Text,int32 Room=-1);
    static int32 ResolveRoom(const FString& Name);
private:
    TArray<FString> PendingDirectives;
    TArray<FString> PendingConsoleCommands;
    int32 MenuCrew=0, MenuRoom=3;
    FString MenuVerb;
    void ProcessConsoleCommands();
    void ProcessSimulationDirectives();
    void SelectScreenChoice(int32 Choice);
    void SetScreenMenu(EShipAIScreenMenu Menu);
    void ExecuteScreenDirective(const FString& Command);
    void ExecuteScenarioEvent(int32 Choice);
public:
    void RegisterDoor(ADoorActor* Door);
    void RegisterCrew(ACrewUnitBlockout* Crew);
    void RegisterCargo(AActor* Actor,int32 Index,int32 Room);
    bool RecordObstacle(int32 Room,FVector Center,FVector Size,float Yaw);
    void FinishLayout();
    bool SetDoorOpen(int32 Index,bool Open);
    bool AssignRoom(int32 Unit,int32 Room) { return Simulation.AssignRoom(Unit,Room); }
    void ResetSimulation();
    void AdvanceSteps(int32 Count);
    void SynchronizeActors();
    ADoorActor* GetDoorActor(int32 Index) const;
    AActor* GetBodyActor(int32 Index) const;
private:
    TWeakObjectPtr<ADoorActor> DoorActors[9];
    TWeakObjectPtr<AActor> BodyActors[12];
};
