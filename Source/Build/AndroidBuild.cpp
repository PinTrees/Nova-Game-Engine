#include "pch.h"
#include "AndroidBuild.h"
#include "AndroidTools.h"
#include "BuildSettings.h"
#include "PathManager.h"
#include "Debug.h"
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace AndroidBuild
{
	namespace
	{
		constexpr const char* kAbi = "x86_64";   // 지금은 MuMu (x86_64) 만 — 실제 휴대폰 (arm64-v8a) 은 나중

		enum class Stage { Shaders, Game, Package };

		struct Job
		{
			Options Opt;
			std::wstring Sdk, Java, BuildTools, AndroidJar, Lib, Staging;
			std::string Package, Product, Version, Orientation;
			int VersionCode = 1;
			bool HasIcon = false;
			bool AppBundle = false;
			std::wstring Keystore;            // 비면 디버그 키
			std::string StorePass, Alias, KeyPass;
			bool Development = false;
			Stage Step = Stage::Shaders;
			int Frame = 0;
			std::atomic<float> Progress{ 0.0f };
			std::mutex Lock;
			std::string Status;
			std::thread Worker;
			std::atomic<bool> WorkerDone{ false };
			Result R;
			std::chrono::steady_clock::time_point T0 = std::chrono::steady_clock::now();

			void SetStatus(const std::string& s, float p)
			{
				{
					std::lock_guard<std::mutex> g(Lock);
					Status = s;
				}
				Progress = p;
				EditorLog::Write("AndroidBuild", "%s", s.c_str());
			}
		};

		std::shared_ptr<Job> s_Job;
		std::mutex s_LastLock;
		Result s_Last;

		std::wstring Env(const wchar_t* name)
		{
			wchar_t buf[2048] = {};
			const DWORD n = ::GetEnvironmentVariableW(name, buf, 2048);
			return n > 0 && n < 2048 ? std::wstring(buf, n) : std::wstring();
		}

		// Hub 가 설치한 공용 도구: NOVA_ANDROID_TOOLS → 엔진 (<NOVA>\Editors\<버전>) 옆 → %LOCALAPPDATA%\NOVA\AndroidTools
		std::vector<fs::path> NovaTools()
		{
			std::vector<fs::path> out;
			std::error_code ec;
			for (const fs::path& p : { fs::path(Env(L"NOVA_ANDROID_TOOLS")), (fs::path(PathManager::GetI()->GetEnginePathW()) / L".." / L".." / L"AndroidTools").lexically_normal(),
				fs::path(Env(L"LOCALAPPDATA")) / L"NOVA" / L"AndroidTools" })
				if (!p.empty() && fs::is_directory(p, ec))
					out.push_back(p);
			return out;
		}

		// 이름이 버전인 폴더 중 가장 높은 것 (build-tools/36.1.0 …)
		fs::path Newest(const fs::path& dir)
		{
			auto key = [](const std::wstring& name) {
				std::vector<int> v;
				int cur = -1;
				for (wchar_t c : name)
				{
					if (iswdigit(c)) cur = (cur < 0 ? 0 : cur * 10) + (c - L'0');
					else if (cur >= 0) { v.push_back(cur); cur = -1; }
				}
				if (cur >= 0) v.push_back(cur);
				return v;
			};
			fs::path best;
			std::vector<int> bestKey;
			std::error_code ec;
			for (const auto& e : fs::directory_iterator(dir, ec))
				if (e.is_directory(ec))
				{
					const std::vector<int> k = key(e.path().filename().wstring());
					if (best.empty() || k > bestKey) { best = e.path(); bestKey = k; }
				}
			return best;
		}

		std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

		// 외부 프로그램 (출력을 모은다). 창 없이
		DWORD Run(const std::wstring& commandLine, const std::wstring& workDir, std::string& output)
		{
			SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
			HANDLE readPipe = nullptr, writePipe = nullptr;
			if (!::CreatePipe(&readPipe, &writePipe, &sa, 0))
				return (DWORD)-1;
			::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdOutput = writePipe;
			si.hStdError = writePipe;
			PROCESS_INFORMATION pi = {};
			std::wstring cmd = commandLine;
			const BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
			::CloseHandle(writePipe);
			if (!ok)
			{
				::CloseHandle(readPipe);
				output = "cannot start: " + wstring_to_string(commandLine);
				return (DWORD)-1;
			}
			std::string raw;
			char buf[4096];
			DWORD read = 0;
			while (::ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
				raw.append(buf, read);
			::WaitForSingleObject(pi.hProcess, INFINITE);
			DWORD code = 1;
			::GetExitCodeProcess(pi.hProcess, &code);
			::CloseHandle(pi.hProcess);
			::CloseHandle(pi.hThread);
			::CloseHandle(readPipe);
			output = raw;
			return code;
		}

		// 출력을 모으지 않고 (adb 서버처럼 뒤에 남는 프로세스가 파이프를 물고 있으면 Run 이 끝나지 않는다)
		void RunDetached(const std::wstring& commandLine)
		{
			STARTUPINFOW si = {};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi = {};
			std::wstring cmd = commandLine;
			if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
			{
				::WaitForSingleObject(pi.hProcess, 30000);
				::CloseHandle(pi.hProcess);
				::CloseHandle(pi.hThread);
			}
		}

		// Mono 의 선택 구성 요소 (디버거 · 진단 추적 · 핫 리로드) 는 게임에 넣지 않는다 (없으면 Mono 가 빈 구현을 쓴다)
		bool IsOptionalMonoLibrary(const std::string& name)
		{
			return name.find("component-debugger") != std::string::npos || name.find("component-diagnostics_tracing") != std::string::npos ||
				name.find("component-hot_reload") != std::string::npos;
		}

		std::wstring Adb() { return (fs::path(FindSdk()) / L"platform-tools" / L"adb.exe").wstring(); }

		uint32_t Crc32(const uint8_t* data, size_t size)
		{
			static uint32_t table[256];
			static bool init = false;
			if (!init)
			{
				for (uint32_t i = 0; i < 256; ++i)
				{
					uint32_t c = i;
					for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					table[i] = c;
				}
				init = true;
			}
			uint32_t crc = 0xFFFFFFFFu;
			for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
			return crc ^ 0xFFFFFFFFu;
		}

		// DEFLATE (RFC 1951) — zlib 없이: LZ77 (32 KB 창, 해시 사슬) + 고정 허프만 (BTYPE 01) 한 블록. .so 는 원래의 40 ~ 50 %
		std::vector<uint8_t> Deflate(const uint8_t* data, size_t n)
		{
			std::vector<uint8_t> out;
			out.reserve(n / 2 + 64);
			uint32_t bitBuf = 0;
			int bitCount = 0;
			auto bits = [&](uint32_t v, int count) {   // LSB 먼저
				bitBuf |= v << bitCount;
				bitCount += count;
				while (bitCount >= 8) { out.push_back((uint8_t)bitBuf); bitBuf >>= 8; bitCount -= 8; }
			};
			auto huff = [&](uint32_t code, int len) {   // 허프만 부호는 MSB 먼저 → 뒤집어서
				uint32_t r = 0;
				for (int i = 0; i < len; ++i) r |= ((code >> i) & 1) << (len - 1 - i);
				bits(r, len);
			};
			auto literal = [&](uint32_t sym) {
				if (sym < 144) huff(0x30 + sym, 8);
				else if (sym < 256) huff(0x190 + sym - 144, 9);
				else if (sym < 280) huff(sym - 256, 7);
				else huff(0xC0 + sym - 280, 8);
			};
			static const uint16_t lenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
			static const uint8_t lenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
			static const uint16_t distBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
			static const uint8_t distExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
			constexpr uint32_t kWindow = 32768, kHashBits = 15, kMaxChain = 48, kNice = 128;
			std::vector<int64_t> head(1u << kHashBits, -1), prev(kWindow, -1);
			auto hash = [&](size_t i) { return (((uint32_t)data[i] << 10) ^ ((uint32_t)data[i + 1] << 5) ^ data[i + 2]) & ((1u << kHashBits) - 1); };
			auto insert = [&](size_t i) { if (i + 2 < n) { const uint32_t h = hash(i); prev[i % kWindow] = head[h]; head[h] = (int64_t)i; } };
			bits(1, 1);   // BFINAL
			bits(1, 2);   // BTYPE = 01 (고정 허프만)
			size_t i = 0;
			while (i < n)
			{
				size_t bestLen = 0, bestDist = 0;
				if (i + 2 < n)
				{
					int64_t cand = head[hash(i)];
					const size_t maxLen = (std::min)((size_t)258, n - i);
					for (uint32_t chain = 0; cand >= 0 && i - (size_t)cand <= kWindow && chain < kMaxChain; ++chain)
					{
						const uint8_t* a = data + cand;
						const uint8_t* b = data + i;
						if (bestLen == 0 || (bestLen < maxLen && a[bestLen] == b[bestLen]))
						{
							size_t l = 0;
							while (l < maxLen && a[l] == b[l]) ++l;
							if (l > bestLen) { bestLen = l; bestDist = i - (size_t)cand; if (l >= kNice) break; }
						}
						const int64_t next = prev[(size_t)cand % kWindow];
						if (next >= cand) break;
						cand = next;
					}
				}
				if (bestLen >= 3)
				{
					int c = 0;
					while (c < 28 && lenBase[c + 1] <= bestLen) ++c;
					literal(257 + c);
					if (lenExtra[c]) bits((uint32_t)(bestLen - lenBase[c]), lenExtra[c]);
					int d = 0;
					while (d < 29 && distBase[d + 1] <= bestDist) ++d;
					huff((uint32_t)d, 5);
					if (distExtra[d]) bits((uint32_t)(bestDist - distBase[d]), distExtra[d]);
					for (size_t k = 0; k < bestLen; ++k) insert(i + k);
					i += bestLen;
				}
				else
				{
					literal(data[i]);
					insert(i);
					++i;
				}
			}
			literal(256);   // 블록 끝
			if (bitCount > 0) out.push_back((uint8_t)bitBuf);
			return out;
		}

		// APK (zip) 끝에 파일들을 한 번에 더한다 (aapt 없이 — SDK 의 옛 도구에 기대지 않는다). deflate 가 이득일 때만 압축 (Android 는 extractNativeLibs 로 푼다).
		//  기존 항목은 그대로 두고 중앙 디렉터리 · 끝 레코드만 다시 쓴다 (zip64 아님 — APK 는 4 GB 보다 작다)
		bool ZipAppendStored(const fs::path& zipPath, const std::vector<std::pair<std::string, fs::path>>& adds, std::string& error)
		{
			auto readAll = [](const fs::path& p, std::vector<uint8_t>& out) {
				std::ifstream in(p, std::ios::binary);
				if (!in) return false;
				out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
				return true;
			};
			std::vector<uint8_t> zip;
			if (!readAll(zipPath, zip)) { error = "cannot read " + wstring_to_string(zipPath.wstring()); return false; }
			auto rd16 = [&](size_t o) { return (uint32_t)zip[o] | ((uint32_t)zip[o + 1] << 8); };
			auto rd32 = [&](size_t o) { return rd16(o) | (rd16(o + 2) << 16); };
			size_t eocd = std::string::npos;
			for (size_t i = zip.size() >= 22 ? zip.size() - 22 : 0; i != (size_t)-1 && zip.size() - i <= 22 + 65535; --i)
				if (rd32(i) == 0x06054b50u) { eocd = i; break; }
			if (eocd == std::string::npos) { error = "not a zip (no end record)"; return false; }
			const uint32_t entries = rd16(eocd + 10), cdSize = rd32(eocd + 12), cdOffset = rd32(eocd + 16);
			std::vector<uint8_t> out(zip.begin(), zip.begin() + cdOffset);
			auto w16 = [&](uint32_t v) { const uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; out.insert(out.end(), b, b + 2); };
			auto w32 = [&](uint32_t v) { w16(v & 0xFFFF); w16(v >> 16); };
			// 지역 머리 + 데이터 (파일마다)
			struct Added { std::string Name; uint32_t Crc, Size, Packed, Method, Offset; };
			std::vector<Added> added;
			for (const auto& [name, file] : adds)
			{
				std::vector<uint8_t> data;
				if (!readAll(file, data)) { error = "cannot read " + wstring_to_string(file.wstring()); return false; }
				std::vector<uint8_t> packed = Deflate(data.data(), data.size());
				const bool deflated = packed.size() < data.size();
				const std::vector<uint8_t>& body = deflated ? packed : data;
				const Added a = { name, Crc32(data.data(), data.size()), (uint32_t)data.size(), (uint32_t)body.size(), deflated ? 8u : 0u, (uint32_t)out.size() };
				w32(0x04034b50u); w16(20); w16(0); w16(a.Method); w16(0); w16(0x21); w32(a.Crc); w32(a.Packed); w32(a.Size); w16((uint32_t)name.size()); w16(0);
				out.insert(out.end(), name.begin(), name.end());
				out.insert(out.end(), body.begin(), body.end());
				added.push_back(a);
			}
			// 예전 중앙 디렉터리 + 새 항목들
			const uint32_t newCdOffset = (uint32_t)out.size();
			out.insert(out.end(), zip.begin() + cdOffset, zip.begin() + cdOffset + cdSize);
			for (const Added& a : added)
			{
				w32(0x02014b50u); w16(20); w16(20); w16(0); w16(a.Method); w16(0); w16(0x21); w32(a.Crc); w32(a.Packed); w32(a.Size); w16((uint32_t)a.Name.size());
				w16(0); w16(0); w16(0); w16(0); w32(0); w32(a.Offset);
				out.insert(out.end(), a.Name.begin(), a.Name.end());
			}
			const uint32_t newCdSize = (uint32_t)out.size() - newCdOffset, total = entries + (uint32_t)added.size();
			// 끝 레코드
			w32(0x06054b50u); w16(0); w16(0); w16(total); w16(total); w32(newCdSize); w32(newCdOffset); w16(0);
			// 항목 이름의 '\' → '/' (중앙 디렉터리와 지역 머리 모두, 길이가 같아 제자리에서). 에디터에서 띄운 aapt2 는 하위 폴더를
			//  'assets/game\Assets\…' 로 적어 기기의 AssetManager 가 assets/game/… 을 못 찾는다 (명령 창 · 파이썬에서 띄우면 '/')
			auto o16 = [&](size_t o) { return (uint32_t)out[o] | ((uint32_t)out[o + 1] << 8); };
			auto o32 = [&](size_t o) { return o16(o) | (o16(o + 2) << 16); };
			size_t p = newCdOffset;
			for (uint32_t i = 0; i < total && p + 46 <= out.size() && o32(p) == 0x02014b50u; ++i)
			{
				const uint32_t nl = o16(p + 28), el = o16(p + 30), cl = o16(p + 32), local = o32(p + 42);
				for (uint32_t k = 0; k < nl; ++k)
					if (out[p + 46 + k] == '\\') out[p + 46 + k] = '/';
				if (local + 30 <= out.size() && o32(local) == 0x04034b50u && o16(local + 26) == nl)
					for (uint32_t k = 0; k < nl; ++k)
						if (out[local + 30 + k] == '\\') out[local + 30 + k] = '/';
				p += 46 + nl + el + cl;
			}
			std::ofstream os(zipPath, std::ios::binary | std::ios::trunc);
			os.write((const char*)out.data(), (std::streamsize)out.size());
			if (!os) { error = "cannot write " + wstring_to_string(zipPath.wstring()); return false; }
			return true;
		}

		std::string Sanitize(const std::string& s)
		{
			std::string out;
			for (char c : s)
				if (isalnum((unsigned char)c) || c == '_')
					out += c;
			if (out.empty()) out = "Game";
			if (!isalpha((unsigned char)out[0])) out = "N" + out;
			return out;
		}

		std::string XmlEscape(const std::string& s)
		{
			std::string out;
			for (char c : s)
				switch (c)
				{
				case '&': out += "&amp;"; break;
				case '<': out += "&lt;"; break;
				case '>': out += "&gt;"; break;
				case '"': out += "&quot;"; break;
				default: out += c;
				}
			return out;
		}

		// NativeActivity (Java 코드 없음) — Android/AndroidManifest.xml 과 같은 내용에 패키지 이름 · 제품 이름 · 버전
		std::string Manifest(const Job& j)
		{
			return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
				"<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\" package=\"" + j.Package + "\" android:versionCode=\"" + std::to_string(j.VersionCode) + "\" android:versionName=\"" + XmlEscape(j.Version) + "\">\n"
				"    <uses-sdk android:minSdkVersion=\"26\" android:targetSdkVersion=\"34\" />\n"
				"    <uses-feature android:glEsVersion=\"0x00030002\" android:required=\"true\" />\n"
				"    <application android:label=\"" + XmlEscape(j.Product) + "\"" + (j.HasIcon ? " android:icon=\"@mipmap/ic_launcher\"" : "") + " android:hasCode=\"false\" android:extractNativeLibs=\"true\"" + (j.Development ? " android:debuggable=\"true\"" : "") + ">\n"
				"        <activity android:name=\"android.app.NativeActivity\" android:exported=\"true\" android:configChanges=\"orientation|screenSize|keyboardHidden|screenLayout\"\n"
				"                  android:screenOrientation=\"" + j.Orientation + "\"\n"
				"                  android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n"
				"            <meta-data android:name=\"android.app.lib_name\" android:value=\"nova\" />\n"
				"            <intent-filter>\n"
				"                <action android:name=\"android.intent.action.MAIN\" />\n"
				"                <category android:name=\"android.intent.category.LAUNCHER\" />\n"
				"            </intent-filter>\n"
				"        </activity>\n"
				"    </application>\n"
				"</manifest>\n";
		}

		// zip 항목을 압축된 그대로 읽고 (풀지 않음) 쓴다 — AAB 는 aapt2 의 proto 출력 항목을 이름만 바꿔 base/ 모듈로 옮긴다
		struct ZipItem { std::string Name; uint16_t Method = 0; uint32_t Crc = 0, Packed = 0, Size = 0; std::vector<uint8_t> Data; };

		bool ZipReadRaw(const fs::path& path, std::vector<ZipItem>& items, std::string& error)
		{
			std::ifstream in(path, std::ios::binary);
			std::vector<uint8_t> z((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			auto r16 = [&](size_t o) { return o + 2 <= z.size() ? (uint32_t)z[o] | (uint32_t)z[o + 1] << 8 : 0u; };
			auto r32 = [&](size_t o) { return r16(o) | r16(o + 2) << 16; };
			size_t eocd = std::string::npos;
			for (size_t i = z.size() >= 22 ? z.size() - 22 : 0; i != (size_t)-1 && z.size() - i <= 22 + 65535; --i)
				if (r32(i) == 0x06054b50u) { eocd = i; break; }
			if (eocd == std::string::npos) { error = "not a zip: " + wstring_to_string(path.wstring()); return false; }
			size_t p = r32(eocd + 16);
			for (uint32_t i = 0, n = r16(eocd + 10); i < n && r32(p) == 0x02014b50u; ++i)
			{
				ZipItem it;
				it.Method = (uint16_t)r16(p + 10);
				it.Crc = r32(p + 16);
				it.Packed = r32(p + 20);
				it.Size = r32(p + 24);
				const uint32_t nl = r16(p + 28), el = r16(p + 30), cl = r16(p + 32), local = r32(p + 42);
				it.Name.assign((const char*)&z[p + 46], nl);
				std::replace(it.Name.begin(), it.Name.end(), '\\', '/');
				const size_t data = (size_t)local + 30 + r16(local + 26) + r16(local + 28);
				if (data + it.Packed > z.size()) { error = "broken zip entry " + it.Name; return false; }
				it.Data.assign(z.begin() + data, z.begin() + data + it.Packed);
				items.push_back(std::move(it));
				p += 46 + nl + el + cl;
			}
			return true;
		}

		ZipItem ZipFile(const std::string& name, const std::vector<uint8_t>& data)
		{
			ZipItem it;
			it.Name = name;
			it.Crc = Crc32(data.data(), data.size());
			it.Size = (uint32_t)data.size();
			std::vector<uint8_t> packed = Deflate(data.data(), data.size());
			if (packed.size() < data.size()) { it.Method = 8; it.Data = std::move(packed); }
			else it.Data = data;
			it.Packed = (uint32_t)it.Data.size();
			return it;
		}

		bool ZipWrite(const fs::path& path, const std::vector<ZipItem>& items, std::string& error)
		{
			std::vector<uint8_t> out;
			auto w16 = [&](uint32_t v) { const uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; out.insert(out.end(), b, b + 2); };
			auto w32 = [&](uint32_t v) { w16(v & 0xFFFF); w16(v >> 16); };
			std::vector<uint32_t> offsets;
			for (const ZipItem& it : items)
			{
				offsets.push_back((uint32_t)out.size());
				w32(0x04034b50u); w16(20); w16(0); w16(it.Method); w16(0); w16(0x21); w32(it.Crc); w32(it.Packed); w32(it.Size); w16((uint32_t)it.Name.size()); w16(0);
				out.insert(out.end(), it.Name.begin(), it.Name.end());
				out.insert(out.end(), it.Data.begin(), it.Data.end());
			}
			const uint32_t cd = (uint32_t)out.size();
			for (size_t i = 0; i < items.size(); ++i)
			{
				const ZipItem& it = items[i];
				w32(0x02014b50u); w16(20); w16(20); w16(0); w16(it.Method); w16(0); w16(0x21); w32(it.Crc); w32(it.Packed); w32(it.Size); w16((uint32_t)it.Name.size());
				w16(0); w16(0); w16(0); w16(0); w32(0); w32(offsets[i]);
				out.insert(out.end(), it.Name.begin(), it.Name.end());
			}
			const uint32_t cdSize = (uint32_t)out.size() - cd;
			w32(0x06054b50u); w16(0); w16(0); w16((uint32_t)items.size()); w16((uint32_t)items.size()); w32(cdSize); w32(cd); w16(0);
			std::ofstream os(path, std::ios::binary | std::ios::trunc);
			os.write((const char*)out.data(), (std::streamsize)out.size());
			if (!os) { error = "cannot write " + wstring_to_string(path.wstring()); return false; }
			return true;
		}

		// 서명 비밀번호는 명령줄 (프로세스 목록에 보인다) 대신 환경 변수로 넘긴다 (apksigner env: · jarsigner :env)
		struct SecretEnv
		{
			SecretEnv(const std::string& store, const std::string& key)
			{
				::SetEnvironmentVariableW(L"NOVA_KS_PASS", string_to_wstring(store).c_str());
				::SetEnvironmentVariableW(L"NOVA_KEY_PASS", string_to_wstring(key).c_str());
			}
			~SecretEnv()
			{
				::SetEnvironmentVariableW(L"NOVA_KS_PASS", nullptr);
				::SetEnvironmentVariableW(L"NOVA_KEY_PASS", nullptr);
			}
		};

		// 작업 스레드: (아이콘 자원) → APK (aapt2 · 라이브러리 · zipalign · apksigner) · AAB (aapt2 proto · base 모듈 · jarsigner) → (Build And Run) 설치 · 실행
		void Package(std::shared_ptr<Job> j)
		{
			auto fail = [&](const std::string& step, const std::string& output) {
				j->R.Error = step + (output.empty() ? "" : ": " + output.substr(output.size() > 1500 ? output.size() - 1500 : 0));
				j->WorkerDone = true;
			};
			std::error_code ec;
			const fs::path stage = j->Staging;
			std::string out;
			const std::wstring aapt2 = Quote((fs::path(j->BuildTools) / L"aapt2.exe").wstring());
			const std::wstring javaBin = (fs::path(j->Java) / L"bin").wstring();

			j->SetStatus("Writing AndroidManifest.xml", 0.55f);
			std::ofstream(stage / L"AndroidManifest.xml", std::ios::binary | std::ios::trunc) << Manifest(*j);

			// 아이콘 (Start 가 res/mipmap-*/ic_launcher.png 를 만들었으면): aapt2 compile → 링크 입력
			std::wstring resInput;
			if (j->HasIcon)
			{
				fs::remove(stage / L"res.zip", ec);
				if (Run(aapt2 + L" compile --dir res -o res.zip", stage.wstring(), out) != 0)
					return fail("aapt2 compile (icon) failed", out);
				resInput = L" res.zip";
			}

			// 네이티브 라이브러리: 플레이어 + C# 스크립트가 있으면 (게임 데이터의 Managed/) Mono (libmonosgen-2.0 · System.Native · 구성 요소)
			std::vector<std::pair<std::string, fs::path>> libs = { { std::string("lib/") + kAbi + "/libnova.so", j->Lib } };
			std::wstring monoLib, monoNative;
			if (fs::exists(stage / L"assets" / L"game" / L"Managed", ec) && AndroidTools::MonoRuntime(kAbi, monoLib, monoNative))
				for (const auto& e : fs::directory_iterator(monoNative, ec))
					if (e.path().extension() == L".so" && !IsOptionalMonoLibrary(e.path().filename().string()))
						libs.push_back({ std::string("lib/") + kAbi + "/" + wstring_to_string(e.path().filename().wstring()), e.path() });

			// 서명 키: Custom Keystore (Publishing Settings) 또는 디버그 키 (Unity 와 같이 없으면 만든다)
			fs::path keystore = j->Keystore;
			std::string storePass = j->StorePass, alias = j->Alias, keyPass = j->KeyPass;
			if (keystore.empty())
			{
				keystore = fs::path(Env(L"USERPROFILE")) / L".android" / L"debug.keystore";
				storePass = keyPass = "android";
				alias = "androiddebugkey";
				if (!fs::exists(keystore, ec))
				{
					std::string err;
					if (!CreateKeystore(keystore.wstring(), storePass, alias, keyPass, "CN=Android Debug,O=Android,C=US", 27, err))
						return fail("debug keystore", err);
				}
			}
			j->R.Signer = keystore == fs::path(Env(L"USERPROFILE")) / L".android" / L"debug.keystore" ? "debug key" : wstring_to_string(keystore.filename().wstring()) + " (" + alias + ")";
			SecretEnv secrets(storePass, keyPass);

			const fs::path outApk = fs::path(j->Opt.OutputApk).replace_extension(L".apk");
			const fs::path outAab = fs::path(j->Opt.OutputApk).replace_extension(L".aab");
			fs::create_directories(outApk.parent_path(), ec);
			// APK: 그냥 빌드 · Build And Run (AAB 를 만들 때도 실행은 같은 내용의 APK 로 — Unity 는 bundletool 로 같은 일)
			const bool wantApk = !j->AppBundle || j->Opt.Run;
			if (wantApk)
			{
				j->SetStatus("Linking APK (aapt2)", 0.6f);
				const fs::path base = stage / L"base.apk", aligned = stage / L"aligned.apk";
				fs::remove(base, ec);
				if (Run(aapt2 + L" link -o " + Quote(base.wstring()) + L" --manifest " + Quote((stage / L"AndroidManifest.xml").wstring()) +
					L" -I " + Quote(j->AndroidJar) + L" -A assets" + resInput, stage.wstring(), out) != 0)   // 하위 폴더 이름의 '\' 는 아래 ZipAppendStored 가 '/' 로
					return fail("aapt2 link failed", out);
				j->SetStatus("Adding the player library (libnova.so)", 0.68f);
				if (!ZipAppendStored(base, libs, out))
					return fail("adding native libraries failed", out);
				j->SetStatus("Aligning (zipalign)", 0.74f);
				fs::remove(aligned, ec);
				if (Run(Quote((fs::path(j->BuildTools) / L"zipalign.exe").wstring()) + L" -p -f 4 " + Quote(base.wstring()) + L" " + Quote(aligned.wstring()), stage.wstring(), out) != 0)
					return fail("zipalign failed", out);
				j->SetStatus("Signing APK (apksigner, " + j->R.Signer + ")", 0.78f);
				if (Run(Quote(javaBin + L"\\java.exe") + L" -jar " + Quote((fs::path(j->BuildTools) / L"lib" / L"apksigner.jar").wstring()) + L" sign --ks " + Quote(keystore.wstring()) +
					L" --v4-signing-enabled false --ks-pass env:NOVA_KS_PASS --key-pass env:NOVA_KEY_PASS --ks-key-alias " + Quote(string_to_wstring(alias)) + L" --out " + Quote(outApk.wstring()) + L" " + Quote(aligned.wstring()),
					stage.wstring(), out) != 0)
					return fail("apksigner failed (keystore password or alias?)", out);
				j->R.Apk = wstring_to_string(outApk.wstring());
				j->R.Bytes = fs::file_size(outApk, ec);
			}
			// AAB (Google Play): aapt2 의 proto 형식 → base/ 모듈 (manifest/ · res/ · resources.pb · assets/ · lib/) + BundleConfig.pb → jarsigner
			if (j->AppBundle)
			{
				j->SetStatus("Linking App Bundle (aapt2 --proto-format)", 0.82f);
				const fs::path proto = stage / L"base-proto.zip";
				fs::remove(proto, ec);
				if (Run(aapt2 + L" link --proto-format -o " + Quote(proto.wstring()) + L" --manifest " + Quote((stage / L"AndroidManifest.xml").wstring()) +
					L" -I " + Quote(j->AndroidJar) + L" -A assets" + resInput, stage.wstring(), out) != 0)
					return fail("aapt2 link --proto-format failed", out);
				std::vector<ZipItem> in, bundle;
				if (!ZipReadRaw(proto, in, out))
					return fail("reading the proto output failed", out);
				// BundleConfig.pb: { bundletool { version: "1.18.3" } } (protobuf: 필드 1 메시지 · 그 안 필드 2 문자열)
				const std::string tool = "1.18.3";
				std::vector<uint8_t> config = { 0x0A, (uint8_t)(tool.size() + 2), 0x12, (uint8_t)tool.size() };
				config.insert(config.end(), tool.begin(), tool.end());
				bundle.push_back(ZipFile("BundleConfig.pb", config));
				for (ZipItem& it : in)
				{
					if (it.Name == "AndroidManifest.xml") it.Name = "base/manifest/AndroidManifest.xml";
					else if (it.Name == "resources.pb" || it.Name.rfind("res/", 0) == 0 || it.Name.rfind("assets/", 0) == 0) it.Name = "base/" + it.Name;
					else it.Name = "base/root/" + it.Name;
					bundle.push_back(std::move(it));
				}
				j->SetStatus("Adding libraries to the App Bundle", 0.86f);
				for (const auto& [name, file] : libs)
				{
					std::ifstream lf(file, std::ios::binary);
					std::vector<uint8_t> data((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
					bundle.push_back(ZipFile("base/" + name, data));
				}
				fs::remove(outAab, ec);
				if (!ZipWrite(outAab, bundle, out))
					return fail("writing the App Bundle failed", out);
				j->SetStatus("Signing App Bundle (jarsigner, " + j->R.Signer + ")", 0.88f);
				if (Run(Quote(javaBin + L"\\jarsigner.exe") + L" -keystore " + Quote(keystore.wstring()) + L" -storepass:env NOVA_KS_PASS -keypass:env NOVA_KEY_PASS" +
					L" -sigalg SHA256withRSA -digestalg SHA-256 " + Quote(outAab.wstring()) + L" " + Quote(string_to_wstring(alias)), stage.wstring(), out) != 0)
					return fail("jarsigner failed (keystore password or alias?)", out);
				j->R.Aab = wstring_to_string(outAab.wstring());
				if (!wantApk) j->R.Bytes = fs::file_size(outAab, ec);
			}

			if (j->Opt.Run)
			{
				j->SetStatus("Looking for a device (adb)", 0.9f);
				const std::vector<std::string> devices = Devices(true);
				std::string device = j->Opt.Device;
				if (device.empty() && !devices.empty()) device = devices[0];
				if (device.empty() || std::find(devices.begin(), devices.end(), device) == devices.end())
					return fail("no Android device (start MuMu Player or connect a device with USB debugging)", device.empty() ? "" : "device " + device + " not connected");
				j->R.Device = device;
				j->SetStatus("Installing on " + device, 0.93f);
				const std::wstring adb = Quote(Adb()) + L" -s " + string_to_wstring(device);
				if (Run(adb + L" install -r " + Quote(outApk.wstring()), L"", out) != 0 || out.find("Success") == std::string::npos)
					return fail("adb install failed" + std::string(out.find("UPDATE_INCOMPATIBLE") != std::string::npos ? " (installed app is signed with another key - uninstall it first)" : ""), out);
				j->SetStatus("Starting " + j->Package, 0.97f);
				Run(adb + L" shell am force-stop " + string_to_wstring(j->Package), L"", out);
				if (Run(adb + L" shell am start -n " + string_to_wstring(j->Package) + L"/android.app.NativeActivity", L"", out) != 0 || out.find("Error") != std::string::npos)
					return fail("adb am start failed", out);
			}
			j->R.Success = true;
			j->WorkerDone = true;
		}

		void Finish(std::shared_ptr<Job> j)
		{
			j->R.Done = true;
			j->R.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - j->T0).count();
			if (j->R.Success)
			{
				char msg[512];
				sprintf_s(msg, "Android build succeeded: %s (%.1f MB, %.1f s)%s", j->R.Apk.c_str(), j->R.Bytes / 1048576.0, j->R.Seconds,
					j->R.Device.empty() ? "" : (" — running on " + j->R.Device).c_str());
				Debug::Log(msg);
			}
			else
				Debug::LogError("Android build failed: " + j->R.Error);
			EditorLog::Write("AndroidBuild", "%s %s", j->R.Success ? "done" : "failed", j->R.Success ? j->R.Apk.c_str() : j->R.Error.c_str());
			std::lock_guard<std::mutex> g(s_LastLock);
			s_Last = j->R;
		}
	}

	std::wstring FindSdk()
	{
		std::error_code ec;
		std::vector<fs::path> candidates = { Env(L"ANDROID_HOME"), Env(L"ANDROID_SDK_ROOT") };
		for (const fs::path& t : NovaTools()) candidates.push_back(t / L"sdk");
		candidates.push_back(fs::path(Env(L"LOCALAPPDATA")) / L"Android" / L"Sdk");
		for (const fs::path& p : candidates)
			if (!p.empty() && fs::is_directory(p / L"build-tools", ec))
				return p.wstring();
		return {};
	}

	std::wstring FindJava()
	{
		std::error_code ec;
		std::vector<fs::path> candidates = { Env(L"JAVA_HOME") };
		for (const fs::path& t : NovaTools()) candidates.push_back(t / L"jdk");
		candidates.push_back(L"C:\\Program Files\\Android\\Android Studio\\jbr");
		for (const fs::path& p : candidates)
			if (!p.empty() && fs::exists(p / L"bin" / L"java.exe", ec))
				return p.wstring();
		return {};
	}

	std::wstring PlayerLibrary(const std::string& abi)
	{
		std::error_code ec;
		const fs::path engine = PathManager::GetI()->GetEnginePathW();
		for (const fs::path& p : { engine / L"Android" / L"Player" / string_to_wstring(abi) / L"libnova.so",
			engine / L"Android" / L"build" / L"cmake" / (string_to_wstring(abi) + L"-Release") / L"libnova.so" })
			if (fs::is_regular_file(p, ec))
				return p.wstring();
		return {};
	}

	std::vector<std::string> Devices(bool connectMuMu)
	{
		std::vector<std::string> out;
		std::error_code ec;
		if (!fs::exists(Adb(), ec))
			return out;
		RunDetached(Quote(Adb()) + L" start-server");
		std::string text;
		if (connectMuMu)
		{
			// MuMu 플레이어 12: 켜져 있는 VM 의 adb 포트로 connect (MuMuManager info -v all)
			std::vector<fs::path> managers;
			for (const wchar_t* root : { L"C:\\Program Files\\Netease", L"D:\\Program Files\\Netease", L"C:\\Program Files (x86)\\Netease" })
				for (const wchar_t* sub : { L"MuMuPlayer\\nx_main\\MuMuManager.exe", L"MuMuPlayer-12.0\\shell\\MuMuManager.exe" })
					if (fs::exists(fs::path(root) / sub, ec))
						managers.push_back(fs::path(root) / sub);
			if (!managers.empty() && Run(Quote(managers[0].wstring()) + L" info -v all", L"", text) == 0)
			{
				const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
				std::vector<nlohmann::json> vms;
				if (j.is_object() && j.contains("adb_port")) vms.push_back(j);
				else if (j.is_object()) for (const auto& [k, v] : j.items()) if (v.is_object()) vms.push_back(v);
				for (const auto& vm : vms)
					if (vm.value("is_android_started", false) && vm.contains("adb_port") && !vm["adb_port"].is_null())
					{
						const std::string port = vm["adb_port"].is_number() ? std::to_string(vm["adb_port"].get<int>()) : vm["adb_port"].get<std::string>();
						Run(Quote(Adb()) + L" connect 127.0.0.1:" + string_to_wstring(port), L"", text);
					}
			}
		}
		if (Run(Quote(Adb()) + L" devices", L"", text) != 0)
			return out;
		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line))
		{
			const size_t tab = line.find('\t');
			if (tab != std::string::npos && line.compare(tab + 1, 6, "device") == 0)
				out.push_back(line.substr(0, tab));
		}
		return out;
	}

	std::string DefaultPackageName()
	{
		return "com." + Sanitize(BuildSettings::GetPlayer().CompanyName) + "." + Sanitize(BuildSettings::ProductName());
	}

	std::string PackageName()
	{
		const std::string& p = BuildSettings::GetPlayer().AndroidPackageName;
		return p.empty() ? DefaultPackageName() : p;
	}

	bool CreateKeystore(const std::wstring& path, const std::string& storePass, const std::string& alias, const std::string& keyPass,
		const std::string& distinguishedName, int validityYears, std::string& error)
	{
		std::error_code ec;
		if (fs::exists(path, ec)) { error = "a keystore already exists at " + wstring_to_string(path); return false; }
		if (storePass.size() < 6 || keyPass.size() < 6) { error = "passwords need at least 6 characters (keytool)"; return false; }
		if (alias.empty()) { error = "alias is empty"; return false; }
		const std::wstring java = FindJava();
		if (java.empty()) { error = "JDK not found - install \"Android Build Support\" in NOVA Hub"; return false; }
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::string dname = distinguishedName.empty() ? "CN=" + BuildSettings::GetPlayer().CompanyName : distinguishedName;
		dname.erase(std::remove(dname.begin(), dname.end(), '"'), dname.end());
		SecretEnv secrets(storePass, keyPass);
		std::string out;
		if (Run(Quote((fs::path(java) / L"bin" / L"keytool.exe").wstring()) + L" -genkeypair -keystore " + Quote(path) + L" -storetype PKCS12 -storepass:env NOVA_KS_PASS"
			L" -alias " + Quote(string_to_wstring(alias)) + L" -keypass:env NOVA_KEY_PASS -keyalg RSA -keysize 2048 -validity " + std::to_wstring((std::max)(1, validityYears) * 365) +
			L" -dname " + Quote(string_to_wstring(dname)), L"", out) != 0 || !fs::exists(path, ec))
		{
			error = "keytool failed: " + out.substr(0, 600);
			return false;
		}
		EditorLog::Write("AndroidBuild", "keystore created %s (alias %s)", wstring_to_string(path).c_str(), alias.c_str());
		return true;
	}

	bool Start(const Options& options, std::string& error)
	{
		if (IsRunning()) { error = "an Android build is already running"; return false; }
		if (BuildSettings::EnabledScenes().empty()) { error = "no scenes in Build Settings"; return false; }
		auto j = std::make_shared<Job>();
		j->Opt = options;
		j->Sdk = FindSdk();
		j->Java = FindJava();
		std::error_code ec;
		if (j->Sdk.empty()) { error = "Android SDK not found — install \"Android Build Support\" in NOVA Hub (or set ANDROID_HOME)"; return false; }
		if (j->Java.empty()) { error = "JDK not found — install \"Android Build Support\" in NOVA Hub (or set JAVA_HOME)"; return false; }
		j->BuildTools = Newest(fs::path(j->Sdk) / L"build-tools").wstring();
		j->AndroidJar = (fs::path(j->Sdk) / L"platforms" / L"android-34" / L"android.jar").wstring();
		if (j->BuildTools.empty() || !fs::exists(fs::path(j->BuildTools) / L"aapt2.exe", ec)) { error = "Android SDK build-tools not found in " + wstring_to_string(j->Sdk); return false; }
		if (!fs::exists(j->AndroidJar, ec)) { error = "Android SDK platform android-34 not found in " + wstring_to_string(j->Sdk); return false; }
		j->Lib = PlayerLibrary(kAbi);
		if (j->Lib.empty()) { error = std::string("NOVA Android player library (Android/Player/") + kAbi + "/libnova.so) is missing in this engine"; return false; }
		j->Package = PackageName();
		j->Product = BuildSettings::ProductName();
		j->Version = BuildSettings::GetPlayer().Version;
		// Default Orientation → screenOrientation (Unity 와 같은 대응: Landscape Left = landscape, Landscape Right = reverseLandscape, Auto = fullUser)
		static const char* kOrientation[] = { "portrait", "reversePortrait", "reverseLandscape", "landscape", "fullUser" };
		j->Orientation = kOrientation[std::clamp(BuildSettings::GetPlayer().AndroidOrientation, 0, 4)];
		j->Development = BuildSettings::DevelopmentBuild();
		const BuildSettings::Player& player = BuildSettings::GetPlayer();
		j->VersionCode = (std::max)(1, player.AndroidVersionCode);
		j->AppBundle = options.AppBundle < 0 ? BuildSettings::AndroidBuildAppBundle() : options.AppBundle != 0;
		// 서명 키: 명령 (CLI) → Player Settings 의 Custom Keystore (비밀번호는 이번 실행에 넣은 것) → 디버그 키
		auto projectPath = [](const std::string& s) {
			const fs::path path = string_to_wstring(s);
			return path.is_absolute() ? path : fs::path(PathManager::GetI()->GetContentPathW()) / path;
		};
		if (!options.Keystore.empty() || player.AndroidCustomKeystore)
		{
			j->Keystore = projectPath(!options.Keystore.empty() ? options.Keystore : player.AndroidKeystorePath).wstring();
			j->Alias = !options.KeyAlias.empty() ? options.KeyAlias : player.AndroidKeyAlias;
			j->StorePass = !options.KeystorePass.empty() ? options.KeystorePass : BuildSettings::AndroidKeystorePass();
			j->KeyPass = !options.KeyAliasPass.empty() ? options.KeyAliasPass : BuildSettings::AndroidKeyAliasPass();
			if (j->KeyPass.empty()) j->KeyPass = j->StorePass;
			if (!fs::is_regular_file(j->Keystore, ec)) { error = "keystore not found: " + wstring_to_string(j->Keystore) + " (Player Settings > Publishing Settings)"; return false; }
			if (j->Alias.empty() || j->StorePass.empty()) { error = "enter the keystore password and alias in Player Settings > Publishing Settings (passwords are not saved, like Unity)"; return false; }
		}
		j->Staging = (fs::path(PathManager::GetI()->GetContentPathW()) / L"Library" / L"AndroidBuild").wstring();
		fs::remove_all(fs::path(j->Staging) / L"assets", ec);
		fs::create_directories(j->Staging, ec);
		// 아이콘 (Player Settings, 비면 NOVA 로고) → res/mipmap-<밀도>/ic_launcher.png (48 · 72 · 96 · 144 · 192 px). WIC 는 메인 스레드 (COM) 에서
		{
			const fs::path res = fs::path(j->Staging) / L"res";
			fs::remove_all(res, ec);
			const fs::path icon = !player.AndroidIcon.empty() ? fs::path(PathManager::GetI()->GetMovePathW(string_to_wstring(player.AndroidIcon)))
				: fs::path(PathManager::GetI()->GetEnginePathW()) / L"ProjectSetting" / L"logo" / L"nova-logo-512.png";
			DirectX::ScratchImage src, rgba;
			if (fs::is_regular_file(icon, ec) && SUCCEEDED(DirectX::LoadFromWICFile(icon.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, nullptr, src)))
			{
				const DirectX::Image* im = src.GetImage(0, 0, 0);
				const bool ok = im->format == DXGI_FORMAT_R8G8B8A8_UNORM ? SUCCEEDED(rgba.InitializeFromImage(*im))
					: SUCCEEDED(DirectX::Convert(*im, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba));
				static const std::pair<const wchar_t*, size_t> kDensity[] = { { L"mdpi", 48 }, { L"hdpi", 72 }, { L"xhdpi", 96 }, { L"xxhdpi", 144 }, { L"xxxhdpi", 192 } };
				int written = 0;
				for (const auto& [dpi, size] : kDensity)
				{
					DirectX::ScratchImage scaled;
					if (!ok || FAILED(DirectX::Resize(*rgba.GetImage(0, 0, 0), size, size, DirectX::TEX_FILTER_CUBIC, scaled))) break;
					const fs::path dir = res / (std::wstring(L"mipmap-") + dpi);
					fs::create_directories(dir, ec);
					if (SUCCEEDED(DirectX::SaveToWICFile(*scaled.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), (dir / L"ic_launcher.png").c_str())))
						++written;
				}
				j->HasIcon = written == (int)std::size(kDensity);
			}
			if (!j->HasIcon)
				EditorLog::Write("AndroidBuild", "icon %s could not be read - the app uses the default Android icon", wstring_to_string(icon.wstring()).c_str());
		}
		j->SetStatus("Converting shaders to OpenGL ES", 0.02f);
		s_Job = j;
		BuildSettings::LastAndroidApk() = wstring_to_string(options.OutputApk);   // Build Settings 창의 Last Build (CLI 로 빌드해도)
		BuildSettings::SaveEditorBuild();
		EditorLog::Write("AndroidBuild", "start %s -> %s (sdk %s, build-tools %s, lib %s)", j->Package.c_str(), wstring_to_string(options.OutputApk).c_str(),
			wstring_to_string(j->Sdk).c_str(), wstring_to_string(fs::path(j->BuildTools).filename().wstring()).c_str(), wstring_to_string(j->Lib).c_str());
		return true;
	}

	bool IsRunning() { return s_Job != nullptr; }
	float Progress() { return s_Job ? s_Job->Progress.load() : 1.0f; }

	std::string Status()
	{
		if (!s_Job) return {};
		std::lock_guard<std::mutex> g(s_Job->Lock);
		return s_Job->Status;
	}

	Result LastResult()
	{
		std::lock_guard<std::mutex> g(s_LastLock);
		return s_Last;
	}

	void Update()
	{
		std::shared_ptr<Job> j = s_Job;
		if (!j)
			return;
		// 메인 스레드 단계는 한 프레임 늦게 (진행 창이 먼저 그려지게)
		if (j->Step != Stage::Package && j->Frame++ == 0)
			return;
		std::string error;
		nlohmann::json result;
		const fs::path assets = fs::path(j->Staging) / L"assets";
		if (j->Step == Stage::Shaders)
		{
			if (!AndroidTools::ExportShaders({ { "out", wstring_to_string((assets / L"Shaders").wstring()) } }, result, error) || result.value("written", 0) == 0)
			{
				j->R.Error = "shader export failed: " + (error.empty() ? result.dump() : error);
				Finish(j);
				s_Job.reset();
				return;
			}
			EditorLog::Write("AndroidBuild", "shaders: %d effects, passes failed %d/%d", result.value("written", 0), result.value("passesFailed", 0), result.value("passes", 0));
			j->Step = Stage::Game;
			j->Frame = 0;
			j->SetStatus("Exporting game data (textures, meshes)", 0.2f);
			return;
		}
		if (j->Step == Stage::Game)
		{
			nlohmann::json args = { { "out", wstring_to_string(assets.wstring()) } };
			if (!j->Opt.TextureCompression.empty())
				args["texture-compression"] = j->Opt.TextureCompression;
			if (!AndroidTools::ExportGame(args, result, error))
			{
				j->R.Error = "game data export failed: " + error;
				Finish(j);
				s_Job.reset();
				return;
			}
			EditorLog::Write("AndroidBuild", "game data: %d files, %.1f MB, textures %s", result.value("files", 0), result.value("bytes", 0ull) / 1048576.0,
				result.value("textureCompression", std::string()).c_str());
			j->Step = Stage::Package;
			j->Worker = std::thread(Package, j);
			return;
		}
		if (j->WorkerDone)
		{
			if (j->Worker.joinable()) j->Worker.join();
			Finish(j);
			s_Job.reset();
		}
	}

	void DrawProgress()
	{
		if (!IsRunning())
			return;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(460, 0));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
		if (ImGui::Begin("Building Player (Android)", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::TextUnformatted(Status().c_str());
			ImGui::Spacing();
			ImGui::ProgressBar(Progress(), ImVec2(-1, 18));
			ImGui::Spacing();
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}
