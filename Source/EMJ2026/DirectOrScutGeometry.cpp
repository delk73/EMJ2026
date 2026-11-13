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
    for (const FStation& S : Stations())
        Sites.Add({Radial(S.Face*45.f,OuterApothem()), S.Face*45.f});
    for (float A : {135.f,315.f})
        Sites.Add({Radial(A,InnerRadius*FMath::Cos(PI/8)), A});
    Sites.Add({Radial(270.f,OuterApothem()+PodLength),270.f,true});
    return Sites;
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
    C->SetComplexAsSimpleCollisionEnabled(true,false);
    C->UpdateCollision();
    C->NotifyMeshUpdated();
}
}
