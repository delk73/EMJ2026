#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DirectOrScutBakeCommandlet.generated.h"
UCLASS()
class UDirectOrScutBakeCommandlet : public UCommandlet
{
    GENERATED_BODY()
  public:
    UDirectOrScutBakeCommandlet();
    virtual int32 Main(const FString &Params) override;
};
