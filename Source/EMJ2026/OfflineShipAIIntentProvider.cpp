#include "OfflineShipAIIntentProvider.h"

FShipAIIntent FOfflineShipAIIntentProvider::Translate(const FString &Input, const FSimContextSnapshot &)
{
    FString Line = Input.TrimStartAndEnd().ToLower();
    if (Line == TEXT("calm everyone") || Line == TEXT("reassure the crew"))
    {
        return {TEXT("calm"), TEXT("")};
    }
    if (Line == TEXT("report status"))
    {
        return {TEXT("status"), TEXT("")};
    }
    if (Line.StartsWith(TEXT("send ")))
    {
        Line = Line.Mid(5).Replace(TEXT(" to "), TEXT(" "));
        return {TEXT("direct ") + Line, TEXT("telemetry maintenance")};
    }
    const TCHAR *Commands[] = {TEXT("direct "), TEXT("alert "), TEXT("protocol "), TEXT("open "),  TEXT("close "),
                               TEXT("seal "),   TEXT("vent "),  TEXT("scut "),     TEXT("flush "), TEXT("cam ")};
    for (const auto *Command : Commands)
    {
        if (Line.StartsWith(Command))
        {
            return {Line, TEXT("")};
        }
    }
    if (Line == TEXT("calm") || Line == TEXT("status") || Line == TEXT("crew") || Line == TEXT("rooms"))
    {
        return {Line, TEXT("")};
    }
    return {};
}
