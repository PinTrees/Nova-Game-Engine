#include "pch.h"
#include "Octree.h"
#include "MathHelper.h"

Octree::Octree()
{
}

Octree::~Octree()
{
	
}

void Octree::Build(const std::vector<XMFLOAT3>& vertices, const std::vector<uint32>& indices)
{
	// Cache a copy of the vertices.
	_vertices = vertices;

	// Build AABB to contain the scene mesh.
	BoundingBox sceneBounds = BuildAABB();

	// Allocate the root node and set its AABB to contain the scene mesh.
	_root = make_shared<OctreeNode>();
	_root->Bounds = sceneBounds;

	BuildOctree(_root, indices);
}

bool Octree::RayOctreeIntersect(FXMVECTOR rayPos, FXMVECTOR rayDir)
{
	return RayOctreeIntersect(_root, rayPos, rayDir);
}

BoundingBox Octree::BuildAABB()
{
	XMVECTOR vmin = ::XMVectorReplicate(+MathHelper::Infinity);
	XMVECTOR vmax = ::XMVectorReplicate(-MathHelper::Infinity);

	for (size_t i = 0; i < _vertices.size(); ++i)
	{
		XMVECTOR P = ::XMLoadFloat3(&_vertices[i]);

		vmin = ::XMVectorMin(vmin, P);
		vmax = ::XMVectorMax(vmax, P);
	}

	BoundingBox bounds;
	XMVECTOR C = 0.5f * (vmin + vmax);
	XMVECTOR E = 0.5f * (vmax - vmin);

	::XMStoreFloat3(&bounds.Center, C);
	::XMStoreFloat3(&bounds.Extents, E);

	return bounds;
}

void Octree::BuildOctree(shared_ptr<OctreeNode> parent, const std::vector<uint32>& indices)
{
	size_t triCount = indices.size() / 3;

	if (triCount < 60)
	{
		parent->IsLeaf = true;
		parent->Indices = indices;
	}
	else
	{
		parent->IsLeaf = false;

		BoundingBox subbox[8];
		parent->Subdivide(subbox);

		for (int32 i = 0; i < 8; ++i)
		{
			// Allocate a new subnode.
			parent->Children[i] = make_shared<OctreeNode>();
			parent->Children[i]->Bounds = subbox[i];

			// Find triangles that intersect this node's bounding box.
			std::vector<uint32> intersectedTriangleIndices;

			for (size_t j = 0; j < triCount; ++j)
			{
				uint32 i0 = indices[j * 3 + 0];
				uint32 i1 = indices[j * 3 + 1];
				uint32 i2 = indices[j * 3 + 2];

				XMVECTOR v0 = ::XMLoadFloat3(&_vertices[i0]);
				XMVECTOR v1 = ::XMLoadFloat3(&_vertices[i1]);
				XMVECTOR v2 = ::XMLoadFloat3(&_vertices[i2]);
				
				if (subbox[i].Intersects(v0, v1, v2))
				{
					intersectedTriangleIndices.push_back(i0);
					intersectedTriangleIndices.push_back(i1);
					intersectedTriangleIndices.push_back(i2);
				}
			}

			// Recurse.
			BuildOctree(parent->Children[i], intersectedTriangleIndices);
		}
	}
}

bool Octree::RayOctreeIntersect(shared_ptr<OctreeNode> parent, FXMVECTOR rayPos, FXMVECTOR rayDir)
{
	// Recurs until we find a leaf node (all the triangles are in the leaves).
	if (!parent->IsLeaf)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			// Recurse down this node if the ray hit the child's box.
			float t;

			if (parent->Children[i]->Bounds.Intersects(rayPos, rayDir, OUT t))
			{
				// If we hit a triangle down this branch, we can bail out that we hit a triangle.
				if (RayOctreeIntersect(parent->Children[i], rayPos, rayDir))
					return true;
			}
		}

		// If we get here. then we did not hit any triangles.
		return false;
	}
	else
	{
		size_t triCount = parent->Indices.size() / 3;

		for (size_t i = 0; i < triCount; ++i)
		{
			uint32 i0 = parent->Indices[i * 3 + 0];
			uint32 i1 = parent->Indices[i * 3 + 1];
			uint32 i2 = parent->Indices[i * 3 + 2];

			XMVECTOR v0 = ::XMLoadFloat3(&_vertices[i0]);
			XMVECTOR v1 = ::XMLoadFloat3(&_vertices[i1]);
			XMVECTOR v2 = ::XMLoadFloat3(&_vertices[i2]);

			float t;
			
			if (TriangleTests::Intersects(rayPos, rayDir, v0, v1, v2, OUT t))
				return true;
		}

		return false;
	}
}