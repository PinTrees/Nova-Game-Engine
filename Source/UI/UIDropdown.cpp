#include "pch.h"
#include "UIDropdown.h"
#include "UIToggle.h"
#include "UIText.h"
#include "UIImage.h"
#include "UICanvas.h"
#include "RectTransform.h"
#include "UnityGUI.h"
#include "ScriptEngine.h"

namespace
{
	// src 를 복제해 parent 아래에 (fileID 새로, 안의 참조도 새 ID 로). 씬 목록에도 넣는다
	GameObject* CloneTree(GameObject* src, GameObject* parent)
	{
		json j = *src;
		GameObject* g = new GameObject();
		from_json(j, *g);
		g->RegenerateFileIDs();
		g->SetParent(parent, false);
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			scene->RegisterGameObjectTree(g);
		return g;
	}

	GameObject* Find(uint64 id)
	{
		Scene* scene = SceneManager::GetI()->GetCurrentScene();
		return id && scene ? scene->FindByFileID(id) : nullptr;
	}

	// root 에서 target 까지 자식 순번 (복제본에서 같은 자리를 찾는다)
	bool PathTo(GameObject* root, GameObject* target, std::vector<int>& path)
	{
		path.clear();
		for (GameObject* g = target; g && g != root; g = g->GetParent())
		{
			GameObject* p = g->GetParent();
			if (p == nullptr)
				return false;
			const auto& kids = p->GetChildren();
			const auto it = std::find(kids.begin(), kids.end(), g);
			if (it == kids.end())
				return false;
			path.insert(path.begin(), (int)(it - kids.begin()));
		}
		return target != nullptr;
	}

	GameObject* Follow(GameObject* root, const std::vector<int>& path)
	{
		GameObject* g = root;
		for (int i : path)
		{
			if (g == nullptr || i < 0 || i >= (int)g->GetChildren().size())
				return nullptr;
			g = g->GetChildren()[i];
		}
		return g;
	}

	// 맨 위 캔버스 (목록 · Blocker 를 여기 마지막 자식으로 = 다른 UI 위에)
	GameObject* RootCanvas(GameObject* go)
	{
		GameObject* top = nullptr;
		for (GameObject* g = go; g; g = g->GetParent())
			if (g->GetComponent<Canvas>())
				top = g;
		return top;
	}
}

Dropdown::Dropdown()
{
	m_InspectorTitleName = "Dropdown";
}

Dropdown::~Dropdown() = default;

void Dropdown::SetValue(int v, bool notify)
{
	v = Options.empty() ? 0 : std::clamp(v, 0, (int)Options.size() - 1);
	const bool changed = v != m_Value;
	m_Value = v;
	RefreshShownValue();
	if (changed && notify && m_pGameObject && Application::IsPlaying())
	{
		m_OnValueChanged.Invoke(m_pGameObject, "On Value Changed", std::to_string(v));
		ScriptEngine::InvokeUIEvent(m_pGameObject->GetFileID(), 7, (float)v);   // C# dropdown.onValueChanged
	}
}

void Dropdown::RefreshShownValue()
{
	if (GameObject* cap = Find(m_CaptionText))
		if (Text* t = cap->GetComponent<Text>())
		{
			const std::string s = m_Value >= 0 && m_Value < (int)Options.size() ? Options[m_Value].Text : std::string();
			if (t->GetText() != s)
				t->SetText(s);
		}
}

void Dropdown::OnClick()
{
	if (!m_Interactable)
		return;
	if (IsExpanded()) Hide();
	else Show();
}

void Dropdown::Show()
{
	if (IsExpanded() || !Application::IsPlaying() || m_pGameObject == nullptr)
		return;
	GameObject* templ = Find(m_Template);
	GameObject* canvas = RootCanvas(m_pGameObject);
	RectTransform* tRect = templ ? templ->GetComponent<RectTransform>() : nullptr;
	RectTransform* cRect = canvas ? canvas->GetComponent<RectTransform>() : nullptr;
	if (templ == nullptr || canvas == nullptr || tRect == nullptr || cRect == nullptr)
	{
		EditorLog::Write("UI", "Dropdown '%s': no Template", m_pGameObject->GetName().c_str());
		return;
	}

	// 1) Blocker: 캔버스 전체를 덮는 투명 Toggle (누르면 닫힘)
	{
		GameObject* b = new GameObject("Blocker");
		b->SetLayerIndex(m_pGameObject->GetLayerIndex());
		auto rt = std::make_shared<RectTransform>();
		b->AddComponent(rt);
		rt->SetAnchorMin(Vec2(0, 0));
		rt->SetAnchorMax(Vec2(1, 1));
		rt->SetSizeDelta(Vec2(0, 0));
		auto img = std::make_shared<UIImage>();
		const float clear[4] = { 0, 0, 0, 0 };
		img->SetColor(clear);
		b->AddComponent(img);
		auto tg = std::make_shared<Toggle>();
		b->AddComponent(tg);
		b->SetParent(canvas, false);
		if (Scene* scene = SceneManager::GetI()->GetCurrentScene())
			scene->RegisterGameObjectTree(b);
		m_BlockerID = b->GetFileID();
		m_BlockerState = tg->IsOn();
	}

	// 2) 목록 = Template 복제 (캔버스 아래, Template 이 있던 자리 그대로)
	std::vector<int> itemPath;
	const bool haveItem = PathTo(templ, Find(m_ItemText), itemPath);
	GameObject* list = CloneTree(templ, canvas);
	list->SetName("Dropdown List");
	list->SetActive(true);
	m_ListID = list->GetFileID();
	if (RectTransform* lr = list->GetComponent<RectTransform>())
	{
		// Template 의 월드 사각형 → 캔버스 로컬 (가운데 기준점)
		Vec3 corners[4];
		tRect->GetWorldCorners(corners);
		const Matrix inv = canvas->GetTransform()->GetWorldMatrix().Invert();
		Vec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
		for (const Vec3& c : corners)
		{
			const Vec3 l = Vec3::Transform(c, inv);
			mn = Vec2::Min(mn, Vec2(l.x, l.y));
			mx = Vec2::Max(mx, Vec2(l.x, l.y));
		}
		const Vec2 parentCenter = cRect->GetRectMin() + cRect->GetRectSize() * 0.5f;
		lr->SetAnchorMin(Vec2(0.5f, 0.5f));
		lr->SetAnchorMax(Vec2(0.5f, 0.5f));
		lr->SetPivot(Vec2(0.5f, 0.5f));
		lr->SetSizeDelta(mx - mn);
		lr->SetAnchoredPosition((mn + mx) * 0.5f - parentCenter);
	}

	// 3) 항목: 복제된 Item(글자의 Toggle 조상)을 옵션마다 하나씩
	m_ItemToggles.clear();
	m_ItemStates.clear();
	GameObject* itemText = haveItem ? Follow(list, itemPath) : nullptr;
	GameObject* item = itemText;
	while (item && item->GetComponent<Toggle>() == nullptr)
		item = item->GetParent();
	GameObject* content = item ? item->GetParent() : nullptr;
	if (item && content)
	{
		RectTransform* ir = item->GetComponent<RectTransform>();
		// 높이 = sizeDelta (방금 복제해 아직 레이아웃 전이라 GetRectSize 는 기본값). Item 은 위쪽 가로 늘림 앵커라 sizeDelta.y = 높이
		const float itemH = ir && ir->GetSizeDelta().y > 1.0f ? ir->GetSizeDelta().y : 20.0f;
		// 첫 항목 위치 (Content 위쪽 기준)
		const float top = ir ? ir->GetAnchoredPosition().y : -itemH * 0.5f;
		// 항목을 지우지 않고 두 번째부터 복제 (첫 번째 = 원본 복제본)
		std::vector<GameObject*> items = { item };
		for (int i = 1; i < (int)Options.size(); ++i)
		{
			GameObject* copy = CloneTree(item, content);
			items.push_back(copy);
		}
		for (int i = 0; i < (int)items.size(); ++i)
		{
			GameObject* it = items[i];
			const std::string label = i < (int)Options.size() ? Options[i].Text : std::string();
			it->SetName("Item " + std::to_string(i) + ": " + label);
			if (Options.empty())
				it->SetActive(false);
			// 글자: 이 항목 안의 첫 Text
			std::function<Text*(GameObject*)> firstText = [&](GameObject* g) -> Text* {
				if (Text* t = g->GetComponent<Text>()) return t;
				for (GameObject* c : g->GetChildren()) if (Text* t = firstText(c)) return t;
				return nullptr;
			};
			if (Text* t = firstText(it))
				t->SetText(label);
			if (RectTransform* r = it->GetComponent<RectTransform>())
				r->SetAnchoredPosition(Vec2(r->GetAnchoredPosition().x, top - itemH * i));
			Toggle* tg = it->GetComponent<Toggle>();
			if (tg)
				tg->SetIsOn(i == m_Value, false);
			m_ItemToggles.push_back(it->GetFileID());
			m_ItemStates.push_back(tg && tg->IsOn());
		}
		// Content 높이 = 항목 수 (Scroll Rect 로 넘치면 굴린다)
		const float contentH = itemH * (std::max)(1, (int)Options.size()) + 8.0f;
		if (RectTransform* cr = content->GetComponent<RectTransform>())
			cr->SetSizeDelta(Vec2(cr->GetSizeDelta().x, contentH));
		// 항목이 적으면 목록도 그만큼 (위쪽 모서리는 그대로 — Unity 와 같음)
		if (RectTransform* lr = list->GetComponent<RectTransform>(); lr && contentH < lr->GetSizeDelta().y)
		{
			const float shrink = lr->GetSizeDelta().y - contentH;
			lr->SetSizeDelta(Vec2(lr->GetSizeDelta().x, contentH));
			lr->SetAnchoredPosition(lr->GetAnchoredPosition() + Vec2(0.0f, shrink * 0.5f));
		}
	}
	EditorLog::Write("UI", "Dropdown '%s' opened (%d options)", m_pGameObject->GetName().c_str(), (int)Options.size());
}

void Dropdown::Hide()
{
	for (uint64* id : { &m_ListID, &m_BlockerID })
	{
		if (GameObject* g = Find(*id))
			GameObject::Destroy(g);
		*id = 0;
	}
	m_ItemToggles.clear();
	m_ItemStates.clear();
}

void Dropdown::UpdateBeforeLayout(float, bool playing)
{
	RefreshShownValue();
	if (!IsExpanded())
		return;
	if (!playing || Find(m_ListID) == nullptr)
	{
		Hide();
		return;
	}
	// 항목이 눌렸다 (isOn 이 바뀜) → 그 옵션
	for (size_t i = 0; i < m_ItemToggles.size(); ++i)
		if (GameObject* g = Find(m_ItemToggles[i]))
			if (Toggle* t = g->GetComponent<Toggle>(); t && t->IsOn() != m_ItemStates[i])
			{
				Hide();
				SetValue((int)i);
				return;
			}
	// 목록 밖 (Blocker) 을 눌렀다
	if (GameObject* b = Find(m_BlockerID))
		if (Toggle* t = b->GetComponent<Toggle>(); t && t->IsOn() != m_BlockerState)
			Hide();
}

void Dropdown::RemapFileIDs(const std::unordered_map<uint64, uint64>& map)
{
	UISelectable::RemapFileIDs(map);
	for (uint64* id : { &m_Template, &m_CaptionText, &m_CaptionImage, &m_ItemText, &m_ItemImage })
		if (auto it = map.find(*id); it != map.end())
			*id = it->second;
}

void Dropdown::OnInspectorGUI()
{
	DrawSelectableInspector();
	auto ref = [&](const char* label, uint64 id, const char* type) {
		GameObject* g = Find(id);
		UnityGUI::ValueLabel(label, g ? (g->GetName() + " (" + type + ")").c_str() : (std::string("None (") + type + ")").c_str());
	};
	ref("Template", m_Template, "Rect Transform");
	ref("Caption Text", m_CaptionText, "Text");
	ref("Item Text", m_ItemText, "Text");
	int v = m_Value;
	if (UnityGUI::Int("Value", &v))
		SetValue(v, false);
	// Options: 글자 목록 (+ / -)
	UnityGUI::Label("Options", 0, true);
	int remove = -1;
	for (size_t i = 0; i < Options.size(); ++i)
	{
		ImGui::PushID((int)i);
		char label[32];
		snprintf(label, sizeof(label), "Option %d", (int)i);
		UnityGUI::TextField(label, &Options[i].Text, 1);
		ImGui::SameLine();
		if (ImGui::SmallButton("-"))
			remove = (int)i;
		ImGui::PopID();
	}
	if (remove >= 0)
		Options.erase(Options.begin() + remove);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + UnityGUI::kBaseIndent);
	if (ImGui::SmallButton("+ Add Option"))
		Options.push_back({ "Option " + std::string(1, (char)('A' + (int)Options.size() % 26)), std::string() });
	m_OnValueChanged.Draw("On Value Changed (Int32)", "int");
}

GENERATE_COMPONENT_FUNC_TOJSON(Dropdown)
{
	json j;
	SERIALIZE_TYPE(j, Dropdown);
	SelectableToJson(j);
	j["template"] = m_Template;
	j["captionText"] = m_CaptionText;
	j["captionImage"] = m_CaptionImage;
	j["itemText"] = m_ItemText;
	j["itemImage"] = m_ItemImage;
	j["value"] = m_Value;
	json opts = json::array();
	for (const Option& o : Options)
		opts.push_back({ { "text", o.Text }, { "image", o.Image } });
	j["options"] = opts;
	j["onValueChanged"] = m_OnValueChanged.ToJson();
	return j;
}

GENERATE_COMPONENT_FUNC_FROMJSON(Dropdown)
{
	SelectableFromJson(j);
	m_Template = j.value("template", (uint64)0);
	m_CaptionText = j.value("captionText", (uint64)0);
	m_CaptionImage = j.value("captionImage", (uint64)0);
	m_ItemText = j.value("itemText", (uint64)0);
	m_ItemImage = j.value("itemImage", (uint64)0);
	Options.clear();
	if (j.contains("options") && j["options"].is_array())
		for (const auto& o : j["options"])
			Options.push_back({ o.value("text", std::string()), o.value("image", std::string()) });
	m_Value = j.value("value", 0);
	m_OnValueChanged = UIEventList();
	if (j.contains("onValueChanged"))
		m_OnValueChanged.FromJson(j["onValueChanged"]);
}
