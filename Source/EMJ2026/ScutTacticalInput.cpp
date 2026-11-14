#include "ScutTacticalInput.h"
#include "SDirectOrScutWorkstationWidget.h"

void FScutTacticalInput::Tick(float, FSlateApplication &, TSharedRef<ICursor>) {}
bool FScutTacticalInput::HandleKeyDownEvent(FSlateApplication &, const FKeyEvent &E)
{
    auto W = Owner.Pin();
    if (!W || !(W->HasKeyboardFocus() || W->HasFocusedDescendants()))
    {
        return false;
    }
    const auto K = E.GetKey();
    if (K != EKeys::F9 && K != EKeys::F10)
    {
        return false;
    }
    return W->OnPreviewKeyDown(W->GetCachedGeometry(), E).IsEventHandled();
}
