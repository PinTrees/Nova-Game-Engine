#pragma once
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

// Unity 의 Shader Graph: 노드를 이어 재질 셰이더를 만든다 (.shadergraph — JSON).
//  - Master (Fragment): Base Color · Normal (Tangent Space) · Metallic · Smoothness · Emission · Ambient Occlusion · Alpha
//    Lit = 엔진의 URP Lit 조명 (ShadeLit — 빛 · 그림자 · 하늘 · SSAO · 안개가 다른 재질과 같다), Unlit = 색 그대로
//  - Blackboard 속성 (Float · Color · Vector2/3/4 · Texture2D): 재질마다 값 (재질 Inspector), 코드에서 gSG_<ref>
//  - 코드 생성: Master 에 이어진 노드만, 위상 순서로 HLSL 한 줄씩 → "32. InstancedBasic.fx" 를 포함한 .fx (ShaderGraphRuntime)
//  - 형 변환은 Unity 와 같다: 작은 벡터 → 큰 벡터 (float 은 모든 칸에, 나머지는 0), 큰 → 작은 (앞 칸만)
namespace ShaderGraph
{
	using json = nlohmann::json;

	// 포트 형: 1..4 = float1..4, 0 = 동적 (이은 것 중 가장 큰 폭), 10 = Texture2D
	constexpr int kDynamic = 0;
	constexpr int kTexture = 10;

	struct PortDef
	{
		std::string Name;
		int Width = 1;
		float Default[4] = { 0, 0, 0, 0 };
		std::string Bind;   // 이어지지 않았을 때 쓰는 값: "uv", "posW", "normalW", "viewW" (비면 Default)
	};

	struct NodeContext;
	struct NodeDef
	{
		std::string Type;       // "Multiply"
		std::string Category;   // "Math/Basic"
		std::vector<PortDef> In, Out;
		std::function<void(NodeContext&)> Gen;
		json DefaultOptions = json::object();   // 노드마다 설정 (Swizzle mask, Sample Texture 의 Type …)
		std::string Help;
	};

	const std::vector<NodeDef>& NodeDefs();
	const NodeDef* FindDef(const std::string& type);

	struct Property
	{
		std::string Name;        // 보이는 이름 ("Base Color")
		std::string Ref;         // 코드 이름 ("_BaseColor") — gSG__BaseColor
		std::string Type;        // Float, Color, Vector2, Vector3, Vector4, Texture2D
		float Value[4] = { 0, 0, 0, 0 };
		std::string Texture;     // Texture2D 기본 그림 (프로젝트 경로)
		bool Range = false;      // Float: 슬라이더
		float Min = 0.0f, Max = 1.0f;
		int Width() const;       // 1..4, Texture = kTexture
	};

	struct Node
	{
		int Id = 0;
		std::string Type;
		float X = 0, Y = 0;
		json Values = json::object();    // 이어지지 않은 입력의 값 { "B": [1, 1, 1, 1] }
		json Options = json::object();   // { "mask": "xyz" } / Property 노드 { "ref": "_BaseColor" }
	};

	struct Edge
	{
		int FromNode = 0; std::string FromPort;
		int ToNode = 0; std::string ToPort;   // ToNode 0 = Master
	};

	struct Graph
	{
		std::string Material = "Lit";   // Lit, Unlit
		std::string Path = "Shader Graphs";   // 셰이더 이름 앞부분 (Unity 의 Blackboard 경로) → "<Path>/<파일 이름>"
		std::string Surface = "Opaque";       // Opaque, Transparent (투명 패스: 먼 것부터 섞기, 그림자 · 프리패스 없음)
		bool AlphaClip = false;               // Alpha Clipping: Alpha < Alpha Clip Threshold 를 잘라낸다 (프리패스 · 그림자도)
		std::vector<Property> Properties;
		std::vector<Node> Nodes;
		std::vector<Edge> Edges;
		int NextId = 1;

		json ToJson() const;
		bool FromJson(const json& j, std::string& error);
		bool Save(const std::wstring& fullPath, std::string& error) const;
		bool Load(const std::wstring& fullPath, std::string& error);

		Node* FindNode(int id);
		const Node* FindNode(int id) const;
		const Property* FindProperty(const std::string& ref) const;
		int AddNode(const std::string& type, float x, float y);
		void RemoveNode(int id);
		// 입력 하나에는 선 하나 (새로 이으면 바꾼다). 형이 안 맞거나 고리가 생기면 false
		bool Connect(int fromNode, const std::string& fromPort, int toNode, const std::string& toPort, std::string& error);
		void Disconnect(int toNode, const std::string& toPort);
		std::string UniqueRef(const std::string& name) const;
	};

	// Master 입력 (Lit / Unlit, Alpha Clipping 에 따라)
	const std::vector<PortDef>& MasterInputs(const Graph& g);

	// 그 노드의 출력 포트 폭 (동적 포트는 입력에서 정한다), Property 노드는 속성 형
	int OutputWidth(const Graph& g, const Node& n, const std::string& port);

	struct CodeResult
	{
		std::string Hlsl;       // .fx 전체
		std::string Error;      // 비면 성공
		std::vector<int> Used;  // 쓰인 노드
		bool ClipsAlpha = false;
	};
	CodeResult Generate(const Graph& g);          // 재질 셰이더 (.fx — 엔진 32. InstancedBasic.fx 포함)
	CodeResult GeneratePreview(const Graph& g);   // 창의 미리보기 (작은 독립 .fx — SGPreviewNodeTech · SGPreviewMainTech)

	// 노드 코드 생성 중 쓰는 도우미
	struct NodeContext
	{
		const Graph* G = nullptr;
		const Node* N = nullptr;
		const NodeDef* D = nullptr;
		std::string* Code = nullptr;
		std::function<std::string(const Node&, const std::string&)> OutVar;   // 노드 출력 변수 이름
		// 입력 식 (폭을 width 로 맞춤). width 0 = 그 입력의 폭 그대로
		std::string In(const std::string& port, int width = 0) const;
		int InWidth(const std::string& port) const;      // 이은 출력 / 기본값의 폭
		std::string TextureIn(const std::string& port) const;   // Texture2D 입력 → 변수 이름 (없으면 "")
		// 출력 선언: floatN <var> = expr;
		void Out(const std::string& port, int width, const std::string& expr) const;
		json Option(const char* key) const;
		int DynamicWidth(std::initializer_list<const char*> ports) const;   // 동적: 이은 것 중 가장 큰 폭 (없으면 1)
	};

	std::string Cast(const std::string& expr, int from, int to);
	std::string TypeName(int width);
	std::string Sanitize(const std::string& s);
}
