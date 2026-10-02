#include "pch.h"
#include "SpriteSkinnedRenderer.h"
#include "UnityGUI.h"
#include "EngineTime.h"
#include "AssetImportSettings.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	long long FileStamp(const std::string& full)
	{
		std::error_code ec;
		const auto t = fs::last_write_time(fs::path(std::u8string(full.begin(), full.end())), ec);
		return ec ? 0 : (long long)t.time_since_epoch().count();
	}

	// 절대 경로 (프로젝트 안) → 프로젝트 기준
	std::string ProjectRelative(std::string p)
	{
		const std::string root = wstring_to_string(PathManager::GetI()->GetContentPathW());
		if (!root.empty() && _strnicmp(p.c_str(), root.c_str(), root.size()) == 0)
			p = p.substr(root.size());
		return p;
	}

	// Assets 안의 .skel2d (Inspector 의 ⊙ 목록)
	std::vector<std::string> FindSkeletons()
	{
		std::vector<std::string> out;
		std::error_code ec;
		const fs::path assets = PathManager::GetI()->GetMovePathW(L"Assets\\");
		const fs::path root = PathManager::GetI()->GetMovePathW(L"");
		for (const auto& e : fs::recursive_directory_iterator(assets, fs::directory_options::skip_permission_denied, ec))
			if (e.is_regular_file(ec) && e.path().extension() == L".skel2d")
				out.push_back(wstring_to_string(fs::relative(e.path(), root, ec).wstring()));
		std::sort(out.begin(), out.end());
		return out;
	}
}

SpriteSkinnedRenderer::SpriteSkinnedRenderer()
{
	m_InspectorTitleName = "Sprite Skinned Renderer";
}

SpriteSkinnedRenderer::~SpriteSkinnedRenderer() = default;

void SpriteSkinnedRenderer::SetSkeleton(const std::string& path)
{
	m_Skeleton = ProjectRelative(path);
	m_CheckedAt = 0;
}

// 뼈대 파일 읽기 (경로가 바뀌거나 파일이 저장되면 다시 — 0.5 초마다 확인)
Anim2D::Document* SpriteSkinnedRenderer::Skeleton()
{
	const unsigned long long now = GetTickCount64();
	if (m_CheckedAt != 0 && now - m_CheckedAt < 500 && m_LoadedPath == m_Skeleton)
		return m_Doc.get();
	m_CheckedAt = now;
	if (m_Skeleton.empty())
	{
		m_Doc.reset();
		m_LoadedPath.clear();
		m_Error.clear();
		return nullptr;
	}
	const std::string full = Anim2D::FullPath(m_Skeleton);
	const long long stamp = FileStamp(full);
	if (m_Doc && m_LoadedPath == m_Skeleton && stamp == m_LoadedStamp)
		return m_Doc.get();
	auto doc = std::make_unique<Anim2D::Document>();
	std::string err;
	if (stamp == 0 || !doc->Load(full, err))
	{
		m_Error = stamp == 0 ? "file not found" : err;
		m_Doc.reset();
	}
	else
	{
		m_Error.clear();
		m_Doc = std::move(doc);
		m_Textures.clear();   // 그림이 바뀌었을 수 있다
	}
	m_LoadedPath = m_Skeleton;
	m_LoadedStamp = stamp;
	return m_Doc.get();
}

bool SpriteSkinnedRenderer::Play(const std::string& animation, bool loop)
{
	Anim2D::Document* d = Skeleton();
	if (d && d->FindAnim(animation) < 0)
		return false;
	m_Animation = animation;
	Loop = loop;
	TrackTime = 0.0f;
	return true;
}

bool SpriteSkinnedRenderer::IsComplete() const
{
	if (Loop || !m_Doc)
		return false;
	const int ai = m_Animation.empty() ? (m_Doc->Animations.empty() ? -1 : 0) : m_Doc->FindAnim(m_Animation);
	return ai >= 0 && TrackTime >= m_Doc->Animations[ai].Length;
}

void SpriteSkinnedRenderer::Update()
{
	Anim2D::Document* d = Skeleton();
	if (d == nullptr)
		return;
	TrackTime += (std::max)(0.0f, ::Time::DeltaTime()) * TimeScale;
	const int ai = m_Animation.empty() ? (d->Animations.empty() ? -1 : 0) : d->FindAnim(m_Animation);
	if (ai < 0)
		return;
	const float len = d->Animations[ai].Length;
	if (len > 0.0f)
	{
		if (Loop) TrackTime = fmodf(TrackTime, len);
		else TrackTime = (std::min)(TrackTime, len);
		if (TrackTime < 0.0f) TrackTime += len;
	}
}

void SpriteSkinnedRenderer::PoseNow()
{
	Anim2D::Document& d = *m_Doc;
	const int ai = m_Animation.empty() ? (d.Animations.empty() ? -1 : 0) : d.FindAnim(m_Animation);
	if (ai < 0)
	{
		d.ResetPose();
		return;
	}
	// 애니메이션의 반복 여부는 이 컴포넌트의 Loop 를 따른다
	Anim2D::Animation& a = d.Animations[ai];
	const bool keep = a.Loop;
	a.Loop = Loop;
	d.ApplyAnimation(a, TrackTime);
	a.Loop = keep;
}

const SpriteSkinnedRenderer::Tex* SpriteSkinnedRenderer::Texture(const std::string& image)
{
	auto it = m_Textures.find(image);
	if (it != m_Textures.end())
		return it->second.Srv ? &it->second : nullptr;
	Tex& t = m_Textures[image];
	const std::string rel = ProjectRelative(image);
	t.Srv = ResourceManager::GetI()->LoadTexture(string_to_wstring(rel));
	if (!t.Srv)
		return nullptr;
	const std::wstring full = PathManager::GetI()->GetMovePathW(string_to_wstring(rel));
	AssetImport::TextureInfo info;
	if (AssetImport::GetTextureInfo(full, info) && info.SourceWidth > 0)
	{
		t.W = (float)info.SourceWidth;
		t.H = (float)info.SourceHeight;
	}
	else
	{
		// 가져오기 설정 밖의 그림: 텍스처 크기
		ComPtr<GfxResource> res;
		t.Srv->GetResource(res.GetAddressOf());
		ComPtr<GfxTexture2D> tex;
		if (res && SUCCEEDED(res.As(&tex)))
		{
			D3D11_TEXTURE2D_DESC desc;
			tex->GetDesc(&desc);
			t.W = (float)desc.Width;
			t.H = (float)desc.Height;
		}
	}
	t.Point = AssetImport::LoadTexture(full).FilterMode == AssetImport::TextureSettings::Point;
	return &t;
}

template <class Fn>
void SpriteSkinnedRenderer::ForEachQuad(Fn fn)
{
	Anim2D::Document& d = *m_Doc;
	const float k = 1.0f / (std::max)(0.01f, m_PixelsPerUnit), sx = FlipX ? -k : k;
	for (const Anim2D::Slot& s : d.Slots)
	{
		const Anim2D::Attachment* a = s.Find(s.Current);
		if (a == nullptr)
			continue;
		const Tex* t = Texture(a->Image);
		float c[4][2];
		if (!Anim2D::AttachmentQuad(d, s, *a, t ? t->W : 0.0f, t ? t->H : 0.0f, c))
			continue;
		Vec3 p[4];
		for (int i = 0; i < 4; ++i)
			p[i] = Vec3(c[i][0] * sx, c[i][1] * k, 0.0f);
		fn(s, t, p);
	}
}

void SpriteSkinnedRenderer::CollectSprites(SpriteBatch& batch)
{
	if (m_pGameObject == nullptr || Skeleton() == nullptr)
		return;
	PoseNow();
	const Matrix world = m_pGameObject->GetTransform()->GetWorldMatrix();
	batch.Begin(SortingOrder, m_pGameObject->GetTransform()->GetPosition());
	const Vec2 uv[4] = { Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), Vec2(0, 0) };
	ForEachQuad([&](const Anim2D::Slot& s, const Tex* t, Vec3 p[4]) {
		if (t == nullptr)
			return;   // 그림을 못 읽음
		for (int i = 0; i < 4; ++i)
			p[i] = Vec3::Transform(p[i], world);
		const float col[4] = { Color[0] * s.PColor[0], Color[1] * s.PColor[1], Color[2] * s.PColor[2], Color[3] * s.PColor[3] };
		batch.Quad(p, uv, SpriteBatch::PackColor(col), t->Srv.Get(), t->Point);
	});
}

bool SpriteSkinnedRenderer::SpriteLocalBounds(Vec3& bmin, Vec3& bmax)
{
	if (Skeleton() == nullptr)
		return false;
	PoseNow();
	bmin = Vec3(FLT_MAX, FLT_MAX, -0.01f);
	bmax = Vec3(-FLT_MAX, -FLT_MAX, 0.01f);
	ForEachQuad([&](const Anim2D::Slot&, const Tex*, Vec3 p[4]) {
		for (int i = 0; i < 4; ++i)
		{
			bmin.x = (std::min)(bmin.x, p[i].x); bmin.y = (std::min)(bmin.y, p[i].y);
			bmax.x = (std::max)(bmax.x, p[i].x); bmax.y = (std::max)(bmax.y, p[i].y);
		}
	});
	return bmin.x <= bmax.x;
}

// ------------------------------------------------------------------ Inspector
void SpriteSkinnedRenderer::OnInspectorGUI()
{
	// Skeleton Data [ 이름 ⊙ ] — Project 의 .skel2d 를 끌어 놓기, ⊙ = 목록
	const std::string text = m_Skeleton.empty() ? "None (2D Skeleton)" : fs::path(std::u8string(m_Skeleton.begin(), m_Skeleton.end())).stem().string();
	ImVec2 fmin, fmax;
	const int pressed = UnityGUI::ObjectFieldButtons("Skeleton", text.c_str(), "sprite_skinned_renderer", nullptr, 0, &fmin, &fmax);
	const ImVec2 after = ImGui::GetCursorScreenPos();
	if (pressed == -1)
		ImGui::OpenPopup("##skelPick");
	if (ImGui::BeginPopup("##skelPick"))
	{
		if (ImGui::Selectable("None"))
			SetSkeleton("");
		for (const std::string& p : FindSkeletons())
			if (ImGui::Selectable(p.c_str(), p == m_Skeleton))
				SetSkeleton(p);
		ImGui::EndPopup();
	}
	ImGui::SetCursorScreenPos(fmin);
	ImGui::InvisibleButton("##skelDrop", ImVec2((std::max)(1.0f, fmax.x - fmin.x - 22.0f), fmax.y - fmin.y));
	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE");
		if (payload == nullptr)
			payload = ImGui::AcceptDragDropPayload("SKEL2D_FILE");
		if (payload)
		{
			const std::string dropped(static_cast<const char*>(payload->Data));
			if (fs::path(dropped).extension() == ".skel2d")
				SetSkeleton(dropped);
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::SetCursorScreenPos(after);

	Anim2D::Document* d = Skeleton();
	if (!m_Error.empty())
		UnityGUI::HelpBox(("Cannot read the skeleton: " + m_Error).c_str());
	// Animation: 목록에서 고르기 (비면 첫 애니메이션)
	std::vector<std::string> names = { "(first)" };
	int cur = 0;
	if (d)
		for (const Anim2D::Animation& a : d->Animations)
		{
			if (a.Name == m_Animation)
				cur = (int)names.size();
			names.push_back(a.Name);
		}
	std::vector<const char*> items;
	for (const std::string& n : names)
		items.push_back(n.c_str());
	if (UnityGUI::Dropdown("Animation", &cur, items.data(), (int)items.size()))
	{
		m_Animation = cur == 0 ? std::string() : names[cur];
		TrackTime = 0.0f;
	}
	UnityGUI::Toggle("Loop", &Loop);
	UnityGUI::Float("Time Scale", &TimeScale);
	// 편집 중 미리 보기: 이 시각의 자세를 씬에 그린다 (Play 를 누르면 처음부터)
	if (d && !Application::IsPlaying())
	{
		const int ai = m_Animation.empty() ? (d->Animations.empty() ? -1 : 0) : d->FindAnim(m_Animation);
		if (ai >= 0)
			UnityGUI::Slider("Preview Time", &TrackTime, 0.0f, d->Animations[ai].Length);
	}
	UnityGUI::Float("Pixels Per Unit", &m_PixelsPerUnit);
	m_PixelsPerUnit = (std::max)(0.01f, m_PixelsPerUnit);
	UnityGUI::Color("Color", Color);
	UnityGUI::Toggle("Flip X", &FlipX);
	if (UnityGUI::FoldoutPlain("Additional Settings"))
	{
		UnityGUI::ValueLabel("Sorting Layer", "Default", 1);
		UnityGUI::Int("Order in Layer", &SortingOrder, 1);
	}
	if (d)
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%d bones, %d slots, %d animations", (int)d->Bones.size(), (int)d->Slots.size(), (int)d->Animations.size());
		UnityGUI::ValueLabel("Skeleton Info", buf);
	}
}

// ------------------------------------------------------------------ 저장
GENERATE_COMPONENT_FUNC_TOJSON(SpriteSkinnedRenderer)
{
	json j;
	j["type"] = "SpriteSkinnedRenderer";
	j["enabled"] = m_Enabled;
	j["skeleton"] = m_Skeleton;
	j["animation"] = m_Animation;
	j["loop"] = Loop;
	j["timeScale"] = TimeScale;
	j["previewTime"] = TrackTime;
	j["pixelsPerUnit"] = m_PixelsPerUnit;
	j["color"] = { Color[0], Color[1], Color[2], Color[3] };
	j["flipX"] = FlipX;
	j["sortingOrder"] = SortingOrder;
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(SpriteSkinnedRenderer)
{
	m_Enabled = j.value("enabled", true);
	SetSkeleton(j.value("skeleton", std::string()));
	m_Animation = j.value("animation", std::string());
	Loop = j.value("loop", true);
	TimeScale = j.value("timeScale", 1.0f);
	TrackTime = j.value("previewTime", 0.0f);
	m_PixelsPerUnit = (std::max)(0.01f, j.value("pixelsPerUnit", 100.0f));
	if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
		for (int i = 0; i < 4; ++i)
			Color[i] = j["color"][i].get<float>();
	FlipX = j.value("flipX", false);
	SortingOrder = j.value("sortingOrder", 0);
}
