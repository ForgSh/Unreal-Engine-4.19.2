// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "ARPin.h"
#include "ARSystem.h"
#include "ARDebugDrawHelpers.h"
#include "DrawDebugHelpers.h"
#include "Components/SceneComponent.h"

//
//
//

uint32 UARPin::DebugPinId = 0;

void UARPin::InitARPin( const TSharedRef<FARSystemBase, ESPMode::ThreadSafe>& InTrackingSystemOwner, USceneComponent* InComponentToPin, const FTransform& InLocalToTrackingTransform, UARTrackedGeometry* InTrackedGeometry, const FName InDebugName )
{
	ARSystem = InTrackingSystemOwner;
	LocalToTrackingTransform = InLocalToTrackingTransform;
	TrackingState = EARTrackingState::Tracking;
	TrackedGeometry = InTrackedGeometry;
	PinnedComponent = InComponentToPin;
	DebugName = (InDebugName != NAME_None)
	? InDebugName
	: FName(*FString::Printf(TEXT("PIN %02d"), DebugPinId++));
}

FTransform UARPin::GetLocalToTrackingTransform() const
{
	return LocalToTrackingTransform;
}

FTransform UARPin::GetLocalToWorldTransform() const
{
	return LocalToTrackingTransform * GetARSystem()->GetTrackingToWorldTransform();
}

EARTrackingState UARPin::GetTrackingState() const
{
	return TrackingState;
}

UARTrackedGeometry* UARPin::GetTrackedGeometry() const
{
	return TrackedGeometry;
}

USceneComponent* UARPin::GetPinnedComponent() const
{
	return PinnedComponent;
}

void UARPin::OnTrackingStateChanged(EARTrackingState NewTrackingState)
{
	if (NewTrackingState == EARTrackingState::StoppedTracking)
	{
		TrackedGeometry = nullptr;
		DebugName = FName( *FString::Printf(TEXT("%s [ORPHAN]"), *DebugName.ToString()) );
	}
	
	TrackingState = NewTrackingState;
	OnARTrackingStateChanged.Broadcast(NewTrackingState);
}

void UARPin::OnTransformUpdated(const FTransform& TrackedGeometry_OldLocalToTrackingTransform, const FTransform& TrackedGeometry_NewLocalToTrackingTransform)
{
	// We have an existing TrackedGeometry LocalToTracking and a TrackedGeometry NewLocalToTracking.
	// We want to find TrackedGeometry's TrackingToNewTracking. It represents the change in transforms.
	// Then we apply this change to the ARPin: that we get ARPin.LocalToTracking *= TrackingToNewTracking;
	FTransform TrackingToNewTracking = TrackedGeometry_OldLocalToTrackingTransform.GetRelativeTransformReverse(TrackedGeometry_NewLocalToTrackingTransform);
	
	const FTransform OldLocalToWorldTransform = GetLocalToWorldTransform();
	LocalToTrackingTransform *= TrackingToNewTracking;
	const FTransform NewLocalToWorldTransform = GetLocalToWorldTransform();
	
	// Move the component to match the Pin's new location
	if (PinnedComponent)
	{
		PinnedComponent->SetWorldTransform(NewLocalToWorldTransform);
	}
	
	// Notify any subscribes that the Pin moved
	OnARTransformUpdated.Broadcast(TrackingToNewTracking);
}

void UARPin::OnTransformUpdated(const FTransform& NewLocalToTrackingTransform)
{
	FTransform TrackingToNewTracking = LocalToTrackingTransform.GetRelativeTransformReverse(NewLocalToTrackingTransform);
	LocalToTrackingTransform = NewLocalToTrackingTransform;

	const FTransform NewLocalToWorldTransform = GetLocalToWorldTransform();

	// Move the component to match the Pin's new location
	if (PinnedComponent)
	{
		PinnedComponent->SetWorldTransform(NewLocalToWorldTransform);
	}

	// Notify any subscribes that the Pin moved
	OnARTransformUpdated.Broadcast(TrackingToNewTracking);
}

void UARPin::DebugDraw( UWorld* World, const FLinearColor& Color, float Scale, float PersistForSeconds) const
{
	const FTransform LocalToWorldTransform = GetLocalToWorldTransform();
	const FVector Location_WorldSpace = LocalToWorldTransform.GetLocation();
	DrawDebugCrosshairs( World, Location_WorldSpace, FRotator(LocalToWorldTransform.GetRotation()), Scale, Color.ToFColor(false), (PersistForSeconds > 0.0f), PersistForSeconds );
	
	const FName MyDebugName = GetDebugName();
	const FString CurAnchorDebugName = MyDebugName.ToString();
	ARDebugHelpers::DrawDebugString( World, Location_WorldSpace, CurAnchorDebugName, 0.25f*Scale, Color.ToFColor(false), PersistForSeconds, true);
}

FName UARPin::GetDebugName() const
{
	return DebugName;
}

void UARPin::SetOnARTrackingStateChanged( const FOnARTrackingStateChanged& InHandler )
{
	OnARTrackingStateChanged = InHandler;
}

void UARPin::SetOnARTransformUpdated( const FOnARTransformUpdated& InHandler )
{
	OnARTransformUpdated = InHandler;
}

TSharedPtr<FARSystemBase, ESPMode::ThreadSafe> UARPin::GetARSystem() const
{
	auto MyARSystem = ARSystem.Pin();
	return MyARSystem;
}
