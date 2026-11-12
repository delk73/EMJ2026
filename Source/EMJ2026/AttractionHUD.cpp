#include "AttractionHUD.h"
#include "AttractionSimulation.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
AAttractionHUD::AAttractionHUD() {}
void AAttractionHUD::BeginPlay(){Super::BeginPlay();}
float AAttractionHUD::TrayWidth(int32 Width,int32 Height){return 0;}
FBox2D AAttractionHUD::TileRect(int32 Index,int32 Width,int32 Height){return FBox2D(FVector2D::ZeroVector,FVector2D::ZeroVector);}
int32 AAttractionHUD::HitTray(FVector2D Point,int32 Width,int32 Height){return INDEX_NONE;}
void AAttractionHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas)return;
    for(TActorIterator<AAttractionSimulation> It(GetWorld());It;++It)
    {
        const float Coverage=It->TargetCoverage();const int32 Excess=It->ExcessNodes();
        const FString Status=FString::Printf(TEXT("Target Coverage %.0f%%   Excess Nodes %d\nReward %s   Builder %d   Stage %d\nLMB: paint (7s fade)   RMB: cut   Space: clear paint   N: next   R: reset"),Coverage,Excess,It->bRewardActive?TEXT("ON"):TEXT("OFF"),It->ConnectedAgents(),It->StageIndex+1);
        DrawText(Status,FLinearColor(0.85f,0.85f,0.85f),18,16,GEngine->GetSmallFont());
        if(Coverage>=85&&Excess<=2)DrawText(TEXT("Pattern Clean"),FLinearColor(0.65f,0.65f,0.65f),18,76,GEngine->GetSmallFont());
        break;
    }
}
