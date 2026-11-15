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
    FLocalHttpShipAIIntentProvider(FString InEndpoint, float InTimeout, FString InModel = TEXT(""));
    ~FLocalHttpShipAIIntentProvider() override;
    void Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete) override;
    void CancelAll() override;
    static bool IsLocalEndpoint(const FString &Endpoint);
    static bool ParseResponse(const FString &Json, FShipAIDirective &Directive);
    static bool ParseChatResponse(const FString &Json, const FShipAIRequest &Request, FShipAIDirective &Directive);
    static FString BuildChatRequest(const FShipAIRequest &Request, const FString &Model);

  private:
    FString Endpoint, Model;
    float Timeout;
    TArray<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> Requests;
};
