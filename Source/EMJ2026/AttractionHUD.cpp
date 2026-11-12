#include "AttractionHUD.h"
#include "AttractionPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
float TrayScale(int32 Width,int32 Height)
{
    return FMath::Clamp(FMath::Min(Width/1280.f,Height/720.f),0.25f,2.f);
}
}

AAttractionHUD::AAttractionHUD()
{
    GlyphMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_TrayGlyphSDF.M_TrayGlyphSDF")));
}

void AAttractionHUD::BeginPlay()
{
    Super::BeginPlay();
    if (UMaterialInterface* Parent = GlyphMaterial.LoadSynchronous())
        for (int32 I=0; I<3; ++I)
        {
            auto* MID = UMaterialInstanceDynamic::Create(Parent,this);
            MID->SetScalarParameterValue(TEXT("GlyphIndex"),I);
            Glyphs.Add(MID);
        }
}

float AAttractionHUD::TrayWidth(int32 Width,int32 Height) { return 144*TrayScale(Width,Height); }

FBox2D AAttractionHUD::TileRect(int32 Index,int32 Width,int32 Height)
{
    const float Scale = TrayScale(Width,Height);
    const float Top = (Height-380*Scale)*0.5f;
    const FVector2D Min(20*Scale,Top+Index*138*Scale);
    return FBox2D(Min,Min+FVector2D(104*Scale,104*Scale));
}

int32 AAttractionHUD::HitTray(FVector2D Point,int32 Width,int32 Height)
{
    for (int32 I=0; I<3; ++I) if (TileRect(I,Width,Height).IsInside(Point)) return I;
    return INDEX_NONE;
}

void AAttractionHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* Controller = Cast<AAttractionPlayerController>(PlayerOwner);
    if (!Canvas || !Controller) return;
    const int32 W = Canvas->SizeX, H = Canvas->SizeY;
    const float Scale = TrayScale(W,H), Dock = TrayWidth(W,H);
    float X=0,Y=0;
    const bool HasMouse = Controller->GetMousePosition(X,Y);
    const int32 Hovered = HasMouse ? HitTray(FVector2D(X,Y),W,H) : INDEX_NONE;
    const int32 Dragged = Controller->GetDragArchetypeIndex();
    DrawRect(FLinearColor(0.055f,0.055f,0.055f,0.94f),0,0,Dock,H);
    DrawLine(Dock,0,Dock,H,FLinearColor(0.35f,0.35f,0.35f),1);
    static const TCHAR* Names[] = {TEXT("DRIVER"),TEXT("BUFFER"),TEXT("ANCHOR")};
    for (int32 I=0; I<3; ++I)
    {
        const FBox2D Rect = TileRect(I,W,H);
        Hover[I] = FMath::FInterpTo(Hover[I],Hovered==I ? 1.f : 0.f,GetWorld()->GetDeltaSeconds(),10.f);
        if (Glyphs.IsValidIndex(I))
        {
            Glyphs[I]->SetScalarParameterValue(TEXT("HoverAlpha"),Hover[I]);
            Glyphs[I]->SetScalarParameterValue(TEXT("DragAlpha"),Dragged==I ? 1.f : 0.f);
            DrawMaterialSimple(Glyphs[I],Rect.Min.X,Rect.Min.Y,Rect.GetSize().X,Rect.GetSize().Y);
        }
        DrawText(Names[I],FLinearColor(0.8f,0.8f,0.8f),Rect.Min.X,Rect.Max.Y+4*Scale,GEngine->GetSmallFont(),Scale);
    }
    DrawText(TEXT("Drag to field"),FLinearColor(0.8f,0.8f,0.8f),12*Scale,16*Scale,GEngine->GetSmallFont(),Scale);
    DrawText(TEXT("Click: pin   Drag: stir   RMB: slice   R: reset   Wheel: zoom"),
        FLinearColor(0.85f,0.85f,0.85f),Dock+16*Scale,14*Scale,GEngine->GetSmallFont(),Scale);
    if (Dragged>=0 && Glyphs.IsValidIndex(Dragged) && HasMouse && Controller->CanPreviewDrop())
        DrawMaterialSimple(Glyphs[Dragged],X-34*Scale,Y-34*Scale,68*Scale,68*Scale);
}
