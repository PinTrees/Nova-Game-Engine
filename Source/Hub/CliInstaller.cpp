#include "pch.h"
#include "CliInstaller.h"
#include "EngineInfo.h"
#include "HubEngineInstaller.h"

namespace
{
	namespace fs = std::filesystem;

	std::wstring Env(const wchar_t* name)
	{
		wchar_t buf[MAX_PATH] = {};
		return ::GetEnvironmentVariableW(name, buf, MAX_PATH) > 0 ? std::wstring(buf) : std::wstring();
	}

	std::wstring InstallDir()
	{
		const std::wstring custom = Env(L"NOVA_CLI_INSTALL_DIR");
		if (!custom.empty())
			return custom;
		return Env(L"LOCALAPPDATA") + L"\\NOVA\\CLI";
	}

	bool PathDisabled() { return !Env(L"NOVA_CLI_NO_PATH").empty(); }

	std::wstring SourceEngineExe()
	{
		wchar_t self[MAX_PATH] = {};
		::GetModuleFileNameW(nullptr, self, MAX_PATH);
		if (_wcsicmp(fs::path(self).filename().c_str(), L"NovaHub.exe") == 0)
			return HubEngineInstaller().EditorFor({});
		return self;
	}
	std::wstring SourceExe()
	{
		const auto engine = SourceEngineExe();
		return engine.empty() ? std::wstring() : (fs::path(engine).parent_path() / L"nova.exe").wstring();
	}

	std::wstring Lower(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(), ::towlower);
		return s;
	}

	std::wstring Trim(std::wstring s)
	{
		while (!s.empty() && (s.back() == L'\\' || s.back() == L' '))
			s.pop_back();
		while (!s.empty() && s.front() == L' ')
			s.erase(s.begin());
		return s;
	}

	// 사용자 PATH (HKCU\Environment\Path, 보통 REG_EXPAND_SZ)
	bool ReadUserPath(std::wstring& value, DWORD& type)
	{
		value.clear();
		type = REG_EXPAND_SZ;
		HKEY key;
		if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ, &key) != ERROR_SUCCESS)
			return false;
		DWORD size = 0;
		LONG r = ::RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &size);
		if (r == ERROR_SUCCESS && size > 0)
		{
			std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
			r = ::RegQueryValueExW(key, L"Path", nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &size);
			if (r == ERROR_SUCCESS)
				value.assign(buf.c_str());
		}
		::RegCloseKey(key);
		return true;
	}

	bool WriteUserPath(const std::wstring& value, DWORD type)
	{
		HKEY key;
		if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
			return false;
		const LONG r = ::RegSetValueExW(key, L"Path", 0, type == REG_SZ ? REG_SZ : REG_EXPAND_SZ,
			reinterpret_cast<const BYTE*>(value.c_str()), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
		::RegCloseKey(key);
		// 탐색기·새 터미널이 바뀐 환경 변수를 다시 읽게
		DWORD_PTR result = 0;
		::SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 3000, &result);
		return r == ERROR_SUCCESS;
	}

	std::vector<std::wstring> SplitPath(const std::wstring& value)
	{
		std::vector<std::wstring> parts;
		size_t start = 0;
		while (start <= value.size())
		{
			const size_t semi = value.find(L';', start);
			const std::wstring part = value.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
			if (!Trim(part).empty())
				parts.push_back(part);
			if (semi == std::wstring::npos)
				break;
			start = semi + 1;
		}
		return parts;
	}

	bool PathContains(const std::wstring& value, const std::wstring& dir)
	{
		const std::wstring want = Lower(Trim(dir));
		for (const std::wstring& p : SplitPath(value))
			if (Lower(Trim(p)) == want)
				return true;
		return false;
	}

	bool SameFile(const std::wstring& a, const std::wstring& b)
	{
		std::error_code ec;
		if (!fs::exists(a, ec) || !fs::exists(b, ec) || fs::file_size(a, ec) != fs::file_size(b, ec))
			return false;
		std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
		return std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(fb));
	}
}

namespace CliInstaller
{
	Status Query()
	{
		Status s;
		s.Dir = InstallDir();
		std::error_code ec;
		const std::wstring src = SourceExe();
		const std::wstring dst = s.Dir + L"\\nova.exe";
		s.SourceFound = fs::exists(src, ec);
		s.Installed = fs::exists(dst, ec);
		s.UpToDate = s.Installed && s.SourceFound && SameFile(src, dst);
		std::wstring path;
		DWORD type;
		s.OnPath = ReadUserPath(path, type) && PathContains(path, s.Dir);
		return s;
	}

	bool Install(std::string& error)
	{
		const std::wstring dir = InstallDir();
		const std::wstring src = SourceExe();
		std::error_code ec;
		if (!fs::exists(src, ec))
		{
			error = "엔진 폴더에 nova.exe 가 없습니다 (엔진을 다시 빌드하세요)";
			return false;
		}
		fs::create_directories(dir, ec);
		if (!::CopyFileW(src.c_str(), (dir + L"\\nova.exe").c_str(), FALSE))
		{
			error = "nova.exe 를 복사하지 못했습니다 (실행 중인 nova 가 있으면 끝낸 뒤 다시)";
			return false;
		}
		// nova open 이 엔진을 찾는 곳
		const auto self = SourceEngineExe();
		nlohmann::json j = {
			{ "exe", wstring_to_string(self) },
			{ "engine", wstring_to_string(fs::path(self).parent_path().parent_path().wstring()) },
			{ "version", ENGINE_VERSION_A },
		};
		std::ofstream(dir + L"\\engine.json", std::ios::trunc) << j.dump(2);
		if (!PathDisabled())
		{
			std::wstring path;
			DWORD type;
			ReadUserPath(path, type);
			if (!PathContains(path, dir))
			{
				std::wstring next = path;
				if (!next.empty() && next.back() != L';')
					next += L';';
				next += dir;
				if (!WriteUserPath(next, type))
				{
					error = "사용자 PATH 에 추가하지 못했습니다";
					return false;
				}
			}
		}
		return true;
	}

	bool Uninstall(std::string& error)
	{
		const std::wstring dir = InstallDir();
		std::error_code ec;
		fs::remove(dir + L"\\nova.exe", ec);
		if (ec)
		{
			error = "nova.exe 를 지우지 못했습니다 (실행 중인 nova 가 있으면 끝낸 뒤 다시)";
			return false;
		}
		fs::remove(dir + L"\\engine.json", ec);
		fs::remove(dir, ec);   // 비었으면
		if (!PathDisabled())
		{
			std::wstring path;
			DWORD type;
			if (ReadUserPath(path, type) && PathContains(path, dir))
			{
				std::wstring next;
				const std::wstring want = Lower(Trim(dir));
				for (const std::wstring& p : SplitPath(path))
					if (Lower(Trim(p)) != want)
						next += (next.empty() ? L"" : L";") + p;
				WriteUserPath(next, type);
			}
		}
		return true;
	}
}
