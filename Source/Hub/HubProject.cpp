#include "pch.h"
#include "HubProject.h"
#include "EngineInfo.h"
#include "HubEngineInstaller.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <ctime>
#include <shlobj.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::vector<HubProject> HubProjectRegistry::s_Projects;

namespace
{
	std::string ToUtf8(const std::wstring& w)
	{
		if (w.empty()) return {};
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
		std::string s(n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
		return s;
	}

	std::wstring FromUtf8(const std::string& s)
	{
		if (s.empty()) return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	fs::path KnownFolder(REFKNOWNFOLDERID id)
	{
		PWSTR raw = nullptr;
		fs::path result;
		if (SUCCEEDED(::SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw)
			result = raw;
		::CoTaskMemFree(raw);
		return result;
	}

	fs::path RegistryFile()
	{
		return HubEngineInstaller::StateRoot() / L"projects.json";
	}

	std::string ReadProjectName(const fs::path& root)
	{
		std::ifstream in(root / L"ProjectSettings" / L"ProjectSettings.json");
		if (in.is_open())
		{
			try
			{
				json j; in >> j;
				std::string name = j.value("projectName", std::string());
				if (!name.empty()) return name;
			}
			catch (...) {}
		}
		return ToUtf8(root.filename().wstring());
	}

	bool SamePath(const std::wstring& a, const std::wstring& b)
	{
		std::error_code ec;
		if (fs::equivalent(a, b, ec)) return true;
		return _wcsicmp(fs::path(a).lexically_normal().wstring().c_str(), fs::path(b).lexically_normal().wstring().c_str()) == 0;
	}

	std::map<std::wstring, HANDLE> s_EditorProcesses;

	std::wstring ExePath()
	{
		wchar_t buf[MAX_PATH] = { 0 };
		::GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return buf;
	}
}

const std::vector<HubTemplate>& HubProjectRegistry::Templates()
{
	static const std::vector<HubTemplate> templates =
	{
		{ "3D",    "3D (Core)", "기본 카메라와 조명이 있는 3D 프로젝트입니다." },
		{ "Empty", "빈 프로젝트", "아무것도 없는 빈 프로젝트입니다." },
	};
	return templates;
}

std::wstring HubProjectRegistry::DefaultLocation()
{
	fs::path docs = KnownFolder(FOLDERID_Documents);
	if (docs.empty())
		docs = fs::current_path();
	return (docs / L"NOVA Projects").wstring();
}

void HubProjectRegistry::Load()
{
	s_Projects.clear();

	std::ifstream in(RegistryFile());
	if (!in.is_open())
		return;

	try
	{
		json j; in >> j;
		for (const auto& item : j.value("projects", json::array()))
		{
			HubProject p;
			p.Name = item.value("name", std::string());
			p.Path = FromUtf8(item.value("path", std::string()));
			p.EngineVersion = item.value("engineVersion", std::string(ENGINE_VERSION_A));
			p.Template = item.value("template", std::string());
			p.LastOpened = item.value("lastOpened", 0LL);
			p.Favorite = item.value("favorite", false);
			if (!p.Path.empty())
				s_Projects.push_back(std::move(p));
		}
	}
	catch (...)
	{
		s_Projects.clear();
	}
}

void HubProjectRegistry::Save()
{
	json j;
	j["projects"] = json::array();
	for (const auto& p : s_Projects)
	{
		j["projects"].push_back({
			{ "name", p.Name },
			{ "path", ToUtf8(p.Path) },
			{ "engineVersion", p.EngineVersion },
			{ "template", p.Template },
			{ "lastOpened", p.LastOpened },
			{ "favorite", p.Favorite },
		});
	}

	fs::path file = RegistryFile();
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::ofstream out(file);
	if (out.is_open())
		out << j.dump(2);
}

bool HubProjectRegistry::Exists(const HubProject& project)
{
	std::error_code ec;
	return fs::exists(fs::path(project.Path) / L"Assets", ec);
}

bool HubProjectRegistry::IsValidProjectName(const std::string& name, std::string& error)
{
	if (name.empty())
	{
		error = "프로젝트 이름을 입력하세요.";
		return false;
	}
	if (name.find_first_of("\\/:*?\"<>|") != std::string::npos)
	{
		error = "이름에 사용할 수 없는 문자가 있습니다.  \\ / : * ? \" < > |";
		return false;
	}
	if (name.back() == ' ' || name.back() == '.')
	{
		error = "이름은 공백이나 마침표로 끝날 수 없습니다.";
		return false;
	}
	return true;
}

bool HubProjectRegistry::AddExisting(const std::wstring& folder, std::string& error)
{
	std::error_code ec;
	fs::path root = fs::path(folder).lexically_normal();
	if (root.filename().empty() && root.has_parent_path())
		root = root.parent_path();   // 끝의 구분자 제거

	if (!fs::exists(root / L"Assets", ec))
	{
		error = "선택한 폴더는 NOVA 프로젝트가 아닙니다. (Assets 폴더가 없습니다)";
		return false;
	}

	for (size_t i = 0; i < s_Projects.size(); ++i)
	{
		if (SamePath(s_Projects[i].Path, root.wstring()))
		{
			HubProject existing = s_Projects[i];
			s_Projects.erase(s_Projects.begin() + i);
			s_Projects.insert(s_Projects.begin(), existing);
			Save();
			return true;
		}
	}

	HubProject p;
	p.Name = ReadProjectName(root);
	p.Path = root.wstring();
	p.EngineVersion = ENGINE_VERSION_A;
	{
		std::ifstream in(root / L"ProjectSettings" / L"ProjectSettings.json");
		const json settings = json::parse(in, nullptr, false);
		if (settings.is_object() && settings.contains("engineVersion") && settings["engineVersion"].is_string())
			p.EngineVersion = settings["engineVersion"].get<std::string>();
	}
	p.LastOpened = (long long)std::time(nullptr);
	s_Projects.insert(s_Projects.begin(), p);
	Save();
	return true;
}

bool HubProjectRegistry::Create(const std::string& name, const std::wstring& location, const std::string& templateId, std::string& error, const std::string& engineVersion)
{
	if (!IsValidProjectName(name, error))
		return false;
	if (location.empty())
	{
		error = "생성 위치를 입력하세요.";
		return false;
	}

	std::error_code ec;
	fs::path root = fs::path(location) / FromUtf8(name);
	if (fs::exists(root, ec) && !fs::is_empty(root, ec))
	{
		error = "이미 같은 이름의 폴더가 있고 비어 있지 않습니다.";
		return false;
	}

	fs::create_directories(root / L"Assets" / L"Scenes", ec);
	if (ec)
	{
		error = "폴더를 만들 수 없습니다: " + ec.message();
		return false;
	}
	fs::create_directories(root / L"ProjectSettings", ec);

	json settings;
	settings["projectName"] = name;
	settings["engineVersion"] = engineVersion.empty() ? ENGINE_VERSION_A : engineVersion;
	settings["template"] = templateId;
	settings["created"] = (long long)std::time(nullptr);
	{
		std::ofstream out(root / L"ProjectSettings" / L"ProjectSettings.json");
		if (!out.is_open())
		{
			error = "ProjectSettings 파일을 쓸 수 없습니다.";
			return false;
		}
		out << settings.dump(4);
	}

	// 마지막으로 연 씬이 없으면 에디터가 기본 씬(Main Camera + Directional Light)으로 시작한다.
	{
		json editor;
		editor["LastOpenedScenePath"] = "";
		std::ofstream out(root / L"Assets" / L"EditorSettings.json");
		if (out.is_open())
			out << editor.dump(4);
	}

	HubProject p;
	p.Name = name;
	p.Path = root.wstring();
	p.EngineVersion = engineVersion.empty() ? ENGINE_VERSION_A : engineVersion;
	p.Template = templateId;
	p.LastOpened = (long long)std::time(nullptr);
	s_Projects.insert(s_Projects.begin(), p);
	Save();
	return true;
}

void HubProjectRegistry::Remove(size_t index)
{
	if (index < s_Projects.size())
	{
		s_Projects.erase(s_Projects.begin() + index);
		Save();
	}
}

void HubProjectRegistry::MarkOpened(size_t index)
{
	if (index < s_Projects.size())
	{
		s_Projects[index].LastOpened = (long long)std::time(nullptr);
		Save();
	}
}

bool HubLauncher::LaunchEditor(const std::wstring& projectPath, std::string& error, const std::wstring& editorExecutable)
{
	std::wstring path = projectPath;
	while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
		path.pop_back();   // 끝의 역슬래시가 따옴표를 이스케이프하는 문제 방지

	std::wstring exe = editorExecutable.empty() ? ExePath() : editorExecutable;
	std::wstring cmd = L"\"" + exe + L"\" --project \"" + path + L"\"";
	std::wstring workDir = fs::path(exe).parent_path().wstring();

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	if (!::CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi))
	{
		error = "에디터를 실행하지 못했습니다. (오류 코드 " + std::to_string(::GetLastError()) + ")";
		return false;
	}

	::CloseHandle(pi.hThread);
	auto it = s_EditorProcesses.find(path);
	if (it != s_EditorProcesses.end() && it->second)
		::CloseHandle(it->second);
	s_EditorProcesses[path] = pi.hProcess;
	return true;
}

bool HubLauncher::LaunchHub()
{
	std::wstring exe = ExePath();
	const fs::path installedHub = KnownFolder(FOLDERID_LocalAppData) / L"NOVA" / L"HubApp" / L"Binaries" / L"NovaHub.exe";
	if (fs::is_regular_file(installedHub)) exe = installedHub.wstring();
	std::wstring cmd = L"\"" + exe + L"\"";
	std::wstring workDir = fs::path(exe).parent_path().wstring();

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	if (!::CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi))
		return false;

	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);
	return true;
}

bool HubLauncher::IsEditorRunning(const std::wstring& projectPath)
{
	std::wstring path = projectPath;
	while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
		path.pop_back();

	auto it = s_EditorProcesses.find(path);
	if (it == s_EditorProcesses.end() || !it->second)
		return false;
	return ::WaitForSingleObject(it->second, 0) == WAIT_TIMEOUT;
}
