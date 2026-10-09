#pragma once

// ³ªÁß¿¡ ¸ŞÅ¸µ¥ÀÌÅÍÆÄÀÏ »ı¼º ½Ã ÇÁ·ÎÁ§Æ® ¼³Á¤¿¡¼­ Á¶Á¤, Ä«¸Ş¶ó ¹üÀ§¿¡¼­ ·»´õÇÒ ¼ö ÀÖ´Â ÃÖ´ë »çÀÌÁî
// Á¤·ÄµÈ º¤ÅÍ ¶óÀÌÆ®µé¿¡¼­ ÃÖ´ë »çÀÌÁî±îÁö¸¸ °É·¯³¾ °Í
#define LIGHT_SIZE 4 // Light Size´Â ÀÌ °÷¿¡¼­¸¸ ¼³Á¤°¡´É
#define LIGHT_MULTYPLE 6
#define LIGHT_MAX_SIZE (LIGHT_SIZE * LIGHT_MULTYPLE)

class Light;

// Forward+ (í´ëŸ¬ìŠ¤í„° ì¡°ëª…): ê·¸ë¦¼ì ìˆëŠ” ì•ì˜ ë¹› (LIGHT_SIZE) ë°–ì˜ ì ê´‘ Â· ìŠ¤í¬íŠ¸ê´‘ Â· ì…ì ë¹› â€” ê·¸ë¦¼ì ì—†ì´ í´ëŸ¬ìŠ¤í„°ë¡œ ë¹„ì¶˜ë‹¤ (ClusteredLighting)
struct AdditionalLight
{
	Vec3 Position;
	float Range = 0.0f;
	Vec3 Color;              // Diffuse (ê°ë§ˆ, ì„¸ê¸° ê³±í•¨ â€” ì…°ì´ë”ê°€ ì„ í˜•ìœ¼ë¡œ)
	int Type = 0;            // 0 ì ê´‘, 1 ìŠ¤í¬íŠ¸ê´‘
	Vec3 Direction;
	float SpotAngle = 0.0f;  // ì›ë¿” ì „ì²´ ê° (ë„)
	uint32 Mask = 0xFFFFFFFFu;   // Light.cullingMask
};
constexpr int kMaxAdditionalLights = 1024;

class NOVA_API LightManager
{
	SINGLE_HEADER(LightManager)

private:
	vector<shared_ptr<Light>> m_Lights;
	vector<shared_ptr<Light>>* m_StagingLights = nullptr; // ¾ÀÀÇ ¸ğµç ºûµé

	int m_SortedLightSize = 0;
	int m_SortedEditorLightSize = 0;

	// Frustum Culling, °Å¸® ±â¹İ Á¤·Ä, Typeº° Á¤·ÄÀÌ ¿Ï·áµÈ Lightº¤ÅÍ
	vector<shared_ptr<Light>> m_SortedEditorLights;
	vector<shared_ptr<Light>> m_SortedLights;

	// Frustum Culling, °Å¸® ±â¹İ Á¤·Ä ÈÄ Typeº°·Î ¸ğ¾ÆµĞ Lightº¤ÅÍ, RenderEffect¿¡ ³Ñ±â±â ¿ëµµ
	vector<DirectionalLight> m_EditorDirLights;
	vector<PointLight> m_EditorPointLights;
	vector<SpotLight> m_EditorSpotLights;

	vector<DirectionalLight> m_DirLights;
	vector<PointLight> m_PointLights;
	vector<SpotLight> m_SpotLights;

	// Forward+: ì•ì˜ LIGHT_SIZE ë°–ì˜ ë¹› (ê°€ê¹Œìš´ ìˆœ, ìµœëŒ€ kMaxAdditionalLights)
	vector<AdditionalLight> m_AdditionalLights;
	vector<AdditionalLight> m_EditorAdditionalLights;
public:
	void Init();

	void SetLight(shared_ptr<Light> light) { if (m_StagingLights) { m_StagingLights->push_back(light); return; } m_Lights.push_back(light); }
	// ì”¬ ìŠ¤íŠ¸ë¦¬ë°: ì•„ì§ ë°”ê¿” ë¼ìš°ì§€ ì•Šì€ ì”¬ì„ ì§“ëŠ” ë™ì•ˆì˜ Light ëŠ” ì—¬ê¸°ì— ëª¨ì€ë‹¤ (ì§€ê¸ˆ ì”¬ì„ ë¹„ì¶”ì§€ ì•Šê²Œ â€” ë°”ê¿” ë¼ìš¸ ë•Œ SetLight)
	void SetStagingSink(vector<shared_ptr<Light>>* sink) { m_StagingLights = sink; }
	void DeleteLight(InstanceID id);

	void ViewUpdates();
	void EditorViewUpdates();

	// Camera Å¬·¡½º¿¡¼­ Frustum ÄÃ¸µ µÈ ¶óÀÌÆ®µé¸¸ Á¤·Ä
	void SortingLights(vector<shared_ptr<Light>> cullingLights, Vec3 cameraPos);
	void SortingEditorLights(vector<shared_ptr<Light>> cullingLights, Vec3 cameraPos);

	int GetSortedLightSize() { return m_SortedLightSize; }
	int GetSortedEditorLightSize() { return m_SortedEditorLightSize; }

	// ì¼œì ¸ ìˆê³  (enabled) ê³„ì¸µì´ ëª¨ë‘ í™œì„±ì¸ ë¹›ë§Œ ë¹„ì¶˜ë‹¤ (Unity: light.enabled = false)
	static bool IsLit(const shared_ptr<Light>& light);
	vector<shared_ptr<Light>> GetLights() { return m_Lights; }
	vector<shared_ptr<Light>> GetSortedLights() { return m_SortedLights; }
	vector<shared_ptr<Light>> GetSortedEditorLights() { return m_SortedEditorLights; }

	// Å¸ÀÔº° Lights ¹İÈ¯
	vector<DirectionalLight> GetDirLights() { return m_DirLights; }
	vector<PointLight> GetPointLights() { return m_PointLights; }
	vector<SpotLight> GetSpotLights() { return m_SpotLights; }

	vector<DirectionalLight> GetEditorDirLights() { return m_EditorDirLights; }
	vector<PointLight> GetEditorPointLights() { return m_EditorPointLights; }
	vector<SpotLight> GetEditorSpotLights() { return m_EditorSpotLights; }

	const vector<AdditionalLight>& GetAdditionalLights() const { return m_AdditionalLights; }
	const vector<AdditionalLight>& GetEditorAdditionalLights() const { return m_EditorAdditionalLights; }
};

