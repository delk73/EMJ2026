#pragma once
#include "Framework/Application/IInputProcessor.h"
class SDirectOrScutWorkstationWidget;
class FScutTacticalInput final : public IInputProcessor
{
  public:
    TWeakPtr<SDirectOrScutWorkstationWidget> Owner;
    void Tick(float DeltaTime, FSlateApplication &Application, TSharedRef<ICursor> Cursor) override;
    bool HandleKeyDownEvent(FSlateApplication &Application, const FKeyEvent &Event) override;
};
