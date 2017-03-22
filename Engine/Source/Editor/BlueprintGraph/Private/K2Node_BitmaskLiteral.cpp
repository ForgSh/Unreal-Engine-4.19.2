// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.


#include "K2Node_BitmaskLiteral.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "EditorCategoryUtils.h"
#include "KismetCompiler.h"
#include "BlueprintEditorUtils.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "UObject/PropertyPortFlags.h"

#define LOCTEXT_NAMESPACE "UK2Node_BitmaskLiteral"

const FString& UK2Node_BitmaskLiteral::GetBitmaskInputPinName()
{
	static const FString PinName(TEXT("Bitmask"));
	return PinName;
}

UK2Node_BitmaskLiteral::UK2Node_BitmaskLiteral(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, BitflagsEnum(nullptr)
{
	
}

void UK2Node_BitmaskLiteral::ValidateBitflagsEnumType()
{
	if (BitflagsEnum != nullptr)
	{
		// Reset enum type reference if it no longer has the proper meta data.
		const FString BitflagsMetaDataKey = FBlueprintMetadata::MD_Bitflags.ToString();
		if (BitflagsEnum->IsPendingKill() || !BitflagsEnum->HasMetaData(*BitflagsMetaDataKey))
		{
			// Note: The input pin's default value is intentionally not reset here. Losing an associated enum type means the node will now expose the max
			// number of bitflags, so this will ensure that we preserve the previous default value when the enum type representing the bitflags is removed.
			BitflagsEnum = nullptr;
		}
		else
		{
			// Adjust the default value in case an entry was added or removed, or in case the enum/value mode was changed.
			int32 ValidBitflagsMask = 0;
			const int32 BitmaskBitCount = sizeof(int32) << 3;
			UEdGraphPin* InputPin = FindPinChecked(GetBitmaskInputPinName());
			int32 OldDefaultValue = FCString::Atoi(*InputPin->DefaultValue);
			const bool bUseEnumValuesAsMaskValues = BitflagsEnum->GetBoolMetaData(FBlueprintMetadata::MD_UseEnumValuesAsMaskValuesInEditor);

			// Note: This loop is not inclusive of (BitflagsEnum->NumEnums() - 1) in order to exclude the implicit "MAX" value that gets added to the enum type at compile time.
			for (int32 BitflagsEnumIndex = 0; BitflagsEnumIndex < BitflagsEnum->NumEnums() - 1; ++BitflagsEnumIndex)
			{
				const int64 EnumValue = BitflagsEnum->GetValueByIndex(BitflagsEnumIndex);
				if (EnumValue >= 0)
				{
					if (bUseEnumValuesAsMaskValues)
					{
						if (EnumValue < MAX_int32 && FMath::IsPowerOfTwo(EnumValue))
						{
							ValidBitflagsMask |= EnumValue;
						}
					}
					else if (EnumValue < BitmaskBitCount)
					{
						ValidBitflagsMask |= 1 << static_cast<int32>(EnumValue);
					}
				}
			}

			const int32 NewDefaultValue = OldDefaultValue & ValidBitflagsMask;
			if (NewDefaultValue != OldDefaultValue)
			{
				InputPin->GetSchema()->TrySetDefaultValue(*InputPin, FString::FromInt(NewDefaultValue));
			}
		}
	}
}

void UK2Node_BitmaskLiteral::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// Post-load validation of the enum type.
	if (Ar.IsLoading() && Ar.IsPersistent() && !Ar.HasAnyPortFlags(PPF_Duplicate | PPF_DuplicateForPIE))
	{
		ValidateBitflagsEnumType();
	}
}

void UK2Node_BitmaskLiteral::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* InputPin = CreatePin(EGPD_Input, Schema->PC_Int, Schema->PSC_Bitmask, BitflagsEnum, false, false, GetBitmaskInputPinName());
	Schema->SetPinDefaultValueBasedOnType(InputPin);

	CreatePin(EGPD_Output, Schema->PC_Int, Schema->PSC_Bitmask, BitflagsEnum, false, false, Schema->PN_ReturnValue);

	Super::AllocateDefaultPins();
}

void UK2Node_BitmaskLiteral::ReconstructNode()
{
	// Validate the enum type prior to reconstruction so that the input pin's default value is reset first (if needed).
	ValidateBitflagsEnumType();

	Super::ReconstructNode();
}

void UK2Node_BitmaskLiteral::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	check(nullptr != Schema);

	const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt);
	UK2Node_CallFunction* MakeLiteralInt = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	MakeLiteralInt->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(FunctionName));
	MakeLiteralInt->AllocateDefaultPins();

	UEdGraphPin* OrgInputPin = FindPinChecked(GetBitmaskInputPinName());
	UEdGraphPin* NewInputPin = MakeLiteralInt->FindPinChecked(TEXT("Value"));
	check(nullptr != NewInputPin);
	CompilerContext.MovePinLinksToIntermediate(*OrgInputPin, *NewInputPin);

	UEdGraphPin* OrgReturnPin = FindPinChecked(Schema->PN_ReturnValue);
	UEdGraphPin* NewReturnPin = MakeLiteralInt->GetReturnValuePin();
	check(nullptr != NewReturnPin);
	CompilerContext.MovePinLinksToIntermediate(*OrgReturnPin, *NewReturnPin);
}

FText UK2Node_BitmaskLiteral::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Make Bitmask");
}

void UK2Node_BitmaskLiteral::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_BitmaskLiteral::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Math);
}

#undef LOCTEXT_NAMESPACE
