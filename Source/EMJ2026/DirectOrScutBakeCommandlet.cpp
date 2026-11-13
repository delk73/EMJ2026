#include "DirectOrScutBakeCommandlet.h"
#if WITH_EDITOR
#include "DirectOrScutGameMode.h"
#include "DirectOrScutGeometry.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
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
    if (!Params.Contains(TEXT("CrtOnly")))
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
    {
        const FString Path=TEXT("/Game/DirectOrScut/Materials/M_CrtMonitorFeed");
        auto* Package=CreatePackage(*Path); Package->FullyLoad();
        auto* M=FindObject<UMaterial>(Package,TEXT("M_CrtMonitorFeed")); const bool New=M==nullptr;
        if (!M) M=NewObject<UMaterial>(Package,TEXT("M_CrtMonitorFeed"),RF_Public|RF_Standalone);
        else UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);
        M->MaterialDomain=MD_UI; M->BlendMode=BLEND_Translucent;
        auto* Shader=CastChecked<UMaterialExpressionCustom>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionCustom::StaticClass()));
        Shader->OutputType=CMOT_Float4;
        auto Input=[&](FName Name,UMaterialExpression* E) { FCustomInput I; I.InputName=Name; I.Input.Connect(0,E); Shader->Inputs.Add(I); };
        Shader->Inputs.Reset();
        auto* Texture=CastChecked<UMaterialExpressionTextureObjectParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionTextureObjectParameter::StaticClass()));
        Texture->ParameterName=TEXT("Texture"); Texture->Texture=LoadObject<UTexture>(nullptr,TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")); Input(TEXT("Texture"),Texture);
        Input(TEXT("UV"),UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionTextureCoordinate::StaticClass()));
        auto Scalar=[&](const TCHAR* Name,float Value) { auto* P=CastChecked<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(M,UMaterialExpressionScalarParameter::StaticClass())); P->ParameterName=Name; P->DefaultValue=Value; Input(Name,P); };
        Scalar(TEXT("Glass"),1); Scalar(TEXT("LensK1"),0); Scalar(TEXT("Clock"),0); Scalar(TEXT("SignalNoise"),.012f); Scalar(TEXT("Distortion"),0);
        Shader->Code=TEXT(R"HLSL(
float2 p=UV*2-1;
float2 uv=.5+.5*p*(1+(Glass*.012+LensK1)*dot(p,p));
float row=floor(uv.y*640);
float n=frac(sin(dot(float2(row,floor(Clock*30)),float2(12.9898,78.233)))*43758.5453);
float brown=sin(Clock*.7+row*.03)*.4+sin(Clock*1.3+row*.11)*.2;
uv.x+=(n-.5+brown)*SignalNoise*.08+Distortion*.02*sin(row*.12+Clock*18);
uv.y+=Distortion*.06*sin(Clock*3);
float2 split=float2(Distortion*.009,0);
float4 c=Texture2DSample(Texture,TextureSampler,uv);
c.r=Texture2DSample(Texture,TextureSampler,uv+split).r;
c.b=Texture2DSample(Texture,TextureSampler,uv-split).b;
float scan=.94+.06*sin(uv.y*640*3.14159);
float edge=saturate(1-dot(p*p,p*p)*.18*Glass);
float mask=step(0,uv.x)*step(uv.x,1)*step(0,uv.y)*step(uv.y,1);
float3 glow=(Texture2DSample(Texture,TextureSampler,uv+float2(.0015,0)).rgb+Texture2DSample(Texture,TextureSampler,uv-float2(.0015,0)).rgb)*.035*Glass;
c.rgb=(c.rgb*scan+glow+(n-.5)*(SignalNoise+Distortion*.25))*edge*mask;
c.a=1;
return c;
)HLSL");
        UMaterialEditingLibrary::ConnectMaterialProperty(Shader,TEXT(""),MP_EmissiveColor);
        // The monitor is opaque; the procedural corner mask is part of its emitted colour.
        UMaterialEditingLibrary::RecompileMaterial(M); if (New) FAssetRegistryModule::AssetCreated(M); M->MarkPackageDirty();
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        if (!UPackage::SavePackage(Package,M,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args)) return 5;
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
