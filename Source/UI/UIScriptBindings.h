#pragma once

// C# 의 NovaEngine.UI / RectTransform / Canvas 가 부르는 네이티브 함수 (NativeApiTable 의 UI_* 항목).
// 번호표는 ScriptCore/Interop/NativeApi.cs 의 주석과 같다.
namespace UIScriptBindings
{
	int GetVec(uint64 gameObject, int prop, Vec4* out);    // 컴포넌트가 없으면 0
	void SetVec(uint64 gameObject, int prop, Vec4* value);
	const char* GetString(uint64 gameObject, int prop);
	void SetString(uint64 gameObject, int prop, const char* value);
	// Text 의 textInfo · 링크 · 글자 애니메이션. kind:
	//  0 글자 수, 1 글자 index → out[char, source, visible, line, blX, blY, trX, trY, link, size]
	//  2 줄 수, 3 줄 → out[first, count, top, height], 4 링크 수, 5 링크 → out[first, count]
	//  6 캔버스 점(out[0], out[1]) 의 링크 번호 (-1), 7 글자 위치 더하기 out[x, y], 8 글자 크기 out[0], 9 글자 색 out[rgba], 10 글자 수정 지우기, 11 ForceMeshUpdate
	int TextInfo(uint64 gameObject, int kind, int index, float* out, int max);
	// 링크 문자열: which 0 = id, 1 = 링크 글자
	const char* TextLink(uint64 gameObject, int index, int which);
}
