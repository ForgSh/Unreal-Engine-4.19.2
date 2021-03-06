// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "MeshAttributes.h"
#include "MeshDescriptionHelper.h"
#include "BuildOptimizationHelper.h"
#include "Components.h"
#include "IMeshReductionManagerModule.h"
#include "MeshBuild.h"

DEFINE_LOG_CATEGORY(LogStaticMeshBuilder);

//////////////////////////////////////////////////////////////////////////
//Local functions definition
void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, int32 LodIndex
	, UMeshDescription* MeshDescription
	, FStaticMeshLODResources& StaticMeshLOD
	, const FMeshBuildSettings& LODBuildSettings
	, TArray< uint32 >& IndexBuffer
	, TArray<int32>& OutWedgeMap
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const TMultiMap<int32, int32>& OverlappingCorners
	, float VertexComparisonThreshold
	, TArray<int32>& RemapVerts);
void BuildAllBufferOptimizations(struct FStaticMeshLODResources& StaticMeshLOD, const struct FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices);
//////////////////////////////////////////////////////////////////////////

FStaticMeshBuilder::FStaticMeshBuilder()
{

}

bool FStaticMeshBuilder::Build(FStaticMeshRenderData& StaticMeshRenderData, UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup)
{
	if (StaticMesh->GetOriginalMeshDescription(0) == nullptr)
	{
		//Warn the user that there is no mesh description data
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot find a valid mesh description to build the asset."));
		return false;
	}
	if (StaticMeshRenderData.LODResources.Num() > 0)
	{
		//At this point the render data is suppose to be empty
		UE_LOG(LogStaticMeshBuilder, Error, TEXT("Cannot build static mesh render data twice [%s]."), *StaticMesh->GetFullName());
		
		//Crash in debug
		checkSlow(StaticMeshRenderData.LODResources.Num() == 0);

		return false;
	}
	StaticMeshRenderData.AllocateLODResources(StaticMesh->SourceModels.Num());

	TArray<UMeshDescription*> MeshDescriptions;
	auto GetMeshDescription = [&MeshDescriptions](int32 ArrayIndex)->UMeshDescription*
	{
		if (!MeshDescriptions.IsValidIndex(ArrayIndex))
		{
			return nullptr;
		}
		return MeshDescriptions[ArrayIndex];
	};
	auto SetMeshDescription = [&MeshDescriptions](int32 ArrayIndex, UMeshDescription* InMeshDescription)
	{
		if (!MeshDescriptions.IsValidIndex(ArrayIndex) && ArrayIndex >= 0)
		{
			//Add nullptr missing entries
			MeshDescriptions.AddZeroed(ArrayIndex - MeshDescriptions.Num() + 1);
		}
		//Set the original mesh description
		MeshDescriptions[ArrayIndex] = InMeshDescription;
	};

	for (int32 LodIndex = 0; LodIndex < StaticMesh->SourceModels.Num(); ++LodIndex)
	{
		FMeshBuildSettings& LODBuildSettings = StaticMesh->SourceModels[LodIndex].BuildSettings;
		UMeshDescription* OriginalMeshDescription = StaticMesh->GetOriginalMeshDescription(LodIndex);
		FMeshDescriptionHelper MeshDescriptionHelper(&LODBuildSettings, OriginalMeshDescription);
		
		const FStaticMeshSourceModel& SrcModel = StaticMesh->SourceModels[LodIndex];
		FMeshReductionSettings ReductionSettings = LODGroup.GetSettings(SrcModel.ReductionSettings, LodIndex);

		UMeshDescription* MeshDescription = nullptr;
		

		bool bUseReduction = (ReductionSettings.PercentTriangles < 1.0f || ReductionSettings.MaxDeviation > 0.0f) && (GetMeshDescription(0) != nullptr);

		if (OriginalMeshDescription != nullptr)
		{
			MeshDescription = MeshDescriptionHelper.GetRenderMeshDescription(StaticMesh);
		}
		
		if(MeshDescription == nullptr)
		{
			int32 BaseReduceLodIndex = 0;
			if (bUseReduction)
			{
				//Create an empty mesh description that the reduce will fill
				MeshDescription = NewObject<UMeshDescription>(StaticMesh, NAME_None);
				UStaticMesh::RegisterMeshAttributes(MeshDescription);
			}
			else if(GetMeshDescription(BaseReduceLodIndex) != nullptr)
			{
				//Duplicate the lodindex 0 we have a 100% reduction which is like a duplicate
				MeshDescription = Cast<UMeshDescription>(StaticDuplicateObject(GetMeshDescription(BaseReduceLodIndex), StaticMesh, NAME_None, RF_NoFlags));
			}
			if (LodIndex > 0)
			{
				LODBuildSettings = StaticMesh->SourceModels[BaseReduceLodIndex].BuildSettings;
				//Make sure the SectionInfoMap is taken from the Base RawMesh
				int32 SectionNumber = StaticMesh->OriginalSectionInfoMap.GetSectionNumber(BaseReduceLodIndex);
				for (int32 SectionIndex = 0; SectionIndex < SectionNumber; ++SectionIndex)
				{
					FMeshSectionInfo Info = StaticMesh->OriginalSectionInfoMap.Get(BaseReduceLodIndex, SectionIndex);
					StaticMesh->SectionInfoMap.Set(LodIndex, SectionIndex, Info);
					StaticMesh->OriginalSectionInfoMap.Set(LodIndex, SectionIndex, Info);
				}
			}
		}

		check(MeshDescription != nullptr);
		SetMeshDescription(LodIndex, MeshDescription);
		
		//Reduce LODs
		if (bUseReduction)
		{
			int32 BaseLODIndex = 0;
			float OverlappingThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
			TMultiMap<int32, int32> OverlappingCorners;
			FMeshDescriptionOperations::FindOverlappingCorners(OverlappingCorners, GetMeshDescription(BaseLODIndex), OverlappingThreshold);

			//Create a new destination mesh in case we reduce ourself
			UMeshDescription* MeshDescriptionReduced = NewObject<UMeshDescription>(StaticMesh, NAME_None);
			UStaticMesh::RegisterMeshAttributes(MeshDescriptionReduced);
			MeshDescriptionHelper.ReduceLOD(GetMeshDescription(BaseLODIndex), MeshDescriptionReduced, ReductionSettings, OverlappingCorners);
			SetMeshDescription(LodIndex, MeshDescriptionReduced);
			MeshDescription = MeshDescriptionReduced;

			if (MeshDescriptionReduced != nullptr)
			{
				const TPolygonGroupAttributeArray<FName>& PolygonGroupImportedMaterialSlotNames = MeshDescriptionReduced->PolygonGroupAttributes().GetAttributes<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
				// Recompute adjacency information. Since we change the vertices when we reduce
				MeshDescriptionHelper.FindOverlappingCorners(MeshDescriptionReduced, OverlappingThreshold);
			
				//Make sure the static mesh SectionInfoMap is up to date with the new reduce LOD
				//We have to remap the material index with the ReductionSettings.BaseLODModel sectionInfoMap
			
				//Set the new SectionInfoMap for this reduced LOD base on the ReductionSettings.BaseLODModel SectionInfoMap
				const FMeshSectionInfoMap& LODModelSectionInfoMap = StaticMesh->SectionInfoMap;
				TArray<int32> UniqueMaterialIndex;
				//Find all unique Material in used order
				for (const FPolygonGroupID& PolygonGroupID : MeshDescriptionReduced->PolygonGroups().GetElementIDs())
				{
					int32 MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
					if (MaterialIndex == INDEX_NONE)
					{
						MaterialIndex = PolygonGroupID.GetValue();
					}
					UniqueMaterialIndex.AddUnique(MaterialIndex);
				}
				//All used material represent a different section
				for (int32 SectionIndex = 0; SectionIndex < UniqueMaterialIndex.Num(); ++SectionIndex)
				{
					//Section material index have to be remap with the ReductionSettings.BaseLODModel SectionInfoMap to create
					//a valid new section info map for the reduced LOD.
					if (LODModelSectionInfoMap.IsValidSection(ReductionSettings.BaseLODModel, UniqueMaterialIndex[SectionIndex]))
					{
						FMeshSectionInfo SectionInfo = LODModelSectionInfoMap.Get(ReductionSettings.BaseLODModel, UniqueMaterialIndex[SectionIndex]);
						//Try to recuperate the valid data
						if (LODModelSectionInfoMap.IsValidSection(LodIndex, SectionIndex))
						{
							//If the old LOD section was using the same Material copy the data
							FMeshSectionInfo OriginalLODSectionInfo = LODModelSectionInfoMap.Get(LodIndex, SectionIndex);
							if (OriginalLODSectionInfo.MaterialIndex == SectionInfo.MaterialIndex)
							{
								SectionInfo.bCastShadow = OriginalLODSectionInfo.bCastShadow;
								SectionInfo.bEnableCollision = OriginalLODSectionInfo.bEnableCollision;
							}
						}
						//Copy the BaseLODModel section info to the reduce LODIndex.
						StaticMesh->SectionInfoMap.Set(LodIndex, SectionIndex, SectionInfo);
					}
				}
			}
		}

		const FPolygonGroupArray& PolygonGroups = MeshDescription->PolygonGroups();

		FStaticMeshLODResources& StaticMeshLOD = StaticMeshRenderData.LODResources[LodIndex];

		//discover degenerate triangle with this threshold
		float VertexComparisonThreshold = LODBuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;

		//Build new vertex buffers
		TArray< FStaticMeshBuildVertex > StaticMeshBuildVertices;

		TArray< uint32 > IndexBuffer;

		StaticMeshLOD.Sections.Empty(PolygonGroups.Num());
		TArray<int32> RemapVerts; //Because we will remove MeshVertex that are redundant, we need a remap
								  //Render data Wedge map is only set for LOD 0???
		TArray<int32> TempWedgeMap;
		TArray<int32> &WedgeMap = (LodIndex == 0) ? StaticMeshRenderData.WedgeMap : TempWedgeMap;

		//Prepare the PerSectionIndices array so we can optimize the index buffer for the GPU
		TArray<TArray<uint32> > PerSectionIndices;
		PerSectionIndices.AddDefaulted(MeshDescription->PolygonGroups().Num());

		//Build the vertex and index buffer
		BuildVertexBuffer(StaticMesh, LodIndex, MeshDescription, StaticMeshLOD, LODBuildSettings, IndexBuffer, WedgeMap, PerSectionIndices, StaticMeshBuildVertices, MeshDescriptionHelper.GetOverlappingCorners(), VertexComparisonThreshold, RemapVerts);

		// Concatenate the per-section index buffers.
		TArray<uint32> CombinedIndices;
		bool bNeeds32BitIndices = false;
		for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); SectionIndex++)
		{
			FStaticMeshSection& Section = StaticMeshLOD.Sections[SectionIndex];
			TArray<uint32> const& SectionIndices = PerSectionIndices[SectionIndex];
			Section.FirstIndex = 0;
			Section.NumTriangles = 0;
			Section.MinVertexIndex = 0;
			Section.MaxVertexIndex = 0;

			if (SectionIndices.Num())
			{
				Section.FirstIndex = CombinedIndices.Num();
				Section.NumTriangles = SectionIndices.Num() / 3;

				CombinedIndices.AddUninitialized(SectionIndices.Num());
				uint32* DestPtr = &CombinedIndices[Section.FirstIndex];
				uint32 const* SrcPtr = SectionIndices.GetData();

				Section.MinVertexIndex = *SrcPtr;
				Section.MaxVertexIndex = *SrcPtr;

				for (int32 Index = 0; Index < SectionIndices.Num(); Index++)
				{
					uint32 VertIndex = *SrcPtr++;

					bNeeds32BitIndices |= (VertIndex > MAX_uint16);
					Section.MinVertexIndex = FMath::Min<uint32>(VertIndex, Section.MinVertexIndex);
					Section.MaxVertexIndex = FMath::Max<uint32>(VertIndex, Section.MaxVertexIndex);
					*DestPtr++ = VertIndex;
				}
			}
		}

		const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;
		StaticMeshLOD.IndexBuffer.SetIndices(CombinedIndices, IndexBufferStride);

		BuildAllBufferOptimizations(StaticMeshLOD, LODBuildSettings, CombinedIndices, bNeeds32BitIndices, StaticMeshBuildVertices);
	} //End of LOD for loop

	// Calculate the bounding box.
	FBox BoundingBox(ForceInit);
	FPositionVertexBuffer& BasePositionVertexBuffer = StaticMeshRenderData.LODResources[0].VertexBuffers.PositionVertexBuffer;
	for (uint32 VertexIndex = 0; VertexIndex < BasePositionVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		BoundingBox += BasePositionVertexBuffer.VertexPosition(VertexIndex);
	}
	BoundingBox.GetCenterAndExtents(StaticMeshRenderData.Bounds.Origin, StaticMeshRenderData.Bounds.BoxExtent);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	StaticMeshRenderData.Bounds.SphereRadius = 0.0f;
	for (uint32 VertexIndex = 0; VertexIndex < BasePositionVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		StaticMeshRenderData.Bounds.SphereRadius = FMath::Max(
			(BasePositionVertexBuffer.VertexPosition(VertexIndex) - StaticMeshRenderData.Bounds.Origin).Size(),
			StaticMeshRenderData.Bounds.SphereRadius
		);
	}

	return true;
}

bool AreVerticesEqual(FStaticMeshBuildVertex const& A, FStaticMeshBuildVertex const& B, float ComparisonThreshold)
{
	if (   !A.Position.Equals(B.Position, ComparisonThreshold)
		|| !NormalsEqual(A.TangentX, B.TangentX)
		|| !NormalsEqual(A.TangentY, B.TangentY)
		|| !NormalsEqual(A.TangentZ, B.TangentZ)
		|| A.Color != B.Color)
	{
		return false;
	}

	// UVs
	for (int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; UVIndex++)
	{
		if (!UVsEqual(A.UVs[UVIndex], B.UVs[UVIndex]))
		{
			return false;
		}
	}

	return true;
}

void BuildVertexBuffer(
	  UStaticMesh *StaticMesh
	, int32 LodIndex
	, UMeshDescription* MeshDescription
	, FStaticMeshLODResources& StaticMeshLOD
	, const FMeshBuildSettings& LODBuildSettings
	, TArray< uint32 >& IndexBuffer
	, TArray<int32>& OutWedgeMap
	, TArray<TArray<uint32> >& OutPerSectionIndices
	, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices
	, const TMultiMap<int32, int32>& OverlappingCorners
	, float VertexComparisonThreshold
	, TArray<int32>& RemapVerts)
{
	FVertexArray& Vertices = MeshDescription->Vertices();
	FVertexInstanceArray& VertexInstances = MeshDescription->VertexInstances();
	FPolygonGroupArray& PolygonGroupArray = MeshDescription->PolygonGroups();
	FPolygonArray& PolygonArray = MeshDescription->Polygons();
	
	OutWedgeMap.Reset();
	OutWedgeMap.AddZeroed(VertexInstances.Num());

	TArray<int32> RemapVertexInstanceID;
	// set up vertex buffer elements
	StaticMeshBuildVertices.Reserve(VertexInstances.Num());
	bool bHasColor = false;
	//Fill the remap array
	RemapVerts.AddZeroed(VertexInstances.Num());
	for (int32& RemapIndex : RemapVerts)
	{
		RemapIndex = INDEX_NONE;
	}
	TArray<int32> DupVerts;

	const uint32 NumTextureCoord = MeshDescription->VertexInstanceAttributes().GetAttributeIndexCount<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	const TPolygonGroupAttributeArray<FName>& PolygonGroupImportedMaterialSlotNames = MeshDescription->PolygonGroupAttributes().GetAttributes<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	const TVertexAttributeArray<FVector>& VertexPositions = MeshDescription->VertexAttributes().GetAttributes<FVector>( MeshAttribute::Vertex::Position );
	const TVertexInstanceAttributeArray<FVector>& VertexInstanceNormals = MeshDescription->VertexInstanceAttributes().GetAttributes<FVector>( MeshAttribute::VertexInstance::Normal );
	const TVertexInstanceAttributeArray<FVector>& VertexInstanceTangents = MeshDescription->VertexInstanceAttributes().GetAttributes<FVector>( MeshAttribute::VertexInstance::Tangent );
	const TVertexInstanceAttributeArray<float>& VertexInstanceBinormalSigns = MeshDescription->VertexInstanceAttributes().GetAttributes<float>( MeshAttribute::VertexInstance::BinormalSign );
	const TVertexInstanceAttributeArray<FVector4>& VertexInstanceColors = MeshDescription->VertexInstanceAttributes().GetAttributes<FVector4>( MeshAttribute::VertexInstance::Color );
	const TVertexInstanceAttributeIndicesArray<FVector2D>& VertexInstanceUVs = MeshDescription->VertexInstanceAttributes().GetAttributesSet<FVector2D>( MeshAttribute::VertexInstance::TextureCoordinate );

	TMap<FPolygonGroupID, int32> PolygonGroupToSectionIndex;
	
	
	for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
	{
		int32& SectionIndex = PolygonGroupToSectionIndex.FindOrAdd(PolygonGroupID);
		SectionIndex = StaticMeshLOD.Sections.Add(FStaticMeshSection());
		FStaticMeshSection& StaticMeshSection = StaticMeshLOD.Sections[SectionIndex];
		StaticMeshSection.MaterialIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(PolygonGroupImportedMaterialSlotNames[PolygonGroupID]);
		if (StaticMeshSection.MaterialIndex == INDEX_NONE)
		{
			StaticMeshSection.MaterialIndex = PolygonGroupID.GetValue();
		}
	}

	int32 ReserveIndicesCount = 0;
	for (const FPolygonID& PolygonID : MeshDescription->Polygons().GetElementIDs())
	{
		const TArray<FMeshTriangle>& PolygonTriangles = MeshDescription->GetPolygonTriangles(PolygonID);
		ReserveIndicesCount += PolygonTriangles.Num() * 3;
	}
	IndexBuffer.Reset(ReserveIndicesCount);

	for (const FPolygonID& PolygonID : MeshDescription->Polygons().GetElementIDs())
	{
		const FPolygonGroupID PolygonGroupID = MeshDescription->GetPolygonPolygonGroup(PolygonID);
		const int32 SectionIndex = PolygonGroupToSectionIndex[PolygonGroupID];
		TArray<uint32>& SectionIndices = OutPerSectionIndices[SectionIndex];

		const TArray<FMeshTriangle>& PolygonTriangles = MeshDescription->GetPolygonTriangles(PolygonID);
		uint32 MinIndex = TNumericLimits< uint32 >::Max();
		uint32 MaxIndex = TNumericLimits< uint32 >::Min();
		for (int32 TriangleIndex = 0; TriangleIndex < PolygonTriangles.Num(); ++TriangleIndex)
		{
			const FMeshTriangle& Triangle = PolygonTriangles[TriangleIndex];

			FVector CornerPositions[3];
			for (int32 TriVert = 0; TriVert < 3; ++TriVert)
			{
				const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(TriVert);
				const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);
				CornerPositions[TriVert] = VertexPositions[VertexID];
			}
			FOverlappingThresholds OverlappingThresholds;
			OverlappingThresholds.ThresholdPosition = VertexComparisonThreshold;
			// Don't process degenerate triangles.
			if (PointsEqual(CornerPositions[0], CornerPositions[1], OverlappingThresholds)
				|| PointsEqual(CornerPositions[0], CornerPositions[2], OverlappingThresholds)
				|| PointsEqual(CornerPositions[1], CornerPositions[2], OverlappingThresholds))
			{
				continue;
			}

			for (int32 TriVert = 0; TriVert < 3; ++TriVert)
			{
				const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(TriVert);
				const int32 VertexInstanceValue = VertexInstanceID.GetValue();
				const FVector& VertexPosition = CornerPositions[TriVert];
				const FVector& VertexInstanceNormal = VertexInstanceNormals[VertexInstanceID];
				const FVector& VertexInstanceTangent = VertexInstanceTangents[VertexInstanceID];
				const float VertexInstanceBinormalSign = VertexInstanceBinormalSigns[VertexInstanceID];
				const FVector4& VertexInstanceColor = VertexInstanceColors[VertexInstanceID];

				const FLinearColor LinearColor(VertexInstanceColor);
				if (LinearColor != FLinearColor::White)
				{
					bHasColor = true;
				}

				FStaticMeshBuildVertex StaticMeshVertex;

				StaticMeshVertex.Position = VertexPosition * LODBuildSettings.BuildScale3D;
				const FMatrix ScaleMatrix = FScaleMatrix(LODBuildSettings.BuildScale3D).Inverse().GetTransposed();
				StaticMeshVertex.TangentX = ScaleMatrix.TransformVector(VertexInstanceTangent).GetSafeNormal();
				StaticMeshVertex.TangentY = ScaleMatrix.TransformVector(FVector::CrossProduct(VertexInstanceNormal, VertexInstanceTangent).GetSafeNormal() * VertexInstanceBinormalSign).GetSafeNormal();
				StaticMeshVertex.TangentZ = ScaleMatrix.TransformVector(VertexInstanceNormal).GetSafeNormal();
				StaticMeshVertex.Color = LinearColor.ToFColor(true);
				const uint32 MaxNumTexCoords = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, MAX_STATIC_TEXCOORDS);
				for (uint32 UVIndex = 0; UVIndex < MaxNumTexCoords; ++UVIndex)
				{
					if(UVIndex < NumTextureCoord)
					{
						StaticMeshVertex.UVs[UVIndex] = VertexInstanceUVs.GetArrayForIndex(UVIndex)[VertexInstanceID];
					}
					else
					{
						StaticMeshVertex.UVs[UVIndex] = FVector2D(0.0f, 0.0f);
					}
				}
					

				//Never add duplicated vertex instance
				DupVerts.Reset();
				OverlappingCorners.MultiFind(VertexInstanceValue, DupVerts);
				DupVerts.Sort();
				int32 Index = INDEX_NONE;
				for (int32 k = 0; k < DupVerts.Num(); k++)
				{
					if (DupVerts[k] >= VertexInstanceValue)
					{
						break;
					}
					int32 Location = RemapVerts.IsValidIndex(DupVerts[k]) ? RemapVerts[DupVerts[k]] : INDEX_NONE;
					if (Location != INDEX_NONE && AreVerticesEqual(StaticMeshVertex, StaticMeshBuildVertices[Location], VertexComparisonThreshold))
					{
						Index = Location;
						break;
					}
				}
				if (Index == INDEX_NONE)
				{
					Index = StaticMeshBuildVertices.Add(StaticMeshVertex);
				}
				RemapVerts[VertexInstanceValue] = Index;
				const uint32 RenderingVertexIndex = RemapVerts[VertexInstanceValue];
				IndexBuffer.Add(RenderingVertexIndex);
				OutWedgeMap[VertexInstanceValue] = RenderingVertexIndex;
				SectionIndices.Add(RenderingVertexIndex);
			}
		}
	}


	//Optimize before setting the buffer
	if (VertexInstances.Num() < 100000 * 3)
	{
		BuildOptimizationHelper::CacheOptimizeVertexAndIndexBuffer(StaticMeshBuildVertices, OutPerSectionIndices, OutWedgeMap);
		check(OutWedgeMap.Num() == MeshDescription->VertexInstances().Num());
	}

	StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(LODBuildSettings.bUseHighPrecisionTangentBasis);
	StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(LODBuildSettings.bUseFullPrecisionUVs);
	StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer.Init(StaticMeshBuildVertices, NumTextureCoord);
	StaticMeshLOD.VertexBuffers.PositionVertexBuffer.Init(StaticMeshBuildVertices);
	StaticMeshLOD.VertexBuffers.ColorVertexBuffer.Init(StaticMeshBuildVertices);
}

void BuildAllBufferOptimizations(FStaticMeshLODResources& StaticMeshLOD, const FMeshBuildSettings& LODBuildSettings, TArray< uint32 >& IndexBuffer, bool bNeeds32BitIndices, TArray< FStaticMeshBuildVertex >& StaticMeshBuildVertices)
{
	const EIndexBufferStride::Type IndexBufferStride = bNeeds32BitIndices ? EIndexBufferStride::Force32Bit : EIndexBufferStride::Force16Bit;

	// Build the reversed index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> InversedIndices;
		const int32 IndexCount = IndexBuffer.Num();
		InversedIndices.AddUninitialized(IndexCount);

		for (int32 SectionIndex = 0; SectionIndex < StaticMeshLOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& SectionInfo = StaticMeshLOD.Sections[SectionIndex];
			const int32 SectionIndexCount = SectionInfo.NumTriangles * 3;

			for (int32 i = 0; i < SectionIndexCount; ++i)
			{
				InversedIndices[SectionInfo.FirstIndex + i] = IndexBuffer[SectionInfo.FirstIndex + SectionIndexCount - 1 - i];
			}
		}
		StaticMeshLOD.ReversedIndexBuffer.SetIndices(InversedIndices, IndexBufferStride);
	}

	// Build the depth-only index buffer.
	TArray<uint32> DepthOnlyIndices;
	{
		BuildOptimizationHelper::BuildDepthOnlyIndexBuffer(
			DepthOnlyIndices,
			StaticMeshBuildVertices,
			IndexBuffer,
			StaticMeshLOD.Sections
		);

		if (DepthOnlyIndices.Num() < 50000 * 3)
		{
			BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(DepthOnlyIndices);
		}

		StaticMeshLOD.DepthOnlyIndexBuffer.SetIndices(DepthOnlyIndices, IndexBufferStride);
	}

	// Build the inversed depth only index buffer.
	if (LODBuildSettings.bBuildReversedIndexBuffer)
	{
		TArray<uint32> ReversedDepthOnlyIndices;
		const int32 IndexCount = DepthOnlyIndices.Num();
		ReversedDepthOnlyIndices.AddUninitialized(IndexCount);
		for (int32 i = 0; i < IndexCount; ++i)
		{
			ReversedDepthOnlyIndices[i] = DepthOnlyIndices[IndexCount - 1 - i];
		}
		StaticMeshLOD.ReversedDepthOnlyIndexBuffer.SetIndices(ReversedDepthOnlyIndices, IndexBufferStride);
	}

	// Build a list of wireframe edges in the static mesh.
	{
		TArray<BuildOptimizationHelper::FMeshEdge> Edges;
		TArray<uint32> WireframeIndices;

		BuildOptimizationHelper::FStaticMeshEdgeBuilder(IndexBuffer, StaticMeshBuildVertices, Edges).FindEdges();
		WireframeIndices.Empty(2 * Edges.Num());
		for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); EdgeIndex++)
		{
			BuildOptimizationHelper::FMeshEdge&	Edge = Edges[EdgeIndex];
			WireframeIndices.Add(Edge.Vertices[0]);
			WireframeIndices.Add(Edge.Vertices[1]);
		}
		StaticMeshLOD.WireframeIndexBuffer.SetIndices(WireframeIndices, IndexBufferStride);
	}

	// Build the adjacency index buffer used for tessellation.
	if (LODBuildSettings.bBuildAdjacencyBuffer)
	{
		TArray<uint32> AdjacencyIndices;

		BuildOptimizationThirdParty::NvTriStripHelper::BuildStaticAdjacencyIndexBuffer(
			StaticMeshLOD.VertexBuffers.PositionVertexBuffer,
			StaticMeshLOD.VertexBuffers.StaticMeshVertexBuffer,
			IndexBuffer,
			AdjacencyIndices
		);
		StaticMeshLOD.AdjacencyIndexBuffer.SetIndices(AdjacencyIndices, IndexBufferStride);
	}
}
