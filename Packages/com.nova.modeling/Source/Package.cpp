// com.nova.modeling 진입점: Window > Model Editor, .nmodel 에셋, CLI 명령 "model" (nova model <op> …)
#include "pch.h"
#include "ModelEditorWindow.h"
#include "ModelOps.h"
#include "CliServer.h"
#include "EditorExtensions.h"
#include <filesystem>

NOVA_PACKAGE_EXPORT const char* NovaPackage_Abi() { return NOVA_PACKAGE_ABI_VERSION; }

namespace
{
	constexpr const char* kPackage = "com.nova.modeling";
	ModelEditorWindow* s_Window = nullptr;

	// Project 창이 주는 경로 (Assets\... 상대) → 절대 경로 (UTF-8)
	std::string FullPath(const std::string& rel)
	{
		std::filesystem::path p(string_to_wstring(rel));
		if (!p.is_absolute()) p = PathManager::GetI()->GetMovePathW(string_to_wstring(rel));
		return Modeling::U8String(p);
	}

	// nova model help: 연산 목록 (이름 → 인자 설명)
	nlohmann::json HelpList()
	{
		nlohmann::json j = nlohmann::json::object();
		for (const Modeling::OpInfo& op : Modeling::Ops())
			j[op.Name] = op.Help;
		j["window"] = "open / focus Window > Model Editor";
		j["view"] = "[--preset front|back|left|right|top|bottom|persp] [--frame] [--shading solid|toon|normals|uv] [--outline] [--wire] [--xray]: editor window camera / display";
		return j;
	}
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnLoad()
{
	if (Application::IsPlayer())
		return;   // 게임 빌드에는 편집기가 없다
	s_Window = new ModelEditorWindow();
	EditorGUIManager::GetI()->RegisterWindow(s_Window);

	// Project 창: Create > Model (.nmodel), 더블클릭 = Model Editor 에서 열기
	EditorExtensions::AssetType t;
	t.Owner = kPackage;
	t.Extension = ".nmodel";
	t.Icon = "asset_model";
	t.CreateMenu = "Model (Editable Mesh)";
	t.DefaultName = "New Model";
	t.DragPayload = "NMODEL_FILE";
	t.Create = [](const std::string& path) {
		Modeling::Document doc;
		doc.AddObject("Cube");
		doc.Objects[0].M.AddCube(Vec3(1, 1, 1), Vec3(0, 0.5f, 0));
		std::string err;
		doc.Save(FullPath(path), err);
	};
	t.Open = [](const std::string& path) {
		std::string err;
		if (Modeling::Doc().Load(FullPath(path), err) && s_Window)
		{
			ModelEditorWindow::Focus();
			s_Window->FrameAll();
		}
	};
	EditorExtensions::RegisterAssetType(t);

	// CLI: nova model <op> [--인자 …]  → {"op": "...", ...}
	CliServer::Register("model", "model editor op: {op, ...args} (nova model help)", [](const nlohmann::json& args, nlohmann::json& result, std::string& error) {
		const std::string op = args.value("op", std::string("help"));
		if (op == "help")
		{
			result = HelpList();
			return true;
		}
		if (op == "window")
		{
			ModelEditorWindow::Focus();
			result = Modeling::Doc().Summary(false);
			return true;
		}
		if (op == "view")
		{
			if (!s_Window) { error = "no editor window"; return false; }
			auto& cam = s_Window->Camera();
			const std::string preset = args.value("preset", std::string());
			if (!preset.empty() && !cam.SetPreset(preset)) { error = "unknown preset '" + preset + "'"; return false; }
			if (args.value("frame", false)) s_Window->FrameAll();
			// 창 보기 설정: --shading solid|toon|normals|uv, --outline, --wire, --xray
			auto& opt = s_Window->ViewOptions();
			const std::string shade = args.value("shading", std::string());
			if (shade == "solid") opt.Shade = Modeling::Shading::Solid;
			else if (shade == "toon") opt.Shade = Modeling::Shading::Toon;
			else if (shade == "normals") opt.Shade = Modeling::Shading::Normals;
			else if (shade == "uv") opt.Shade = Modeling::Shading::UVChecker;
			if (args.contains("outline")) opt.Outline = args["outline"].get<bool>();
			if (args.contains("wire")) opt.Wireframe = args["wire"].get<bool>();
			if (args.contains("xray")) opt.XRay = args["xray"].get<bool>();
			++Modeling::Doc().Revision;
			result = { { "yaw", cam.Yaw }, { "pitch", cam.Pitch }, { "distance", cam.Distance }, { "ortho", cam.Ortho } };
			return true;
		}
		nlohmann::json opArgs = args;
		opArgs.erase("op");
		return Modeling::RunOp(op, opArgs, result, error);
	});
}

NOVA_PACKAGE_EXPORT void NovaPackage_OnUnload()
{
	CliServer::Unregister("model");
	EditorExtensions::UnregisterOwner(kPackage);
	if (s_Window)
	{
		EditorGUIManager::GetI()->UnregisterWindow(s_Window);
		delete s_Window;
		s_Window = nullptr;
	}
}
