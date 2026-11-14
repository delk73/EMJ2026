#include "DirectOrScutAudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
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
}
void UDirectOrScutAudioComponent::Silence()
{
    Stop();
    NoiseSeed = 1970;
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
    // Bound queued latency; headless execution never allocates a renderer.
    if (Wave->GetAvailableAudioByteCount() > 22050)
    {
        return;
    }
    const float Duration = Kind == 1 ? .28f : Kind == 3 ? .32f : Kind == 6 ? .25f : .09f;
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
        default:
            Sample = Noise * FMath::Exp(-T * 180);
            break;
        }
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
