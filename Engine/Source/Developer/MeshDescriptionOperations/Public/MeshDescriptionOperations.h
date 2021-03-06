// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class UMeshDescription;
struct FRawMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogMeshDescriptionOperations, Log, All);

//////////////////////////////////////////////////////////////////////////
// Any operations on the mesh description that do not depend on engine module
// should be implement here.

class MESHDESCRIPTIONOPERATIONS_API FMeshDescriptionOperations
{
public:
	enum ETangentOptions
	{
		None = 0,
		BlendOverlappingNormals = 0x1,
		IgnoreDegenerateTriangles = 0x2,
		UseMikkTSpace = 0x4,
	};

	enum class ELightmapUVVersion : int32
	{
		BitByBit = 0,
		Segments = 1,
		SmallChartPacking = 2,
		Latest = SmallChartPacking
	};

	/** Convert this mesh description into the old FRawMesh format. */
	static void ConverToRawMesh(const class UMeshDescription* SourceMeshDescription, struct FRawMesh &DestinationRawMesh, const TMap<FName, int32>& MaterialMap);

	/** Convert old FRawMesh format to MeshDescription. */
	static void ConverFromRawMesh(const struct FRawMesh &SourceRawMesh, class UMeshDescription* DestinationMeshDescription, const TMap<int32, FName>& MaterialMap);

	/**
	 * Compute normal, tangent and Bi-Normal for every polygon in the mesh description. (this do not compute Vertex NTBs)
	 * It also remove the degenerated polygon from the mesh description
	 */
	static void CreatePolygonNTB(UMeshDescription* MeshDescription, float ComparisonThreshold);
	
	/** Compute normal, tangent and Bi-Normal(only if bComputeTangent is true) for every vertex in the mesh description. */
	static void CreateNormals(UMeshDescription* MeshDescription, ETangentOptions TangentOptions, bool bComputeTangent);
	
	/** Compute tangent and Bi-Normal using mikkt space for every vertex in the mesh description. */
	static void CreateMikktTangents(UMeshDescription* MeshDescription, ETangentOptions TangentOptions);

	/** Find all overlapping vertex using the threshold in the mesh description. */
	static void FindOverlappingCorners(TMultiMap<int32, int32>& OverlappingCorners, const UMeshDescription* MeshDescription, float ComparisonThreshold);

	static void CreateLightMapUVLayout(UMeshDescription* MeshDescription,
		int32 SrcLightmapIndex,
		int32 DstLightmapIndex,
		int32 MinLightmapResolution,
		ELightmapUVVersion LightmapUVVersion,
		const TMultiMap<int32, int32>& OverlappingCorners);

	/** Create some UVs from the specified mesh description data. */
	static bool GenerateUniqueUVsForStaticMesh(const UMeshDescription* MeshDescription, int32 TextureResolution, TArray<FVector2D>& OutTexCoords);

private:
	
	static void ConvertHardEdgesToSmoothGroup(const UMeshDescription* SourceMeshDescription, struct FRawMesh &DestinationRawMesh);

	static void ConvertSmoothGroupToHardEdges(const FRawMesh &SourceRawMesh, UMeshDescription* DestinationMeshDescription);
};