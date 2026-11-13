#include "DirectOrScutCargoMarker.h"
#include "DirectOrScutGeometry.h"
#include "DirectOrScutSimSubsystem.h"
#include "Components/DynamicMeshComponent.h"
ADirectOrScutCargoMarker::ADirectOrScutCargoMarker()
{
    Mesh=CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Cargo")); RootComponent=Mesh;
}
void ADirectOrScutCargoMarker::BeginPlay()
{
    Super::BeginPlay(); DirectOrScut::Materials(Mesh);
    DirectOrScut::Box(Mesh->GetDynamicMesh(),FVector(0,0,20),FVector(40,40,40),3);
    DirectOrScut::Box(Mesh->GetDynamicMesh(),FVector(0,0,41),FVector(42,8,2),5);
    DirectOrScut::Finish(Mesh);
    GetWorld()->GetSubsystem<UDirectOrScutSimSubsystem>()->RegisterCargo(this,SimBodyIndex,6);
}
