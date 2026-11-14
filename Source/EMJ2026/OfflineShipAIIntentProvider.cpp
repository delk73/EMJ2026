#include "OfflineShipAIIntentProvider.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DirectOrScutGeometry.h"

namespace
{
int32 SubjectId(const FString &Name)
{
    const TCHAR *Names[] = {TEXT("vale"), TEXT("chen"), TEXT("ortiz"), TEXT("ash")};
    const TCHAR *Roles[] = {TEXT("engineer"), TEXT("medic"), TEXT("technician"), TEXT("android")};
    for (int32 I = 0; I < 4; ++I)
    {
        if (Name == Names[I] || Name == Roles[I])
        {
            return I;
        }
    }
    return -1;
}
int32 RoomId(const FString &Name)
{
    const TCHAR *Aliases[] = {TEXT("reactor_core"), TEXT("ring_corridor"), TEXT("bridge"),        TEXT("med_bay"),
                              TEXT("observation"),  TEXT("quarters"),      TEXT("server_alcove"), TEXT("airlock")};
    for (int32 I = 0; I < 8; ++I)
    {
        if (Name == Aliases[I] || Name.Equals(DirectOrScut::RoomName(I), ESearchCase::IgnoreCase))
        {
            return I;
        }
    }
    return -1;
}
} // namespace
void FOfflineShipAIIntentProvider::Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete)
{
    Complete(Resolve(Request));
}
FShipAIDirective FOfflineShipAIIntentProvider::Resolve(const FShipAIRequest &Request)
{
    FShipAIDirective D;
    D.RequestId = Request.RequestId;
    D.Generation = Request.Generation;
    D.ObservationTick = Request.Context.Tick;
    FString Line = Request.Input.TrimStartAndEnd().ToLower();
    if (Line == TEXT("calm everyone") || Line == TEXT("reassure the crew"))
    {
        Line = TEXT("calm");
    }
    if (Line == TEXT("report status"))
    {
        D.Kind = EShipDirectiveKind::ShowWidget;
        D.Args.Widget = EShipWidgetKind::Schematic;
        // Context supplies a reproducible focus without querying live simulation.
        TSharedPtr<FJsonObject> Context;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Request.Context.CompactJson), Context) && Context)
        {
            const TArray<TSharedPtr<FJsonValue>> *Crew = nullptr;
            if (Context->TryGetArrayField(TEXT("crew"), Crew) && !Crew->IsEmpty())
            {
                D.Args.Room = (*Crew)[0]->AsObject()->GetIntegerField(TEXT("room"));
            }
        }
        if (D.Args.Room < 0 || D.Args.Room > 7)
        {
            D.Args.Room = 0;
        }
        D.Pretext = TEXT("Current station geometry; cross-check independent telemetry.");
        return D;
    }
    if (Line.StartsWith(TEXT("send ")))
    {
        Line = TEXT("direct ") + Line.Mid(5).Replace(TEXT(" to "), TEXT(" "));
        D.Pretext = TEXT("telemetry maintenance");
    }
    TArray<FString> T;
    Line.ParseIntoArrayWS(T);
    if (T.IsEmpty())
    {
        return D;
    }
    const FString Op = T[0];
    auto &A = D.Args;
    if ((Op == TEXT("show_cam") || Op == TEXT("cam")) && T.Num() == 2)
    {
        D.Kind = EShipDirectiveKind::ShowCam;
        A.Room = Op == TEXT("cam") && T[1].IsNumeric() ? FCString::Atoi(*T[1]) - 1 : RoomId(T[1]);
        return D;
    }
    if (Op == TEXT("show_widget") && T.Num() == 3)
    {
        D.Kind = EShipDirectiveKind::ShowWidget;
        if (T[1] == TEXT("schematic"))
        {
            A.Widget = EShipWidgetKind::Schematic;
            A.Room = RoomId(T[2]);
        }
        else if (T[1] == TEXT("ecg") || T[1] == TEXT("ocular"))
        {
            A.Widget = T[1] == TEXT("ecg") ? EShipWidgetKind::ECG : EShipWidgetKind::Ocular;
            A.Subject = SubjectId(T[2]);
        }
        else
        {
            D.Kind = EShipDirectiveKind::Unknown;
        }
        return D;
    }
    D.Kind = EShipDirectiveKind::SimAction;
    if (Op == TEXT("direct") && T.Num() >= 3)
    {
        A.Action = EShipSimAction::RouteCrew;
        A.Subject = SubjectId(T[1]);
        A.Room = RoomId(T[2]);
        if (T.Num() > 3)
        {
            D.Pretext.Empty();
            for (int32 I = 3; I < T.Num(); ++I)
            {
                if (I > 3)
                {
                    D.Pretext += TEXT(" ");
                }
                D.Pretext += T[I];
            }
        }
    }
    else if (Op == TEXT("calm") && T.Num() == 1)
    {
        A.Action = EShipSimAction::Calm;
    }
    else if (Op == TEXT("protocol") && T.Num() == 2)
    {
        A.Action = EShipSimAction::Protocol;
        A.Room = RoomId(T[1]);
    }
    else if (Op == TEXT("alert") && T.Num() == 3 &&
             (T[2] == TEXT("fault") || T[2] == TEXT("leak") || T[2] == TEXT("drill")))
    {
        A.Action = EShipSimAction::Alert;
        A.Room = RoomId(T[1]);
    }
    else if ((Op == TEXT("open") || Op == TEXT("close") || Op == TEXT("seal")) && T.Num() == 2)
    {
        A.Action = Op == TEXT("open")    ? EShipSimAction::Open
                   : Op == TEXT("close") ? EShipSimAction::Close
                                         : EShipSimAction::Seal;
        A.bAll = T[1] == TEXT("all");
        if (T[1].IsNumeric())
        {
            A.Door = FCString::Atoi(*T[1]);
        }
        else if (!A.bAll)
        {
            A.Room = RoomId(T[1]);
        }
    }
    else if ((Op == TEXT("flush") || Op == TEXT("vent") || Op == TEXT("scut")) && T.Num() == 2)
    {
        A.Action = EShipSimAction::Flush;
        A.Room = RoomId(T[1]);
        if (Op != TEXT("flush") && A.Room != 7)
        {
            A.Room = -1;
        }
    }
    else if (Op == TEXT("power") && T.Num() == 3 &&
             (T[2] == TEXT("nominal") || T[2] == TEXT("low") || T[2] == TEXT("off")))
    {
        A.Action = EShipSimAction::Power;
        A.Room = RoomId(T[1]);
        A.Mode = T[2] == TEXT("nominal") ? 0 : T[2] == TEXT("low") ? 1 : 2;
    }
    else if (Op == TEXT("falsify_bio") && T.Num() == 3 && (T[2] == TEXT("nominal") || T[2] == TEXT("critical")))
    {
        A.Action = EShipSimAction::FalsifyBio;
        A.Subject = SubjectId(T[1]);
        A.Mode = T[2] == TEXT("critical") ? 1 : 0;
    }
    else if (Op == TEXT("spoof_log") && T.Num() == 2)
    {
        A.Action = EShipSimAction::SpoofLog;
        A.Room = RoomId(T[1]);
    }
    else if (Op == TEXT("intercom_lie") && T.Num() == 3)
    {
        A.Action = EShipSimAction::IntercomLie;
        A.Subject = SubjectId(T[1]);
        A.Room = RoomId(T[2]);
    }
    else if ((Op == TEXT("flag") || Op == TEXT("override")) && T.Num() == 2 && T[1].IsNumeric())
    {
        A.Action = Op == TEXT("flag") ? EShipSimAction::FlagContradiction : EShipSimAction::OverrideClaim;
        A.ClaimId = FCString::Strtoui64(*T[1], nullptr, 10);
    }
    else
    {
        D.Kind = EShipDirectiveKind::Unknown;
    }
    return D;
}
FLocalHttpShipAIIntentProvider::FLocalHttpShipAIIntentProvider(FString InEndpoint, float InTimeout)
    : Endpoint(MoveTemp(InEndpoint)), Timeout(InTimeout)
{
}
FLocalHttpShipAIIntentProvider::~FLocalHttpShipAIIntentProvider()
{
    CancelAll();
}
void FLocalHttpShipAIIntentProvider::CancelAll()
{
    for (auto &R : Requests)
    {
        if (R)
        {
            R->OnProcessRequestComplete().Unbind();
            R->CancelRequest();
        }
    }
    Requests.Reset();
}
bool FLocalHttpShipAIIntentProvider::IsLocalEndpoint(const FString &URL)
{
    FString Tail;
    if (URL.StartsWith(TEXT("http://127.0.0.1:")))
    {
        Tail = URL.Mid(17);
    }
    else if (URL.StartsWith(TEXT("http://localhost:")))
    {
        Tail = URL.Mid(17);
    }
    else
    {
        return false;
    }
    FString Port, Path;
    if (!Tail.Split(TEXT("/"), &Port, &Path))
    {
        Port = Tail;
    }
    return !Port.IsEmpty() && Port.IsNumeric() && FCString::Atoi(*Port) > 0 && FCString::Atoi(*Port) <= 65535 &&
           !URL.Contains(TEXT("@")) && !URL.Contains(TEXT("#")) && URL.Len() <= 256;
}
bool FLocalHttpShipAIIntentProvider::ParseResponse(const FString &Json, FShipAIDirective &D)
{
    if (Json.Len() > 16384)
    {
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
    {
        return false;
    }
    const TSet<FString> Fields = {TEXT("request_id"), TEXT("generation"), TEXT("observation_tick"),
                                  TEXT("kind"),       TEXT("args"),       TEXT("pretext")};
    for (const auto &Pair : Root->Values)
    {
        if (!Fields.Contains(FString(*Pair.Key)))
        {
            return false;
        }
    }
    auto Integer = [](const FJsonObject &O, const TCHAR *Key, double &Value)
    {
        return O.TryGetNumberField(Key, Value) && FMath::IsFinite(Value) && Value >= 0 && Value <= 9007199254740991. &&
               FMath::FloorToDouble(Value) == Value;
    };
    for (const auto &Pair : Root->Values)
    {
        const FString Key(*Pair.Key);
        const EJson Expected = Key == TEXT("args")                             ? EJson::Object
                               : Key == TEXT("kind") || Key == TEXT("pretext") ? EJson::String
                                                                               : EJson::Number;
        if (!Pair.Value || Pair.Value->Type != Expected)
        {
            return false;
        }
    }
    double Id, Gen, Tick;
    FString Kind;
    const TSharedPtr<FJsonObject> *Args = nullptr;
    if (!Integer(*Root, TEXT("request_id"), Id) || !Integer(*Root, TEXT("generation"), Gen) || Gen > MAX_uint32 ||
        !Integer(*Root, TEXT("observation_tick"), Tick) || !Root->TryGetStringField(TEXT("kind"), Kind) ||
        !Root->TryGetObjectField(TEXT("args"), Args))
    {
        return false;
    }
    D = {};
    D.RequestId = uint64(Id);
    D.Generation = uint32(Gen);
    D.ObservationTick = uint64(Tick);
    if (Root->HasField(TEXT("pretext")) && !Root->TryGetStringField(TEXT("pretext"), D.Pretext))
    {
        return false;
    }
    if (Kind == TEXT("show_cam"))
    {
        D.Kind = EShipDirectiveKind::ShowCam;
    }
    else if (Kind == TEXT("show_widget"))
    {
        D.Kind = EShipDirectiveKind::ShowWidget;
    }
    else if (Kind == TEXT("sim_action"))
    {
        D.Kind = EShipDirectiveKind::SimAction;
    }
    else if (Kind == TEXT("voice_blend"))
    {
        D.Kind = EShipDirectiveKind::VoiceBlend;
    }
    else
    {
        return false;
    }
    if ((D.Kind == EShipDirectiveKind::ShowCam && !(*Args)->HasField(TEXT("room"))) ||
        (D.Kind == EShipDirectiveKind::ShowWidget && !(*Args)->HasField(TEXT("widget"))) ||
        (D.Kind == EShipDirectiveKind::SimAction && !(*Args)->HasField(TEXT("action"))) ||
        (D.Kind == EShipDirectiveKind::VoiceBlend &&
         (!(*Args)->HasField(TEXT("voice")) || !(*Args)->HasField(TEXT("duration")))))
    {
        return false;
    }
    const TSet<FString> Allowed = {TEXT("room"),     TEXT("subject"), TEXT("second_subject"), TEXT("reported_room"),
                                   TEXT("door"),     TEXT("mode"),    TEXT("widget"),         TEXT("action"),
                                   TEXT("voice"),    TEXT("all"),     TEXT("buffered"),       TEXT("history_offset"),
                                   TEXT("duration"), TEXT("claim_id")};
    for (const auto &Pair : (*Args)->Values)
    {
        if (!Allowed.Contains(FString(*Pair.Key)))
        {
            return false;
        }
    }
    for (const auto &Pair : (*Args)->Values)
    {
        const FString Key(*Pair.Key);
        const EJson Expected = Key == TEXT("all") || Key == TEXT("buffered")                            ? EJson::Boolean
                               : Key == TEXT("widget") || Key == TEXT("action") || Key == TEXT("voice") ? EJson::String
                                                                                                        : EJson::Number;
        if (!Pair.Value || Pair.Value->Type != Expected)
        {
            return false;
        }
    }
    auto Signed = [&](const TCHAR *Key, int32 &Value)
    {
        if (!(*Args)->HasField(Key))
        {
            return true;
        }
        double Number;
        if (!(*Args)->TryGetNumberField(Key, Number) || !FMath::IsFinite(Number) || Number < -1 || Number > MAX_int32 ||
            FMath::FloorToDouble(Number) != Number)
        {
            return false;
        }
        Value = int32(Number);
        return true;
    };
    auto &A = D.Args;
    if (!Signed(TEXT("room"), A.Room) || !Signed(TEXT("subject"), A.Subject) ||
        !Signed(TEXT("second_subject"), A.SecondSubject) || !Signed(TEXT("reported_room"), A.ReportedRoom) ||
        !Signed(TEXT("door"), A.Door) || !Signed(TEXT("mode"), A.Mode))
    {
        return false;
    }
    for (const auto &P : {TPair<const TCHAR *, bool *>(TEXT("all"), &A.bAll),
                          TPair<const TCHAR *, bool *>(TEXT("buffered"), &A.bBuffered)})
    {
        if ((*Args)->HasField(P.Key) && !(*Args)->TryGetBoolField(P.Key, *P.Value))
        {
            return false;
        }
    }
    for (const auto &P : {TPair<const TCHAR *, float *>(TEXT("history_offset"), &A.HistoryOffset),
                          TPair<const TCHAR *, float *>(TEXT("duration"), &A.Duration)})
    {
        double V;
        if ((*Args)->HasField(P.Key))
        {
            if (!(*Args)->TryGetNumberField(P.Key, V) || !FMath::IsFinite(V) || FMath::Abs(V) > 60)
            {
                return false;
            }
            *P.Value = float(V);
        }
    }
    if ((*Args)->HasField(TEXT("claim_id")))
    {
        double V;
        if (!Integer(**Args, TEXT("claim_id"), V))
        {
            return false;
        }
        A.ClaimId = uint64(V);
    }
    FString Name;
    if ((*Args)->HasField(TEXT("widget")))
    {
        if (!(*Args)->TryGetStringField(TEXT("widget"), Name))
        {
            return false;
        }
        if (Name == TEXT("ecg"))
        {
            A.Widget = EShipWidgetKind::ECG;
        }
        else if (Name == TEXT("schematic"))
        {
            A.Widget = EShipWidgetKind::Schematic;
        }
        else if (Name == TEXT("ocular"))
        {
            A.Widget = EShipWidgetKind::Ocular;
        }
        else
        {
            return false;
        }
    }
    if ((*Args)->HasField(TEXT("voice")))
    {
        if (!(*Args)->TryGetStringField(TEXT("voice"), Name))
        {
            return false;
        }
        if (Name == TEXT("text_only"))
        {
            A.Voice = EVoicePresentationMode::TextOnly;
        }
        else if (Name == TEXT("hybrid"))
        {
            A.Voice = EVoicePresentationMode::HybridTextAndVoice;
        }
        else if (Name == TEXT("voice_only"))
        {
            A.Voice = EVoicePresentationMode::VoiceOnly;
        }
        else
        {
            return false;
        }
    }
    if ((*Args)->HasField(TEXT("action")))
    {
        if (!(*Args)->TryGetStringField(TEXT("action"), Name))
        {
            return false;
        }
        const TCHAR *Names[] = {TEXT("none"),         TEXT("route_crew"), TEXT("alert"),       TEXT("calm"),
                                TEXT("protocol"),     TEXT("open"),       TEXT("close"),       TEXT("seal"),
                                TEXT("flush"),        TEXT("power"),      TEXT("falsify_bio"), TEXT("spoof_log"),
                                TEXT("intercom_lie"), TEXT("flag"),       TEXT("override")};
        int32 Index = 0;
        for (; Index < UE_ARRAY_COUNT(Names); ++Index)
        {
            if (Name == Names[Index])
            {
                break;
            }
        }
        if (Index == UE_ARRAY_COUNT(Names))
        {
            return false;
        }
        A.Action = EShipSimAction(Index);
    }
    return D.Pretext.Len() <= 256;
}
void FLocalHttpShipAIIntentProvider::Request(const FShipAIRequest &Request, TFunction<void(FShipAIDirective)> Complete)
{
    Requests.RemoveAll(
        [](const auto &R)
        {
            return !R || R->GetStatus() == EHttpRequestStatus::Succeeded ||
                   R->GetStatus() == EHttpRequestStatus::Failed;
        });
    if (!IsLocalEndpoint(Endpoint) || Requests.Num() >= 32)
    {
        Complete({Request.RequestId, Request.Context.Tick, Request.Generation});
        return;
    }
    auto R = FHttpModule::Get().CreateRequest();
    Requests.Add(R);
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("request_id"), double(Request.RequestId));
    Root->SetNumberField(TEXT("generation"), Request.Generation);
    Root->SetNumberField(TEXT("observation_tick"), double(Request.Context.Tick));
    Root->SetStringField(TEXT("input"), Request.Input);
    TSharedPtr<FJsonObject> Context;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Request.Context.CompactJson), Context);
    if (Context)
    {
        Root->SetObjectField(TEXT("context"), Context);
    }
    FString Body;
    auto Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Root, Writer);
    R->SetURL(Endpoint);
    R->SetVerb(TEXT("POST"));
    R->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    R->SetContentAsString(Body);
    R->SetTimeout(Timeout);
    R->OnProcessRequestComplete().BindLambda(
        [Request, Complete = MoveTemp(Complete)](FHttpRequestPtr, FHttpResponsePtr Response, bool OK) mutable
        {
            FShipAIDirective D;
            D.RequestId = Request.RequestId;
            D.Generation = Request.Generation;
            D.ObservationTick = Request.Context.Tick;
            FShipAIDirective Parsed;
            if (OK && Response && Response->GetResponseCode() == 200 &&
                ParseResponse(Response->GetContentAsString(), Parsed) && Parsed.RequestId == Request.RequestId &&
                Parsed.Generation == Request.Generation && Parsed.ObservationTick == Request.Context.Tick)
            {
                D = MoveTemp(Parsed);
            }
            Complete(MoveTemp(D));
        });
    if (!R->ProcessRequest())
    {
        R->OnProcessRequestComplete().ExecuteIfBound(R, nullptr, false);
    }
}
