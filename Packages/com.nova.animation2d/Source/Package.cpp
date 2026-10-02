// com.nova.animation2d 진입점: Window > 2D Animator, .skel2d 에셋, CLI 명령 "anim2d" (nova anim2d <op> …)
#include "pch.h"
#include "Anim2DWindow.h"
#include "Anim2DOps.h"
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
