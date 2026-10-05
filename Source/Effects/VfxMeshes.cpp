#include "pch.h"
#include "VfxMeshes.h"
#include "GeometryGenerator.h"
#include "ResourceManager.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include <chrono>
#include <deque>
#include <map>

namespace Vfx
{
	namespace
	{
		std::map<std::string, std::shared_ptr<const CpuMesh>> s_Meshes;
		std::map<std::pair<std::string, int>, std::shared_ptr<const Sdf>> s_Sdfs;

		const char* kBuiltins[] = { "Cube", "Sphere", "Cylinder", "Cone", "Crystal" };

		void Bounds(CpuMesh& m)
		{
			for (int c = 0; c < 3; ++c) { m.Min[c] = 1e30f; m.Max[c] = -1e30f; }
			for (size_t v = 0; v < m.VertexCount(); ++v)
				for (int c = 0; c < 3; ++c)
				{
					m.Min[c] = (std::min)(m.Min[c], m.PosNormal[v * 6 + c]);
					m.Max[c] = (std::max)(m.Max[c], m.PosNormal[v * 6 + c]);
				}
		}

		// 엔진 기본 메시 (GeometryGenerator — 크기 1, 가운데 0). Crystal = 위아래로 늘린 정이십면체, 면마다 평평한 법선
		void Builtin(const std::string& name, CpuMesh& m)
		{
			GeometryGenerator gen;
			GeometryGenerator::MeshData data;
			const bool flat = name == "Crystal";
			if (name == "Sphere") gen.CreateSphere(0.5f, 16, 12, data);
			else if (name == "Cylinder") gen.CreateCylinder(0.5f, 0.5f, 1.0f, 16, 1, data);
			else if (name == "Cone") gen.CreateCylinder(0.5f, 0.001f, 1.0f, 16, 1, data);
			else if (flat) gen.CreateGeosphere(0.5f, 0, data);
			else gen.CreateBox(1.0f, 1.0f, 1.0f, data);
			if (flat)
			{
				for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
				{
					XMFLOAT3 p[3];
					for (int k = 0; k < 3; ++k)
					{
						p[k] = data.vertices[data.indices[i + k]].position;
						p[k].y *= 1.7f;
					}
					Vec3 n = (Vec3(p[1].x, p[1].y, p[1].z) - Vec3(p[0].x, p[0].y, p[0].z)).Cross(Vec3(p[2].x, p[2].y, p[2].z) - Vec3(p[0].x, p[0].y, p[0].z));
					n.Normalize();
					for (int k = 0; k < 3; ++k)
					{
						m.PosNormal.insert(m.PosNormal.end(), { p[k].x, p[k].y, p[k].z, n.x, n.y, n.z });
						m.Indices.push_back((uint32_t)m.Indices.size());
					}
				}
			}
			else
			{
				for (const auto& v : data.vertices)
					m.PosNormal.insert(m.PosNormal.end(), { v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z });
				m.Indices.assign(data.indices.begin(), data.indices.end());
			}
		}

		// 모델 파일의 메시 하나 (정적 메시가 없으면 스킨 메시의 바인드 자세). 하위 묶음 (Subset) 의 인덱스는 VertexStart 기준
		template<typename V>
		void FromSubsets(const std::vector<V>& verts, const std::vector<USHORT>& idx, const std::vector<MeshGeometry::Subset>& subsets, CpuMesh& m)
		{
			for (const V& v : verts)
				m.PosNormal.insert(m.PosNormal.end(), { v.pos.x, v.pos.y, v.pos.z, v.normal.x, v.normal.y, v.normal.z });
			if (subsets.empty())
			{
				m.Indices.assign(idx.begin(), idx.end());
				return;
			}
			for (const MeshGeometry::Subset& s : subsets)
				for (UINT f = 0; f < s.FaceCount * 3; ++f)
				{
					const size_t at = (size_t)s.FaceStart * 3 + f;
					if (at < idx.size())
						m.Indices.push_back((uint32_t)idx[at] + s.VertexStart);
				}
		}

		void FromModel(const std::string& name, CpuMesh& m)
		{
			std::string path = name;
			int index = -1;   // -1 = 파일 전체 (아래)
			if (const size_t hash = path.rfind('#'); hash != std::string::npos)
			{
				index = (std::max)(0, atoi(path.c_str() + hash + 1));
				path = path.substr(0, hash);
			}
			for (char& c : path) if (c == '/') c = '\\';
			const std::shared_ptr<MeshFile> file = ResourceManager::GetI()->LoadMeshFile(path);
			if (!file) { m.Error = "cannot load model " + path + " (FBX · glTF · GLB · VRM)"; return; }
			if (index < 0)
			{
				// 번호가 없으면: 스킨 메시는 모두 합친다 (바인드 자세는 모두 같은 모델 공간 — 캐릭터 전체), 아니면 첫 정적 메시
				if (!file->SkinnedMeshs.empty())
				{
					for (const auto& sm : file->SkinnedMeshs)
						if (sm)
						{
							CpuMesh part;
							FromSubsets(sm->Vertices, sm->Indices, sm->Subsets, part);
							const uint32_t base = (uint32_t)m.VertexCount();
							m.PosNormal.insert(m.PosNormal.end(), part.PosNormal.begin(), part.PosNormal.end());
							for (uint32_t i : part.Indices) m.Indices.push_back(base + i);
						}
					return;
				}
				index = 0;
			}
			if (index < (int)file->Meshs.size() && file->Meshs[index])
			{
				const Mesh& src = *file->Meshs[index];
				FromSubsets(src.Vertices, src.Indices, src.Subsets, m);
			}
			else if (index < (int)file->SkinnedMeshs.size() && file->SkinnedMeshs[index])
			{
				const SkinnedMesh& src = *file->SkinnedMeshs[index];
				FromSubsets(src.Vertices, src.Indices, src.Subsets, m);
			}
			else
				m.Error = path + " has no mesh #" + std::to_string(index) + " (" + std::to_string(file->Meshs.size()) + " static, " + std::to_string(file->SkinnedMeshs.size()) + " skinned)";
		}

		// ---------------------------------------------------------------- SDF 굽기
		struct V3 { float x, y, z; };
		V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
		float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

		// 점에서 삼각형까지 가장 가까운 점 (Ericson, Real-Time Collision Detection 5.1.5)
		V3 ClosestOnTriangle(V3 p, V3 a, V3 b, V3 c)
		{
			const V3 ab = b - a, ac = c - a, ap = p - a;
			const float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
			if (d1 <= 0 && d2 <= 0) return a;
			const V3 bp = p - b;
			const float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
			if (d3 >= 0 && d4 <= d3) return b;
			const float vc = d1 * d4 - d3 * d2;
			if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
			const V3 cp = p - c;
			const float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
			if (d6 >= 0 && d5 <= d6) return c;
			const float vb = d5 * d2 - d1 * d6;
			if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
			const float va = d3 * d6 - d5 * d4;
			if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
			const float denom = 1.0f / (va + vb + vc);
			return a + ab * (vb * denom) + ac * (vc * denom);
		}

		void Bake(const CpuMesh& m, int resolution, Sdf& out)
		{
			const auto t0 = std::chrono::steady_clock::now();
			const size_t tris = m.Indices.size() / 3;
			if (tris == 0) { out.Error = "mesh has no triangles"; return; }
			const float ext[3] = { m.Max[0] - m.Min[0], m.Max[1] - m.Min[1], m.Max[2] - m.Min[2] };
			const float longest = (std::max)({ ext[0], ext[1], ext[2], 1e-4f });
			const int res = std::clamp(resolution, 8, 128);
			const int pad = 2;
			out.Voxel = longest / (float)(res - 2 * pad);
			for (int c = 0; c < 3; ++c)
			{
				out.N[c] = (std::max)(1, (int)std::ceil(ext[c] / out.Voxel)) + 2 * pad;
				out.Min[c] = m.Min[c] - pad * out.Voxel - 0.5f * ((out.N[c] - 2 * pad) * out.Voxel - ext[c]);
			}
			const int nx = out.N[0], ny = out.N[1], nz = out.N[2];
			const size_t count = (size_t)nx * ny * nz;
			auto center = [&](int x, int y, int z) { return V3{ out.Min[0] + (x + 0.5f) * out.Voxel, out.Min[1] + (y + 0.5f) * out.Voxel, out.Min[2] + (z + 0.5f) * out.Voxel }; };
			auto at = [&](int x, int y, int z) { return ((size_t)z * ny + y) * nx + x; };
			auto vert = [&](uint32_t i) { return V3{ m.PosNormal[i * 6], m.PosNormal[i * 6 + 1], m.PosNormal[i * 6 + 2] }; };

			// 1) 표면 둘레 칸: 가까운 삼각형까지 정확한 거리 + 가장 가까운 점 · 그 삼각형
			std::vector<float> dist(count, 1e30f);
			std::vector<V3> closest(count, V3{ 0, 0, 0 });
			std::vector<int> tri(count, -1);
			for (size_t t = 0; t < tris; ++t)
			{
				const V3 a = vert(m.Indices[t * 3]), b = vert(m.Indices[t * 3 + 1]), c = vert(m.Indices[t * 3 + 2]);
				int lo[3], hi[3];
				const float tmin[3] = { (std::min)({ a.x, b.x, c.x }), (std::min)({ a.y, b.y, c.y }), (std::min)({ a.z, b.z, c.z }) };
				const float tmax[3] = { (std::max)({ a.x, b.x, c.x }), (std::max)({ a.y, b.y, c.y }), (std::max)({ a.z, b.z, c.z }) };
				for (int k = 0; k < 3; ++k)
				{
					lo[k] = std::clamp((int)std::floor((tmin[k] - out.Min[k]) / out.Voxel) - 1, 0, out.N[k] - 1);
					hi[k] = std::clamp((int)std::floor((tmax[k] - out.Min[k]) / out.Voxel) + 1, 0, out.N[k] - 1);
				}
				for (int z = lo[2]; z <= hi[2]; ++z)
					for (int y = lo[1]; y <= hi[1]; ++y)
						for (int x = lo[0]; x <= hi[0]; ++x)
						{
							const V3 p = center(x, y, z);
							const V3 q = ClosestOnTriangle(p, a, b, c);
							const float d = std::sqrt(Dot(p - q, p - q));
							const size_t i = at(x, y, z);
							if (d < dist[i]) { dist[i] = d; closest[i] = q; tri[i] = (int)t; }
						}
			}
			// 2) 나머지 칸: 이웃의 가장 가까운 점을 물려받아 거리 (앞 · 뒤로 두 번 쓸기, 이웃 13 개씩)
			const int offs[13][3] = { { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 }, { -1, -1, 0 }, { 1, -1, 0 }, { -1, 0, -1 }, { 1, 0, -1 }, { 0, -1, -1 }, { 0, 1, -1 },
				{ -1, -1, -1 }, { 1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } };
			for (int pass = 0; pass < 2; ++pass)
				for (int dir = 0; dir < 2; ++dir)
				{
					const int s = dir == 0 ? 1 : -1;
					for (int zi = 0; zi < nz; ++zi)
						for (int yi = 0; yi < ny; ++yi)
							for (int xi = 0; xi < nx; ++xi)
							{
								const int x = dir == 0 ? xi : nx - 1 - xi, y = dir == 0 ? yi : ny - 1 - yi, z = dir == 0 ? zi : nz - 1 - zi;
								const size_t i = at(x, y, z);
								const V3 p = center(x, y, z);
								for (const auto& o : offs)
								{
									const int X = x + o[0] * s, Y = y + o[1] * s, Z = z + o[2] * s;
									if (X < 0 || Y < 0 || Z < 0 || X >= nx || Y >= ny || Z >= nz) continue;
									const size_t j = at(X, Y, Z);
									if (tri[j] < 0) continue;
									const float d = std::sqrt(Dot(p - closest[j], p - closest[j]));
									if (d < dist[i]) { dist[i] = d; closest[i] = closest[j]; tri[i] = tri[j]; }
								}
							}
				}
			// 3) 부호: 상자 가장자리에서 표면 띠를 건너지 않고 닿는 칸 = 바깥. 띠 안의 칸은 가장 가까운 삼각형 면 법선 쪽으로
			const float band = out.Voxel * 0.87f;
			std::vector<uint8_t> outside(count, 0);
			std::deque<size_t> queue;
			for (int z = 0; z < nz; ++z)
				for (int y = 0; y < ny; ++y)
					for (int x = 0; x < nx; ++x)
						if ((x == 0 || y == 0 || z == 0 || x == nx - 1 || y == ny - 1 || z == nz - 1) && dist[at(x, y, z)] > band)
						{
							outside[at(x, y, z)] = 1;
							queue.push_back(at(x, y, z));
						}
			while (!queue.empty())
			{
				const size_t i = queue.front();
				queue.pop_front();
				const int x = (int)(i % nx), y = (int)((i / nx) % ny), z = (int)(i / ((size_t)nx * ny));
				const int nb[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
				for (const auto& o : nb)
				{
					const int X = x + o[0], Y = y + o[1], Z = z + o[2];
					if (X < 0 || Y < 0 || Z < 0 || X >= nx || Y >= ny || Z >= nz) continue;
					const size_t j = at(X, Y, Z);
					if (!outside[j] && dist[j] > band) { outside[j] = 1; queue.push_back(j); }
				}
			}
			out.Distance.resize(count);
			for (int z = 0; z < nz; ++z)
				for (int y = 0; y < ny; ++y)
					for (int x = 0; x < nx; ++x)
					{
						const size_t i = at(x, y, z);
						bool inside;
						if (dist[i] > band || tri[i] < 0)
							inside = !outside[i];
						else
						{
							// 바깥 쪽 = 그 삼각형 정점 법선의 합 (감는 방향과 상관없이 — 모델 파일마다 다를 수 있다)
							const size_t t = (size_t)tri[i];
							auto normal = [&](uint32_t v) { return V3{ m.PosNormal[v * 6 + 3], m.PosNormal[v * 6 + 4], m.PosNormal[v * 6 + 5] }; };
							V3 n = normal(m.Indices[t * 3]) + normal(m.Indices[t * 3 + 1]) + normal(m.Indices[t * 3 + 2]);
							if (Dot(n, n) < 1e-12f)
							{
								const V3 a = vert(m.Indices[t * 3]), b = vert(m.Indices[t * 3 + 1]), c = vert(m.Indices[t * 3 + 2]);
								n = Cross(b - a, c - a);   // 법선이 없으면 면에서 (시계 방향이 앞면)
							}
							inside = Dot(center(x, y, z) - closest[i], n) < 0.0f;
						}
						out.Distance[i] = inside ? -dist[i] : dist[i];
					}
			out.BakeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}
	}

	bool IsBuiltinMesh(const std::string& name)
	{
		for (const char* b : kBuiltins)
			if (name == b)
				return true;
		return false;
	}

	std::shared_ptr<const CpuMesh> LoadCpuMesh(const std::string& name)
	{
		if (auto it = s_Meshes.find(name); it != s_Meshes.end())
			return it->second;
		auto m = std::make_shared<CpuMesh>();
		if (name.empty()) m->Error = "no mesh";
		else if (IsBuiltinMesh(name)) Builtin(name, *m);
		else FromModel(name, *m);
		if (m->Error.empty() && (m->PosNormal.empty() || m->Indices.empty()))
			m->Error = name + ": empty mesh";
		if (m->Error.empty())
			Bounds(*m);
		else
			EditorLog::Write("VFX", "mesh %s: %s", name.c_str(), m->Error.c_str());
		s_Meshes[name] = m;
		return m;
	}

	std::shared_ptr<const Sdf> BakeSdf(const std::string& meshName, int resolution)
	{
		const auto key = std::make_pair(meshName, resolution);
		if (auto it = s_Sdfs.find(key); it != s_Sdfs.end())
			return it->second;
		auto sdf = std::make_shared<Sdf>();
		const auto mesh = LoadCpuMesh(meshName);
		if (!mesh->Error.empty())
			sdf->Error = mesh->Error;
		else
			Bake(*mesh, resolution, *sdf);
		if (sdf->Error.empty())
			EditorLog::Write("VFX", "SDF %s: %dx%dx%d (voxel %.3f) in %.1f ms", meshName.c_str(), sdf->N[0], sdf->N[1], sdf->N[2], sdf->Voxel, sdf->BakeMs);
		s_Sdfs[key] = sdf;
		return sdf;
	}
}
