#include "pch.h"
#include "WebBuild.h"
#include "WebTools.h"
#include "BuildSettings.h"
#include "PathManager.h"
#include "Debug.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#pragma comment(lib, "ws2_32.lib")   // 소켓 = windows.h 의 winsock (pch)

namespace WebBuild
{
	namespace
	{
		namespace fs = std::filesystem;

		// ---- 에디터 빌드 (진행 창이 먼저 그려지게 한 프레임 뒤에 WebTools::ExportGame — 메인 스레드: 씬 · 에셋을 읽는다)
		struct Job
		{
			Options Opt;
			int Frame = 0;
		};
		std::unique_ptr<Job> s_Job;

		// ---- 미리 보기 서버: 127.0.0.1 만, 요청마다 스레드 하나 (브라우저는 여러 연결을 한꺼번에 연다), 응답 뒤 연결을 닫는다
		std::mutex s_ServerLock;
		std::wstring s_Root;
		std::string s_Url;
		SOCKET s_Listen = INVALID_SOCKET;
		std::atomic<bool> s_Stop{ false };

		const char* MimeOf(const std::wstring& ext)
		{
			if (ext == L".html") return "text/html; charset=utf-8";
			if (ext == L".js" || ext == L".mjs") return "text/javascript";
			if (ext == L".wasm") return "application/wasm";
			if (ext == L".json") return "application/json";
			if (ext == L".css") return "text/css";
			if (ext == L".png") return "image/png";
			if (ext == L".ico") return "image/x-icon";
			return "application/octet-stream";
		}

		void SendAll(SOCKET s, const char* data, size_t size)
		{
			while (size > 0)
			{
				const int n = ::send(s, data, (int)(std::min)(size, (size_t)1 << 20), 0);
				if (n <= 0) return;
				data += n;
				size -= (size_t)n;
			}
		}

		void Respond(SOCKET client)
		{
			char buf[8192];
			std::string request;
			while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536)
			{
				const int n = ::recv(client, buf, sizeof(buf), 0);
				if (n <= 0) break;
				request.append(buf, (size_t)n);
			}
			std::string method, target;
			{
				const size_t a = request.find(' '), b = a == std::string::npos ? a : request.find(' ', a + 1);
				if (b != std::string::npos) { method = request.substr(0, a); target = request.substr(a + 1, b - a - 1); }
			}
			std::wstring root;
			{
				std::lock_guard<std::mutex> g(s_ServerLock);
				root = s_Root;
			}
			// 경로: 질의 (?…) 를 떼고 %XX 를 풀고, 폴더 밖 (..) 은 거절
			std::string path = target.substr(0, target.find('?'));
			std::string decoded;
			for (size_t i = 0; i < path.size(); ++i)
			{
				if (path[i] == '%' && i + 2 < path.size()) { decoded += (char)strtol(path.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
				else decoded += path[i];
			}
			if (decoded.empty() || decoded.back() == '/') decoded += "index.html";
			std::error_code ec;
			const fs::path file = (fs::path(root) / fs::path(string_to_wstring(decoded.substr(1))).relative_path()).lexically_normal();
			const std::wstring rel = file.lexically_relative(fs::path(root).lexically_normal()).wstring();
			const bool inside = !rel.empty() && rel.rfind(L"..", 0) != 0;
			std::string head;
			std::string body;
			if ((method == "GET" || method == "HEAD") && inside && fs::is_regular_file(file, ec))
			{
				std::ifstream in(file, std::ios::binary);
				body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
				head = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(MimeOf(file.extension().wstring())) + "\r\n";
			}
			else
			{
				body = "404 Not Found";
				head = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n";
			}
			// 교차 출처 격리 (SharedArrayBuffer · 정밀 타이머 — 나중의 스레드 빌드), 매번 새로 받기 (다시 빌드하면 바로 보이게)
			head += "Content-Length: " + std::to_string(body.size()) + "\r\nCross-Origin-Opener-Policy: same-origin\r\n"
				"Cross-Origin-Embedder-Policy: require-corp\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
			SendAll(client, head.data(), head.size());
			if (method != "HEAD")
				SendAll(client, body.data(), body.size());
			::shutdown(client, 1);   // 보내기 끝 (SD_SEND)
			::closesocket(client);
		}

		void AcceptLoop(SOCKET listen)
		{
			while (!s_Stop)
			{
				SOCKET client = ::accept(listen, nullptr, nullptr);
				if (client == INVALID_SOCKET)
					break;
				std::thread(Respond, client).detach();
			}
		}
	}

	std::wstring EnginePlayerDir() { return WebTools::PlayerDir().wstring(); }
	std::wstring DotnetPlayerDir() { return WebTools::HostFrameworkDir().wstring(); }

	std::string Serve(const std::wstring& folder, int port)
	{
		std::error_code ec;
		const std::wstring root = fs::absolute(folder, ec).wstring();
		{
			std::lock_guard<std::mutex> g(s_ServerLock);
			if (s_Listen != INVALID_SOCKET && (port == 0 || s_Url.find(":" + std::to_string(port) + "/") != std::string::npos))
			{
				s_Root = root;   // 이미 열려 있다 — 폴더만 바꾼다
				return s_Url;
			}
		}
		StopServer();
		WSADATA wsa;
		if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
			return {};
		SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == INVALID_SOCKET)
			return {};
		BOOL exclusive = TRUE;   // 다른 프로그램이 같은 포트를 같이 쓰지 못하게 (SO_EXCLUSIVEADDRUSE — winsock2 에만 이름이 있다)
		::setsockopt(s, SOL_SOCKET, (int)(~SO_REUSEADDR), (const char*)&exclusive, sizeof(exclusive));
		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		int bound = 0;
		for (int p = port > 0 ? port : 8600; p <= (port > 0 ? port : 8699); ++p)
		{
			addr.sin_port = htons((u_short)p);
			if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0) { bound = p; break; }
		}
		if (!bound || ::listen(s, SOMAXCONN) != 0)
		{
			::closesocket(s);
			EditorLog::Write("WebBuild", "web server: no free port (%d)", port);
			return {};
		}
		std::lock_guard<std::mutex> g(s_ServerLock);
		s_Root = root;
		s_Url = "http://localhost:" + std::to_string(bound) + "/";
		s_Listen = s;
		s_Stop = false;
		std::thread(AcceptLoop, s).detach();   // 에디터가 닫힐 때 기다리지 않는다 (소켓을 닫으면 끝난다)
		EditorLog::Write("WebBuild", "web server %s -> %s", s_Url.c_str(), wstring_to_string(root).c_str());
		return s_Url;
	}

	void StopServer()
	{
		SOCKET s;
		{
			std::lock_guard<std::mutex> g(s_ServerLock);
			s = s_Listen;
			s_Listen = INVALID_SOCKET;
			s_Url.clear();
		}
		if (s == INVALID_SOCKET)
			return;
		s_Stop = true;
		::closesocket(s);   // accept 가 깨어나 루프가 끝난다
		EditorLog::Write("WebBuild", "web server stopped");
	}

	std::string ServerUrl()
	{
		std::lock_guard<std::mutex> g(s_ServerLock);
		return s_Url;
	}

	bool Build(const Options& options, nlohmann::json& result, std::string& error)
	{
		if (BuildSettings::EnabledScenes().empty()) { error = "no scenes in Build Settings"; return false; }
		nlohmann::json args = { { "out", wstring_to_string(options.OutputFolder) } };
		if (!options.TextureCompression.empty())
			args["texture-compression"] = options.TextureCompression;
		if (!WebTools::ExportGame(args, result, error))
			return false;
		BuildSettings::LastWebFolder() = wstring_to_string(options.OutputFolder);
		BuildSettings::SaveEditorBuild();
		return true;
	}

	bool Start(const Options& options, std::string& error)
	{
		if (IsRunning()) { error = "a web build is already running"; return false; }
		if (BuildSettings::EnabledScenes().empty()) { error = "no scenes in Build Settings"; return false; }
		if (WebTools::PlayerDir().empty() && WebTools::HostFrameworkDir().empty()) { error = "this engine has no web player (Binaries/Web)"; return false; }
		s_Job = std::make_unique<Job>();
		s_Job->Opt = options;
		EditorLog::Write("WebBuild", "start -> %s%s", wstring_to_string(options.OutputFolder).c_str(), options.Run ? " (run)" : "");
		return true;
	}

	bool IsRunning() { return s_Job != nullptr; }

	void Update()
	{
		if (!s_Job || s_Job->Frame++ < 2)   // 진행 창이 한 번 그려진 뒤에
			return;
		const Options o = s_Job->Opt;
		s_Job.reset();
		nlohmann::json result;
		std::string error;
		if (!Build(o, result, error))
		{
			Debug::LogError("Web build failed: " + error);
			return;
		}
		const nlohmann::json& cs = result.value("csharp", nlohmann::json::object());
		Debug::Log("Web build: " + wstring_to_string(o.OutputFolder) + " (" + std::to_string(result.value("files", 0)) + " files, " +
			std::to_string((result.value("dataBytes", 0ull) + result.value("playerBytes", 0ull)) / 1048576ull) + " MB, " +
			(result.value("runtime", std::string()) == "dotnet" ? "C# scripts" : "no C# scripts") + ")");
		if (cs.contains("error"))
			Debug::LogWarning("Web build: " + cs.value("error", std::string()));
		if (o.Run)
		{
			const std::string url = Serve(o.OutputFolder);
			if (url.empty()) Debug::LogError("Web build: the preview web server could not start");
			else ::ShellExecuteW(nullptr, L"open", string_to_wstring(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		else
			::ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + o.OutputFolder + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
	}

	void DrawProgress()
	{
		if (!IsRunning())
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(460, 0));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (ImGui::Begin("Building Player (Web)", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextUnformatted("Exporting game data and WebGPU shaders");
			ImGui::Spacing();
			ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, 18));
			ImGui::Spacing();
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}
