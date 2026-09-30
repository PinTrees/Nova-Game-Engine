#pragma once

class EditorUtility
{
public:
	static std::wstring OpenTextureFileDialog();
	static std::wstring OpenTextureFileDialog(const std::string& initialPath);
	static std::wstring OpenFileDialog(const std::wstring& initialPath, const string title="Select File", const vector<string> filters = {"txt"});
	static std::wstring OpenFileDialog(const std::wstring& initialPath, const wstring title = L"Select File", const vector<wstring> filters = { L"txt" });

    static std::wstring SaveFileDialog(const std::wstring& initialPath, const std::wstring& title, const std::wstring& filter);
};

