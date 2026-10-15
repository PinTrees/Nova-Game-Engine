#include "pch.h"
#include "TimelineWindow.h"
#include "PlayableDirector.h"
#include "TimelineAsset.h"
#include "SelectionManager.h"
#include "UnityGUI.h"
#include "EditorGUIManager.h"
#include "ImGui/imgui_internal.h"

TimelineWindow* TimelineWindow::s_Instance = nullptr;

namespace
{
	const float kLeft = 210.0f, kRuler = 24.0f, kRow = 34.0f;
	const ImU32 kBack = IM_COL32(40, 40, 40, 255), kLane = IM_COL32(48, 48, 48, 255), kLaneAlt = IM_COL32(52, 52, 52, 255);
	const ImU32 kText = IM_COL32(220, 220, 220, 255), kDim = IM_COL32(140, 140, 140, 255), kHead = IM_COL32(255, 80, 80, 255);

	ImU32 TrackColor(const std::string& type)
	{
		if (type == "Animation") return IM_COL32(70, 120, 200, 255);
		if (type == "Activation") return IM_COL32(80, 160, 90, 255);
		if (type == "Audio") return IM_COL32(200, 150, 60, 255);
		if (type == "Signal") return IM_COL32(200, 200, 200, 255);
		return IM_COL32(170, 90, 190, 255);   // Cinemachine
	}

	GameObject* FindObject(uint64 id)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return id && scene ? scene->FindByFileID(id) : nullptr;
	}

	std::vector<std::string> ListAssets(std::initializer_list<const wchar_t*> exts)
	{
		std::vector<std::string> out;
		std::error_code ec;
		const std::filesystem::path root = PathManager::GetI()->GetMovePathW(L"Assets\\");
		const std::filesystem::path project = root.parent_path().parent_path();
		for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
			!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (!it->is_regular_file(ec))
				continue;
			for (const wchar_t* e : exts)
				if (_wcsicmp(it->path().extension().c_str(), e) == 0)
					out.push_back(wstring_to_string(std::filesystem::relative(it->path(), project, ec).wstring()));
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	double Snap(double t, double fps) { return fps > 0.0 ? std::round(t * fps) / fps : t; }
}

TimelineWindow::TimelineWindow()
	: EditorWindow("Timeline", ICON_FA_FILM)
{
	s_Instance = this;
	SetIsOpened(false);
}

TimelineWindow::~TimelineWindow()
{
	if (s_Instance == this)
		s_Instance = nullptr;
}

void TimelineWindow::Focus(PlayableDirector* director)
{
	if (s_Instance == nullptr)
		return;
	if (director && director->GetGameObject())
		s_Instance->m_DirectorObject = director->GetGameObject()->GetFileID();
	s_Instance->SetIsOpened(true);
	ImGui::SetWindowFocus(s_Instance->GetImGuiName().c_str());
}

PlayableDirector* TimelineWindow::Director()
{
	// 고른 오브젝트에 디렉터가 있으면 그것 (Unity 와 같이 선택을 따라간다)
	if (SelectionManager::GetSelectedObjectType() == SelectionType::GAMEOBJECT)
		if (GameObject* sel = SelectionManager::GetSelectedGameObject())
			if (sel->GetComponent<PlayableDirector>())
				m_DirectorObject = sel->GetFileID();
	GameObject* go = FindObject(m_DirectorObject);
	return go ? go->GetComponent<PlayableDirector>() : nullptr;
}

void TimelineWindow::Update()
{
	PlayableDirector* d = Director();
	if (d && m_Preview && !Application::IsPlaying())
	{
		// 편집 중 재생: 시간을 나아가며 미리 보기 (Hold 면 끝에서 멈춤)
		const double dur = d->Duration();
		double t = d->GetTime() + (std::min)((double)DT, 1.0 / 3.0);
		if (dur > 0.0 && t >= dur)
		{
			if (d->WrapMode == (int)PlayableDirector::Wrap::Loop) t = fmod(t, dur);
			else { t = dur; m_Preview = false; }
		}
		d->SetTime(t);
		d->Evaluate();
	}
}

void TimelineWindow::Preview(PlayableDirector* d)
{
	if (d && !Application::IsPlaying())
		d->Evaluate();
}

void TimelineWindow::BeforeBegin()
{
	// 처음 열면 아래 Project · Console · Animator 탭 옆에 (Unity 도 보통 아래에 둔다)
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 380.0f), ImGuiCond_FirstUseEver);
	if (EditorWindow* project = EditorGUIManager::GetI()->FindWindow("Project"))
		if (ImGuiWindow* w = ImGui::FindWindowByName(project->GetImGuiName().c_str()); w && w->DockId)
			ImGui::SetNextWindowDockID(w->DockId, ImGuiCond_FirstUseEver);
}

void TimelineWindow::OnRender()
{
	PlayableDirector* d = Director();
	if (d == nullptr)
	{
		ImGui::TextDisabled("Select a GameObject with a Playable Director (Add Component > Playable Director) to edit its Timeline.");
		return;
	}
	std::shared_ptr<TimelineAsset> asset = d->GetAsset();
	if (!asset)
	{
		ImGui::TextDisabled("The Playable Director has no Timeline asset.");
		if (ImGui::Button("Create Timeline Asset"))
		{
			d->AssetPath = "Assets\\Timelines\\" + (d->GetGameObject() ? d->GetGameObject()->GetName() : std::string("Timeline")) + ".playable";
			TimelineAsset::Create(d->AssetPath);
		}
		return;
	}
	DrawToolbar(d, asset.get());
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	// 트랙은 모두 보이게 (좁으면 아래 속성 칸이 줄어든다)
	const float tracksMin = kRuler + kRow * (float)(std::max)((size_t)1, asset->Tracks.size()) + 8.0f;
	const float tracksH = (std::min)(avail.y - 40.0f, (std::max)(tracksMin, avail.y - 150.0f));
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	DrawTracks(d, asset.get(), pos, ImVec2(avail.x, (std::max)(80.0f, tracksH)));
	ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + (std::max)(80.0f, tracksH) + 4));
	ImGui::BeginChild("##tlprops", ImVec2(0, 0), false);
	DrawProperties(d, asset.get());
	ImGui::EndChild();
}

void TimelineWindow::DrawToolbar(PlayableDirector* d, TimelineAsset* a)
{
	const bool playing = Application::IsPlaying() ? d->IsPlaying() : m_Preview;
	if (ImGui::SmallButton(ICON_FA_BACKWARD_STEP))
	{
		d->SetTime(0.0);
		Preview(d);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(playing ? ICON_FA_PAUSE : ICON_FA_PLAY))
	{
		if (Application::IsPlaying())
			playing ? d->Pause() : (d->GetTime() > 0.0 ? d->Resume() : d->Play());
		else
			m_Preview = !m_Preview;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(ICON_FA_STOP))
	{
		m_Preview = false;
		d->Stop();   // 켜고 끈 오브젝트 · Animator 덮기를 되돌린다
	}
	ImGui::SameLine();
	ImGui::Text("%6.2f s / %.2f s", d->GetTime(), a->Duration());
	ImGui::SameLine();
	ImGui::TextDisabled("|  %s", a->Path.c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Track"))
		ImGui::OpenPopup("##addtrack");
	if (ImGui::BeginPopup("##addtrack"))
	{
		int count = 0;
		const char* const* types = TimelineAsset::TrackTypes(count);
		for (int i = 0; i < count; ++i)
			if (ImGui::MenuItem((std::string(types[i]) + " Track").c_str()))
			{
				a->AddTrack(types[i], std::string());
				a->Commit();
			}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120);
	ImGui::SliderFloat("##zoom", &m_PixelsPerSecond, 20.0f, 600.0f, "%.0f px/s", ImGuiSliderFlags_Logarithmic);
}

void TimelineWindow::DrawTracks(PlayableDirector* d, TimelineAsset* a, ImVec2 pos, ImVec2 size)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kBack);
	const float x0 = pos.x + kLeft;
	const double fps = a->FrameRate;
	auto timeToX = [&](double t) { return x0 + (float)((t - m_Scroll) * m_PixelsPerSecond); };
	auto xToTime = [&](float x) -> double { return (double)m_Scroll + (double)(x - x0) / m_PixelsPerSecond; };
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const bool hovered = ImGui::IsWindowHovered() && mouse.x >= pos.x && mouse.y >= pos.y && mouse.x < pos.x + size.x && mouse.y < pos.y + size.y;

	// 휠: 가로 이동 (Ctrl = 확대)
	if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
	{
		if (ImGui::GetIO().KeyCtrl)
			m_PixelsPerSecond = std::clamp(m_PixelsPerSecond * (ImGui::GetIO().MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 20.0f, 600.0f);
		else
			m_Scroll = (std::max)(0.0f, m_Scroll - ImGui::GetIO().MouseWheel * 60.0f / m_PixelsPerSecond);
	}

	// 눈금자
	dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(pos.x + size.x, pos.y + kRuler), IM_COL32(56, 56, 56, 255));
	const double step = m_PixelsPerSecond > 200 ? 0.25 : (m_PixelsPerSecond > 60 ? 1.0 : 5.0);
	for (double t = std::floor(m_Scroll / step) * step; timeToX(t) < pos.x + size.x; t += step)
	{
		const float x = timeToX(t);
		if (x < x0)
			continue;
		const bool major = fmod(t + 1e-6, step * 4) < 1e-3 || step >= 1.0;
		dl->AddLine(ImVec2(x, pos.y + (major ? 8 : 16)), ImVec2(x, pos.y + kRuler), kDim);
		if (major)
		{
			char buf[16];
			sprintf_s(buf, "%.2fs", t);
			dl->AddText(ImVec2(x + 3, pos.y + 2), kDim, buf);
		}
	}
	// 끝 표시
	const float endX = timeToX(a->Duration());
	if (endX > x0)
		dl->AddLine(ImVec2(endX, pos.y), ImVec2(endX, pos.y + size.y), IM_COL32(110, 110, 110, 255), 1.0f);

	// 트랙 줄
	int removeTrack = -1;
	for (int ti = 0; ti < (int)a->Tracks.size(); ++ti)
	{
		TimelineTrack& track = a->Tracks[(size_t)ti];
		const float y = pos.y + kRuler + ti * kRow;
		if (y > pos.y + size.y)
			break;
		const bool selTrack = m_SelTrack == ti;
		dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(x0 - 1, y + kRow - 1), selTrack ? IM_COL32(62, 80, 110, 255) : IM_COL32(58, 58, 58, 255));
		dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + 4, y + kRow - 1), TrackColor(track.Type));
		dl->AddText(ImVec2(pos.x + 10, y + 3), track.Muted ? kDim : kText, track.Name.c_str());
		GameObject* bound = d->BoundObject(track);
		const std::string sub = track.Type + (track.Type == "Cinemachine" ? std::string() : std::string(" · ") + (bound ? bound->GetName() : std::string("None")));
		dl->AddText(ImVec2(pos.x + 10, y + 17), kDim, sub.c_str());
		ImGui::SetCursorScreenPos(ImVec2(pos.x, y));
		ImGui::PushID(ti);
		if (ImGui::InvisibleButton("##trackhead", ImVec2(kLeft - 2, kRow - 1)))
		{
			m_SelTrack = ti;
			m_SelClip = m_SelMarker = -1;
		}
		if (ImGui::BeginPopupContextItem("##trackctx"))
		{
			if (ImGui::MenuItem(track.Muted ? "Unmute" : "Mute"))
			{
				track.Muted = !track.Muted;
				a->Commit();
			}
			if (ImGui::MenuItem("Delete Track"))
				removeTrack = ti;
			ImGui::EndPopup();
		}
		ImGui::PopID();
		dl->AddRectFilled(ImVec2(x0, y), ImVec2(pos.x + size.x, y + kRow - 1), ti % 2 ? kLaneAlt : kLane);

		// 클립
		for (int ci = 0; ci < (int)track.Clips.size(); ++ci)
		{
			TimelineClip& c = track.Clips[(size_t)ci];
			const float cx0 = (std::max)(x0, timeToX(c.Start)), cx1 = timeToX(c.End());
			if (cx1 <= x0)
				continue;
			const bool sel = selTrack && m_SelClip == ci;
			dl->AddRectFilled(ImVec2(cx0, y + 3), ImVec2(cx1, y + kRow - 4), TrackColor(track.Type), 3.0f);
			if (c.EaseIn > 0.0)
				dl->AddTriangleFilled(ImVec2(timeToX(c.Start), y + kRow - 4), ImVec2(timeToX(c.Start + c.EaseIn), y + 3), ImVec2(timeToX(c.Start), y + 3), IM_COL32(0, 0, 0, 70));
			if (c.EaseOut > 0.0)
				dl->AddTriangleFilled(ImVec2(cx1, y + kRow - 4), ImVec2(timeToX(c.End() - c.EaseOut), y + 3), ImVec2(cx1, y + 3), IM_COL32(0, 0, 0, 70));
			if (sel)
				dl->AddRect(ImVec2(cx0, y + 3), ImVec2(cx1, y + kRow - 4), IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.0f);
			std::string label = !c.Name.empty() ? c.Name : (!c.Asset.empty() ? std::filesystem::path(c.Asset).stem().string() + (track.Type == "Animation" ? " #" + std::to_string(c.ClipIndex) : std::string()) : track.Type);
			if (track.Type == "Cinemachine")
				if (GameObject* cam = FindObject(c.Object))
					label = cam->GetName();
			dl->PushClipRect(ImVec2(cx0, y), ImVec2(cx1, y + kRow), true);
			dl->AddText(ImVec2(cx0 + 5, y + 10), IM_COL32_WHITE, label.c_str());
			dl->PopClipRect();
			// 끌기: 양 끝 6 px = 자르기, 안쪽 = 옮기기
			ImGui::SetCursorScreenPos(ImVec2(cx0, y + 3));
			ImGui::PushID(ti * 1000 + ci);
			ImGui::InvisibleButton("##clip", ImVec2((std::max)(4.0f, cx1 - cx0), kRow - 7));
			if (ImGui::IsItemActivated())
			{
				m_SelTrack = ti; m_SelClip = ci; m_SelMarker = -1;
				m_DragStart = xToTime(mouse.x);
				m_DragOrigStart = c.Start; m_DragOrigDuration = c.Duration; m_DragOrigClipIn = c.ClipIn;
				m_Drag = mouse.x < cx0 + 6 ? Drag::TrimLeft : (mouse.x > cx1 - 6 ? Drag::TrimRight : Drag::Move);
			}
			if (ImGui::IsItemHovered() && (mouse.x < cx0 + 6 || mouse.x > cx1 - 6))
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			if (ImGui::IsItemActive() && m_SelTrack == ti && m_SelClip == ci)
			{
				const double dt = xToTime(mouse.x) - m_DragStart;
				if (m_Drag == Drag::Move)
					c.Start = Snap((std::max)(0.0, m_DragOrigStart + dt), fps);
				else if (m_Drag == Drag::TrimRight)
					c.Duration = (std::max)(1.0 / fps, Snap(m_DragOrigDuration + dt, fps));
				else if (m_Drag == Drag::TrimLeft)
				{
					const double nd = std::clamp(dt, -m_DragOrigStart, m_DragOrigDuration - 1.0 / fps);
					c.Start = Snap(m_DragOrigStart + nd, fps);
					c.Duration = m_DragOrigDuration - (c.Start - m_DragOrigStart);
					c.ClipIn = (std::max)(0.0, m_DragOrigClipIn + (c.Start - m_DragOrigStart) * c.Speed);
				}
				Preview(d);
			}
			if (ImGui::IsItemDeactivated() && m_Drag != Drag::None)
			{
				m_Drag = Drag::None;
				std::sort(track.Clips.begin(), track.Clips.end(), [](const TimelineClip& p, const TimelineClip& q) { return p.Start < q.Start; });
				m_SelClip = -1;
				a->Commit();
			}
			ImGui::PopID();
		}
		// 마커 (◆)
		for (int mi = 0; mi < (int)track.Markers.size(); ++mi)
		{
			TimelineMarker& m = track.Markers[(size_t)mi];
			const float mx = timeToX(m.Time);
			if (mx < x0)
				continue;
			const bool sel = selTrack && m_SelMarker == mi;
			const float r = 6.0f, cy = y + kRow * 0.5f;
			dl->AddQuadFilled(ImVec2(mx, cy - r), ImVec2(mx + r, cy), ImVec2(mx, cy + r), ImVec2(mx - r, cy), sel ? IM_COL32(255, 230, 120, 255) : IM_COL32(230, 230, 230, 255));
			ImGui::SetCursorScreenPos(ImVec2(mx - r, cy - r));
			ImGui::PushID(100000 + ti * 1000 + mi);
			ImGui::InvisibleButton("##marker", ImVec2(r * 2, r * 2));
			if (ImGui::IsItemActivated())
			{
				m_SelTrack = ti; m_SelMarker = mi; m_SelClip = -1;
				m_Drag = Drag::Marker;
			}
			if (ImGui::IsItemActive() && m_Drag == Drag::Marker)
				m.Time = Snap((std::max)(0.0, xToTime(mouse.x)), fps);
			if (ImGui::IsItemDeactivated() && m_Drag == Drag::Marker)
			{
				m_Drag = Drag::None;
				std::sort(track.Markers.begin(), track.Markers.end(), [](const TimelineMarker& p, const TimelineMarker& q) { return p.Time < q.Time; });
				m_SelMarker = -1;
				a->Commit();
			}
			ImGui::PopID();
		}
		// 빈 곳 오른쪽 클릭: 더하기
		if (hovered && mouse.y >= y && mouse.y < y + kRow && mouse.x >= x0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
		{
			m_ContextTrack = ti;
			m_ContextTime = Snap((std::max)(0.0, xToTime(mouse.x)), fps);
			ImGui::OpenPopup("##laneadd");
		}
	}
	if (ImGui::BeginPopup("##laneadd"))
	{
		if (m_ContextTrack >= 0 && m_ContextTrack < (int)a->Tracks.size())
		{
			TimelineTrack& track = a->Tracks[(size_t)m_ContextTrack];
			auto addClip = [&](const std::string& asset, double duration) {
				TimelineClip c;
				c.Start = m_ContextTime;
				c.Duration = duration;
				c.Asset = asset;
				track.Clips.push_back(c);
				std::sort(track.Clips.begin(), track.Clips.end(), [](const TimelineClip& p, const TimelineClip& q) { return p.Start < q.Start; });
				a->Commit();
			};
			if (track.Type == "Animation")
			{
				if (ImGui::BeginMenu("Add Animation Clip"))
				{
					for (const std::string& f : ListAssets({ L".fbx", L".glb", L".gltf", L".vrm" }))
						if (ImGui::MenuItem(f.c_str()))
						{
							double len = 2.0;
							if (auto clip = ResourceManager::GetI()->LoadAnimationClip(f, 0))
								len = (std::max)(0.1, (double)clip->GetClipEndTime());
							addClip(f, len);
						}
					ImGui::EndMenu();
				}
			}
			else if (track.Type == "Audio")
			{
				if (ImGui::BeginMenu("Add Audio Clip"))
				{
					for (const std::string& f : ListAssets({ L".wav", L".ogg", L".mp3", L".flac" }))
						if (ImGui::MenuItem(f.c_str()))
							addClip(f, 3.0);
					ImGui::EndMenu();
				}
			}
			else if (track.Type == "Activation" || track.Type == "Cinemachine")
			{
				if (ImGui::MenuItem(track.Type == "Activation" ? "Add Activation Clip" : "Add Cinemachine Shot"))
					addClip(std::string(), 2.0);
			}
			if (ImGui::MenuItem("Add Signal Marker"))
			{
				TimelineMarker m;
				m.Time = m_ContextTime;
				m.Method = "OnTimelineSignal";
				track.Markers.push_back(m);
				std::sort(track.Markers.begin(), track.Markers.end(), [](const TimelineMarker& p, const TimelineMarker& q) { return p.Time < q.Time; });
				a->Commit();
			}
		}
		ImGui::EndPopup();
	}
	if (removeTrack >= 0)
	{
		d->Bindings.erase(a->Tracks[(size_t)removeTrack].Id);
		a->Tracks.erase(a->Tracks.begin() + removeTrack);
		m_SelTrack = m_SelClip = m_SelMarker = -1;
		a->Commit();
	}
	// Delete: 고른 클립 · 마커
	if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && m_SelTrack >= 0 && m_SelTrack < (int)a->Tracks.size())
	{
		TimelineTrack& t = a->Tracks[(size_t)m_SelTrack];
		if (m_SelClip >= 0 && m_SelClip < (int)t.Clips.size()) { t.Clips.erase(t.Clips.begin() + m_SelClip); m_SelClip = -1; a->Commit(); }
		else if (m_SelMarker >= 0 && m_SelMarker < (int)t.Markers.size()) { t.Markers.erase(t.Markers.begin() + m_SelMarker); m_SelMarker = -1; a->Commit(); }
	}

	// 재생 헤드 (눈금자에서 끌기 = 미리 보기)
	ImGui::SetCursorScreenPos(ImVec2(x0, pos.y));
	ImGui::InvisibleButton("##ruler", ImVec2((std::max)(1.0f, size.x - kLeft), kRuler));
	if (ImGui::IsItemActive())
	{
		d->SetTime(Snap((std::max)(0.0, xToTime(mouse.x)), fps));
		m_Preview = false;
		Preview(d);
	}
	const float hx = timeToX(d->GetTime());
	if (hx >= x0)
	{
		dl->AddLine(ImVec2(hx, pos.y), ImVec2(hx, pos.y + size.y), kHead, 1.5f);
		dl->AddTriangleFilled(ImVec2(hx - 6, pos.y), ImVec2(hx + 6, pos.y), ImVec2(hx, pos.y + 8), kHead);
	}
	ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));
}

void TimelineWindow::DrawProperties(PlayableDirector* d, TimelineAsset* a)
{
	using namespace UnityGUI;
	if (m_SelTrack < 0 || m_SelTrack >= (int)a->Tracks.size())
	{
		ImGui::TextDisabled("Select a track, clip or marker. Right-click a track lane to add clips and signal markers.");
		return;
	}
	TimelineTrack& t = a->Tracks[(size_t)m_SelTrack];
	bool changed = false;
	if (m_SelClip >= 0 && m_SelClip < (int)t.Clips.size())
	{
		TimelineClip& c = t.Clips[(size_t)m_SelClip];
		Label((t.Name + " · clip").c_str(), 0, true);
		changed |= TextField("Name", &c.Name);
		float v[6] = { (float)c.Start, (float)c.Duration, (float)c.ClipIn, (float)c.Speed, (float)c.EaseIn, (float)c.EaseOut };
		const char* names[6] = { "Start", "Duration", "Clip In", "Speed", "Ease In", "Ease Out" };
		for (int i = 0; i < 6; ++i)
			if (Float(names[i], &v[i]))
			{
				changed = true;
				c.Start = (std::max)(0.0f, v[0]); c.Duration = (std::max)(0.01f, v[1]); c.ClipIn = (std::max)(0.0f, v[2]);
				c.Speed = v[3]; c.EaseIn = (std::max)(0.0f, v[4]); c.EaseOut = (std::max)(0.0f, v[5]);
			}
		if (t.Type == "Animation" || t.Type == "Audio")
			changed |= TextField("Asset", &c.Asset);
		if (t.Type == "Animation")
		{
			int idx = c.ClipIndex;
			if (ImGui::InputInt("Clip Index", &idx)) { c.ClipIndex = (std::max)(0, idx); changed = true; }
			changed |= Toggle("Loop", &c.Loop);
		}
		if (t.Type == "Cinemachine")
			changed |= GameObjectField("Camera", &c.Object);
	}
	else if (m_SelMarker >= 0 && m_SelMarker < (int)t.Markers.size())
	{
		TimelineMarker& m = t.Markers[(size_t)m_SelMarker];
		Label((t.Name + " · signal").c_str(), 0, true);
		float time = (float)m.Time;
		if (Float("Time", &time)) { m.Time = (std::max)(0.0f, time); changed = true; }
		changed |= TextField("Method", &m.Method);
		changed |= TextField("String", &m.String);
		changed |= Float("Float", &m.Float);
		int iv = m.Int;
		if (ImGui::InputInt("Int", &iv)) { m.Int = iv; changed = true; }
		changed |= Toggle("Retroactive", &m.Retroactive);
	}
	else
	{
		Label((t.Name + " · " + t.Type + " track").c_str(), 0, true);
		changed |= TextField("Name", &t.Name);
		changed |= Toggle("Muted", &t.Muted);
		if (t.Type != "Cinemachine")
		{
			uint64 id = d->Bindings.count(t.Id) ? d->Bindings[t.Id] : 0;
			if (GameObjectField("Binding", &id))
				d->SetBinding(t.Id, id);
		}
		if (t.Type == "Signal" || !t.Markers.empty())
			HelpBox("Signal markers call the method on the scripts of the bound GameObject when the playhead passes them (Play mode).", false);
	}
	if (changed)
	{
		a->Commit();
		Preview(d);
	}
}
