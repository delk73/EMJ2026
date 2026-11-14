#include "SScutCameraTile.h"
#include "SDirectOrScutWorkstationWidget.h"
#include "Styling/CoreStyle.h"

void SScutCameraTile::Construct(const FArguments &Args)
{
    Owner = Args._Owner;
    Feed = Args._Feed;
    SetTag(FName(*FString::Printf(TEXT("ScutCamera%d"), Feed)));
    SBorder::Construct(SBorder::FArguments()
                           .Padding(3)
                           .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                           .BorderBackgroundColor_Lambda(
                               [this]()
                               {
                                   auto W = Owner.Pin();
                                   return W && W->SelectedCameraIndex == Feed - 1 ? FLinearColor(.25f, 1.f, .08f)
                                                                                  : FLinearColor(.025f, .045f, .03f);
                               })[Args._Content.Widget]);
}
FReply SScutCameraTile::OnMouseButtonDown(const FGeometry &, const FPointerEvent &E)
{
    if (E.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }
    if (auto W = Owner.Pin())
    {
        W->SetConsoleOpen(false);
        W->SelectFeed(Feed, false);
        return FReply::Handled().SetUserFocus(W.ToSharedRef(), EFocusCause::Mouse);
    }
    return FReply::Unhandled();
}
FReply SScutCameraTile::OnMouseButtonDoubleClick(const FGeometry &, const FPointerEvent &E)
{
    if (E.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }
    if (auto W = Owner.Pin())
    {
        W->SetConsoleOpen(false);
        W->SelectFeed(Feed, false);
        W->ToggleExpanded();
        return FReply::Handled().SetUserFocus(W.ToSharedRef(), EFocusCause::Mouse);
    }
    return FReply::Unhandled();
}
