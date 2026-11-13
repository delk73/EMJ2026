#include "DirectOrScutBakeCommandlet.h"
#if WITH_EDITOR
#include "DirectOrScutGameMode.h"
#include "DirectOrScutGeometry.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#endif
UDirectOrScutBakeCommandlet::UDirectOrScutBakeCommandlet()
{
    IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true;
}
int32 UDirectOrScutBakeCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    const TCHAR* Hex[]={TEXT("D8D5CE"),TEXT("EAE7E0"),TEXT("F5F8FF"),TEXT("D97A1E"),TEXT("D92020"),TEXT("2C3035"),TEXT("24C8D8"),TEXT("22252A"),TEXT("32D879")};
    const float Rough[]={.32f,.38f,.32f,.45f,.3f,.55f,.35f,.6f,.3f};
    for (int32 I=0;I<DirectOrScut::MaterialCount;++I)
    {
        FString Name=FString::Printf(TEXT("M_Slot%d"),I), Path=TEXT("/Game/DirectOrScut/Materials/")+Name;
        auto* Package=CreatePackage(*Path);
        Package->FullyLoad();
        auto* M=FindObject<UMaterial>(Package,*Name);
        const bool bNew=M==nullptr;
        if (bNew) M=NewObject<UMaterial>(Package,*Name,RF_Public|RF_Standalone);
        else UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);
        auto* Color=CastChecked<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionConstant3Vector::StaticClass()));
        Color->Constant=FLinearColor(FColor::FromHex(Hex[I]));
        UMaterialEditingLibrary::ConnectMaterialProperty(Color,TEXT(""),MP_BaseColor);
        auto Scalar=[&](float Value,EMaterialProperty Property)
        {
            auto* E=CastChecked<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionScalarParameter::StaticClass()));
            E->DefaultValue=Value; E->ParameterName=Property==MP_Roughness?TEXT("Roughness"):TEXT("Metallic"); UMaterialEditingLibrary::ConnectMaterialProperty(E,TEXT(""),Property);
        };
        Scalar(Rough[I],MP_Roughness); Scalar(I==0?.05f:I==1?.1f:0.f,MP_Metallic);
        if (I==2 || I==4 || I==8)
        {
            auto* E=CastChecked<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionConstant3Vector::StaticClass()));
            E->Constant=Color->Constant*(I==2?1.5f:1.2f);
            UMaterialEditingLibrary::ConnectMaterialProperty(E,TEXT(""),MP_EmissiveColor);
        }
        UMaterialEditingLibrary::RecompileMaterial(M); if (bNew) FAssetRegistryModule::AssetCreated(M); M->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        if (!UPackage::SavePackage(Package,M,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args)) return 2;
    }
    if (!FPackageName::DoesPackageExist(TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout")))
    {
        auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!World) return 3;
        World->GetWorldSettings()->DefaultGameMode=ADirectOrScutGameMode::StaticClass();
        if (!UEditorLoadingAndSavingUtils::SaveMap(World,TEXT("/Game/DirectOrScut/Maps/DirectOrScutBlockout"))) return 4;
    }
    UE_LOG(LogTemp,Display,TEXT("DIRECT OR SCUT: saved nine clinical procedural materials and dedicated blockout map"));
    return 0;
#else
    return 1;
#endif
}
