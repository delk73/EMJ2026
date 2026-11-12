#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AttractionSimulation.generated.h"

UENUM(BlueprintType)
enum class EAttractionAgentRole : uint8 { None, Builder, Filler, Trimmer };

USTRUCT(BlueprintType)
struct FAttractionStageConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> Lobes; // XY center, Z soft radius
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector2D> RootAnchors;
};

UENUM(BlueprintType)
enum class EAttractionArchetype : uint8 { Driver, Buffer, Anchor };

// Force on receiver i from emitter j = Receive_i dot (ChannelMatrix * Emit_j).
// Positive affinity attracts; negative affinity repels. Reciprocity is optional.
USTRUCT(BlueprintType)
struct FAttractionNode
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EAttractionAgentRole AgentRole = EAttractionAgentRole::None;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bConnectedToRoot = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRootAnchor = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 PotencyTier = 1;
    float WorkCharge = 0;
    float AgentSlideProgress = 0;
    int32 AgentSlideTarget = INDEX_NONE;
    FVector GrowthDirection = FVector(1,0,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EAttractionArchetype Archetype = EAttractionArchetype::Driver;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Position = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Velocity = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01")) float Mass = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Emit = FVector(1,0,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Receive = FVector(1,0,0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float RepulsionScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bPinned = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float CaptureRadius = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) int32 Valence = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 BondType = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float InternalEnergy = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float PulseTimeRemaining = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float PulseCooldown = 0;
};

USTRUCT(BlueprintType)
struct FAttractionBondRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TypeA = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TypeB = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float RestLength = 140.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Stiffness = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Damping = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float BreakStrain = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float MaxRelativeSpeed = 250.f;
};

USTRUCT(BlueprintType)
struct FAttractionTether
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 A = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 B = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FAttractionBondRule Rule;
};

USTRUCT(BlueprintType)
struct FAttractionTriangle
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 A = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 B = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 C = 0;
};

// Self-contained XY board. Positions/velocities are local to this actor, in cm / cm/s.
UCLASS()
class EMJ2026_API AAttractionSimulation : public AActor
{
    GENERATED_BODY()
public:
    AAttractionSimulation();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, Category="Board") bool bUseDemo = true;
    UPROPERTY(EditAnywhere, Category="Board") int32 Seed = 48;
    UPROPERTY(EditAnywhere, Category="Board") TArray<FAttractionNode> InitialNodes;
    UPROPERTY(EditAnywhere, Category="Board") TArray<FAttractionBondRule> BondRules;
    UPROPERTY(EditAnywhere, Category="Forces") FVector ChannelRow0 = FVector(1,0,0);
    UPROPERTY(EditAnywhere, Category="Forces") FVector ChannelRow1 = FVector(0,1,0);
    UPROPERTY(EditAnywhere, Category="Forces") FVector ChannelRow2 = FVector(0,0,1);
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="0")) float FieldStrength = 800000.f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="1")) float Softening = 80.f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="0")) float Drag = 0.35f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="1")) float PullRange = 240.f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="0")) float CushionStrength = 300.f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="0")) float Viscosity = 2.f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="1")) float MaxAcceleration = 3000.f;
    UPROPERTY(EditAnywhere, Category="Forces", meta=(ClampMin="1")) float MaxSpeed = 1000.f;
    UPROPERTY(EditAnywhere, Category="Simulation") bool bRunning = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Simulation") TArray<FAttractionNode> Nodes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Simulation") TArray<FAttractionTether> Tethers;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Simulation") TArray<FAttractionTriangle> Triangles;
    static constexpr int32 MaxSimulationNodes = 220;
    static constexpr int32 RewardGridSize = 64;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reward") TArray<float> RewardGrid;
    UPROPERTY(EditAnywhere, Category="Agents", meta=(ClampMin="100")) float EdgeRestLength = 240.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Agents") float BuildProgress = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Agents") int32 AgentNode = INDEX_NONE;
    UFUNCTION(BlueprintCallable) void PaintReward(FVector2D Center);
    UFUNCTION(BlueprintCallable) void ClearReward();
    UFUNCTION(BlueprintCallable) float SampleReward(FVector2D Position) const;
    UFUNCTION(BlueprintCallable) int32 PickLatticeNode(FVector Position) const;
    UFUNCTION(BlueprintCallable) int32 PickLatticeComponent(FVector Position) const;
    UFUNCTION(BlueprintCallable) bool BeginPatchDrag(int32 NodeIndex);
    UFUNCTION(BlueprintCallable) void UpdatePatchDrag(FVector CursorDelta, float DeltaSeconds);
    UFUNCTION(BlueprintCallable) bool EndPatchDrag(bool bTryGraft);
    UFUNCTION(BlueprintPure) FVector GetBuilderPosition() const { return AgentPosition(); }
    // The same geometric/field query is used by selection and commit-time validation.
    UFUNCTION(BlueprintCallable) bool EvaluateFrontierCandidate(int32 A, int32 B, FVector& Apex, int32& Weld, float& Score) const;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Simulation") void ResetSimulation();
    // Severed pairs remain suppressed until reset, so a cut cannot instantly reattach.
    UFUNCTION(BlueprintCallable, Category="Simulation") bool SliceTether(int32 TetherIndex);
    UFUNCTION(BlueprintCallable, Category="Simulation") int32 DropNode(FAttractionNode Profile, FVector LocalPosition);
    UFUNCTION(BlueprintCallable, Category="Simulation") static FAttractionNode MakeArchetype(EAttractionArchetype Archetype);


    // Shared fixed-step entry point also supports lightweight commandlet diagnostics.
    UFUNCTION(BlueprintCallable, Category="Simulation") void AdvanceSimulation(float DeltaSeconds);
    bool IsInsidePlayfield(const FVector& Position) const;
    UPROPERTY(EditAnywhere, Category="Playfield", meta=(ClampMin="400")) float PlayfieldHalfExtentX = 4400.f;
    UPROPERTY(EditAnywhere, Category="Playfield", meta=(ClampMin="400")) float PlayfieldHalfExtentY = 2600.f;
    UPROPERTY(EditAnywhere, Category="Playfield", meta=(ClampMin="50")) float BoundarySoftness = 350.f;
    UPROPERTY(VisibleAnywhere, Category="Liquid") float PlaneHalfExtent = 12000.f;
    UPROPERTY(EditAnywhere, Category="Liquid", meta=(ClampMin="20")) float DropletRadius = 115.f;
    UPROPERTY(EditAnywhere, Category="Liquid", meta=(ClampMin="10")) float FilamentRadius = 55.f;
    UPROPERTY(EditAnywhere, Category="Liquid", meta=(ClampMin="0.01")) float IsoThreshold = 0.18f;
    UPROPERTY(EditAnywhere, Category="Liquid", meta=(ClampMin="0.1")) float SurfaceHeight = 100.f;
    UPROPERTY(EditAnywhere, Category="Stages") TArray<FAttractionStageConfig> Stages;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stages") int32 StageIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reward") bool bRewardActive = false;
    UPROPERTY(EditAnywhere, Category="Agents") float TargetThreshold = 0.25f;
    UFUNCTION(BlueprintCallable) float SampleTargetHeight(FVector2D Pos) const;
    UFUNCTION(BlueprintCallable) void NextStage();
    UFUNCTION(BlueprintCallable) void RefreshConnectivity();
    UFUNCTION(BlueprintCallable) float TargetCoverage() const;
    UFUNCTION(BlueprintCallable) int32 ExcessNodes() const;
    UFUNCTION(BlueprintCallable) int32 ConnectedAgents() const;
private:
    struct FBoundaryEdge { int32 A, B, Opposite; };
    TArray<FBoundaryEdge> BoundaryEdges() const;
    TArray<FBoundaryEdge> CarrierBoundaryEdges() const;
    TArray<int32> ComponentNodes(int32 NodeIndex) const;
    TArray<int32> BoundaryVertices() const;
    void RepairAgentCarrier(const FVector& PreviousPosition);
    void RemoveCutOrphans(int32 CutA, int32 CutB, const FVector& PreviousCarrier);
    bool GraftComponent(const TArray<int32>& Patch);
    bool IsGraftGeometryValid(const TArray<FAttractionNode>& ProposedNodes,
        const TArray<FAttractionTether>& ProposedTethers, const TArray<FAttractionTriangle>& ProposedTriangles) const;
    TArray<int32> DraggedPatch;
    TArray<FVector> PatchStartPositions;
    FVector PatchTargetOffset = FVector::ZeroVector;
    FVector PatchCurrentOffset = FVector::ZeroVector;
    int32 EdgeIncidence(int32 A, int32 B) const;
    bool HasTether(int32 A, int32 B) const;
    void DecayReward(float Dt);
    void CancelBuild();
    FVector AgentPosition() const;
    TArray<int32> AgentPath;
    int32 PathIndex = 0;
    int32 PreviousPatrolNode = INDEX_NONE;
    float TravelProgress = 0;
    int32 BuildA = INDEX_NONE, BuildB = INDEX_NONE, BuildWeld = INDEX_NONE;
    FVector BuildApex = FVector::ZeroVector;
    bool bBuilding = false;
    bool AddStructuralTether(int32 A, int32 B);
    void WorkAgents(float Dt);
    UPROPERTY(VisibleAnywhere, Category="Liquid") TObjectPtr<class UStaticMeshComponent> LiquidPlane;
    UPROPERTY() TSoftObjectPtr<class UMaterialInterface> LiquidMaterialAsset;
    UPROPERTY(Transient) TObjectPtr<class UTexture2D> LiquidDataTexture;
    UPROPERTY(Transient) TObjectPtr<class UTexture2D> RewardTexture;
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> LiquidMID;
    struct FPinchingFilament
    {
        FVector A, B;
        float Radius = 0;
        float Amplitude = 0;
        float Age = 0;
    };
    // Presentation only: severed springs stop applying force immediately.
    TArray<FPinchingFilament> PinchingFilaments;
    TSet<uint64> SuppressedPairs;
    double Accumulator = 0;
    void BuildVisuals();
    void UpdateLiquidSurface();
    void Step(float Dt);
    static uint64 PairKey(int32 A, int32 B);
};
