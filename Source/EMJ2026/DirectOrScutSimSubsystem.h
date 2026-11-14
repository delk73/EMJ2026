#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DirectOrScutSimulation.h"
#include "DirectOrScutSimSubsystem.generated.h"

enum class EExperimentPhase : uint8 { Briefing, Active, Resolved };
enum class EExperimentEnding : uint8 { None, COVERT_SUCCESS, MUTINY_EXPOSED, CREW_COLLAPSE, SCUT_CONTAINMENT, EXPERIMENT_INCOMPLETE };

enum class ECameraFeedState : uint8 { Online, Looped, Offline };
struct FSurveillanceCameraState { ECameraFeedState State=ECameraFeedState::Online; uint32 TamperTicks=0, RepairGraceTicks=0; };

UENUM()
enum class ERoomId : uint8 { ReactorCore=0, RingCorridor=1, Bridge=2, MedBay=3, ObservationDeck=4, QuartersMess=5, ServerArchive=6, AirlockChamber=7 };
struct FCompartmentSetTransform
{
    FVector WorldOrigin=FVector::ZeroVector;
    float Yaw=0;
};
DECLARE_MULTICAST_DELEGATE_TwoParams(FBiometricCrossing,int32,uint64);
struct FDirectorCommandRecord { uint64 Tick=0; FString Input; bool bConsole=false; };
struct FCinematicCue { uint64 Tick=0; ERoomId RoomId=ERoomId::ReactorCore; float Severity=0; FString Subtitle; };
struct FCrewStatDelta { float Stress=0, Suspicion=0, Compliance=0, Affinity=0, Utility=0, Integrity=0; };
enum class EDirectorAction : uint8 { Stats, Seal, Vent, Android, Triage, Release, Mentorship };
struct FInteractionChoice { FString Label; FCrewStatDelta Delta; EDirectorAction Action=EDirectorAction::Stats; };
struct FInteractionNode { FString Subtitle; TArray<FInteractionChoice> Choices; };
enum class EBeatGate : uint8 { Suspicion, Stress, AndroidTask, Collapse, Concealment, Quarantine };
struct FScenarioBeat { EBeatGate Gate; FInteractionNode Node; bool bFired=false, bResolved=false; int32 Crew=-1; };
struct FDirectorMenuOption { FString Label; bool bSupport=true; int32 Room=-1; FInteractionChoice Choice; };
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
struct FShipAIEvent { FString Text; int32 Room=-1; bool bTerminalVisible=true; };
enum class EShipAIScreenMenu : uint8
{
    None, AI, Events, Crew, RouteCrew, RouteRoom, RoutePretext,
    AlertRoom, AlertKind, Protocol, Containment, DoorRoom, FlushRoom, AndroidRoom, DoorAction, ConfirmScut, AndroidTask, Experiment, CameraRepair
};
struct FShipAIScreenChoice
{
    FString Label, Detail;
    bool bEnabled=true;
};
class ADirectOrScutSetAnchor;
enum class ECrewStagingPosture : uint8 { Upright, Panic, Blocked, Incapacitated, Triage };
struct FCrewStagingState {
    FVector Position=FVector::ZeroVector;
    float Yaw=0;
    int32 Room=-1, Anchor=INDEX_NONE, Partner=INDEX_NONE;
    ECrewStagingPosture Posture=ECrewStagingPosture::Upright;
    bool bHuddle=false;
};
struct FCrewSecretAgenda {
    uint32 Progress=0, ObservedTicks=0, Interruptions=0, Completions=0;
    bool bActivated=false, bCompleted=false;
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
    static constexpr uint64 ExperimentTicks=18000;
    static constexpr uint64 ExposureSustainTicks=90;
    EExperimentPhase ExperimentPhase=EExperimentPhase::Briefing;
    static constexpr uint32 TallyGraceTicks=150;
    static constexpr uint32 CameraLoopTicks=150;
    static constexpr uint32 CameraOfflineTicks=300;
    static constexpr uint32 CameraRepairGraceTicks=300;
    static constexpr float TallySuspicionPerSecond=.005f;
    ECameraFeedState CameraFeedState(int32 Room) const;
    bool IsCameraTallyActive(int32 Room) const;
    void QueueCameraInspection(int32 Room);
    FString BriefingObjective() const;
    uint64 SurveillanceStateHash() const;
    static const TCHAR* CameraStateName(ECameraFeedState State);
    EExperimentEnding ExperimentEnding=EExperimentEnding::None;
    float PeakSuspicion=0;
    uint64 ExposureTicks=0;
    uint16 ScutTargets=0;
    uint16 ScutEliminated=0;
    bool bScutProtected=true;
    void EvaluateExperimentEnding();
    bool BeginScut(int32 Room);
    static const TCHAR* EndingName(EExperimentEnding Ending);
    void ResetExperiment() { ResetSimulation(); }
    void SwitchScenario(bool Medical) { bMedicalScenario=Medical; ResetExperiment(); }

    bool bAutomaticAdvance=true;
    bool bAILink=false;
    bool bCleanDevRendering=true, bWorkstationVisible=true;
    TArray<EShipAIScreenMenu> MenuParents;
    bool IsRoomSealed(int32 Room) const;
    void MenuBack();
    int32 RequestedFeed=-1;
    int32 FollowedCrew=-1;
    uint64 FollowRevision=0;
    bool CanFollowCrew(int32 Crew) const;
    bool SetFollowedCrew(int32 Crew);
    FString FollowStatus() const;
    uint64 EventRevision=0;
    uint64 RuntimeRevision=0;
    FString TerminalBuffer() const;
    FString LatestPriorityAlert() const;
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
    void LogEvent(const FString& Text,int32 Room=-1,bool TerminalVisible=true);
    static int32 ResolveRoom(const FString& Name);
private:
    FSurveillanceCameraState Cameras[8];
    uint32 TallyTicks[12]{};
    int32 TallyRoom[12]{};
    int32 InspectedRoom=-1, PendingInspectedRoom=-2;
    void ResetSurveillance();
    void ApplyCameraInspection();
    void StepSurveillance();
    void TamperCamera(int32 Room,ECameraFeedState State);
    bool RepairCamera(int32 Room);
    void StartExperiment();
    double ResolvedCommandAccumulator=0;
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
    void RegisterSetAnchor(ADirectOrScutSetAnchor* Anchor);
    void ResetStaging();
    void StepStagingAndAgendas();
    FString AgendaTelemetry(int32 Crew, bool Reveal=false) const;
    TArray<TWeakObjectPtr<ADirectOrScutSetAnchor>> SetAnchors;
    FCrewStagingState CrewStaging[4];
    FCrewSecretAgenda SecretAgendas[4];
    uint32 StagingDoorRevision=0;
    void RegisterDoor(ADoorActor* Door);
    void RegisterCrew(ACrewUnitBlockout* Crew);
    FBiometricCrossing OnHeartbeatTick, OnBreathCycle;
    TMap<ERoomId,FCompartmentSetTransform> CompartmentSets;
    FVector ToSetWorld(int32 Room,FVector2D Position) const;
    static FCompartmentSetTransform DefaultSetTransform(int32 Room);
    static FVector MapSimulationToSet(int32 Room,FVector2D Position);
    TArray<FDirectorCommandRecord> CommandRecords;
    TArray<FCinematicCue> CinematicCues;
    TArray<FDirectorMenuOption> DirectorOptions;
    TArray<FScenarioBeat> BeatDeck;
    bool bMedicalScenario=false;
    void ConfigureScenario(bool Medical);
    void EvaluateBeatDeck();
    void RecordCue(int32 Room,float Severity,const FString& Subtitle,bool TerminalVisible=true);
    bool ExecuteDirectorOption(int32 Index);
    bool ResolveBeat(int32 Beat,int32 Choice);
    void ApplyInteraction(const FInteractionChoice& Choice,int32 Crew,int32 Room);
    void FixedStep();
    bool RecordObstacle(int32 Room,FVector Center,FVector Size,float Yaw);
    void FinishLayout();
    bool SetDoorOpen(int32 Index,bool Open);
    bool AssignRoom(int32 Unit,int32 Room) { return ExperimentPhase==EExperimentPhase::Active && Simulation.AssignRoom(Unit,Room); }
    void ResetSimulation();
    void AdvanceSteps(int32 Count);
    void SynchronizeActors();
    ADoorActor* GetDoorActor(int32 Index) const;
    AActor* GetBodyActor(int32 Index) const;
private:
    TWeakObjectPtr<ADoorActor> DoorActors[9];
    TWeakObjectPtr<AActor> BodyActors[12];
};
