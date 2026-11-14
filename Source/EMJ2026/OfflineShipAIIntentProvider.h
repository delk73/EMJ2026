#pragma once
#include "DirectOrScutSimSubsystem.h"

class FOfflineShipAIIntentProvider final : public IShipAIIntentProvider
{
  public:
    FShipAIIntent Translate(const FString &Input, const FSimContextSnapshot &Context) override;
};
