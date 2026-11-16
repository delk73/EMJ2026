#pragma once
#include "CoreMinimal.h"
#include "Components/AudioComponent.h"
#include "DirectOrScutAudioComponent.generated.h"
class USoundWaveProcedural;
UCLASS()
class EMJ2026_API UDirectOrScutAudioComponent : public UAudioComponent
{
    GENERATED_BODY()
  public:
    UDirectOrScutAudioComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction *ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void InitializeSynth();
    void Synthesize(int32 Kind);
    void Silence();
    UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> Wave;

  private:
    uint32 NoiseSeed = 1970;
    double LastPresentationCue = -10;
    double AmbienceTime = 0;
    float AirFilter = 0;
};
