#include "CompartmentCameraRig.h"
#include "DirectOrScutGeometry.h"
#include "DirectOrScutSimSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshBooleanFunctions.h"
ACompartmentCameraRig::ACompartmentCameraRig()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));
    for (int32 I=0;I<9;++I) { auto* C=CreateDefaultSubobject<USceneCaptureComponent2D>(*FString::Printf(TEXT("Feed%d"),I)); C->SetupAttachment(RootComponent); C->bCaptureEveryFrame=false; C->bCaptureOnMovement=false; Captures.Add(C); }
}
int32 ACompartmentCameraRig::RoomForFeed(int32 F) { const int32 Map[]={-1,0,2,3,4,5,6,7,1}; return F>=0 && F<9?Map[F]:-1; }
int32 ACompartmentCameraRig::FeedForRoom(int32 R) { for (int32 F=1;F<9;++F) if (RoomForFeed(F)==R) return F; return 0; }
void ACompartmentCameraRig::BeginPlay()
{
    Super::BeginPlay();
    // Restore the cutaway hull optically for CCTV, without changing simulation/collision.
    auto* Shell=DirectOrScut::Component(this,TEXT("CCTVEnclosure"));
    Shell->SetVisibleInSceneCaptureOnly(true); Shell->SetCastShadow(false);
    auto* Mesh=Shell->GetDynamicMesh();
    const auto Sites=DirectOrScut::DoorSites();
    auto Wall=[&](FVector Center,float Length,float Yaw)
    {
        if (Center.X+Center.Y<=40) return;
        auto* Segment=NewObject<UDynamicMesh>(this);
        DirectOrScut::Box(Segment,Center+FVector(0,0,157.5),FVector(Length,36,205),1,Yaw);
        const FVector Tangent=DirectOrScut::Radial(Yaw,1);
        for (const auto& Site:Sites)
        {
            const FVector D=Site.Position-Center;
            if (FMath::Abs(FVector::DotProduct(D,Tangent))>Length/2-100 || FMath::Abs(FVector::DotProduct(D,DirectOrScut::Radial(Yaw+90,1)))>25) continue;
            auto* Cutter=NewObject<UDynamicMesh>(this);
            DirectOrScut::Box(Cutter,Site.Position+FVector(0,0,120),FVector(140,240,240),1,Site.Yaw);
            UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Segment,FTransform::Identity,Cutter,FTransform::Identity,EGeometryScriptBooleanOperation::Subtract,{});
        }
        UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Mesh,FTransform::Identity,Segment,FTransform::Identity,EGeometryScriptBooleanOperation::Union,{});
    };
    for (const auto& Station:DirectOrScut::Stations())
    {
        const float A=Station.Face*45.f; const FRotator Rotation(0,A,0); const FVector P=DirectOrScut::Radial(A,DirectOrScut::PodCenterRadius());
        const auto Poly=DirectOrScut::Octagon(DirectOrScut::PodLength,DirectOrScut::PodWidth,90);
        DirectOrScut::Prism(Mesh,Poly,12,FTransform(Rotation,P+FVector(0,0,260)),1);
        for (int32 J=0;J<8;++J) { if (J==6) continue; const FVector V0=P+Rotation.RotateVector(FVector(Poly[J],0)),V1=P+Rotation.RotateVector(FVector(Poly[(J+1)%8],0)); Wall((V0+V1)/2,FVector::Distance(V0,V1),(V1-V0).Rotation().Yaw); }
    }
    TArray<FVector2D> Core;
    for (int32 I=0;I<8;++I)
    {
        auto P=DirectOrScut::Radial(I*45.f-22.5f,500); Core.Add(FVector2D(P));
        const float A=I*45.f;
        for (float R:{DirectOrScut::InnerRadius,DirectOrScut::OuterRadius,500.f}) Wall(DirectOrScut::Radial(A,R*FMath::Cos(PI/8)),2*R*FMath::Sin(PI/8),A+90);
        const FVector IA=DirectOrScut::Radial(A-22.5f,DirectOrScut::InnerRadius),IB=DirectOrScut::Radial(A+22.5f,DirectOrScut::InnerRadius),OA=DirectOrScut::Radial(A-22.5f,DirectOrScut::OuterRadius),OB=DirectOrScut::Radial(A+22.5f,DirectOrScut::OuterRadius);
        DirectOrScut::Prism(Mesh,{{IA.X,IA.Y},{OA.X,OA.Y},{OB.X,OB.Y},{IB.X,IB.Y}},12,FTransform(FVector(0,0,260)),1);
    }
    DirectOrScut::Prism(Mesh,Core,12,FTransform(FVector(0,0,260)),1);
    DirectOrScut::Finish(Shell,false); Captures[0]->HideComponent(Shell);
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DirectOrScut/Materials/M_CrtMonitorFeed.M_CrtMonitorFeed"));
    for (int32 F=0;F<9;++F)
    {
        auto* C=Captures[F].Get();
        auto* RT=NewObject<UTextureRenderTarget2D>(this); RT->RenderTargetFormat=RTF_RGBA8; RT->InitAutoFormat(F==0?1024:512,F==0?640:320); RT->UpdateResourceImmediate(true); Targets.Add(RT); C->TextureTarget=RT;
        C->CaptureSource=SCS_FinalColorLDR;
        C->PostProcessSettings.bOverride_AutoExposureMethod=true; C->PostProcessSettings.AutoExposureMethod=AEM_Manual;
        C->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true; C->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
        if (F==0) { const FRotator View(-45,-135,0); C->SetWorldLocationAndRotation(-View.Vector()*4500,View); C->ProjectionType=ECameraProjectionMode::Orthographic; C->OrthoWidth=5600; }
        else
        {
            int32 R=RoomForFeed(F); float A=R>=2?DirectOrScut::Stations()[R-2].Face*45.f:0;
            FVector Center=R>=2?DirectOrScut::Radial(A,DirectOrScut::PodCenterRadius()):R==1?DirectOrScut::Radial(0,1140):FVector::ZeroVector;
            FVector Pos=Center-DirectOrScut::Radial(A,R>=2?390: R==1?100:440); Pos.Z=145;
            if (R>=2 && R<=6) { A+=90; Pos=Center-DirectOrScut::Radial(A,390); Pos.Z=145; }
            if (R==1) { Pos=FVector(1140,-110,145); A=90; }
            C->SetWorldLocationAndRotation(Pos,FRotator(0,A,0)); C->FOVAngle=73.74f;
        }
        auto* MID=Material?UMaterialInstanceDynamic::Create(Material,this):nullptr; FeedMaterials.Add(MID);
        if (MID) { MID->SetTextureParameterValue(TEXT("Texture"),RT); MID->SetScalarParameterValue(TEXT("LensK1"),F==0?0:.035f); MID->SetScalarParameterValue(TEXT("Glass"),0); }
    }
}
void ACompartmentCameraRig::SetVisibleFeeds(int32 P,int32 A,bool Grid) { Active=FMath::Clamp(P,1,8); PiP=A; bGrid=Grid; }
void ACompartmentCameraRig::Tick(float Delta)
{
    Super::Tick(Delta); CaptureClock+=Delta; if (CaptureClock<1.f/15) return; CaptureClock=0;
    auto* S=GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>();
    for (int32 F=0;F<9;++F)
    {
        if (F==0 || (!bGrid && F!=Active && F!=PiP)) continue;
        const bool Primary=!bGrid && F==Active;
        if (Targets[F]->SizeX!=(Primary?1024:512)) Targets[F]->ResizeTarget(Primary?1024:512,Primary?640:320);
        if (auto* M=FeedMaterials[F].Get())
        {
            const int32 R=RoomForFeed(F); float Noise=.015f;
            if (S && R>=0) { Noise+=FVector::Distance(Captures[F]->GetComponentLocation(),Captures[2]->GetComponentLocation())/400000.f; for (const auto& U:S->Simulation.Bodies) if (U.bRegistered && U.Room==0 && U.State==EDirectOrScutUnitState::Working) Noise+=.006f; M->SetScalarParameterValue(TEXT("Distortion"),S->Simulation.Rooms[R].bIsPressurized?0:1); }
            M->SetScalarParameterValue(TEXT("SignalNoise"),Noise); M->SetScalarParameterValue(TEXT("Clock"),GetWorld()->GetTimeSeconds());
        }
        Captures[F]->CaptureScene(); ++CaptureCounts[F];
    }
}
