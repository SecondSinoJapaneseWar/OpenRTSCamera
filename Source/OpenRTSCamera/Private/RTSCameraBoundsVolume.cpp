// Copyright 2024 Jesus Bracho All Rights Reserved.

#include "RTSCameraBoundsVolume.h"
#include "Components/PrimitiveComponent.h"

ARTSCameraBoundsVolume::ARTSCameraBoundsVolume()
{
	// Default constructor
}

void ARTSCameraBoundsVolume::BeginPlay()
{
	Super::BeginPlay();
	Tags.AddUnique(FName("OpenRTSCamera#CameraBounds"));
}
