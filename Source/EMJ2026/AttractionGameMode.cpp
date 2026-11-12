#include "AttractionGameMode.h"
#include "AttractionHUD.h"
#include "AttractionPlayerController.h"

AAttractionGameMode::AAttractionGameMode()
{
    PlayerControllerClass = AAttractionPlayerController::StaticClass();
    HUDClass = AAttractionHUD::StaticClass();
    DefaultPawnClass = nullptr;
}
