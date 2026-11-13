#include "DirectOrScutHullActor.h"
#include "DirectOrScutGeometry.h"
#include "DoorActor.h"
#include "CrewUnitBlockout.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"

using namespace DirectOrScut;
namespace
{
FTransform PortalTransform(FVector P, float Yaw)
{
    const FQuat Q=FMatrix(FPlane(0,1,0,0),FPlane(0,0,1,0),FPlane(1,0,0,0),FPlane(0,0,0,1)).ToQuat();
    return FTransform(FRotator(0,Yaw,0).Quaternion()*Q,P);
}
void ClinicalLight(AActor* A, FVector P, float Intensity, float Radius)
{
    auto* L=NewObject<UPointLightComponent>(A); A->AddInstanceComponent(L);
    L->SetupAttachment(A->GetRootComponent()); L->SetRelativeLocation(P);
    L->bUseTemperature=true; L->SetTemperature(5800.f);
    L->SetLightColor(FLinearColor::White); L->SetIntensity(Intensity);
    L->SetAttenuationRadius(Radius); L->SetSourceRadius(100.f);
    L->SetCastShadows(false); L->RegisterComponent();
}
}
ADirectOrScutHullActor::ADirectOrScutHullActor()
{
    Hull=CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ProceduralHull")); RootComponent=Hull;
}
void ADirectOrScutHullActor::BeginPlay()
{
    Super::BeginPlay(); Generate();
}
void ADirectOrScutHullActor::Generate()
{
    Materials(Hull);
    auto* M=Hull->GetDynamicMesh();
    const auto Sites=DoorSites();
    TArray<FDoorSite> Openings=Sites;
    // The two core mouths remain open: one bulkhead per spoke, at the ring end.
    for (float A : {135.f,315.f}) Openings.Add({Radial(A,500*FMath::Cos(PI/8)),A});
    int32 WallIndex=0;
    auto Wall=[&](FVector Center,float Length,float Yaw)
    {
        const float Height=Center.X+Center.Y > 40 ? 55.f : 260.f;
        auto* C=Component(this,*FString::Printf(TEXT("Bulkhead%d"),WallIndex++));
        auto* WallMesh=C->GetDynamicMesh();
        TArray<FVector2D> Cross={{-18,0},{18,0},{18,Height-12},{6,Height},{-6,Height},{-18,Height-12}};
        const FQuat Q=PortalTransform(FVector::ZeroVector,Yaw).GetRotation();
        Prism(WallMesh,Cross,Length,FTransform(Q,Center-Q.RotateVector(FVector(0,0,Length/2))),1);
        const FVector Tangent=Radial(Yaw,1);
        for (int32 Index=0;Index<Openings.Num();++Index)
        {
            const auto& S=Openings[Index];
            const FVector Delta=S.Position-Center;
            const float Width=Index>=Sites.Num()?280.f:200.f;
            if (FMath::Abs(FVector::DotProduct(Delta,Tangent))>Length/2-Width/2-5 ||
                FMath::Abs(FVector::DotProduct(Delta,Radial(Yaw+90,1)))>22) continue;
            auto* Cutter=NewObject<UDynamicMesh>(this);
            Prism(Cutter,Octagon(Width,240,30),110,PortalTransform(S.Position+FVector(0,0,120)-Radial(S.Yaw,55),S.Yaw),1);
            UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(WallMesh,FTransform::Identity,Cutter,FTransform::Identity,EGeometryScriptBooleanOperation::Subtract,{});
        }
        Finish(C);
    };
    // Ring deck: satin composite, fine graphite panel joints, continuous edge channels.
    for (int32 I=0;I<8;++I)
    {
        const float A=I*45.f;
        FVector OA=Radial(A-22.5f,OuterRadius), OB=Radial(A+22.5f,OuterRadius);
        FVector IA=Radial(A-22.5f,InnerRadius), IB=Radial(A+22.5f,InnerRadius);
        Prism(M,{{IA.X,IA.Y},{OA.X,OA.Y},{OB.X,OB.Y},{IB.X,IB.Y}},24,FTransform(FVector(0,0,-24)),0);
        for (float Radius : {InnerRadius,OuterRadius})
        {
            const float Apothem=Radius*FMath::Cos(PI/8);
            const float Length=2*Radius*FMath::Sin(PI/8);
            Wall(Radial(A,Apothem),Length,A+90);
            for (float T=-Length/2+80;T<Length/2;T+=160)
            {
                FVector P=Radial(A,Apothem)+Radial(A+90,T);
                bool Gate=false;
                for (auto& S:Sites) if (FVector::Dist2D(P,S.Position)<145) Gate=true;
                if (Gate) continue;
                float H=P.X+P.Y>40?55:260;
                Prism(M,{{-28,0},{28,0},{28,H-30},{-2,H},{-28,H}},22,PortalTransform(P-Radial(A+90,11),A+90),1);
                Box(M,P+Radial(A,Radius==OuterRadius?-20.f:20.f)+FVector(0,0,H/2),FVector(3,12,H-14),7,A);
            }
            const float StripR=Apothem+(Radius==InnerRadius?46.f:-46.f);
            Box(M,Radial(A,StripR)+FVector(0,0,-0.5),FVector(2*StripR*FMath::Tan(PI/8),14,1),5,A+90);
            Box(M,Radial(A,StripR),FVector(2*StripR*FMath::Tan(PI/8),7,0.8),2,A+90);
        }
        const float MidR=(InnerRadius+OuterRadius)*0.5f*FMath::Cos(PI/8);
        const float Span=2*MidR*FMath::Tan(PI/8);
        for (float T=-Span/2+100;T<Span/2;T+=240)
            Box(M,Radial(A,MidR)+Radial(A+90,T)+FVector(0,0,0.15),FVector((OuterRadius-InnerRadius)*FMath::Cos(PI/8)-70,1.5,0.3),5,A);
        ClinicalLight(this,Radial(A,MidR)+FVector(0,0,330),9000,850);
    }
    TArray<FVector2D> Core;
    for (int32 I=0;I<8;++I) { auto P=Radial(I*45.f-22.5f,500); Core.Add({P.X,P.Y}); }
    Prism(M,Core,24,FTransform(FVector(0,0,-24)),0);
    for (int32 I=0;I<8;++I) Wall(Radial(I*45.f,500*FMath::Cos(PI/8)),2*500*FMath::Sin(PI/8),I*45.f+90);
    for (float A : {135.f,315.f})
    {
        const float Start=500*FMath::Cos(PI/8), End=InnerRadius*FMath::Cos(PI/8);
        Box(M,Radial(A,(Start+End)/2)+FVector(0,0,-12),FVector(End-Start,280,24),0,A);
        for (float Side : {-140.f,140.f})
        {
            Wall(Radial(A,(Start+End)/2)+Radial(A+90,Side),End-Start,A);
            Box(M,Radial(A,(Start+End)/2)+Radial(A+90,Side*.72f),FVector(End-Start,7,0.8),2,A);
        }
    }
    for (int32 StationIndex=0;StationIndex<Stations().Num();++StationIndex)
    {
        const auto& S=Stations()[StationIndex];
        const float A=S.Face*45.f;
        const FRotator Rotation(0,A,0);
        const FVector P=Radial(A,PodCenterRadius());
        auto Local=[&](float X,float Y,float Z) { return P+Rotation.RotateVector(FVector(X,Y,Z)); };
        auto B=[&](FVector Position,FVector Size,int32 ID,float RelativeYaw=0.f)
        { Box(M,P+Rotation.RotateVector(Position),Size,ID,A+RelativeYaw); };
        const auto Poly=Octagon(PodLength,PodWidth,90);
        Prism(M,Poly,24,FTransform(Rotation,P+FVector(0,0,-24)),0);
        for (int32 J=0;J<8;++J)
        {
            // The left face is the ring's existing wall, with its single shared doorway.
            if (J==6) continue;
            FVector V0=Local(Poly[J].X,Poly[J].Y,0), V1=Local(Poly[(J+1)%8].X,Poly[(J+1)%8].Y,0);
            FVector D=V1-V0;
            Wall((V0+V1)/2,D.Size(),D.Rotation().Yaw);
            FVector C=(V0+V1)/2+(P-(V0+V1)/2).GetSafeNormal()*44;
            Box(M,C,FVector(D.Size()-35,7,0.8),2,D.Rotation().Yaw);
        }
        for (float X : {-240.f,0.f,240.f}) B(FVector(X,0,0.15),FVector(1.5,740,.3),5);
        for (float Y : {-210.f,210.f}) B(FVector(0,Y,0.15),FVector(850,1.5,.3),5);
        if (StationIndex==0) // Faceted wrap-around bridge console bank.
        {
            for (int32 J=0;J<7;++J)
            {
                const float Arc=-85.f+J*(170.f/6);
                FVector V=Radial(Arc,285)+FVector(-10,0,0);
                B(V+FVector(0,0,48),FVector(105,130,96),1,Arc);
                B(V+FVector(0,0,99),FVector(112,136,10),7,Arc);
                B(V+FVector(0,0,107),FVector(80,105,4),7,Arc);
                B(V+FVector(0,0,112),FVector(8,88,3),3,Arc);
            }
        }
        else if (StationIndex==1) // Twin diagnostic slabs, cyan instrument bands.
        {
            for (float Y : {-195.f,195.f})
            {
                B(FVector(60,Y,26),FVector(235,105,52),7);
                B(FVector(60,Y,64),FVector(295,135,24),1);
                B(FVector(60,Y,78),FVector(245,92,4),6);
                B(FVector(220,Y,108),FVector(35,140,216),1);
                B(FVector(199,Y,160),FVector(8,100,38),6);
            }
        }
        else if (StationIndex==2) // Two faceted industrial crusher drums in a white cradle.
        {
            B(FVector(60,0,20),FVector(420,570,40),7);
            for (float Y : {-145.f,145.f})
            {
                FGeometryScriptPrimitiveOptions O; O.MaterialID=1;
                UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(M,O,FTransform(Rotation,Local(60,Y,40)),105,180,16);
                B(FVector(60,Y,225),FVector(150,150,10),3);
                B(FVector(60,Y,232),FVector(78,78,4),7);
            }
            B(FVector(-125,0,65),FVector(45,500,130),1);
            B(FVector(-150,0,103),FVector(5,440,10),3);
        }
        else if (StationIndex==3) // Square crucible with a dark recessed mouth; no orange light flood.
        {
            B(FVector(60,0,22),FVector(370,370,44),7);
            B(FVector(60,0,93),FVector(300,300,142),1);
            B(FVector(60,0,166),FVector(310,310,6),3);
            B(FVector(60,0,171),FVector(215,215,4),7);
            B(FVector(60,0,174),FVector(110,110,2),3);
            for (float Y : {-190.f,190.f}) B(FVector(60,Y,92),FVector(360,30,45),5);
        }
        else if (StationIndex==4) // Cargo pallet pads, restrained safety bands and crates.
        {
            for (float Y : {-200.f,200.f})
            {
                B(FVector(70,Y,8),FVector(350,290,16),5);
                B(FVector(70,Y,17),FVector(320,260,2),3);
                for (float X : {-35.f,150.f})
                {
                    B(FVector(X,Y,72),FVector(130,190,108),1);
                    B(FVector(X,Y,128),FVector(140,200,4),5);
                }
            }
        }
        else // SCUT: framed exterior hatch is the ninth real door.
        {
            for (int32 J=0;J<10;++J) B(FVector(365,-180+J*40,0.4),FVector(90,30,.8),J%2?3:4);
            for (float Y : {-300.f,300.f})
            {
                B(FVector(40,Y,70),FVector(320,60,140),1);
                B(FVector(40,Y-34,85),FVector(260,4,12),4);
            }
        }
        auto* Label=NewObject<UTextRenderComponent>(this); AddInstanceComponent(Label);
        Label->SetupAttachment(RootComponent); Label->SetRelativeLocation(Local(-280,0,1));
        Label->SetRelativeRotation(FRotator(90,180,0)); Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetText(FText::FromString(S.Name)); Label->SetWorldSize(26);
        Label->SetTextRenderColor(FColor(44,48,53)); Label->RegisterComponent();
        ClinicalLight(this,P+FVector(0,0,480),11000,1050);
    }
    for (int32 I=0;I<6;++I)
    {
        FVector P=Radial(I*60.f,210);
        Box(M,P+FVector(0,0,125),FVector(55,110,250),7,I*60.f);
        Box(M,P+Radial(I*60.f,-29)+FVector(0,0,130),FVector(4,70,190),1,I*60.f);
        Box(M,P+Radial(I*60.f,-32)+FVector(0,0,160),FVector(2,48,7),I%2?3:4,I*60.f);
    }
    ClinicalLight(this,FVector(0,0,440),10000,850);
    auto* Stars=Component(this,TEXT("DistantStarfield"));
    FRandomStream Sky(1970);
    for (int32 I=0;I<180;++I)
    {
        FVector P(Sky.FRandRange(-6500,6500),Sky.FRandRange(-6500,6500),-1600);
        const float Size=Sky.FRandRange(1.5f,4.f);
        Box(Stars->GetDynamicMesh(),P,FVector(Size),2);
    }
    Finish(Stars,false); Finish(Hull);
    int32 PortalIndex=0;
    for (const auto& S:Sites)
    {
        auto* Frame=Component(this,*FString::Printf(TEXT("Portal%d"),PortalIndex++));
        Prism(Frame->GetDynamicMesh(),Octagon(S.bExterior?290:256,S.bExterior?302:284,44),55,
            PortalTransform(S.Position+FVector(0,0,120)-Radial(S.Yaw,27.5f),S.Yaw),S.bExterior?4:1);
        auto* Cutter=NewObject<UDynamicMesh>(this);
        Prism(Cutter,Octagon(200,240,30),110,PortalTransform(S.Position+FVector(0,0,120)-Radial(S.Yaw,55),S.Yaw),1);
        UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Frame->GetDynamicMesh(),FTransform::Identity,Cutter,FTransform::Identity,EGeometryScriptBooleanOperation::Subtract,{});
        Finish(Frame);
        auto* Door=GetWorld()->SpawnActorDeferred<ADoorActor>(ADoorActor::StaticClass(),FTransform(FRotator(0,S.Yaw,0),S.Position),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Door->bExterior=S.bExterior;
        Door->FinishSpawning(FTransform(FRotator(0,S.Yaw,0),S.Position));
    }
    for (int32 I=0;I<4;++I)
    {
        FVector Waypoint=Radial(Stations()[I].Face*45.f,PodCenterRadius()-240)+Radial(Stations()[I].Face*45.f+90,150);
        auto* Unit=GetWorld()->SpawnActorDeferred<ACrewUnitBlockout>(ACrewUnitBlockout::StaticClass(),FTransform(Waypoint),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Unit->bAndroid=I==3; Unit->SuitSlot=I==0?3:I==1?6:5;
        Unit->FinishSpawning(FTransform(Waypoint));
    }
    UE_LOG(LogTemp,Display,TEXT("DIRECT OR SCUT: clinical hull; 980/1300 ring, 950x850 pod interiors, 2 spokes, 9 doors (8 interior + SCUT), 4 units"));
}
