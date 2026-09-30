#include "pch.h"
#include "ExternalScriptEditor.h"
#include "NovaCodeWindow.h"
#include "EditorPrefs.h"
#include "PathManager.h"
#include <filesystem>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace
{
	const char* kPrefKind = "ExternalScriptEditor.Kind";
	const char* kPrefPath = "ExternalScriptEditor.Path";
	const char* kPrefName = "ExternalScriptEditor.Name";
	const char* kPrefArgs = "ExternalScriptEditor.Args";

	std::vector<ExternalScriptEditor::Editor> s_Installed;
	bool s_Detected = false;

	const char* KindId(ExternalScriptEditor::Kind k)
	{
		switch (k)
		{
		case ExternalScriptEditor::Kind::VSCode:        return "vscode";
		case ExternalScriptEditor::Kind::VisualStudio:  return "visualstudio";
		case ExternalScriptEditor::Kind::Rider:         return "rider";
		case ExternalScriptEditor::Kind::SystemDefault: return "default";
		case ExternalScriptEditor::Kind::Custom:        return "custom";
		default:                                        return "builtin";
		}
	}

	ExternalScriptEditor::Kind KindFromId(const std::string& id)
	{
		using K = ExternalScriptEditor::Kind;
		if (id == "vscode") return K::VSCode;
		if (id == "visualstudio") return K::VisualStudio;
		if (id == "rider") return K::Rider;
		if (id == "default") return K::SystemDefault;
		if (id == "custom") return K::Custom;
		return K::BuiltIn;
	}

	std::wstring Env(const wchar_t* name)
	{
		wchar_t buf[MAX_PATH] = {};
		return ::GetEnvironmentVariableW(name, buf, MAX_PATH) > 0 ? std::wstring(buf) : std::wstring();
	}

	bool Exists(const std::wstring& p)
	{
		std::error_code ec;
		return !p.empty() && fs::exists(p, ec);
	}

	std::wstring ProjectRoot()
	{
		std::wstring root = PathManager::GetI()->GetMovePathW(L"");
		if (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
			root.pop_back();
		return root;
	}

	// 콘솔 프로그램을 창 없이 실행하고 출력을 받는다 (vswhere)
	std::string RunAndRead(const std::wstring& commandLine)
	{
		SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!::CreatePipe(&readPipe, &writePipe, &sa, 0))
			return std::string();
		::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = writePipe;
		si.hStdError = writePipe;
		PROCESS_INFORMATION pi = {};
		std::wstring cmd = commandLine;
		const BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
		::CloseHandle(writePipe);
		std::string out;
		if (ok)
		{
			char buf[4096];
			DWORD read = 0;
			while (::ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
				out.append(buf, read);
			::WaitForSingleObject(pi.hProcess, 5000);
			::CloseHandle(pi.hProcess);
			::CloseHandle(pi.hThread);
		}
		::CloseHandle(readPipe);
		return out;
	}

	void Detect()
	{
		using ExternalScriptEditor::Editor;
		using ExternalScriptEditor::Kind;
		s_Installed.clear();
		s_Installed.push_back({ Kind::BuiltIn, "NOVA Code (built-in)", L"" });

		// Visual Studio Code (사용자 설치 → 시스템 설치)
		for (const std::wstring& p : { Env(L"LOCALAPPDATA") + L"\\Programs\\Microsoft VS Code\\Code.exe", Env(L"ProgramFiles") + L"\\Microsoft VS Code\\Code.exe" })
			if (Exists(p))
			{
				s_Installed.push_back({ Kind::VSCode, "Visual Studio Code", p });
				break;
			}

		// Visual Studio (vswhere 가 설치된 모든 버전을 알려준다)
		const std::wstring vswhere = Env(L"ProgramFiles(x86)") + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
		if (Exists(vswhere))
		{
			const std::string out = RunAndRead(L"\"" + vswhere + L"\" -all -prerelease -format json -utf8");
			json j = json::parse(out, nullptr, false);
			if (j.is_array())
				for (const json& inst : j)
				{
					const std::string path = inst.value("productPath", "");
					if (path.empty() || path.find("devenv.exe") == std::string::npos)
						continue;
					std::string name = inst.value("displayName", "Visual Studio");
					if (inst.contains("catalog") && inst["catalog"].is_object())
					{
						std::string ver = inst["catalog"].value("productDisplayVersion", "");
						if (!ver.empty())
						{
							// 17.9.34622.214 → [17.9]
							const size_t dot = ver.find('.', ver.find('.') + 1);
							name += " [" + ver.substr(0, dot) + "]";
						}
					}
					s_Installed.push_back({ Kind::VisualStudio, name, string_to_wstring(path) });
				}
		}

		// JetBrains Rider (Toolbox / 단독 설치)
		std::vector<std::wstring> riders = { Env(L"LOCALAPPDATA") + L"\\Programs\\Rider\\bin\\rider64.exe" };
		std::error_code ec;
		const fs::path jb = Env(L"ProgramFiles") + L"\\JetBrains";
		if (fs::exists(jb, ec))
			for (const auto& e : fs::directory_iterator(jb, ec))
				if (e.path().filename().wstring().find(L"Rider") != std::wstring::npos)
					riders.push_back((e.path() / L"bin" / L"rider64.exe").wstring());
		for (const std::wstring& p : riders)
			if (Exists(p))
			{
				s_Installed.push_back({ Kind::Rider, "JetBrains Rider", p });
				break;
			}

		s_Installed.push_back({ Kind::SystemDefault, "Open by file extension", L"" });
		s_Detected = true;
		EditorLog::Write("App", "External script editors detected: %d", (int)s_Installed.size());
	}

	void Replace(std::wstring& s, const std::wstring& from, const std::wstring& to)
	{
		for (size_t p = s.find(from); p != std::wstring::npos; p = s.find(from, p + to.size()))
			s.replace(p, from.size(), to);
	}

	bool Launch(const std::wstring& exe, const std::wstring& args)
	{
		const HINSTANCE r = ::ShellExecuteW(nullptr, L"open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
		const bool ok = (INT_PTR)r > 32;
		EditorLog::Write("App", "open script with %s %s -> %s", wstring_to_string(exe).c_str(), wstring_to_string(args).c_str(), ok ? "ok" : "failed");
		return ok;
	}
}

namespace ExternalScriptEditor
{
	const std::vector<Editor>& Installed(bool refresh)
	{
		if (!s_Detected || refresh)
			Detect();
		return s_Installed;
	}

	Editor Current()
	{
		Editor e;
		e.Type = KindFromId(EditorPrefs::GetString(kPrefKind, "builtin"));
		e.Path = string_to_wstring(EditorPrefs::GetString(kPrefPath));
		e.Name = EditorPrefs::GetString(kPrefName);
		if (e.Type == Kind::BuiltIn) e.Name = "NOVA Code (built-in)";
		if (e.Type == Kind::SystemDefault) e.Name = "Open by file extension";
		if (e.Name.empty()) e.Name = wstring_to_string(fs::path(e.Path).stem().wstring());
		return e;
	}

	void SetCurrent(const Editor& editor)
	{
		EditorPrefs::SetString(kPrefKind, KindId(editor.Type));
		EditorPrefs::SetString(kPrefPath, wstring_to_string(editor.Path));
		EditorPrefs::SetString(kPrefName, editor.Name);
		EditorLog::Write("App", "External Script Editor = %s", editor.Name.c_str());
	}

	bool IsBuiltIn() { return Current().Type == Kind::BuiltIn; }

	const char* DefaultCustomArgs() { return "\"$(File)\""; }
	std::string CustomArgs() { return EditorPrefs::GetString(kPrefArgs, DefaultCustomArgs()); }
	void SetCustomArgs(const std::string& args) { EditorPrefs::SetString(kPrefArgs, args); }

	void Open(const std::wstring& file, int line)
	{
		if (file.empty())
			return;
		const Editor e = Current();
		const std::wstring lineStr = std::to_wstring((std::max)(1, line));
		const std::wstring root = ProjectRoot();
		bool ok = false;
		switch (e.Type)
		{
		case Kind::BuiltIn:
			NovaCodeWindow::Open(file, line);
			return;
		case Kind::VSCode:
			// 프로젝트 폴더를 작업 공간으로 열고 파일:줄로 이동 (IntelliSense 가 Assembly-CSharp.csproj 를 쓴다)
			if (Exists(e.Path))
				ok = Launch(e.Path, L"\"" + root + L"\" -g \"" + file + L":" + lineStr + L"\"");
			break;
		case Kind::VisualStudio:
			// 실행 중인 Visual Studio 가 있어도 /edit 는 그 창에서 연다
			if (Exists(e.Path))
				ok = Launch(e.Path, L"/edit \"" + file + L"\"" + (line > 0 ? L" /command \"Edit.GoTo " + lineStr + L"\"" : L""));
			break;
		case Kind::Rider:
			if (Exists(e.Path))
				ok = Launch(e.Path, (line > 0 ? L"--line " + lineStr + L" " : L"") + L"\"" + file + L"\"");
			break;
		case Kind::Custom:
			if (Exists(e.Path))
			{
				std::wstring args = string_to_wstring(CustomArgs());
				Replace(args, L"$(File)", file);
				Replace(args, L"$(Line)", lineStr);
				Replace(args, L"$(ProjectPath)", root);
				ok = Launch(e.Path, args);
			}
			break;
		default:
			break;
		}
		if (!ok)
			::ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);   // Open by file extension (또는 실패 시)
	}
}
