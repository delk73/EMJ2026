#pragma once
#include "DirectOrScutSimSubsystem.h"
#include "Interfaces/IHttpRequest.h"

class FOfflineShipAIIntentProvider final : public IShipAIIntentProvider
{
  public:
    void Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete) override;
    static FShipAIDirective Resolve(const FShipAIRequest &Request);
};
// Optional loopback adapter. No SDK, embedded inference or model weights.
class FLocalHttpShipAIIntentProvider final : public IShipAIIntentProvider
{
  public:
    FLocalHttpShipAIIntentProvider(FString InEndpoint, float InTimeout);
    ~FLocalHttpShipAIIntentProvider() override;
    void Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete) override;
    void CancelAll() override;
    static bool IsLocalEndpoint(const FString &Endpoint);
    static bool ParseResponse(const FString &Json, FShipAIDirective &Directive);

  private:
    FString Endpoint;
    float Timeout;
    TArray<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> Requests;
};
