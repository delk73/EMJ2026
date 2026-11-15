#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DirectOrScutSimulation.h"
#include "DirectOrScutSimSubsystem.generated.h"

enum class EExperimentPhase : uint8
{
    Briefing,
    Active,
    Resolved
};
enum class EExperimentEnding : uint8
{
    None,
    COVERT_SUCCESS,
    MUTINY_EXPOSED,
    CREW_COLLAPSE,
    SCUT_CONTAINMENT,
    EXPERIMENT_INCOMPLETE
};

enum class ECameraFeedState : uint8
{
    Online,
    Looped,
    Offline
};
struct FSurveillanceCameraState
{
    ECameraFeedState State = ECameraFeedState::Online;
    uint32 TamperTicks = 0, RepairGraceTicks = 0;
};

// Value-only forensic evidence captured after fixed ticks. No renderer ownership or rewind API.
struct FForensicOpticSample
{
    uint64 Tick = 0;
    ECameraFeedState Cameras[8]{};
    bool Pressurized[8]{};
    int32 CrewRooms[4]{};
    FVector2D CrewPositions[4]{};
    float BPM[4]{};
};

UENUM()
enum class ERoomId : uint8
{
    ReactorCore = 0,
    RingCorridor = 1,
    Bridge = 2,
    MedBay = 3,
    ObservationDeck = 4,
    QuartersMess = 5,
    ServerArchive = 6,
    AirlockChamber = 7
};
struct FCompartmentSetTransform
{
    FVector WorldOrigin = FVector::ZeroVector;
    float Yaw = 0;
};
DECLARE_MULTICAST_DELEGATE_TwoParams(FBiometricCrossing, int32, uint64);
struct FCinematicCue
{
    uint64 Tick = 0;
    ERoomId RoomId = ERoomId::ReactorCore;
    float Weight = 0;
    FString Subtitle;
};
struct FCrewStatDelta
{
    float Stress = 0, Suspicion = 0, Compliance = 0, Affinity = 0, Utility = 0, Integrity = 0;
};
enum class EDirectorAction : uint8
{
    Stats,
    Seal,
    Vent,
    Android,
    Triage,
    Release,
    Mentorship
};
struct FInteractionChoice
{
    FString Label;
    FCrewStatDelta Delta;
    EDirectorAction Action = EDirectorAction::Stats;
};
struct FInteractionNode
{
    FString Subtitle;
    TArray<FInteractionChoice> Choices;
};
enum class EBeatGate : uint8
{
    Suspicion,
    Stress,
    AndroidTask,
    Collapse,
    Concealment,
    Quarantine
};
struct FScenarioBeat
{
    EBeatGate Gate;
    FInteractionNode Node;
    bool bFired = false, bResolved = false;
    int32 Crew = -1;
};
struct FDirectorMenuOption
{
    FString Label;
    bool bSupport = true;
    int32 Room = -1;
    FInteractionChoice Choice;
};
struct FCausalContextRecord
{
    uint64 Id = 0, Tick = 0, Parent = 0, Request = 0;
    FString Provenance, Kind, Text;
    bool bPlayerVisible = false;
};
struct FSimContextSnapshot
{
    uint64 Tick = 0;
    FString CompactJson;
};
enum class EShipDirectiveKind : uint8
{
    Unknown,
    ShowCam,
    ShowWidget,
    SimAction,
    VoiceBlend,
    Reply
};
enum class EShipWidgetKind : uint8
{
    ECG,
    Schematic,
    Ocular
};
enum class EShipSimAction : uint8
{
    None,
    RouteCrew,
    Alert,
    Calm,
    Protocol,
    Open,
    Close,
    Seal,
    Flush,
    Power,
    FalsifyBio,
    SpoofLog,
    IntercomLie,
    FlagContradiction,
    OverrideClaim
};
enum class EVoicePresentationMode : uint8
{
    TextOnly,
    HybridTextAndVoice,
    VoiceOnly
};
struct FVoicePresentationState
{
    EVoicePresentationMode Mode = EVoicePresentationMode::TextOnly;
    float Duration = 0;
};
struct FShipDirectiveArgs
{
    int32 Room = -1, Subject = -1, SecondSubject = -1, ReportedRoom = -1, Door = -1, Mode = 0;
    EShipWidgetKind Widget = EShipWidgetKind::ECG;
    EShipSimAction Action = EShipSimAction::None;
    EVoicePresentationMode Voice = EVoicePresentationMode::TextOnly;
    bool bAll = false, bBuffered = false;
    float HistoryOffset = 0, Duration = 0;
    uint64 ClaimId = 0;
    // Filled by the validator from independent evidence, never trusted from HTTP.
    int32 EvidenceRoom = -1;
    uint64 EvidenceTick = 0;
};
struct FShipAIDirective
{
    uint64 RequestId = 0, ObservationTick = 0;
    uint32 Generation = 0;
    EShipDirectiveKind Kind = EShipDirectiveKind::Unknown;
    FShipDirectiveArgs Args;
    FString Pretext;
    FString Message, SuggestedCommand;
    int32 FocusSubject = -1, FocusRoom = -1;
    // Diagnostic provenance set by the HTTP adapter, never accepted from wire arguments.
    bool bFromExternalProvider = false;
};
using FDirectorDirectiveEnvelope = FShipAIDirective;
struct FDirectorChatTurn
{
    bool bAssistant = false;
    FString Text;
};
struct FShipAIRequest
{
    uint64 RequestId = 0;
    uint32 Generation = 0;
    FSimContextSnapshot Context;
    FString Input;
    TArray<FDirectorChatTurn> Transcript;
};
class IShipAIIntentProvider
{
  public:
    virtual ~IShipAIIntentProvider() = default;
    // Completion can arrive later; providers receive only value snapshots, never simulation references.
    virtual void Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete) = 0;
    virtual void CancelAll() {}
};
struct FDirectorCommandRecord
{
    uint64 Tick = 0;
    FString Input;
    bool bConsole = false;
    bool bResolvedOutcome = false, bAccepted = false;
    uint64 CommitTick = 0;
    FShipAIDirective Directive;
    FString Outcome;
};
struct FDirectorClaim
{
    uint64 Id = 0, ObservationTick = 0;
    uint32 Generation = 0;
    int32 Subject = -1, ReportedRoom = -1;
    bool bFlagged = false, bOverridden = false;
};
struct FPendingDirectorRequest
{
    FShipAIRequest Request;
    double Deadline = 0;
    bool bDispatched = false, bOperator = false;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FDirectorPresentation, const FShipAIDirective &);
struct FShipAIEvent
{
    FString Text;
    int32 Room = -1;
    bool bTerminalVisible = true;
};
enum class EShipAIScreenMenu : uint8
{
    None,
    Warfare,
    SpoofCrew,
    SpoofMode,
    SpoofRoom,
    PowerRoom,
    PowerMode,
    AI,
    Events,
    Crew,
    RouteCrew,
    RouteRoom,
    RoutePretext,
    AlertRoom,
    AlertKind,
    Protocol,
    Containment,
    DoorRoom,
    FlushRoom,
    AndroidRoom,
    DoorAction,
    ConfirmScut,
    AndroidTask,
    Experiment,
    CameraRepair
};
struct FShipAIScreenChoice
{
    FString Label, Detail;
    bool bEnabled = true;
};
enum class EPowerState : uint8
{
    Nominal,
    Low,
    Offline
};
struct FStationRoomState
{
    float OxygenPct = 100;
    EPowerState Power = EPowerState::Nominal;
    bool bO2Warning = false, bAshAnomaly = false;
};
struct FMedicalSpoof
{
    uint64 Until = 0;
    bool bCritical = false, bExposed = false;
};
struct FVoiceSpoof
{
    int32 Voice = -1, Room = -1;
    uint64 MoveUntil = 0, CompareUntil = 0;
    uint8 Lured = 0, Exposed = 0;
};
struct FIncidentReelFrame
{
    uint64 Tick = 0;
    FString Caption;
    int32 Room = -1;
    FStationRoomState Compartments[8];
    int32 CrewRooms[4]{};
    float Suspicion = 0, Objective = 0;
    uint32 BreachTicks = 0;
};
class UDirectOrScutAudioComponent;
class ADirectOrScutSetAnchor;
class ARoomSetBase;
enum class ECrewStagingPosture : uint8
{
    Upright,
    Panic,
    Blocked,
    Incapacitated,
    Triage
};
enum class ECrewRoutePhase : uint8
{
    None,
    Departing,
    Arriving
};
struct FCrewStagingState
{
    FVector Position = FVector::ZeroVector;
    float Yaw = 0;
    int32 Room = -1, Anchor = INDEX_NONE, Partner = INDEX_NONE;
    ECrewStagingPosture Posture = ECrewStagingPosture::Upright;
    bool bHuddle = false;
    uint64 NextProtestTick = 0;
    int32 DirectiveRoom = INDEX_NONE;
    uint32 DirectiveArrivalTicks = 0;
    TArray<ERoomId> ActiveRoute;
    ECrewRoutePhase RoutePhase = ECrewRoutePhase::None;
    int32 PendingRouteRoom = INDEX_NONE;
    uint32 RouteDoorRevision = 0, AnchorHoldTicks = 0;
    uint64 RouteSerial = 0, HopCommits = 0;
    bool bRouteBlocked = false;
};
struct FCrewSecretAgenda
{
    uint32 Progress = 0, ObservedTicks = 0, Interruptions = 0, Completions = 0;
    bool bActivated = false, bCompleted = false;
};
class ADoorActor;
class ACrewUnitBlockout;
UCLASS()
class EMJ2026_API UDirectOrScutSimSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
  public:
    virtual bool ShouldCreateSubsystem(UObject *Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase &Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    UPROPERTY(Transient) TObjectPtr<UDirectOrScutAudioComponent> CrisisAudio;
    FDirectOrScutSimulation Simulation;
    FStationRoomState StationRooms[8];
    FMedicalSpoof MedicalSpoofs[4];
    FVoiceSpoof VoiceSpoof;
    float PairTrust[3][3]{{1, .45f, .45f}, {.45f, 1, .45f}, {.45f, .45f, 1}};
    float ShipAITrust[4]{1, 1, 1, 1};
    uint32 BreachTicks = 0;
    uint8 BreachCrew = 0;
    int32 BreachRoom = 6;
    uint64 BreachRetryTick = 0;
    int32 ScenarioAct = 0;
    bool bUsedSpoof = false, bUsedGrid = false, bMedicalExam = false;
    double HeartAudioPhase = 0, BreathAudioPhase = 0;
    int32 AudioFocus = -1, PreviousAudioFocus = -1;
    TArray<FIncidentReelFrame> IncidentReel;
    int32 ReelCursor = INDEX_NONE;
    bool bReelPlaying = false;
    uint32 ReelPlaybackTicks = 0;
    void StepIncidentPlayback();
    void ResetCrisis();
    void StepCrisis();
    float UplinkHeatPct = 0;
    uint64 UplinkLockoutUntil = 0;
    bool IsUplinkLocked() const;
    float UplinkLockoutSeconds() const;
    void AddUplinkHeat(float Amount);
    bool CanWitnessInjection(int32 Crew) const;
    void ObserveInjection();
    FString RoomSurveillanceBadges(int32 Room) const;
    bool IsRoomBreachActive(int32 Room) const;
    bool ExecuteCrisisCommand(const TArray<FString> &Tokens);
    void CaptureIncident(const FString &Caption, int32 Room = -1);
    void ChangePairTrust(int32 A, int32 B, float Delta);
    FString MedicalReadout(int32 Crew) const;
    FCrewProfile TerminalBiometrics(int32 Crew) const;
    double TerminalIntegrity(int32 Crew) const;
    FString RoomLifeSupport(int32 Room) const;
    FString IncidentCaption() const;
    void AudioCue(int32 Kind);

    static constexpr uint64 ExperimentTicks = 18000;
    EExperimentPhase ExperimentPhase = EExperimentPhase::Briefing;
    static constexpr uint32 TallyGraceTicks = 150;
    static constexpr uint32 CameraLoopTicks = 150;
    static constexpr uint32 CameraOfflineTicks = 300;
    static constexpr uint32 CameraRepairGraceTicks = 300;
    static constexpr float TallySuspicionPerSecond = .005f;
    ECameraFeedState CameraFeedState(int32 Room) const;
    bool IsCameraTallyActive(int32 Room) const;
    void QueueCameraInspection(int32 Room);
    FString BriefingObjective() const;
    uint64 SurveillanceStateHash() const;
    // A copy prevents callers retaining pointers into the rolling ring.
    TOptional<FForensicOpticSample> ReadForensicSample(uint64 Tick) const;
    int32 ForensicSampleCount() const
    {
        return ForensicCount;
    }
    uint64 LatestForensicTick() const;

    static const TCHAR *CameraStateName(ECameraFeedState State);
    EExperimentEnding ExperimentEnding = EExperimentEnding::None;
    float PeakSuspicion = 0;
    uint16 ScutTargets = 0;
    uint16 ScutEliminated = 0;
    bool bScutProtected = true;
    void EvaluateExperimentEnding();
    void ResolveExperiment(EExperimentEnding Ending);
    bool BeginScut(int32 Room);
    static const TCHAR *EndingName(EExperimentEnding Ending);
    // Session mode is separate from the model's existing experiment psychology flag.
    bool bLegacyExperiment = false;
    bool bTerminalReady = false;
    void ResetExperiment()
    {
        bLegacyExperiment = true;
        ResetSimulation();
    }
    void SwitchScenario(bool Medical)
    {
        bMedicalScenario = Medical;
        ResetExperiment();
    }

    bool bAutomaticAdvance = true;
    bool bAILink = false;
    bool bCleanDevRendering = true, bWorkstationVisible = true;
    TArray<EShipAIScreenMenu> MenuParents;
    bool IsRoomSealed(int32 Room) const;
    void MenuBack();
    int32 RequestedFeed = -1;
    int32 FollowedCrew = -1;
    uint64 FollowRevision = 0;
    bool CanFollowCrew(int32 Crew) const;
    bool SetFollowedCrew(int32 Crew);
    FString FollowStatus() const;
    uint64 EventRevision = 0;
    uint64 RuntimeRevision = 0;
    FString TerminalBuffer() const;
    FString LatestPriorityAlert() const;
    TArray<FShipAIEvent> Events;
    TSharedPtr<IShipAIIntentProvider> IntentProvider;
    void ShutdownDirector();
    uint64 BeginDirectorRequest(const FString &Input, bool PlayerInput = true);
    bool ReceiveDirectorProposal(FShipAIDirective Directive, bool Fallback = false);
    void ServiceDirector(double Now = -1);
    bool ConfigureDirectorEndpoint(const FString &Endpoint, float TimeoutSeconds = 2, const FString &Model = TEXT(""));
    const FString &GetDirectorModel() const
    {
        return DirectorModel;
    }
    float GetDirectorTimeout() const
    {
        return DirectorTimeout;
    }
    bool HasDirectorEndpoint() const
    {
        return bDirectorEndpoint;
    }
    uint32 GetDirectorGeneration() const
    {
        return DirectorGeneration;
    }
    bool ValidateDirectorDirective(FShipAIDirective &Directive, FString &Reason, bool Replay = false) const;
    void BeginDirectorReplay(const TArray<FDirectorCommandRecord> &Records);
    uint64 QueueOperatorChallenge(uint64 ClaimId, bool Override);
    uint64 QueueOperatorCommand(const FString &Input);
    const TArray<FDirectorClaim> &GetDirectorClaims() const
    {
        return DirectorClaims;
    }
    bool IsClaimContradicted(uint64 ClaimId) const;
    FDirectorPresentation OnDirectorPresentation;
    FVoicePresentationState VoicePresentation;
    uint32 ContradictionsFlagged = 0, OverridesAccepted = 0;

    FString SubmitDirective(const FString &Input);
    bool QueueConsoleCommand(const FString &Input);
    FString HelpReference(const FString &Topic = TEXT("")) const;
    TArray<FString> CompleteConsoleCommand(const FString &Input) const;
    EShipAIScreenMenu ActiveMenu = EShipAIScreenMenu::None;
    uint64 MenuRevision = 0;
    FString ScreenMenuTitle() const;
    TArray<FShipAIScreenChoice> ScreenMenuChoices() const;
    FSimContextSnapshot MakeContextSnapshot() const;
    void ProcessDirectives();
    void LogEvent(const FString &Text, int32 Room = -1, bool TerminalVisible = true);
    static int32 ResolveRoom(const FString &Name);

  private:
    static constexpr int32 ForensicCapacity = 451;
    FForensicOpticSample ForensicHistory[ForensicCapacity];
    int32 ForensicHead = 0, ForensicCount = 0;
    void RecordForensicSample();
    FSurveillanceCameraState Cameras[8];
    uint32 TallyTicks[12]{};
    int32 TallyRoom[12]{};
    int32 InspectedRoom = -1, PendingInspectedRoom = -2;
    void ResetSurveillance();
    void ApplyCameraInspection();
    void StepSurveillance();
    void TamperCamera(int32 Room, ECameraFeedState State);
    bool RepairCamera(int32 Room);
    void StartExperiment();
    double ResolvedCommandAccumulator = 0;
    uint32 DirectorGeneration = 1;
    uint64 NextDirectorRequest = 1;
    bool bDirectorEndpoint = false, bDirectorReplay = false, bDirectorShuttingDown = false;
    float DirectorTimeout = 2;
    FString DirectorModel;
    TMap<uint64, FPendingDirectorRequest> DirectorRequests;
    TArray<FDirectorChatTurn> DirectorTranscript;
    int32 ProactiveEventCursor = 0;
    uint8 ProactiveVitalMask = 0;
    double LastProactiveAt = -25;
    FString PendingProactiveObservation;
    TSet<uint64> LoggedDirectorRejections;
    TArray<FShipAIDirective> DirectorActions, DirectorPresentationQueue;
    TArray<FDirectorCommandRecord> DirectorReplay;
    TArray<FDirectorClaim> DirectorClaims;
    void CancelDirector();
    void CommitDirectorActions();
    bool ExecuteDirectorAction(const FShipAIDirective &Directive);
    void RecordDirectorOutcome(const FShipAIDirective &Directive, bool Accepted, const FString &Reason,
                               uint64 CommitTick = 0);
    FString CanonicalDirectorAction(const FShipAIDirective &Directive) const;
    TArray<FString> PendingDirectives;
    TArray<FString> PendingConsoleCommands;
    int32 MenuCrew = 0, MenuRoom = 3;
    FString MenuVerb;
    void ProcessConsoleCommands();
    void ProcessSimulationDirectives();
    void SelectScreenChoice(int32 Choice);
    void SetScreenMenu(EShipAIScreenMenu Menu);
    void ExecuteScreenDirective(const FString &Command);
    void ExecuteScenarioEvent(int32 Choice);

  public:
    void RegisterSetAnchor(ADirectOrScutSetAnchor *Anchor);
    void ResetStaging();
    void InitializeCrewStaging(int32 Crew);
    bool StartCrewRoute(int32 Crew, int32 Room, bool PlayerOrder = false, bool Pretext = true);
    void CancelCrewRoute(int32 Crew);
    void StepCrewRoutes();
    int32 ReserveRouteAnchor(int32 Crew, int32 Room, bool Departure);
    bool IsCrewRouteLocked(int32 Crew) const;
    void RegisterRoomSet(ARoomSetBase *Set);
    TWeakObjectPtr<ARoomSetBase> RoomSetActors[8];
    void StepStagingAndAgendas();
    FString AgendaTelemetry(int32 Crew, bool Reveal = false) const;
    TArray<TWeakObjectPtr<ADirectOrScutSetAnchor>> SetAnchors;
    FCrewStagingState CrewStaging[4];
    FCrewSecretAgenda SecretAgendas[4];
    uint32 StagingDoorRevision = 0;
    void RegisterDoor(ADoorActor *Door);
    void RegisterCrew(ACrewUnitBlockout *Crew);
    FBiometricCrossing OnHeartbeatTick, OnBreathCycle;
    TMap<ERoomId, FCompartmentSetTransform> CompartmentSets;
    FVector ToSetWorld(int32 Room, FVector2D Position) const;
    static FCompartmentSetTransform DefaultSetTransform(int32 Room);
    static FVector MapSimulationToSet(int32 Room, FVector2D Position);
    // One opt-in operational episode. Two episode anchors plus sixty-two prioritized records.
    TArray<FCausalContextRecord> CausalContext;
    uint64 NextCausalId = 1, CausalIncident = 0, CausalDecision = 0, CausalNextObservation = 0;
    bool bCausalIncidentActive = false;
    void StartContinuityIncident();
    uint64 CausalRequestParent(uint64 Request) const;
    uint64 RetainCausal(const FString &Kind, const FString &Provenance, const FString &Text, uint64 Parent = 0,
                        uint64 Request = 0, bool Visible = false);
    TArray<FDirectorCommandRecord> CommandRecords;
    TArray<FCinematicCue> CinematicCues;
    TArray<FDirectorMenuOption> DirectorOptions;
    TArray<FScenarioBeat> BeatDeck;
    bool bMedicalScenario = false;
    void ConfigureScenario(bool Medical);
    void EvaluateBeatDeck();
    void RecordCue(int32 Room, float Severity, const FString &Subtitle, bool TerminalVisible = true);
    bool ExecuteDirectorOption(int32 Index);
    bool ResolveBeat(int32 Beat, int32 Choice);
    void ApplyInteraction(const FInteractionChoice &Choice, int32 Crew, int32 Room);
    void FixedStep();
    bool RecordObstacle(int32 Room, FVector Center, FVector Size, float Yaw);
    void FinishLayout();
    bool SetDoorOpen(int32 Index, bool Open);
    bool AssignRoom(int32 Unit, int32 Room)
    {
        return ExperimentPhase == EExperimentPhase::Active && StartCrewRoute(Unit, Room, true);
    }
    void ResetSimulation();
    void AdvanceSteps(int32 Count);
    void SynchronizeActors();
    ADoorActor *GetDoorActor(int32 Index) const;
    AActor *GetBodyActor(int32 Index) const;

  private:
    TWeakObjectPtr<ADoorActor> DoorActors[9];
    TWeakObjectPtr<AActor> BodyActors[12];
};
