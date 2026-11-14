#pragma once

#include "CoreMinimal.h"

class UDynamicMesh;
class UDynamicMeshComponent;
class AActor;

namespace DirectOrScut
{
inline constexpr float InnerRadius = 980.f;
inline constexpr float OuterRadius = 1300.f;
inline constexpr float PodLength = 986.f; // Wall centerlines: 950uu clear between 18uu shells.
inline constexpr float PodWidth = 886.f;  // 850uu clear.
inline constexpr int32 MaterialCount = 9;
inline constexpr ECollisionChannel DoorClickChannel = ECC_GameTraceChannel1;
inline constexpr ECollisionChannel CrewClickChannel = ECC_GameTraceChannel2;
float OuterApothem();
float PodCenterRadius();
// Units are centimeters; every floor slab terminates at Z=0.
struct FStation
{
    const TCHAR *Name;
    int32 Face;
    int32 Accent;
};
struct FDoorSite
{
    FVector Position;
    float Yaw;
    bool bExterior = false;
    int32 RoomA = INDEX_NONE;
    int32 RoomB = INDEX_NONE;
};
const TCHAR *RoomName(int32 Room);
FVector2D RoomWaypoint(int32 Room);
FVector2D RoomStandby(int32 Room);
FVector CrewSpawnPoint(int32 Unit);
bool RoomContains(int32 Room, FVector2D Point, double Clearance = 0);
int32 RoomAtPoint(FVector2D Point);
const TArray<FStation> &Stations();
TArray<FDoorSite> DoorSites();
FVector Radial(float Degrees, float Radius);
TArray<FVector2D> Octagon(float Width, float Height, float Chamfer);
UDynamicMeshComponent *Component(AActor *Owner, FName Name);
void Materials(UDynamicMeshComponent *Component);
void Box(UDynamicMesh *Mesh, FVector Center, FVector Size, int32 Material, float Yaw = 0);
void Prism(UDynamicMesh *Mesh, const TArray<FVector2D> &Profile, float Depth, const FTransform &Transform,
           int32 Material);
void Finish(UDynamicMeshComponent *Component, bool Collision = true);
} // namespace DirectOrScut
