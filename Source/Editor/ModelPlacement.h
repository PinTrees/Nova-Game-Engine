#pragma once
#include <string>

class GameObject;

// 모델 파일 (FBX · GLB · glTF · VRM) 을 씬에 놓는다 — Project 창에서 Hierarchy · Scene 뷰로 끌어 놓을 때 (Unity 와 같은 결과)
//  - 정적 모델: 파일 이름의 루트 + 모델의 노드마다 GameObject (노드의 로컬 위치 · 회전 · 배율), 메시가 있는 노드 = Mesh Filter + Mesh Renderer
//    (재질 칸 = 서브메시 재질 수만큼 Default-Material, GLB 는 묻힌 재질을 꺼내 붙임). 노드 하나짜리 모델은 루트에 바로 (Unity 처럼)
//    파일 단위 (FBX cm → m, Unity 의 Convert Units) 는 최상위 자식의 위치 · 배율에
//  - 노드 이름이 `이름_LOD0` · `이름_LOD1` … 이면 그 부모에 LOD Group 을 만들어 LOD 마다 넣는다 (Unity 의 모델 가져오기)
//  - 스킨 메시가 있으면 캐릭터 (Skinned Mesh Renderer + Animator) — 예전과 같음
namespace ModelPlacement
{
	// path = 프로젝트 상대 또는 절대 경로. parent 가 있으면 그 아래 (로컬 원점), 없으면 루트로 position 에.
	//  씬에 넣고 고른 뒤 Undo 이름까지 정한다. 실패하면 nullptr (error)
	GameObject* Instantiate(const std::string& path, GameObject* parent, const Vec3& position, std::string* error = nullptr);
	void RegisterEditor();   // nova modelfile place <path> [--parent P] [--position x,y,z] | info <path>
}
