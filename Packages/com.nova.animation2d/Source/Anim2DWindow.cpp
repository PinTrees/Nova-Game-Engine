#include "pch.h"
#include "Anim2DWindow.h"
#include "Anim2DOps.h"
#include "UndoSystem.h"
#include "ImGui/imgui_internal.h"
#include <functional>
#include <set>
#include <commdlg.h>
#include <filesystem>

using namespace Anim2D;
using json = nlohmann::json;

Anim2DWindow* Anim2DWindow::s_Instance = nullptr;

namespace
{
	constexpr float kPi = 3.14159265358979f;

	std::string Utf8(const std::wstring& w)
	{
		if (w.empty()) return std::string();
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	std::string FileDialog(bool save, const wchar_t* filter, const wchar_t* defExt)
	{
		wchar_t buf[1024] = {};
		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = Application::GetI()->GetMainHwnd();
		ofn.lpstrFile = buf;
		ofn.nMaxFile = 1024;
		ofn.lpstrFilter = filter;
		ofn.lpstrDefExt = defExt;
		ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
		return (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) ? Utf8(buf) : std::string();
	}

	// 점 p 에서 선분 ab 까지 (화면 픽셀)
	float SegDist(float px, float py, float ax, float ay, float bx, float by)
	{
		const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
		const float t = l2 > 1e-6f ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.0f, 1.0f) : 0.0f;
		const float ex = ax + dx * t - px, ey = ay + dy * t - py;
		return sqrtf(ex * ex + ey * ey);
	}
}

Anim2DWindow::Anim2DWindow()
	: EditorWindow("2D Animator", ICON_FA_PERSON_RUNNING)
{
	s_Instance = this;
	SetIsOpened(false);
	if (Doc().Bones.empty()) Doc().New();
}

Anim2DWindow::~Anim2DWindow()
{
	if (s_Instance == this) s_Instance = nullptr;
}

void Anim2DWindow::Focus()
{
	if (!s_Instance) return;
	s_Instance->SetIsOpened(true);
	ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());
}

void Anim2DWindow::BeforeBegin()
{
	ImGui::SetNextWindowSize(ImVec2(1200.0f, 780.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* scene = EditorGUIManager::GetI()->FindWindow("Scene"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(scene->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void Anim2DWindow::Update()
{
	// 재생: 시간 → 자세 (루프)
	Document& d = Doc();
	if (m_Playing && d.Active())
	{
		Animation* a = d.Active();
		d.AnimateMode = true;
		d.Time += ImGui::GetIO().DeltaTime;
		if (d.Time > a->Length) d.Time = a->Loop ? fmodf(d.Time, a->Length) : a->Length;
		d.Repose();
		++d.Revision;
	}
}

bool Anim2DWindow::Run(const std::string& op, const json& args)
{
	json r;
	std::string err;
	if (!RunOp(op, args, r, err)) { SetStatus("! " + err); return false; }
	SetStatus(op);
	m_Dirty = true;
	return true;
}

void Anim2DWindow::FrameAll()
{
	Document& d = Doc();
	d.Pose();
	float mnx, mny, mxx, mxy;
	if (!Raster::Bounds(d, mnx, mny, mxx, mxy, true) || m_ViewSize.x < 16) { m_View = View2D(); m_Dirty = true; return; }
	m_View.CX = (mnx + mxx) * 0.5f;
	m_View.CY = (mny + mxy) * 0.5f;
	m_View.Zoom = std::clamp((std::min)(m_ViewSize.x / ((mxx - mnx) * 1.3f + 1), m_ViewSize.y / ((mxy - mny) * 1.3f + 1)), 0.02f, 50.0f);
	m_Dirty = true;
}

// ------------------------------------------------------------------ 파일
void Anim2DWindow::OpenFile()
{
	const std::string p = FileDialog(false, L"2D skeleton (*.skel2d)\0*.skel2d\0\0", L"skel2d");
	if (!p.empty() && Run("open", { { "path", p } })) FrameAll();
}

void Anim2DWindow::SaveFile(bool as)
{
	std::string p = Doc().Path;
	if (as || p.empty()) p = FileDialog(true, L"2D skeleton (*.skel2d)\0*.skel2d\0\0", L"skel2d");
	if (!p.empty()) Run("save", { { "path", p } });
}

void Anim2DWindow::ExportSheet()
{
	if (!Doc().Active()) { SetStatus("! no animation to export"); return; }
	const std::string p = FileDialog(true, L"Sprite sheet (*.png)\0*.png\0\0", L"png");
	if (!p.empty()) Run("export", { { "path", p }, { "anim", Doc().Active()->Name } });
}

void Anim2DWindow::AddImage(const std::string& path, const float* world)
{
	Document& d = Doc();
	const int bone = d.SelectedBone >= 0 ? d.SelectedBone : 0;
	if (d.Bones.empty()) return;
	json args = { { "bone", d.Bones[bone].Name }, { "image", path } };
	if (world)
	{
		// 놓은 자리 → 본 기준 (본 역행렬)
		d.Pose();
		const Bone& b = d.Bones[bone];
		const float det = b.A * b.D - b.B * b.C;
		if (fabsf(det) > 1e-9f)
		{
			const float x = world[0] - b.WX, y = world[1] - b.WY;
			args["x"] = (b.D * x - b.B * y) / det;
			args["y"] = (b.A * y - b.C * x) / det;
		}
	}
	Run("image.add", args);
}

// ------------------------------------------------------------------ 그리기
void Anim2DWindow::UploadTexture()
{
	auto device = Application::GetI()->GetDevice();
	auto ctx = Application::GetI()->GetDeviceContext();
	if (!device || !ctx) return;
	if (!m_Texture || m_TexW != m_Raster.Width || m_TexH != m_Raster.Height)
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = m_Raster.Width;
		td.Height = m_Raster.Height;
		td.MipLevels = td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		m_Srv.Reset();
		m_Texture.Reset();
		if (FAILED(device->CreateTexture2D(&td, nullptr, m_Texture.GetAddressOf()))) return;
		device->CreateShaderResourceView(m_Texture.Get(), nullptr, m_Srv.GetAddressOf());
		m_TexW = m_Raster.Width;
		m_TexH = m_Raster.Height;
	}
	ctx->UpdateSubresource(m_Texture.Get(), 0, nullptr, m_Raster.Color.data(), m_Raster.Width * 4, 0);
}

void Anim2DWindow::RenderViewport()
{
	Document& d = Doc();
	const int w = (std::max)(8, (int)m_ViewSize.x), h = (std::max)(8, (int)m_ViewSize.y);
	if (w != m_Raster.Width || h != m_Raster.Height) { m_Raster.Resize(w, h); m_Dirty = true; }
	if (d.Revision != m_DrawnRevision) m_Dirty = true;
	if (!m_Dirty) return;
	m_Opt.SelectedBone = d.SelectedBone;
	m_Opt.SelectedSlot = d.SelectedSlot;
	m_Raster.Render(d, m_View, m_Opt);
	UploadTexture();
	m_DrawnRevision = d.Revision;
	m_Dirty = false;
}

void Anim2DWindow::DrawMenuBar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
	if (!ImGui::BeginMenuBar())
	{
		ImGui::PopStyleVar(2);
		return;
	}
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New")) { Run("new"); FrameAll(); }
		if (ImGui::MenuItem("Open...")) OpenFile();
		ImGui::Separator();
		if (ImGui::MenuItem("Save", "Ctrl+S")) SaveFile(false);
		if (ImGui::MenuItem("Save As...")) SaveFile(true);
		ImGui::Separator();
		if (ImGui::MenuItem("Export Sprite Sheet...", nullptr, false, Doc().Active() != nullptr)) ExportSheet();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		if (ImGui::MenuItem("Undo", "Ctrl+Z")) Run("undo");
		if (ImGui::MenuItem("Redo", "Ctrl+Y")) Run("redo");
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		if (ImGui::MenuItem("Bones", nullptr, m_Opt.Bones)) { m_Opt.Bones = !m_Opt.Bones; m_Dirty = true; }
		if (ImGui::MenuItem("Grid", nullptr, m_Opt.Grid)) { m_Opt.Grid = !m_Opt.Grid; m_Dirty = true; }
		if (ImGui::MenuItem("Frame All", "F")) FrameAll();
		ImGui::EndMenu();
	}
	if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < 6.0)
	{
		ImGui::SameLine(0, 30);
		ImGui::TextColored(m_Status[0] == '!' ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", m_Status.c_str());
	}
	ImGui::EndMenuBar();
	ImGui::PopStyleVar(2);
}

void Anim2DWindow::DrawToolbar()
{
	Document& d = Doc();
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6, ImGui::GetCursorPosY() + 4));
	// 모드
	const bool animate = d.AnimateMode;
	ImGui::PushStyleColor(ImGuiCol_Button, !animate ? ImVec4(0.2f, 0.45f, 0.75f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
	if (ImGui::Button("Setup")) Run("mode", { { "mode", "setup" } });
	ImGui::PopStyleColor();
	ImGui::SameLine(0, 2);
	ImGui::PushStyleColor(ImGuiCol_Button, animate ? ImVec4(0.75f, 0.35f, 0.2f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
	if (ImGui::Button("Animate"))
	{
		if (!d.Active()) Run("anim.new", { { "name", std::string(m_NewAnim) } });
		else Run("mode", { { "mode", "animate" } });
	}
	ImGui::PopStyleColor();
	ImGui::SameLine(0, 18);
	auto toolButton = [&](const char* label, Tool t, const char* tip) {
		const bool on = m_Tool == t;
		if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.36f, 1));
		if (ImGui::Button(label)) m_Tool = t;
		if (on) ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
		ImGui::SameLine(0, 2);
	};
	toolButton(ICON_FA_ROTATE " Rotate", Tool::Rotate, "R: drag a bone to rotate it around its head");
	toolButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Translate", Tool::Translate, "T: drag to move the bone");
	toolButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER " Scale", Tool::Scale, "S: drag away from the head to scale");
	if (!animate) toolButton(ICON_FA_BONE " Create", Tool::CreateBone, "B: drag to create a child bone of the selected bone (head = press, tail = release)");
	ImGui::SameLine(0, 18);
	if (ImGui::Button(ICON_FA_IMAGE " Add Image..."))
	{
		const std::string p = FileDialog(false, L"Images (*.png)\0*.png\0\0", L"png");
		if (!p.empty()) AddImage(p, nullptr);
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Image on the selected bone (or drag a PNG from the Project window into the view)");
}

void Anim2DWindow::DrawTree()
{
	Document& d = Doc();
	ImGui::TextDisabled("HIERARCHY");
	ImGui::BeginChild("##tree", ImVec2(0, (std::max)(160.0f, ImGui::GetContentRegionAvail().y * 0.55f)), true);
	std::function<void(int, int)> bone = [&](int i, int depth) {
		const Bone& b = d.Bones[i];
		ImGui::PushID(i);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * 12.0f);
		if (ImGui::Selectable((std::string(ICON_FA_BONE " ") + b.Name).c_str(), d.SelectedBone == i && d.SelectedSlot < 0))
		{
			d.SelectedBone = i; d.SelectedSlot = -1; d.Changed();
			strncpy_s(m_Rename, b.Name.c_str(), _TRUNCATE);
		}
		ImGui::PopID();
		for (int s = 0; s < (int)d.Slots.size(); ++s)
		{
			if (d.Slots[s].Bone != i) continue;
			ImGui::PushID(10000 + s);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * 12.0f + 14.0f);
			if (ImGui::Selectable((std::string(ICON_FA_IMAGE " ") + d.Slots[s].Name).c_str(), d.SelectedSlot == s))
			{
				d.SelectedSlot = s; d.SelectedBone = i; d.Changed();
				strncpy_s(m_Rename, d.Slots[s].Name.c_str(), _TRUNCATE);
			}
			ImGui::PopID();
		}
		for (int c = 0; c < (int)d.Bones.size(); ++c) if (d.Bones[c].Parent == i) bone(c, depth + 1);
	};
	for (int i = 0; i < (int)d.Bones.size(); ++i) if (d.Bones[i].Parent < 0) bone(i, 0);
	ImGui::EndChild();
}

void Anim2DWindow::DrawProperties()
{
	Document& d = Doc();
	ImGui::Spacing();
	if (d.SelectedSlot >= 0 && d.SelectedSlot < (int)d.Slots.size())
	{
		Slot& s = d.Slots[d.SelectedSlot];
		ImGui::TextDisabled("SLOT  %s  (bone %s)", s.Name.c_str(), d.Bones[s.Bone].Name.c_str());
		// 첨부 (지금 보이는 것): Setup = 셋업 첨부, Animate = 이 시각의 첨부 (키로)
		const std::string& cur = d.AnimateMode ? s.Current : s.SetupAttachment;
		if (ImGui::BeginCombo("Attachment", cur.empty() ? "(none)" : cur.c_str()))
		{
			if (ImGui::Selectable("(none)", cur.empty())) d.AnimateMode ? Run("pose", { { "slot", s.Name }, { "attachment", "" } }) : Run("slot.set", { { "name", s.Name }, { "attachment", "" } });
			for (const Attachment& a : s.Attachments)
				if (ImGui::Selectable(a.Name.c_str(), a.Name == cur))
					d.AnimateMode ? Run("pose", { { "slot", s.Name }, { "attachment", a.Name } }) : Run("slot.set", { { "name", s.Name }, { "attachment", a.Name } });
			ImGui::EndCombo();
		}
		float col[4];
		float* src = d.AnimateMode ? s.PColor : s.Color;
		memcpy(col, src, sizeof(col));
		if (ImGui::ColorEdit4("Color", col, ImGuiColorEditFlags_NoInputs))
			d.AnimateMode ? Run("pose", { { "slot", s.Name }, { "color", { col[0], col[1], col[2], col[3] } } }) : Run("slot.set", { { "name", s.Name }, { "color", { col[0], col[1], col[2], col[3] } } });
		if (!d.AnimateMode)
			if (const Attachment* a = s.Find(s.SetupAttachment))
			{
				float pos[2] = { a->X, a->Y }, rot = a->Rotation, sc[2] = { a->ScaleX, a->ScaleY };
				bool ch = ImGui::DragFloat2("Offset", pos, 0.5f);
				ch |= ImGui::DragFloat("Rotation##att", &rot, 0.5f);
				ch |= ImGui::DragFloat2("Scale##att", sc, 0.01f);
				if (ch) Run("attachment.set", { { "slot", s.Name }, { "x", pos[0] }, { "y", pos[1] }, { "rotation", rot }, { "scale", { sc[0], sc[1] } } });
				ImGui::TextDisabled("%s", a->Image.c_str());
			}
		if (ImGui::Button("Back")) Run("slot.set", { { "name", s.Name }, { "order", "back" } });
		ImGui::SameLine();
		if (ImGui::Button("Backward")) Run("slot.set", { { "name", s.Name }, { "order", "backward" } });
		ImGui::SameLine();
		if (ImGui::Button("Forward")) Run("slot.set", { { "name", s.Name }, { "order", "forward" } });
		ImGui::SameLine();
		if (ImGui::Button("Front")) Run("slot.set", { { "name", s.Name }, { "order", "front" } });
		if (ImGui::Button(ICON_FA_IMAGE " Add Attachment..."))
		{
			const std::string p = FileDialog(false, L"Images (*.png)\0*.png\0\0", L"png");
			if (!p.empty())
				Run("attachment.add", { { "slot", s.Name }, { "name", std::filesystem::path(std::u8string(p.begin(), p.end())).stem().string() }, { "image", p } });
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_TRASH)) Run("slot.delete", { { "name", s.Name } });
		return;
	}
	if (d.SelectedBone >= 0 && d.SelectedBone < (int)d.Bones.size())
	{
		Bone& b = d.Bones[d.SelectedBone];
		ImGui::TextDisabled("BONE  %s  %s", b.Name.c_str(), d.AnimateMode ? "(pose at this time)" : "(setup)");
		float pos[2] = { d.AnimateMode ? b.PX : b.X, d.AnimateMode ? b.PY : b.Y };
		float rot = d.AnimateMode ? b.PR : b.Rotation;
		float sc[2] = { d.AnimateMode ? b.PSX : b.ScaleX, d.AnimateMode ? b.PSY : b.ScaleY };
		float len = b.Length;
		bool ch = ImGui::DragFloat2("Position", pos, 0.5f);
		ch |= ImGui::DragFloat("Rotation", &rot, 0.5f);
		ch |= ImGui::DragFloat2("Scale", sc, 0.01f);
		bool chLen = !d.AnimateMode && ImGui::DragFloat("Length", &len, 0.5f, 0.0f, 10000.0f);
		if (ch || chLen)
		{
			json args = { { d.AnimateMode ? "bone" : "name", b.Name }, { "x", pos[0] }, { "y", pos[1] }, { "rotation", rot }, { "scale", { sc[0], sc[1] } } };
			if (!d.AnimateMode) args["length"] = len;
			Run(d.AnimateMode ? "pose" : "bone.set", args);
		}
		if (!d.AnimateMode)
		{
			ImGui::SetNextItemWidth(150);
			ImGui::InputText("##rename", m_Rename, sizeof(m_Rename));
			ImGui::SameLine();
			if (ImGui::Button("Rename")) Run("bone.set", { { "name", b.Name }, { "rename", std::string(m_Rename) } });
			ImGui::SameLine();
			if (b.Parent >= 0 && ImGui::Button(ICON_FA_TRASH)) Run("bone.delete", { { "name", b.Name } });
		}
	}
	else
		ImGui::TextDisabled("Click a bone or an image");
}

void Anim2DWindow::DrawTimeline(float height)
{
	Document& d = Doc();
	ImGui::BeginChild("##timeline", ImVec2(0, height), true);
	// 애니메이션 고르기 · 새로
	Animation* a = d.Active();
	ImGui::SetNextItemWidth(140);
	if (ImGui::BeginCombo("##anim", a ? a->Name.c_str() : "(no animation)"))
	{
		for (int i = 0; i < (int)d.Animations.size(); ++i)
			if (ImGui::Selectable(d.Animations[i].Name.c_str(), i == d.ActiveAnim)) Run("anim.select", { { "name", d.Animations[i].Name } });
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90);
	ImGui::InputText("##newAnim", m_NewAnim, sizeof(m_NewAnim));
	ImGui::SameLine();
	if (ImGui::Button("+ Animation")) Run("anim.new", { { "name", std::string(m_NewAnim) } });
	a = d.Active();
	if (!a) { ImGui::TextDisabled("Make an animation, then pose bones in Animate mode and press Key (K)"); ImGui::EndChild(); return; }
	ImGui::SameLine(0, 20);
	if (ImGui::Button(m_Playing ? ICON_FA_PAUSE : ICON_FA_PLAY)) { m_Playing = !m_Playing; if (m_Playing) d.AnimateMode = true; }
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_KEY " Key")) Run("anim.key", { { "curve", m_Curve == 1 ? "stepped" : (m_Curve == 2 ? "smooth" : "linear") } });
	ImGui::SameLine();
	static const char* kCurves[] = { "Linear", "Stepped", "Smooth" };
	ImGui::SetNextItemWidth(80);
	ImGui::Combo("##curve", &m_Curve, kCurves, 3);
	ImGui::SameLine();
	ImGui::Checkbox("Auto Key", &m_AutoKey);
	ImGui::SameLine();
	float len = a->Length;
	ImGui::SetNextItemWidth(70);
	if (ImGui::DragFloat("Length", &len, 0.05f, 0.05f, 600.0f, "%.2f s")) Run("anim.set", { { "length", len } });
	ImGui::SameLine();
	bool loop = a->Loop;
	if (ImGui::Checkbox("Loop", &loop)) Run("anim.set", { { "loop", loop } });

	// 눈금 + 키 (위 줄 = 모든 키, 아래 줄 = 고른 본)
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const float w = ImGui::GetContentRegionAvail().x - 8, h = ImGui::GetContentRegionAvail().y - 6;
	ImGui::InvisibleButton("##track", ImVec2(w, h));
	const float x0 = p0.x + 4, x1 = p0.x + w - 4;
	auto tx = [&](float t) { return x0 + (x1 - x0) * (a->Length > 0 ? t / a->Length : 0); };
	dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(30, 30, 33, 255));
	for (int k = 0; k <= 20; ++k)
	{
		const float t = a->Length * k / 20.0f, x = tx(t);
		dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + (k % 5 == 0 ? 14 : 8)), IM_COL32(110, 110, 110, 255));
		if (k % 5 == 0) { char buf[16]; snprintf(buf, sizeof(buf), "%.2f", t); dl->AddText(ImVec2(x + 2, p0.y), IM_COL32(140, 140, 140, 255), buf); }
	}
	std::set<float> all, sel;
	const std::string selBone = d.SelectedBone >= 0 && d.SelectedBone < (int)d.Bones.size() ? d.Bones[d.SelectedBone].Name : "";
	for (const auto& [name, tl] : a->Bones)
		for (const auto* keys : { &tl.Rotate, &tl.Translate, &tl.Scale })
			for (const Key& k : *keys) { all.insert(roundf(k.Time * 1000) / 1000); if (name == selBone) sel.insert(roundf(k.Time * 1000) / 1000); }
	for (const auto& [name, tl] : a->Slots) { for (const Key& k : tl.Color) all.insert(roundf(k.Time * 1000) / 1000); for (const auto& k : tl.Attach) all.insert(roundf(k.first * 1000) / 1000); }
	auto diamond = [&](float x, float y, ImU32 c) { dl->AddQuadFilled(ImVec2(x, y - 5), ImVec2(x + 5, y), ImVec2(x, y + 5), ImVec2(x - 5, y), c); };
	const float yAll = p0.y + 30, ySel = p0.y + 54;
	for (float t : all) diamond(tx(t), yAll, IM_COL32(230, 190, 60, 255));
	for (float t : sel) diamond(tx(t), ySel, IM_COL32(60, 170, 255, 255));
	if (!selBone.empty()) dl->AddText(ImVec2(x0 + 2, ySel + 8), IM_COL32(120, 160, 200, 255), selBone.c_str());
	// 지금 시각 (끌어서 옮김)
	const float cx = tx(d.Time);
	dl->AddLine(ImVec2(cx, p0.y), ImVec2(cx, p0.y + h), IM_COL32(255, 80, 70, 255), 2.0f);
	if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f) * a->Length;
		// 키 가까이면 그 키에 붙는다
		float snap = t;
		for (float k : all) if (fabsf(tx(k) - ImGui::GetIO().MousePos.x) < 6) snap = k;
		m_Playing = false;
		Run("anim.time", { { "time", snap } });
	}
	ImGui::EndChild();
}

// ------------------------------------------------------------------ 입력
int Anim2DWindow::PickBone(ImVec2 m) const
{
	const Document& d = Doc();
	int best = -1;
	float bestD = 8.0f;
	for (int i = 0; i < (int)d.Bones.size(); ++i)
	{
		const Bone& b = d.Bones[i];
		float hx, hy, tx, ty;
		m_Raster.WorldToScreen(b.WX, b.WY, hx, hy);
		m_Raster.WorldToScreen(b.A * b.Length + b.WX, b.C * b.Length + b.WY, tx, ty);
		const float dd = SegDist(m.x, m.y, hx, hy, tx, ty) - (i == d.SelectedBone ? 2.0f : 0.0f);
		if (dd < bestD) { bestD = dd; best = i; }
	}
	return best;
}

void Anim2DWindow::BeginDrag()
{
	Document& d = Doc();
	m_Dragging = true;
	d.PushUndo(m_Tool == Tool::CreateBone ? "create bone" : "drag bone");
	m_Raster.ScreenToWorld(m_PressMouse.x, m_PressMouse.y, m_StartWorld[0], m_StartWorld[1]);
	if (m_Tool == Tool::CreateBone) return;
	const Bone& b = d.Bones[d.SelectedBone];
	if (d.AnimateMode) { m_StartValue[0] = b.PX; m_StartValue[1] = b.PY; m_StartValue[2] = b.PR; m_StartValue[3] = b.PSX; m_StartValue[4] = b.PSY; }
	else { m_StartValue[0] = b.X; m_StartValue[1] = b.Y; m_StartValue[2] = b.Rotation; m_StartValue[3] = b.ScaleX; m_StartValue[4] = b.ScaleY; }
	m_StartAngle = atan2f(m_StartWorld[1] - b.WY, m_StartWorld[0] - b.WX);
	m_StartDist = (std::max)(1.0f, sqrtf((m_StartWorld[0] - b.WX) * (m_StartWorld[0] - b.WX) + (m_StartWorld[1] - b.WY) * (m_StartWorld[1] - b.WY)));
}

void Anim2DWindow::ApplyDrag(ImVec2 mouse)
{
	Document& d = Doc();
	float wx, wy;
	m_Raster.ScreenToWorld(mouse.x, mouse.y, wx, wy);
	if (m_Tool == Tool::CreateBone) { m_Dirty = true; return; }   // 놓을 때 만든다 (덧그림으로 미리 보기)
	Bone& b = d.Bones[d.SelectedBone];
	float* X = d.AnimateMode ? &b.PX : &b.X;
	float* Y = d.AnimateMode ? &b.PY : &b.Y;
	float* R = d.AnimateMode ? &b.PR : &b.Rotation;
	float* SX = d.AnimateMode ? &b.PSX : &b.ScaleX;
	float* SY = d.AnimateMode ? &b.PSY : &b.ScaleY;
	if (m_Tool == Tool::Rotate)
	{
		float delta = (atan2f(wy - b.WY, wx - b.WX) - m_StartAngle) * 180.0f / kPi;
		while (delta > 180) delta -= 360;
		while (delta < -180) delta += 360;
		*R = m_StartValue[2] + delta;
		if (ImGui::GetIO().KeyCtrl) *R = roundf(*R / 15.0f) * 15.0f;
	}
	else if (m_Tool == Tool::Translate)
	{
		// 시작 머리 월드 + 마우스 이동 → 부모 기준
		float sx0, sy0, sx1, sy1;
		d.WorldToParent(d.SelectedBone, m_StartWorld[0], m_StartWorld[1], sx0, sy0);
		d.WorldToParent(d.SelectedBone, wx, wy, sx1, sy1);
		*X = m_StartValue[0] + (sx1 - sx0);
		*Y = m_StartValue[1] + (sy1 - sy0);
	}
	else if (m_Tool == Tool::Scale)
	{
		const float dist = sqrtf((wx - b.WX) * (wx - b.WX) + (wy - b.WY) * (wy - b.WY));
		const float k = (std::max)(0.01f, dist / m_StartDist);
		*SX = m_StartValue[3] * k;
		*SY = ImGui::GetIO().KeyShift ? m_StartValue[4] : m_StartValue[4] * k;
	}
	if (!d.AnimateMode) d.ResetPose();
	else { d.Posed = true; d.UpdateWorld(); }
	++d.Revision;
	m_Dirty = true;
}

void Anim2DWindow::EndDrag()
{
	Document& d = Doc();
	m_Dragging = false;
	if (m_Tool == Tool::CreateBone)
	{
		float wx, wy;
		const ImVec2 mouse = ToView(ImGui::GetIO().MousePos);
		m_Raster.ScreenToWorld(mouse.x, mouse.y, wx, wy);
		const float dx = wx - m_StartWorld[0], dy = wy - m_StartWorld[1];
		d.CancelUndo();   // 연산이 스스로 Undo 를 쌓는다
		Run("bone.add", { { "name", "bone" }, { "parent", d.Bones[d.SelectedBone >= 0 ? d.SelectedBone : 0].Name }, { "world", true }, { "x", m_StartWorld[0] }, { "y", m_StartWorld[1] },
			{ "rotation", atan2f(dy, dx) * 180.0f / kPi }, { "length", sqrtf(dx * dx + dy * dy) } });
		return;
	}
	if (d.AnimateMode && m_AutoKey && d.Active() && d.SelectedBone >= 0)
	{
		d.KeyPose(*d.Active(), d.Time, { d.Bones[d.SelectedBone].Name }, m_Curve == 1 ? Curve::Stepped : (m_Curve == 2 ? Curve::Smooth : Curve::Linear), false);
		d.Posed = false;
		SetStatus("auto key " + d.Bones[d.SelectedBone].Name);
	}
	d.Changed();
}

void Anim2DWindow::HandleViewport(bool hovered)
{
	ImGuiIO& io = ImGui::GetIO();
	Document& d = Doc();
	const ImVec2 mouse = ToView(io.MousePos);
	// 확대 (마우스 자리 고정) · 화면 이동
	if (hovered && io.MouseWheel != 0.0f)
	{
		float wx, wy;
		m_Raster.ScreenToWorld(mouse.x, mouse.y, wx, wy);
		m_View.Zoom = std::clamp(m_View.Zoom * powf(1.15f, io.MouseWheel), 0.02f, 50.0f);
		m_Raster.View = m_View;
		float wx2, wy2;
		m_Raster.ScreenToWorld(mouse.x, mouse.y, wx2, wy2);
		m_View.CX += wx - wx2;
		m_View.CY += wy - wy2;
		m_Dirty = true;
	}
	if ((ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0)) && (hovered || ImGui::IsItemActive()))
	{
		m_View.CX -= io.MouseDelta.x / m_View.Zoom;
		m_View.CY += io.MouseDelta.y / m_View.Zoom;
		m_Dirty = true;
	}
	// 왼쪽: 누르면 고르고, 움직이면 도구
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_PressPending = true;
		m_PressMouse = mouse;
		if (m_Tool != Tool::CreateBone)
		{
			const int b = PickBone(mouse);
			if (b >= 0) { d.SelectedBone = b; d.SelectedSlot = -1; }
			else
			{
				const int x = (int)mouse.x, y = (int)mouse.y;
				const int s = x >= 0 && y >= 0 && x < m_Raster.Width && y < m_Raster.Height ? m_Raster.SlotId[(size_t)y * m_Raster.Width + x] : -1;
				if (s >= 0) { d.SelectedSlot = s; d.SelectedBone = d.Slots[s].Bone; }
				else if (!io.KeyShift) { d.SelectedSlot = -1; }
			}
			++d.Revision;
		}
	}
	if (m_PressPending && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const float dx = mouse.x - m_PressMouse.x, dy = mouse.y - m_PressMouse.y;
		if (!m_Dragging && dx * dx + dy * dy > 9.0f && (m_Tool == Tool::CreateBone || d.SelectedBone >= 0))
		{
			if (d.AnimateMode && !d.Active()) m_PressPending = false;
			else BeginDrag();
		}
		if (m_Dragging) ApplyDrag(mouse);
	}
	if (m_PressPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		m_PressPending = false;
		if (m_Dragging) EndDrag();
	}
	// Project 창에서 PNG 를 끌어 놓기 → 고른 본에 그림
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PNG_FILE"))
		{
			float w[2];
			m_Raster.ScreenToWorld(mouse.x, mouse.y, w[0], w[1]);
			AddImage(std::string((const char*)p->Data), w);
		}
		ImGui::EndDragDropTarget();
	}
}

void Anim2DWindow::HandleShortcuts()
{
	ImGuiIO& io = ImGui::GetIO();
	Document& d = Doc();
	if (io.KeyCtrl)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Z)) Run(io.KeyShift ? "redo" : "undo");
		if (ImGui::IsKeyPressed(ImGuiKey_Y)) Run("redo");
		if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveFile(false);
		return;
	}
	if (ImGui::IsAnyItemActive()) return;
	if (ImGui::IsKeyPressed(ImGuiKey_R)) m_Tool = Tool::Rotate;
	if (ImGui::IsKeyPressed(ImGuiKey_T)) m_Tool = Tool::Translate;
	if (ImGui::IsKeyPressed(ImGuiKey_S)) m_Tool = Tool::Scale;
	if (ImGui::IsKeyPressed(ImGuiKey_B) && !d.AnimateMode) m_Tool = Tool::CreateBone;
	if (ImGui::IsKeyPressed(ImGuiKey_F)) FrameAll();
	if (ImGui::IsKeyPressed(ImGuiKey_K) && d.Active()) Run("anim.key", { { "curve", m_Curve == 1 ? "stepped" : (m_Curve == 2 ? "smooth" : "linear") } });
	if (ImGui::IsKeyPressed(ImGuiKey_Space) && d.Active()) { m_Playing = !m_Playing; d.AnimateMode = true; }
	if (ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		if (d.SelectedSlot >= 0) Run("slot.delete", { { "name", d.Slots[d.SelectedSlot].Name } });
		else if (d.SelectedBone > 0) Run("bone.delete", { { "name", d.Bones[d.SelectedBone].Name } });
	}
}

void Anim2DWindow::OnRender()
{
	Document& d = Doc();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		Undo::BlockShortcuts();
	DrawMenuBar();
	DrawToolbar();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float panelW = (std::min)(m_PanelWidth, avail.x * 0.4f);
	const float timelineH = (std::min)(m_TimelineHeight, avail.y * 0.4f);
	m_ViewSize = ImVec2((std::max)(16.0f, avail.x - panelW), (std::max)(16.0f, avail.y - timelineH));
	m_ViewPos = ImGui::GetCursorScreenPos();
	if (m_FirstFrame && m_ViewSize.x > 32) { m_FirstFrame = false; FrameAll(); }
	ImGui::InvisibleButton("##view2d", m_ViewSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) HandleShortcuts();
	RenderViewport();
	HandleViewport(hovered);
	RenderViewport();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	if (m_Srv) dl->AddImage((ImTextureID)m_Srv.Get(), m_ViewPos, ImVec2(m_ViewPos.x + m_ViewSize.x, m_ViewPos.y + m_ViewSize.y));
	// 본 만들기 미리 보기 · 모드 표시
	if (m_Dragging && m_Tool == Tool::CreateBone)
	{
		float sx, sy;
		m_Raster.WorldToScreen(m_StartWorld[0], m_StartWorld[1], sx, sy);
		dl->AddLine(ImVec2(m_ViewPos.x + sx, m_ViewPos.y + sy), ImGui::GetIO().MousePos, IM_COL32(255, 150, 40, 255), 2.0f);
	}
	dl->AddText(ImVec2(m_ViewPos.x + 10, m_ViewPos.y + 8), d.AnimateMode ? IM_COL32(255, 140, 90, 255) : IM_COL32(140, 180, 255, 255),
		d.AnimateMode ? (std::string("ANIMATE  ") + (d.Active() ? d.Active()->Name : "") + "  " + std::to_string(d.Time).substr(0, 4) + " s").c_str() : "SETUP");

	// 오른쪽 패널
	ImGui::SetCursorScreenPos(ImVec2(m_ViewPos.x + m_ViewSize.x, m_ViewPos.y));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	ImGui::BeginChild("##anim2dPanel", ImVec2(panelW, m_ViewSize.y), true);
	DrawTree();
	DrawProperties();
	ImGui::EndChild();
	ImGui::PopStyleVar();
	// 아래: 타임라인
	ImGui::SetCursorScreenPos(ImVec2(m_ViewPos.x, m_ViewPos.y + m_ViewSize.y));
	DrawTimeline(timelineH);
}
