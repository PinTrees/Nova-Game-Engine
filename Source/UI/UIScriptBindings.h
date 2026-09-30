#pragma once

// C# 의 NovaEngine.UI / RectTransform / Canvas 가 부르는 네이티브 함수 (NativeApiTable 의 UI_* 항목).
// 번호표는 ScriptCore/Interop/NativeApi.cs 의 주석과 같다.
namespace UIScriptBindings
{
	int GetVec(uint64 gameObject, int prop, Vec4* out);    // 컴포넌트가 없으면 0
	void SetVec(uint64 gameObject, int prop, Vec4* value);
	const char* GetString(uint64 gameObject, int prop);
	void SetString(uint64 gameObject, int prop, const char* value);
}
