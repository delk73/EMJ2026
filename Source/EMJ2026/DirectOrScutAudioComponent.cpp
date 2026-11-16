#include "DirectOrScutAudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
UDirectOrScutAudioComponent::UDirectOrScutAudioComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}
void UDirectOrScutAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                FActorComponentTickFunction *ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Wave || Wave->GetAvailableAudioByteCount() > 4410)
    {
        return;
    }
    // Low monitor-room ventilation; continuous sample phase prevents loop seams.
    TArray<int16> PCM;
    PCM.SetNumUninitialized(2646);
    for (int16 &Value : PCM)
    {
        NoiseSeed = 1664525u * NoiseSeed + 1013904223u;
        const float Noise = float((NoiseSeed >> 16) & 65535) / 32767.5f - 1;
        AirFilter += .018f * (Noise - AirFilter);
        const double T = AmbienceTime;
        const float Motor = FMath::Sin(2 * PI * 47 * T) + .3f * FMath::Sin(2 * PI * 94.2 * T);
        const float Rattle = FMath::Pow(FMath::Max(0.f, float(FMath::Sin(T * .73))), 24.f) * Noise;
        Value = int16((.012f * Motor + .065f * AirFilter + .006f * Rattle) * 32767);
        AmbienceTime += 1.0 / 22050;
    }
    Wave->QueueAudio(reinterpret_cast<const uint8 *>(PCM.GetData()), PCM.Num() * sizeof(int16));
    if (!IsPlaying())
    {
        Play();
    }
}
void UDirectOrScutAudioComponent::InitializeSynth()
{
    if (Wave)
    {
        return;
    }
    Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(22050);
    Wave->NumChannels = 1;
    Wave->Duration = INDEFINITELY_LOOPING_DURATION;
    Wave->bLooping = false;
    bAutoDestroy = false;
    SetSound(Wave);
    SetVolumeMultiplier(.35f);
    SetComponentTickEnabled(true);
}
void UDirectOrScutAudioComponent::Silence()
{
    SetComponentTickEnabled(false);
    Stop();
    NoiseSeed = 1970;
    LastPresentationCue = -10;
    if (Wave)
    {
        Wave->ResetAudio();
    }
}
void UDirectOrScutAudioComponent::Synthesize(int32 Kind)
{
    if (!Wave)
    {
        return;
    }
    SetComponentTickEnabled(true);
    // Bound queued latency; headless execution never allocates a renderer.
    if (Wave->GetAvailableAudioByteCount() > 22050)
    {
        return;
    }
    // Presentation bursts are coalesced on wall time, independent of model stepping.
    if (Kind >= 8)
    {
        const double Now = FPlatformTime::Seconds();
        if (Kind != 11 && Now - LastPresentationCue < .3)
        {
            return;
        }
        LastPresentationCue = Now;
    }
    const float Duration = Kind == 11   ? .48f
                           : Kind == 10 ? .22f
                           : Kind == 9  ? .18f
                           : Kind == 8  ? .045f
                           : Kind == 1  ? .28f
                           : Kind == 3  ? .32f
                           : Kind == 6  ? .25f
                                        : .09f;
    const int32 Count = int32(Duration * 22050);
    TArray<int16> PCM;
    PCM.SetNumUninitialized(Count);
    float Filter = 0;
    for (int32 I = 0; I < Count; ++I)
    {
        const float T = I / 22050.f, Envelope = FMath::Exp(-T * (Kind == 1 ? 12.f : 30.f));
        NoiseSeed = 1664525u * NoiseSeed + 1013904223u;
        const float Noise = float((NoiseSeed >> 16) & 65535) / 32767.5f - 1;
        Filter += .12f * (Noise - Filter);
        float Sample = 0;
        switch (Kind)
        {
        case 0:
            Sample = FMath::Sin(2 * PI * 55 * T) * Envelope;
            break;
        case 1:
            Sample = Filter * FMath::Sin(PI * T / Duration);
            break;
        case 2:
            Sample = FMath::Sin(2 * PI * 1600 * T) * Envelope;
            break;
        case 3:
            Sample = (FMath::Sin(2 * PI * 123 * T) + .5f * FMath::Sin(2 * PI * 317 * T) + Filter) * Envelope;
            break;
        case 4:
            Sample = FMath::Sin(2 * PI * 60 * T) * Envelope;
            break;
        case 5:
            Sample = Noise * Envelope;
            break;
        case 6:
            Sample = FMath::Sin(2 * PI * 880 * T) * Envelope;
            break;
        case 8: // Switch receipt: short, muted contact.
            Sample = .28f * FMath::Sin(2 * PI * 640 * T) * Envelope;
            break;
        case 9: // Incoming communication: paired descending carrier tones.
            Sample =
                .3f * FMath::Sin(2 * PI * (T < .085f ? 740 : 554) * T) * FMath::Sin(PI * FMath::Fmod(T, .09f) / .09f);
            break;
        case 10: // Independent instrument or record returned.
            Sample =
                .28f * FMath::Sin(2 * PI * (T < .11f ? 554 : 830) * T) * FMath::Sin(PI * FMath::Fmod(T, .11f) / .11f);
            break;
        case 11: // Filed report: settled two-note cadence.
            Sample = .35f * FMath::Sin(2 * PI * (T < .18f ? 554 : 370) * T) * FMath::Sin(PI * T / Duration);
            break;
        default:
            Sample = Noise * FMath::Exp(-T * 180);
            break;
        }
        Sample *= FMath::Min(T / .004f, 1.f) * FMath::Min((Duration - T) / .008f, 1.f);
        PCM[I] = int16(FMath::Clamp(Sample * .3f, -1.f, 1.f) * 32767);
    }
    Wave->QueueAudio(reinterpret_cast<const uint8 *>(PCM.GetData()), PCM.Num() * sizeof(int16));
    if (!IsPlaying())
    {
        Play();
    }
}

void UDirectOrScutAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Silence();
    Super::EndPlay(EndPlayReason);
}
