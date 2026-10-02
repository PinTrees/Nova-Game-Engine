#pragma once
#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

// Unity 의 Import Settings: 에셋 옆 `<파일>.meta`(JSON) 에 가져오기 설정을 둔다. 없으면 기본값 (Unity 와 같은 값).
//  - 텍스처: Texture Type (Default / Normal map / Sprite — Pixels Per Unit · Pivot), sRGB, Max Size (기본 2048 — 큰 쪽이 넘으면 줄임), Filter Mode,
//           Compression (None / Normal = BC1·BC3 기본 / High = BC7), Generate Mip Maps
//    프로젝트 Assets 와 엔진 Resources\Packages 의 에셋에만 적용 (에디터 아이콘 · 엔진 내부 텍스처는 그대로 — Raw)
//  - 모델(FBX): Scale Factor, Import Animation, Animation Type (Generic / Humanoid — 없으면 사람 본을 찾으면 Humanoid), 사람 본 매핑 직접 지정
//  - 오디오: Load Type (Decompress On Load / Compressed In Memory / Streaming), Force To Mono
// 경로는 디스크 전체 경로 (PathManager::GetMovePathW 결과).
namespace AssetImport
{
	enum class Kind { None, Texture, Model, Audio };

	// Sprite Mode = Multiple 의 스프라이트 하나 (Unity 의 Sprite Editor): 원본 픽셀, 왼쪽 아래 원점, 기준점 = 사각형 안 0..1
	struct SpriteRect
	{
		std::string Name;
		float X = 0, Y = 0, W = 0, H = 0;
		float PivotX = 0.5f, PivotY = 0.5f;
		bool operator==(const SpriteRect& o) const { return Name == o.Name && X == o.X && Y == o.Y && W == o.W && H == o.H && PivotX == o.PivotX && PivotY == o.PivotY; }
	};

	struct NOVA_API TextureSettings
	{
		enum Compression { None = 0, NormalQuality = 1, HighQuality = 2 };
		enum Type { Default = 0, NormalMap = 1, Sprite = 2 };
		int TextureType = Default;
		bool SRGB = true;              // Default · Sprite: 색 텍스처 (끄면 선형 — 마스크 등). Normal map 은 늘 선형
		int MaxSize = 2048;
		int Compression = NormalQuality;   // Unity 기본. 노멀맵도 선형 BC1 (알파 = 높이가 있으면 BC3 — 셰이더가 rgb 를 그대로 읽는다), High = BC7
		bool MipMaps = true;
		// Sprite (2D and UI): 그림 몇 픽셀이 월드 1 단위인가, 기준점 (0..1, 왼쪽 아래 = 0,0), 필터 (Point = 도트 그림)
		enum Filter { Point = 0, Bilinear = 1, Trilinear = 2 };
		float PixelsPerUnit = 100.0f;
		float PivotX = 0.5f, PivotY = 0.5f;
		int FilterMode = Bilinear;
		// Sprite Mode: Single = 그림 전체가 스프라이트 하나, Multiple = 잘라 놓은 여러 스프라이트 ("경로#이름" 으로 가리킨다)
		enum SpriteModes { SingleSprite = 0, MultipleSprites = 1 };
		int SpriteMode = SingleSprite;
		std::vector<SpriteRect> Sprites;
		const SpriteRect* FindSprite(const std::string& name) const;
		nlohmann::json SpritesJson() const;

		static TextureSettings Raw();  // 가져오기 설정이 없는 엔진 내부 텍스처 (원래 크기, 압축 없음, 밉)

		nlohmann::json ToJson() const;
		void FromJson(const nlohmann::json& j);
		bool IsDefault() const;
		std::string CacheTag() const;   // 텍스처 캐시 파일 이름에 붙인다 (설정이 바뀌면 다른 캐시)
	};

	struct NOVA_API ModelSettings
	{
		enum AnimationType { Auto = -1, Generic = 0, Humanoid = 1 };
		float ScaleFactor = 1.0f;
		bool ImportAnimation = true;
		int AnimationType = Auto;
		std::map<std::string, std::string> HumanBones;   // 사람 본 이름(Hips, LeftFoot …) → 노드 이름 (자동 매핑 대신)

		nlohmann::json ToJson() const;
		void FromJson(const nlohmann::json& j);
	};

	struct NOVA_API AudioSettings
	{
		enum LoadType { DecompressOnLoad = 0, CompressedInMemory = 1, Streaming = 2 };
		int LoadType = DecompressOnLoad;
		bool ForceToMono = false;

		nlohmann::json ToJson() const;
		void FromJson(const nlohmann::json& j);
	};

	NOVA_API Kind KindOf(const std::wstring& path);
	// 가져오기 설정을 쓰는 에셋인가 (프로젝트 Assets\ 또는 엔진 Resources\Packages\ 아래)
	NOVA_API bool AppliesTo(const std::wstring& fullPath);
	NOVA_API std::wstring MetaPath(const std::wstring& assetPath);

	// .meta 읽기 (없거나 깨졌으면 기본값). 같은 파일은 수정 시각이 바뀔 때까지 캐시
	NOVA_API TextureSettings LoadTexture(const std::wstring& assetPath);
	NOVA_API ModelSettings LoadModel(const std::wstring& assetPath);
	NOVA_API AudioSettings LoadAudio(const std::wstring& assetPath);
	NOVA_API nlohmann::json LoadJson(const std::wstring& assetPath);   // 종류에 맞는 설정 전체 (기본값 채움)

	// .meta 쓰기 (importer 이름 포함). 성공하면 true
	NOVA_API bool Save(const std::wstring& assetPath, const nlohmann::json& settings);
	// .meta 의 수정 시각 (없으면 0) — 모델 캐시가 .meta 보다 오래되면 다시 가져온다
	NOVA_API long long MetaStamp(const std::wstring& assetPath);

	// 마지막으로 가져온 텍스처의 결과 (Inspector 정보 줄 · CLI 확인용)
	struct TextureInfo
	{
		int SourceWidth = 0, SourceHeight = 0;
		int Width = 0, Height = 0, Mips = 0;
		std::string Format;
		size_t Bytes = 0;
	};
	NOVA_API void RecordTexture(const std::wstring& assetPath, const TextureInfo& info);
	NOVA_API bool GetTextureInfo(const std::wstring& assetPath, TextureInfo& out);
}
