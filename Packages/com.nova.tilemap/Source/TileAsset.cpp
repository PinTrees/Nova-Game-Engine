#include "pch.h"
#include "TileAsset.h"
#include "SpriteRenderer.h"
#include "AssetImportSettings.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace TileAssets
{
	namespace
	{
		struct Entry
		{
			Tile Value;
			bool Exists = false;
			long long Stamp = 0;               // .tile 파일 수정 시각
			unsigned long long CheckedAt = 0;
		};
		std::unordered_map<std::string, Entry>& Cache()
		{
			static std::unordered_map<std::string, Entry> cache;
			return cache;
		}
		uint32 s_Revision = 1;

		long long FileStamp(const std::wstring& full)
		{
			std::error_code ec;
			const auto t = fs::last_write_time(fs::path(full), ec);
			return ec ? 0 : (long long)t.time_since_epoch().count();
		}

		std::string Normalize(std::string p)
		{
			std::replace(p.begin(), p.end(), '/', '\\');
			return p;
		}

		bool ReadFile(const std::wstring& full, Tile& out)
		{
			std::ifstream f(fs::path(full), std::ios::binary);
			if (!f)
				return false;
			const json j = json::parse(f, nullptr, false);
			if (!j.is_object())
				return false;
			out = Tile();
			out.Sprite = j.value("sprite", std::string());
			if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
				for (int i = 0; i < 4; ++i)
					out.Color[i] = j["color"][i].get<float>();
			ParseCollider(j.value("colliderType", std::string("sprite")), out.Collider);
			return true;
		}

		void ResolveSprite(Tile& t)
		{
			SpriteRenderer::SpriteInfo s;
			t.HasSprite = SpriteRenderer::ResolveSprite(t.Sprite, s);
			t.Texture = s.Texture;
			t.Size = t.HasSprite ? s.SizePx / s.PixelsPerUnit : Vec2(0, 0);
			t.Pivot = s.Pivot;
			t.UV = s.UV;
			t.Point = s.Point;
		}

		bool SameShape(const Tile& a, const Tile& b)
		{
			return a.Sprite == b.Sprite && a.HasSprite == b.HasSprite && a.Texture == b.Texture && a.Size == b.Size && a.Pivot == b.Pivot &&
				a.UV == b.UV && a.Point == b.Point && a.Collider == b.Collider && memcmp(a.Color, b.Color, sizeof(a.Color)) == 0;
		}
	}

	std::wstring FullPath(const std::string& relPath)
	{
		if (relPath.size() > 1 && relPath[1] == ':')
			return string_to_wstring(relPath);
		return PathManager::GetI()->GetMovePathW(string_to_wstring(Normalize(relPath)));
	}

	std::string ProjectRelative(const std::wstring& fullPath)
	{
		std::error_code ec;
		const fs::path root = PathManager::GetI()->GetMovePathW(L"");
		const fs::path rel = fs::relative(fs::path(fullPath), root, ec);
		return ec || rel.empty() ? wstring_to_string(fullPath) : wstring_to_string(rel.wstring());
	}

	const Tile* Get(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		const std::string key = Normalize(path);
		Entry& e = Cache()[key];
		const unsigned long long now = GetTickCount64();
		if (e.CheckedAt != 0 && now - e.CheckedAt < 500)
			return e.Exists ? &e.Value : nullptr;
		e.CheckedAt = now;
		const std::wstring full = FullPath(key);
		const long long stamp = FileStamp(full);
		Tile next = e.Value;
		bool exists = e.Exists;
		if (stamp == 0)
			exists = false;
		else if (stamp != e.Stamp || !e.Exists)
			exists = ReadFile(full, next);
		e.Stamp = stamp;
		if (exists)
			ResolveSprite(next);   // 그림 가져오기 설정 (Pixels Per Unit · 자르기) 도 따라온다
		if (exists != e.Exists || (exists && !SameShape(next, e.Value)))
			++s_Revision;
		e.Exists = exists;
		e.Value = next;
		return e.Exists ? &e.Value : nullptr;
	}

	uint32 Revision() { return s_Revision; }

	bool Save(const std::string& path, const Tile& tile, std::string& error)
	{
		const std::wstring full = FullPath(path);
		std::error_code ec;
		fs::create_directories(fs::path(full).parent_path(), ec);
		json j;
		j["sprite"] = tile.Sprite;
		j["color"] = { tile.Color[0], tile.Color[1], tile.Color[2], tile.Color[3] };
		j["colliderType"] = ColliderName(tile.Collider);
		std::ofstream f(fs::path(full), std::ios::binary | std::ios::trunc);
		if (!f)
		{
			error = "cannot write " + path;
			return false;
		}
		f << j.dump(2);
		f.close();
		Cache().erase(Normalize(path));   // 다음 Get 에서 다시 읽는다
		++s_Revision;
		return true;
	}

	const char* ColliderName(ColliderType t)
	{
		switch (t)
		{
		case ColliderType::None: return "none";
		case ColliderType::Grid: return "grid";
		default: return "sprite";
		}
	}

	bool ParseCollider(const std::string& name, ColliderType& out)
	{
		if (name == "none") { out = ColliderType::None; return true; }
		if (name == "sprite") { out = ColliderType::Sprite; return true; }
		if (name == "grid") { out = ColliderType::Grid; return true; }
		return false;
	}

	std::vector<std::string> FindAll(const std::string& folder)
	{
		std::vector<std::string> out;
		std::error_code ec;
		const fs::path dir = FullPath(folder);
		for (const auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && IsTilePath(wstring_to_string(e.path().filename().wstring())))
				out.push_back(ProjectRelative(e.path().wstring()));
		std::sort(out.begin(), out.end());
		return out;
	}

	bool IsTilePath(const std::string& path)
	{
		if (path.size() < 5)
			return false;
		std::string ext = path.substr(path.size() - 5);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return ext == ".tile";
	}

	std::vector<std::string> SpritesOf(const std::string& texturePath)
	{
		std::vector<std::string> out;
		const std::string rel = Normalize(texturePath.substr(0, texturePath.find('#')));
		const AssetImport::TextureSettings ts = AssetImport::LoadTexture(FullPath(rel));
		if (ts.SpriteMode == AssetImport::TextureSettings::MultipleSprites && !ts.Sprites.empty())
		{
			for (const AssetImport::SpriteRect& s : ts.Sprites)
				out.push_back(rel + "#" + s.Name);
		}
		else
			out.push_back(rel);
		return out;
	}
}
