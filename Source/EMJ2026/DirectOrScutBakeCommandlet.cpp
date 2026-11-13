#include "DirectOrScutBakeCommandlet.h"
#if WITH_EDITOR
#include "DirectOrScutGameMode.h"
#include "DirectOrScutHullActor.h"
#include "CompartmentCameraRig.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "DirectOrScutGeometry.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Settings/LevelEditorViewportSettings.h"
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
    if (!Params.Contains(TEXT("CrtOnly")) && !Params.Contains(TEXT("MapOnly")))
    for (int32 I=0;I<DirectOrScut::MaterialCount;++I)
    {
        FString Name=FString::Printf(TEXT("M_Slot%d"),I), Path=TEXT("/Game/DirectOrScut/Materials/")+Name;
        auto* Package=CreatePackage(*Path);
        Package->FullyLoad();
        auto* M=FindObject<UMaterial>(Package,*Name);
        const bool bNew=M==nullptr;
        if (bNew) M=NewObject<UMaterial>(Package,*Name,RF_Public|RF_Standalone);
        else UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);
        auto* Color=CastChecked<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionVectorParameter::StaticClass()));
        Color->ParameterName=TEXT("BaseTint");Color->DefaultValue=FLinearColor(FColor::FromHex(Hex[I]));
        UMaterialEditingLibrary::ConnectMaterialProperty(Color,TEXT(""),MP_BaseColor);
        auto Scalar=[&](float Value,EMaterialProperty Property)
        {
            auto* E=CastChecked<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionScalarParameter::StaticClass()));
            E->DefaultValue=Value; E->ParameterName=Property==MP_Roughness?TEXT("Roughness"):TEXT("Metallic"); UMaterialEditingLibrary::ConnectMaterialProperty(E,TEXT(""),Property);
        };
        if(I==8)
        {
            auto* Tint=CastChecked<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionVectorParameter::StaticClass()));
            Tint->ParameterName=TEXT("Tint");Tint->DefaultValue=FLinearColor::White;
            UMaterialEditingLibrary::ConnectMaterialProperty(Tint,TEXT(""),MP_EmissiveColor);
        }
        Scalar(Rough[I],MP_Roughness); Scalar(I==0?.05f:I==1?.1f:0.f,MP_Metallic);
        if (I==2 || I==4)
        {
            auto* E=CastChecked<UMaterialExpressionConstant3Vector>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionConstant3Vector::StaticClass()));
            E->Constant=Color->DefaultValue*(I==2?1.5f:1.2f);
            UMaterialEditingLibrary::ConnectMaterialProperty(E,TEXT(""),MP_EmissiveColor);
        }
        UMaterialEditingLibrary::RecompileMaterial(M); if (bNew) FAssetRegistryModule::AssetCreated(M); M->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        if (!UPackage::SavePackage(Package,M,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args)) return 2;
    }
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if(!World)return 3;
    World->GetWorldSettings()->DefaultGameMode=ADirectOrScutGameMode::StaticClass();
    World->SpawnActor<ADirectOrScutHullActor>();
    World->SpawnActor<ACompartmentCameraRig>();
    auto* OverviewKey=World->SpawnActor<ADirectionalLight>(FVector(5250,6000,8000),FRotator(-35,-90,0));
    OverviewKey->bIsEditorOnlyActor=true;
    OverviewKey->Tags.Add(TEXT("ScutEditorOverview"));
    OverviewKey->SetActorLabel(TEXT("Soundstage Overview Key (editor only)"));
    auto* Key=OverviewKey->GetLightComponent();Key->SetMobility(EComponentMobility::Movable);
    Key->bUseTemperature=true;Key->SetTemperature(5800);Key->SetLightColor(FLinearColor::White);Key->SetIntensity(4);

    // Persist a front overview with X increasing left-to-right and the raised row above.
    for(auto& View:World->EditorViews)
    {
        View.CamPosition=FVector(5250,12000,4700);
        View.CamRotation=(FVector(5250,0,1950)-View.CamPosition).Rotation();
        View.CamOrthoZoom=17000;View.CamUpdated=true;
    }
    const auto OverviewViews=World->EditorViews;
    for(auto* Client:GEditor->GetLevelViewportClients())if(Client && Client->GetWorld()==World && Client->IsPerspective())
    {
        Client->SetViewLocation(FVector(5250,12000,4700));
        Client->SetViewRotation((FVector(5250,0,1950)-Client->GetViewLocation()).Rotation());
    }
    if(!UEditorLoadingAndSavingUtils::SaveMap(World,TEXT("/Game/Maps/DirectOrScut_Soundstage")))return 4;
    // UE restores a per-project user view ahead of the map's view when one exists.
    // Refresh only this rebaked map's cached view so the existing checkout opens on the overview too.
    auto* ViewSettings=GetMutableDefault<ULevelEditorViewportSettings>();
    ViewSettings->EditorViews.FindOrAdd(World).LevelViewportsInfo=OverviewViews;
    ViewSettings->SaveConfig();
    UE_LOG(LogTemp,Display,TEXT("DIRECT OR SCUT: saved spacecraft soundstage map and UX-matched editor overview"));
    return 0;
#else
    return 1;
#endif
}
