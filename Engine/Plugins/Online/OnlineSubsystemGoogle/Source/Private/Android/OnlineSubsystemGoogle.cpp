// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemGoogle.h"
#include "OnlineSubsystemGooglePrivate.h"
#include "OnlineIdentityGoogle.h"
#include "OnlineExternalUIInterfaceGoogle.h"

#include "Misc/ConfigCacheIni.h"

FOnlineSubsystemGoogle::FOnlineSubsystemGoogle()
{
}

FOnlineSubsystemGoogle::FOnlineSubsystemGoogle(FName InInstanceName)
	: FOnlineSubsystemGoogleCommon(InInstanceName)
{
}

FOnlineSubsystemGoogle::~FOnlineSubsystemGoogle()
{
}

bool FOnlineSubsystemGoogle::Init()
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("FOnlineSubsystemGoogle::Init()"));
	if (FOnlineSubsystemGoogleCommon::Init())
	{
		GoogleIdentity = MakeShareable(new FOnlineIdentityGoogle(this));
		GoogleExternalUI = MakeShareable(new FOnlineExternalUIGoogle(this));
		return true;
	}

	return false;
}

bool FOnlineSubsystemGoogle::Shutdown()
{
	UE_LOG(LogOnline, VeryVerbose, TEXT("FOnlineSubsystemGoogle::Shutdown()"));
	return FOnlineSubsystemGoogleCommon::Shutdown();
}
