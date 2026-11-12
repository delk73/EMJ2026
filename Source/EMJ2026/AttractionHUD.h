#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AttractionHUD.generated.h"

UCLASS()
class EMJ2026_API AAttractionHUD : public AHUD
{
    GENERATED_BODY()
public:
    AAttractionHUD();
    virtual void BeginPlay() override;
    virtual void DrawHUD() override;
    static FBox2D TileRect(int32 Index,int32 Width,int32 Height);
    static float TrayWidth(int32 Width,int32 Height);
    static int32 HitTray(FVector2D Point,int32 Width,int32 Height);
private:
    UPROPERTY() TSoftObjectPtr<class UMaterialInterface> GlyphMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInstanceDynamic>> Glyphs;
    float Hover[3] = {};
};
