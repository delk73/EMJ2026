#include "DirectOrScutGeometry.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DirectOrScut
{
const TArray<FStation>& Stations()
{
    static const TArray<FStation> Table = {
        {TEXT("BRIDGE"),0,1}, {TEXT("MED_BAY"),1,6}, {TEXT("ORE_CRUSHER"),2,3},
        {TEXT("SMELTER_FOUNDRY"),4,3}, {TEXT("DOCKING_BAY"),5,3}, {TEXT("AIRLOCK_CHAMBER"),6,4}};
    return Table;
}
FVector Radial(float Degrees, float Radius)
{
    const float A = FMath::DegreesToRadians(Degrees);
    return FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0);
}
float OuterApothem() { return OuterRadius*FMath::Cos(PI/8); }
float PodCenterRadius() { return OuterApothem()+PodLength/2; }
TArray<FDoorSite> DoorSites()
{
    TArray<FDoorSite> Sites;
    for (int32 I=0;I<Stations().Num();++I)
    {
        const FStation& S=Stations()[I];
        Sites.Add({Radial(S.Face*45.f,OuterApothem()),S.Face*45.f,false,1,I+2});
    }
    for (float A : {135.f,315.f})
        Sites.Add({Radial(A,InnerRadius*FMath::Cos(PI/8)),A,false,0,1});
    Sites.Add({Radial(270.f,OuterApothem()+PodLength),270.f,true,7,-1});
    return Sites;
}
const TCHAR* RoomName(int32 Room)
{
    if (Room==0) return TEXT("REACTOR_CORE");
    if (Room==1) return TEXT("RING_CORRIDOR");
    if (Room>=2 && Room<8) return Stations()[Room-2].Name;
    return TEXT("VACUUM");
}
FVector2D RoomWaypoint(int32 Room)
{
    if (Room==0) return FVector2D(Radial(315.f,350));
    if (Room==1) return FVector2D(Radial(0,(InnerRadius+OuterRadius)*.5f*FMath::Cos(PI/8)));
    if (Room>=2 && Room<8) return FVector2D(Radial(Stations()[Room-2].Face*45.f,PodCenterRadius()-240));
    return FVector2D::ZeroVector;
}
FVector2D RoomStandby(int32 Room)
{
    if (Room<2) return RoomWaypoint(Room);
    return FVector2D(Radial(Stations()[Room-2].Face*45.f,PodCenterRadius()-300));
}
FVector CrewSpawnPoint(int32 Unit)
{
    if (Unit<0 || Unit>=4) return FVector::ZeroVector;
    const float A=Stations()[Unit].Face*45.f;
    return Radial(A,PodCenterRadius()-240)+Radial(A+90,150);
}
bool RoomContains(int32 Room,FVector2D P,double Clearance)
{
    auto OctExtent=[](FVector2D Point)
    {
        double Max=-DBL_MAX;
        for (int32 I=0;I<8;++I) Max=FMath::Max(Max,FVector2D::DotProduct(Point,FVector2D(Radial(I*45.f,1))));
        return Max;
    };
    if (Room==1)
    {
        const double D=OctExtent(P);
        return D<=OuterApothem()-Clearance && D>=InnerRadius*FMath::Cos(PI/8)+Clearance;
    }
    if (Room==0)
    {
        if (OctExtent(P)<=500*FMath::Cos(PI/8)-Clearance) return true;
        for (float A : {135.f,315.f})
        {
            const double X=FVector2D::DotProduct(P,FVector2D(Radial(A,1)));
            const double Y=FVector2D::DotProduct(P,FVector2D(Radial(A+90,1)));
            if (X>=300 && X<=InnerRadius*FMath::Cos(PI/8)-Clearance && FMath::Abs(Y)<=140-Clearance) return true;
        }
        return false;
    }
    if (Room<2 || Room>=8) return false;
    const float A=Stations()[Room-2].Face*45.f;
    const FVector2D D=P-FVector2D(Radial(A,PodCenterRadius()));
    const double X=FMath::Abs(FVector2D::DotProduct(D,FVector2D(Radial(A,1))));
    const double Y=FMath::Abs(FVector2D::DotProduct(D,FVector2D(Radial(A+90,1))));
    return X<=PodLength/2-Clearance && Y<=PodWidth/2-Clearance &&
        X+Y<=(PodLength+PodWidth)/2-90-Clearance*UE_SQRT_2;
}
int32 RoomAtPoint(FVector2D P)
{
    for (int32 I=0;I<8;++I) if (RoomContains(I,P)) return I;
    return INDEX_NONE;
}
TArray<FVector2D> Octagon(float W, float H, float C)
{
    return {{-W/2+C,-H/2},{W/2-C,-H/2},{W/2,-H/2+C},{W/2,H/2-C},
        {W/2-C,H/2},{-W/2+C,H/2},{-W/2,H/2-C},{-W/2,-H/2+C}};
}
UDynamicMeshComponent* Component(AActor* Owner, FName Name)
{
    auto* C = NewObject<UDynamicMeshComponent>(Owner,Name);
    Owner->AddInstanceComponent(C);
    C->SetupAttachment(Owner->GetRootComponent());
    C->RegisterComponent();
    Materials(C);
    return C;
}
void Materials(UDynamicMeshComponent* C)
{
    for (int32 I=0; I<MaterialCount; ++I)
    {
        const FString Path = FString::Printf(TEXT("/Game/DirectOrScut/Materials/M_Slot%d.M_Slot%d"),I,I);
        if (auto* M = LoadObject<UMaterialInterface>(nullptr,*Path))
            C->SetMaterial(I,UMaterialInstanceDynamic::Create(M,C));
    }
}
void Box(UDynamicMesh* M, FVector P, FVector S, int32 ID, float Yaw)
{
    FGeometryScriptPrimitiveOptions O; O.MaterialID=ID;
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(M,O,FTransform(FRotator(0,Yaw,0),P),S.X,S.Y,S.Z,0,0,0,EGeometryScriptPrimitiveOriginMode::Center);
}
void Prism(UDynamicMesh* M, const TArray<FVector2D>& P, float D, const FTransform& T, int32 ID)
{
    FGeometryScriptPrimitiveOptions O; O.MaterialID=ID;
    UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(M,O,T,P,D,0,true);
}
void Finish(UDynamicMeshComponent* C, bool Collision)
{
    UGeometryScriptLibrary_MeshNormalsFunctions::SetPerFaceNormals(C->GetDynamicMesh());
    C->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    C->SetCollisionResponseToAllChannels(ECR_Block);
    C->SetCollisionResponseToChannel(DoorClickChannel,ECR_Ignore);
    C->SetCollisionResponseToChannel(CrewClickChannel,ECR_Ignore);
    C->SetComplexAsSimpleCollisionEnabled(true,false);
    C->UpdateCollision();
    C->NotifyMeshUpdated();
}
}
