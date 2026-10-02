// com.nova.animation2d 진입점: Window > 2D Animator, .skel2d 에셋, CLI 명령 "anim2d" (nova anim2d <op> …),
// 씬 컴포넌트 SpriteSkinnedRenderer (게임 빌드에도 — DLL 을 불러올 때 등록) + C# 함수 (Runtime/SpriteSkinnedRenderer.cs)
#include "pch.h"
#include "Anim2DWindow.h"
#include "Anim2DOps.h"
#include "SpriteSkinnedRenderer.h"
#include "ScriptBindings.h"
#include "CliServer.h"
#include "EditorExtensions.h"

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.animation2d";
	Anim2DWindow* s_Window = nullptr;

	// nova anim2d help: 연산 목록 (이름 → 인자 설명)
	nlohmann::json HelpList()
	{
		nlohmann::json j = nlohmann::json::object();
		for (const Anim2D::OpInfo& op : Anim2D::Ops())
			j[op.Name] = op.Help;
		j["window"] = "open / focus Window > 2D Animator";
		j["view"] = "[--frame] [--bones] [--grid]: editor window display";
		return j;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	if (Application::IsPlayer())
		return;   // 게임 빌드에는 편집기가 없다
	s_Window = new Anim2DWindow();
	EditorGUIManager::GetI()->RegisterWindow(s_Window);

	// Project 창: Create > 2D Skeleton (.skel2d), 더블클릭 = 2D Animator 에서 열기
	EditorExtensions::AssetType t;
	t.Owner = kPackage;
	t.Extension = ".skel2d";
	t.Icon = "animator_controller";
	t.CreateMenu = "2D Skeleton (Animator)";
	t.DefaultName = "New Skeleton";
	t.DragPayload = "SKEL2D_FILE";
	t.Create = [](const std::string& path) {
		Anim2D::Document doc;
		doc.New();
		std::string err;
		doc.Save(Anim2D::FullPath(path), err);
	};
	t.Open = [](const std::string& path) {
		std::string err;
		if (Anim2D::Doc().Load(Anim2D::FullPath(path), err) && s_Window)
		{
			Anim2DWindow::Focus();
			s_Window->FrameAll();
		}
	};
	EditorExtensions::RegisterAssetType(t);

	// CLI: nova anim2d <op> [--인자 …]  → {"op": "...", ...}
	CliServer::Register("anim2d", "2D animator op: {op, ...args} (nova anim2d help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
		const std::string op = args.value("op", std::string("help"));
		if (op == "help")
		{
			result = HelpList();
			return true;
		}
		if (op == "window")
		{
			Anim2DWindow::Focus();
			result = Anim2D::Doc().Summary();
			return true;
		}
		if (op == "view")
		{
			if (!s_Window) { error = "no editor window"; return false; }
			if (args.value("frame", false)) s_Window->FrameAll();
			auto& opt = s_Window->ViewOptions();
			if (args.contains("bones")) opt.Bones = args["bones"].get<bool>();
			if (args.contains("grid")) opt.Grid = args["grid"].get<bool>();
			++Anim2D::Doc().Revision;
			result = { { "bones", opt.Bones }, { "grid", opt.Grid } };
			return true;
		}
		nlohmann::json opArgs = args;
		opArgs.erase("op");
		return Anim2D::RunOp(op, opArgs, result, error);
	});
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("anim2d");
	EditorExtensions::UnregisterOwner(kPackage);
	if (s_Window)
	{
		EditorGUIManager::GetI()->UnregisterWindow(s_Window);
		delete s_Window;
		s_Window = nullptr;
	}
}

// ---- C#: SpriteSkinnedRenderer (Runtime/SpriteSkinnedRenderer.cs)
//  float 0 trackTime, 1 timeScale, 2 완료 (읽기), 10..13 color / int 0 loop, 1 flipX, 2 sortingOrder
namespace
{
	SpriteSkinnedRenderer* FindSsr(uint64 id)
	{
		GameObject* go = ScriptBindings::FindObject(id);
		return go ? go->GetComponentIncludingPending<SpriteSkinnedRenderer>() : nullptr;
	}
}

NOVA_PACKAGE_EXPORT int SSR_Play(uint64 id, const char* animation, int loop)
{
	SpriteSkinnedRenderer* r = FindSsr(id);
	return r && animation && r->Play(animation, loop != 0) ? 1 : 0;
}

NOVA_PACKAGE_EXPORT const char* SSR_Animation(uint64 id)
{
	SpriteSkinnedRenderer* r = FindSsr(id);
	return ScriptBindings::ReturnString(r ? r->GetAnimation() : std::string());
}

NOVA_PACKAGE_EXPORT float SSR_GetFloat(uint64 id, int prop)
{
	SpriteSkinnedRenderer* r = FindSsr(id);
	if (!r) return 0.0f;
	if (prop >= 10 && prop <= 13) return r->Color[prop - 10];
	switch (prop) { case 0: return r->TrackTime; case 1: return r->TimeScale; case 2: return r->IsComplete() ? 1.0f : 0.0f; default: return 0.0f; }
}

NOVA_PACKAGE_EXPORT void SSR_SetFloat(uint64 id, int prop, float v)
{
	SpriteSkinnedRenderer* r = FindSsr(id);
	if (!r) return;
	if (prop >= 10 && prop <= 13) { r->Color[prop - 10] = v; return; }
	switch (prop) { case 0: r->TrackTime = (std::max)(0.0f, v); break; case 1: r->TimeScale = v; break; default: break; }
}

NOVA_PACKAGE_EXPORT int SSR_GetInt(uint64 id, int prop)
{
	SpriteSkinnedRenderer* r = FindSsr(id);
	if (!r) return 0;
	switch (prop) { case 0: return r->Loop ? 1 : 0; case 1: return r->FlipX ? 1 : 0; case 2: return r->SortingOrder; default: return 0; }
}

NOVA_PACKAGE_EXPORT void SSR_SetInt(uint64 id, int prop, int v)
{
	SpriteSkinnedRenderer* r = FindSsr(id);
	if (!r) return;
	switch (prop) { case 0: r->Loop = v != 0; break; case 1: r->FlipX = v != 0; break; case 2: r->SortingOrder = v; break; default: break; }
}
