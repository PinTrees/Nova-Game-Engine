#include "pch.h"
#include "Anim2DOps.h"
#include "Anim2DRaster.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Anim2D
{
	using json = nlohmann::json;

	namespace
	{
		constexpr float kDeg = 3.14159265358979f / 180.0f;
		struct Ctx { Document& D; const json& A; json& R; std::string& E; };
		using OpFn = std::function<bool(Ctx&)>;
		struct Entry { OpInfo Info; OpFn Fn; };

		float F(const json& a, const char* k, float d) { return a.contains(k) && a[k].is_number() ? a[k].get<float>() : d; }
		int I(const json& a, const char* k, int d) { return a.contains(k) && a[k].is_number() ? a[k].get<int>() : d; }
		bool B(const json& a, const char* k, bool d) { return a.contains(k) && a[k].is_boolean() ? a[k].get<bool>() : (a.contains(k) && a[k].is_number() ? a[k].get<int>() != 0 : d); }
		std::string S(const json& a, const char* k, const std::string& d = "") { return a.contains(k) && a[k].is_string() ? a[k].get<std::string>() : d; }
		bool Vec(const json& a, const char* k, int n, float* out)
		{
			if (!a.contains(k)) return false;
			json v = a[k];
			if (v.is_string())
			{
				// "84,46" (JSON 으로 부른 쪽이 문자열로 줄 때)
				json arr = json::array();
				std::stringstream ss(v.get<std::string>());
				std::string t;
				while (std::getline(ss, t, ',')) { try { arr.push_back(std::stof(t)); } catch (...) { return false; } }
				v = arr;
			}
			if (v.is_number()) { for (int i = 0; i < n; ++i) out[i] = v.get<float>(); return true; }
			if (v.is_array() && (int)v.size() >= n - (n == 4 ? 1 : 0))
			{
				for (int i = 0; i < n && i < (int)v.size(); ++i) out[i] = v[i].get<float>();
				return true;
			}
			return false;
		}
		std::vector<std::string> List(const json& a, const char* k)
		{
			std::vector<std::string> out;
			if (!a.contains(k)) return out;
			if (a[k].is_array()) { for (const auto& v : a[k]) out.push_back(v.get<std::string>()); return out; }
			std::stringstream ss(S(a, k));
			std::string t;
			while (std::getline(ss, t, ',')) if (!t.empty()) out.push_back(t);
			return out;
		}
		float R4(float v) { return roundf(v * 1e4f) / 1e4f; }

		bool Done(Ctx& c, int n, const char* what)
		{
			c.D.Pose();
			c.R = c.D.Summary();
			c.R["changed"] = { { what, n } };
			c.D.Changed();
			return true;
		}

		int BoneArg(Ctx& c, const char* key = "bone")
		{
			const std::string n = S(c.A, key);
			const int b = n.empty() ? c.D.SelectedBone : c.D.FindBone(n);
			if (b < 0) c.E = n.empty() ? "which bone? --" + std::string(key) + " <name> (anim2d bone.list)" : "no bone '" + n + "'";
			return b;
		}

		json BoneJson(const Document& d, int i)
		{
			const Bone& b = d.Bones[i];
			return { { "name", b.Name }, { "parent", b.Parent >= 0 ? d.Bones[b.Parent].Name : "" }, { "x", R4(b.X) }, { "y", R4(b.Y) }, { "rotation", R4(b.Rotation) },
				{ "scale", { R4(b.ScaleX), R4(b.ScaleY) } }, { "length", R4(b.Length) },
				{ "world", { { "head", { R4(b.WX), R4(b.WY) } }, { "tail", { R4(b.A * b.Length + b.WX), R4(b.C * b.Length + b.WY) } }, { "rotation", R4(d.WorldRotation(i)) } } } };
		}

		// 본 값 (--x --y --rotation --length --scale --scaleX --scaleY). world = 월드 좌표 · 회전 → 부모 기준
		void ApplyBoneArgs(Ctx& c, int bi, bool setup)
		{
			Document& d = c.D;
			Bone& b = d.Bones[bi];
			float& X = setup ? b.X : b.PX;
			float& Y = setup ? b.Y : b.PY;
			float& Rt = setup ? b.Rotation : b.PR;
			float& SX = setup ? b.ScaleX : b.PSX;
			float& SY = setup ? b.ScaleY : b.PSY;
			const bool world = B(c.A, "world", false);
			if (world) d.UpdateWorld();
			if (c.A.contains("x") || c.A.contains("y"))
			{
				float x = F(c.A, "x", world ? b.WX : X), y = F(c.A, "y", world ? b.WY : Y);
				if (world) d.WorldToParent(bi, x, y, x, y);
				X = x; Y = y;
			}
			if (c.A.contains("rotation"))
			{
				float r = F(c.A, "rotation", 0.0f);
				if (world && b.Parent >= 0) r -= d.WorldRotation(b.Parent);
				Rt = r;
			}
			float sc[2];
			if (Vec(c.A, "scale", 2, sc)) { SX = sc[0]; SY = sc[1]; }
			SX = F(c.A, "scaleX", SX);
			SY = F(c.A, "scaleY", SY);
			if (setup) b.Length = (std::max)(0.0f, F(c.A, "length", b.Length));
		}

		// ---- 그림 만들기 (AI 가 그림 없이 몸 조각을 만들 때): 도형 + 테두리 + 위아래 그라데이션, 가장자리 부드럽게
		bool MakeImage(Ctx& c)
		{
			const std::string path = S(c.A, "path");
			if (path.empty()) { c.E = "need --path <file.png> (Assets/...)"; return false; }
			float size[2] = { 64, 64 };
			Vec(c.A, "size", 2, size);
			const int W = std::clamp((int)size[0], 1, 4096), H = std::clamp((int)size[1], 1, 4096);
			float col[4] = { 1, 1, 1, 1 }, grad[4] = { -1, 0, 0, 1 }, line[4] = { 0.1f, 0.08f, 0.12f, 1 };
			Vec(c.A, "color", 4, col);
			const bool hasGrad = Vec(c.A, "gradient", 4, grad);
			const bool hasLine = Vec(c.A, "outline", 4, line);
			const float lw = hasLine ? F(c.A, "outlineWidth", 3.0f) : 0.0f;
			const std::string shape = S(c.A, "shape", "ellipse");
			const float corner = F(c.A, "radius", (std::min)(W, H) * 0.25f);
			// 도형 안쪽까지 거리 (음수 = 안): 부호 거리 함수
			auto sdf = [&](float x, float y) -> float {
				const float cx = W * 0.5f, cy = H * 0.5f, px = x - cx, py = y - cy;
				if (shape == "rect") { const float dx = fabsf(px) - W * 0.5f, dy = fabsf(py) - H * 0.5f; return (std::max)(dx, dy); }
				if (shape == "roundrect")
				{
					const float rr = (std::min)(corner, (std::min)(W, H) * 0.5f);
					const float qx = fabsf(px) - (W * 0.5f - rr), qy = fabsf(py) - (H * 0.5f - rr);
					return sqrtf((std::max)(qx, 0.0f) * (std::max)(qx, 0.0f) + (std::max)(qy, 0.0f) * (std::max)(qy, 0.0f)) + (std::min)((std::max)(qx, qy), 0.0f) - rr;
				}
				if (shape == "capsule")
				{
					// 세로 캡슐 (W < H) 또는 가로
					const bool vert = H >= W;
					const float r = (vert ? W : H) * 0.5f, half = (vert ? H : W) * 0.5f - r;
					const float a = vert ? py : px, o = vert ? px : py;
					const float t = std::clamp(a, -half, half);
					return sqrtf((a - t) * (a - t) + o * o) - r;
				}
				if (shape == "triangle")
				{
					// 위가 뾰족한 이등변 삼각형
					const float ny = (y - 0.0f) / H;   // 0 위 → 1 아래
					const float halfW = W * 0.5f * ny;
					return (std::max)(fabsf(px) - halfW, (std::max)(-y, y - H)) * 0.7f;
				}
				// ellipse: 정규화한 거리 × 짧은 반지름
				const float rx = W * 0.5f, ry = H * 0.5f;
				const float k = sqrtf((px * px) / (rx * rx) + (py * py) / (ry * ry));
				return (k - 1.0f) * (std::min)(rx, ry);
			};
			DirectX::ScratchImage img;
			if (FAILED(img.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, 1))) { c.E = "cannot make image"; return false; }
			uint8_t* px = img.GetImage(0, 0, 0)->pixels;
			const size_t pitch = img.GetImage(0, 0, 0)->rowPitch;
			for (int y = 0; y < H; ++y)
				for (int x = 0; x < W; ++x)
				{
					const float dist = sdf(x + 0.5f, y + 0.5f);
					const float a = std::clamp(0.5f - dist, 0.0f, 1.0f);   // 1 픽셀 부드러운 가장자리
					float fill[4] = { col[0], col[1], col[2], col[3] };
					if (hasGrad) { const float t = (y + 0.5f) / H; for (int k = 0; k < 3; ++k) fill[k] = col[k] + (grad[k] - col[k]) * t; }
					if (hasLine)
					{
						const float l = std::clamp(dist + lw + 0.5f, 0.0f, 1.0f);   // 가장자리에서 lw 안쪽까지 = 선
						for (int k = 0; k < 3; ++k) fill[k] = fill[k] * (1 - l) + line[k] * l;
					}
					uint8_t* o = px + (size_t)y * pitch + x * 4;
					for (int k = 0; k < 3; ++k) o[k] = (uint8_t)std::clamp((int)(fill[k] * 255.0f + 0.5f), 0, 255);
					o[3] = (uint8_t)std::clamp((int)(a * fill[3] * 255.0f + 0.5f), 0, 255);
				}
			const std::string full = FullPath(path);
			const std::filesystem::path fp = std::filesystem::path(std::u8string(full.begin(), full.end()));
			std::error_code ec;
			if (fp.has_parent_path()) std::filesystem::create_directories(fp.parent_path(), ec);
			if (FAILED(DirectX::SaveToWICFile(*img.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), fp.wstring().c_str())))
			{
				c.E = "cannot write " + full;
				return false;
			}
			c.R = { { "path", path }, { "size", { W, H } }, { "shape", shape } };
			return true;
		}

		// ---- 그리기 크기 · 보기: --size, --frame (보이는 것에 맞춤), --center, --zoom
		bool SetupView(Ctx& c, Raster& r, int& w, int& h, View2D& v)
		{
			float size[2] = { 512, 512 };
			Vec(c.A, "size", 2, size);
			w = std::clamp((int)size[0], 16, 4096);
			h = std::clamp((int)size[1], 16, 4096);
			r.Resize(w, h);
			float mnx, mny, mxx, mxy;
			if (Raster::Bounds(c.D, mnx, mny, mxx, mxy, B(c.A, "bones", false)))
			{
				const float pad = F(c.A, "padding", 0.08f);
				const float bw = (std::max)(1.0f, mxx - mnx), bh = (std::max)(1.0f, mxy - mny);
				v.CX = (mnx + mxx) * 0.5f;
				v.CY = (mny + mxy) * 0.5f;
				v.Zoom = (std::min)(w / (bw * (1 + 2 * pad)), h / (bh * (1 + 2 * pad)));
			}
			float ctr[2];
			if (Vec(c.A, "center", 2, ctr)) { v.CX = ctr[0]; v.CY = ctr[1]; }
			v.Zoom = F(c.A, "zoom", v.Zoom);
			return true;
		}

		RenderOptions RenderOpts(Ctx& c, bool forExport)
		{
			RenderOptions o;
			o.Bones = B(c.A, "bones", !forExport);
			o.Grid = B(c.A, "grid", !forExport);
			const std::string bg = S(c.A, "background", forExport ? "none" : "dark");
			o.Background = bg != "none";
			if (bg.size() == 7 && bg[0] == '#')
			{
				const unsigned v = std::stoul(bg.substr(1), nullptr, 16);
				o.BgColor = 0xFF000000u | ((v >> 16) & 255) | (((v >> 8) & 255) << 8) | ((v & 255) << 16);
			}
			else if (bg == "white") o.BgColor = 0xFFFFFFFF;
			o.SelectedBone = B(c.A, "selection", false) ? c.D.SelectedBone : -1;
			return o;
		}

		// 애니메이션을 고르고 시각을 정한다 (--anim, --time) — 끝나면 원래대로 되돌리는 건 부르는 쪽
		Animation* PickAnim(Ctx& c)
		{
			if (c.A.contains("anim"))
			{
				const int a = c.D.FindAnim(S(c.A, "anim"));
				if (a < 0) { c.E = "no animation '" + S(c.A, "anim") + "'"; return nullptr; }
				return &c.D.Animations[a];
			}
			return c.D.Active();
		}

		bool Render(Ctx& c)
		{
			const std::string path = S(c.A, "path");
			if (path.empty()) { c.E = "need --path <file.png>"; return false; }
			Document& d = c.D;
			const Document::PoseState saved = d.SavePose();
			if (c.A.contains("time") || c.A.contains("anim"))
			{
				Animation* a = PickAnim(c);
				if (!a) { if (c.E.empty()) c.E = "no animation (anim2d anim.new)"; return false; }
				d.ApplyAnimation(*a, F(c.A, "time", d.Time));
			}
			else
				d.Pose();
			Raster r;
			int w, h;
			View2D v;
			SetupView(c, r, w, h, v);
			r.Render(d, v, RenderOpts(c, false));
			d.RestorePose(saved);
			if (!r.SavePng(FullPath(path), c.E)) return false;
			c.R = d.Summary();
			c.R["file"] = path;
			return true;
		}

		// 스프라이트 시트 (+ 프레임 JSON) 또는 PNG 연속 — 애니메이션 전체에서 같은 틀 (모든 프레임 경계의 합)
		bool Export(Ctx& c)
		{
			const std::string path = S(c.A, "path");
			if (path.empty()) { c.E = "need --path <sheet.png | folder>"; return false; }
			Document& d = c.D;
			const Document::PoseState saved = d.SavePose();
			Animation* a = PickAnim(c);
			if (!a) { if (c.E.empty()) c.E = "no animation (anim2d anim.new)"; return false; }
			const float fps = std::clamp(F(c.A, "fps", 12.0f), 1.0f, 120.0f);
			const int frames = std::clamp(I(c.A, "frames", (std::max)(1, (int)roundf(a->Length * fps))), 1, 1024);
			float mnx = FLT_MAX, mny = FLT_MAX, mxx = -FLT_MAX, mxy = -FLT_MAX;
			for (int f = 0; f < frames; ++f)
			{
				d.ApplyAnimation(*a, f / fps);
				float x0, y0, x1, y1;
				if (Raster::Bounds(d, x0, y0, x1, y1, false)) { mnx = (std::min)(mnx, x0); mny = (std::min)(mny, y0); mxx = (std::max)(mxx, x1); mxy = (std::max)(mxy, y1); }
			}
			if (mnx > mxx) { c.E = "nothing visible (add images: anim2d image.add)"; d.RestorePose(saved); return false; }
			float cell[2] = { 256, 256 };
			const bool cellGiven = Vec(c.A, "size", 2, cell);
			const float pad = 4.0f;
			if (!cellGiven) { cell[0] = ceilf(mxx - mnx + pad * 2); cell[1] = ceilf(mxy - mny + pad * 2); }
			const int cw = std::clamp((int)cell[0], 8, 2048), ch = std::clamp((int)cell[1], 8, 2048);
			View2D v;
			v.CX = (mnx + mxx) * 0.5f;
			v.CY = (mny + mxy) * 0.5f;
			v.Zoom = (std::min)(cw / (mxx - mnx + pad * 2), ch / (mxy - mny + pad * 2));
			RenderOptions opt = RenderOpts(c, true);
			const bool sequence = B(c.A, "sequence", false);
			const int cols = sequence ? 1 : std::clamp(I(c.A, "columns", (int)ceilf(sqrtf((float)frames))), 1, frames);
			const int rows = sequence ? 1 : (frames + cols - 1) / cols;
			Raster cellR;
			cellR.Resize(cw, ch);
			std::vector<uint32> sheet;
			if (!sequence) sheet.assign((size_t)cols * cw * rows * ch, 0);
			json frameList = json::array();
			std::vector<std::string> files;
			for (int f = 0; f < frames; ++f)
			{
				d.ApplyAnimation(*a, f / fps);
				cellR.Render(d, v, opt);
				if (sequence)
				{
					char name[32];
					snprintf(name, sizeof(name), "%s_%03d.png", a->Name.c_str(), f);
					const std::string file = FullPath(path) + "/" + name;
					if (!cellR.SavePng(file, c.E)) { d.RestorePose(saved); return false; }
					files.push_back(name);
					continue;
				}
				const int ox = (f % cols) * cw, oy = (f / cols) * ch;
				for (int y = 0; y < ch; ++y) memcpy(&sheet[(size_t)(oy + y) * cols * cw + ox], &cellR.Color[(size_t)y * cw], (size_t)cw * 4);
				frameList.push_back({ { "x", ox }, { "y", oy }, { "w", cw }, { "h", ch } });
			}
			d.RestorePose(saved);
			// 원점 (본 루트 0,0) 이 틀 안 어디인지 — 엔진 · 다른 툴에서 발 위치를 맞출 때
			const float pivotX = (0.0f - (v.CX - cw * 0.5f / v.Zoom)) * v.Zoom, pivotY = ch - (0.0f - (v.CY - ch * 0.5f / v.Zoom)) * v.Zoom;
			c.R = { { "animation", a->Name }, { "frames", frames }, { "fps", fps }, { "cell", { cw, ch } }, { "pivot", { R4(pivotX), R4(pivotY) } } };
			if (sequence) { c.R["folder"] = path; c.R["files"] = files; return true; }
			Raster sheetR;
			sheetR.Width = cols * cw;
			sheetR.Height = rows * ch;
			sheetR.Color = std::move(sheet);
			if (!sheetR.SavePng(FullPath(path), c.E)) return false;
			// 옆에 프레임 JSON (Unity 의 Sprite Editor · TexturePacker 처럼 칸마다 사각형)
			const std::string full = FullPath(path);
			std::filesystem::path jp = std::filesystem::path(std::u8string(full.begin(), full.end()));
			const std::u8string fileName = jp.filename().u8string();
			jp.replace_extension(".json");
			std::ofstream jf(jp, std::ios::binary);
			jf << json({ { "image", std::string(fileName.begin(), fileName.end()) }, { "animation", a->Name }, { "fps", fps }, { "loop", a->Loop },
				{ "cell", { cw, ch } }, { "columns", cols }, { "rows", rows }, { "pivot", { R4(pivotX), R4(pivotY) } }, { "frames", frameList } }).dump(2);
			c.R["sheet"] = path;
			c.R["size"] = { cols * cw, rows * ch };
			c.R["columns"] = cols;
			return true;
		}

		bool Batch(Ctx& c);

		std::vector<Entry>& Registry()
		{
			static std::vector<Entry> ops;
			if (!ops.empty()) return ops;
			auto add = [&](const char* n, const char* h, bool m, OpFn fn) { ops.push_back({ { n, h, m }, std::move(fn) }); };

			// ================= 문서
			add("info", "summary: bones, slots, animations, mode, time, selection", false, [](Ctx& c) { c.D.Pose(); c.R = c.D.Summary(); return true; });
			add("new", "[--name skeleton]: empty skeleton (one root bone)", false, [](Ctx& c) { c.D.New(); c.D.Name = S(c.A, "name", "skeleton"); c.R = c.D.Summary(); return true; });
			add("open", "--path <file.skel2d>", false, [](Ctx& c) { if (!c.D.Load(FullPath(S(c.A, "path")), c.E)) return false; c.R = c.D.Summary(); return true; });
			add("save", "[--path <file.skel2d>] (default: last path)", false, [](Ctx& c) {
				const std::string p = c.A.contains("path") ? FullPath(S(c.A, "path")) : c.D.Path;
				if (p.empty()) { c.E = "need --path <file.skel2d>"; return false; }
				if (!c.D.Save(p, c.E)) return false;
				c.R = c.D.Summary(); return true; });
			add("undo", "undo the last change", false, [](Ctx& c) { if (!c.D.Undo()) { c.E = "nothing to undo"; return false; } c.R = c.D.Summary(); return true; });
			add("redo", "redo", false, [](Ctx& c) { if (!c.D.Redo()) { c.E = "nothing to redo"; return false; } c.R = c.D.Summary(); return true; });
			add("mode", "--mode setup|animate: setup = edit the rest pose, animate = pose at the current time and key it", false, [](Ctx& c) {
				const std::string m = S(c.A, "mode", "setup");
				if (m == "animate" && !c.D.Active()) { c.E = "no animation (anim2d anim.new --name idle)"; return false; }
				c.D.AnimateMode = m == "animate";
				c.D.Repose(); c.D.Changed(); c.R = c.D.Summary(); return true; });
			add("select", "--bone <name> | --slot <name> | --none", false, [](Ctx& c) {
				if (B(c.A, "none", false)) { c.D.SelectedBone = c.D.SelectedSlot = -1; }
				if (c.A.contains("bone")) { c.D.SelectedSlot = -1; c.D.SelectedBone = c.D.FindBone(S(c.A, "bone")); if (c.D.SelectedBone < 0) { c.E = "no bone '" + S(c.A, "bone") + "'"; return false; } }
				if (c.A.contains("slot")) { c.D.SelectedSlot = c.D.FindSlot(S(c.A, "slot")); if (c.D.SelectedSlot < 0) { c.E = "no slot '" + S(c.A, "slot") + "'"; return false; } c.D.SelectedBone = c.D.Slots[c.D.SelectedSlot].Bone; }
				c.D.Changed(); c.R = c.D.Summary(); return true; });
			add("render", "--path <file.png> [--size 512 | w,h] [--anim <name>] [--time <s>] [--bones true] [--grid true] [--background dark|white|none|#rrggbb] [--center x,y] [--zoom] [--padding 0.08]: PNG of the skeleton (framed to what is visible)", false, Render);
			add("export", "--path <sheet.png | folder> [--anim <name>] [--fps 12] [--frames n] [--size w,h (cell)] [--columns] [--sequence] [--background none]: sprite sheet + .json (frame rects, fps, pivot) or a PNG sequence", false, Export);
			add("image.make", "--path <Assets/....png> [--shape ellipse|rect|roundrect|capsule|triangle] [--size w,h] [--color r,g,b,a (0..1)] [--gradient r,g,b (bottom)] [--outline r,g,b] [--outlineWidth 3] [--radius corner]: make a part image (soft edges) - for skeletons without art", false, MakeImage);

			// ================= 본
			add("bone.add", "--name <bone> [--parent <bone> (default: selected, else root)] [--x --y (parent space; --world for world)] [--rotation deg] [--length px] [--scale s|sx,sy]", true, [](Ctx& c) {
				Document& d = c.D;
				Bone b;
				b.Name = d.UniqueBoneName(S(c.A, "name", "bone"));
				const std::string p = S(c.A, "parent");
				b.Parent = !p.empty() ? d.FindBone(p) : (d.SelectedBone >= 0 ? d.SelectedBone : (d.Bones.empty() ? -1 : 0));
				if (!p.empty() && b.Parent < 0) { c.E = "no bone '" + p + "'"; return false; }
				d.Bones.push_back(b);
				const int bi = (int)d.Bones.size() - 1;
				d.ResetPose();
				ApplyBoneArgs(c, bi, true);
				d.SelectedBone = bi;
				d.SelectedSlot = -1;
				d.SortBones();
				Done(c, 1, "bones");
				c.R["bone"] = BoneJson(d, d.FindBone(b.Name));
				return true; });
			add("bone.set", "--name <bone> [--x --y --rotation --length --scale --scaleX --scaleY] [--world] [--parent <bone>] [--rename <new>]: setup pose of a bone", true, [](Ctx& c) {
				Document& d = c.D;
				const int bi = BoneArg(c, "name");
				if (bi < 0) return false;
				d.ResetPose();
				if (c.A.contains("parent"))
				{
					const std::string p = S(c.A, "parent");
					const int pi = p.empty() ? -1 : d.FindBone(p);
					if (!p.empty() && pi < 0) { c.E = "no bone '" + p + "'"; return false; }
					for (int k = pi; k >= 0; k = d.Bones[k].Parent) if (k == bi) { c.E = "parent would make a loop"; return false; }
					d.Bones[bi].Parent = pi;
				}
				ApplyBoneArgs(c, bi, true);
				if (c.A.contains("rename"))
				{
					const std::string nn = S(c.A, "rename");
					if (nn.empty() || d.FindBone(nn) >= 0) { c.E = "bad or taken name '" + nn + "'"; return false; }
					for (Animation& a : d.Animations) { auto it = a.Bones.find(d.Bones[bi].Name); if (it != a.Bones.end()) { a.Bones[nn] = it->second; a.Bones.erase(d.Bones[bi].Name); } }
					d.Bones[bi].Name = nn;
				}
				const std::string name = d.Bones[bi].Name;
				d.SortBones();
				Done(c, 1, "bones");
				c.R["bone"] = BoneJson(d, d.FindBone(name));
				return true; });
			add("bone.delete", "--name <bone>: children go to its parent, its slots too", true, [](Ctx& c) {
				const int bi = BoneArg(c, "name");
				if (bi < 0) return false;
				if (c.D.Bones[bi].Parent < 0 && c.D.Bones.size() > 1) { c.E = "cannot delete the root bone"; return false; }
				c.D.DeleteBone(bi);
				return Done(c, 1, "deleted"); });
			add("bone.list", "bones: name, parent, setup (x, y, rotation, scale, length), world head / tail / rotation", false, [](Ctx& c) {
				c.D.Pose();
				json list = json::array();
				for (int i = 0; i < (int)c.D.Bones.size(); ++i) list.push_back(BoneJson(c.D, i));
				c.R = { { "bones", list } };
				return true; });

			// ================= 슬롯 · 그림
			add("image.add", "--bone <bone> --image <png> [--name <slot>] [--attachment <name>] [--x --y (bone space)] [--rotation] [--scale] [--width --height] [--order front|back|<index>]: new slot with this image on the bone", true, [](Ctx& c) {
				Document& d = c.D;
				const int bi = BoneArg(c);
				if (bi < 0) return false;
				const std::string image = S(c.A, "image");
				if (image.empty()) { c.E = "need --image <file.png>"; return false; }
				if (!GetImage(image)) { c.E = "cannot read image " + image; return false; }
				Slot s;
				const std::string stem = std::filesystem::path(std::u8string(image.begin(), image.end())).stem().string();
				s.Name = d.UniqueSlotName(S(c.A, "name", stem));
				s.Bone = bi;
				Attachment a;
				a.Name = S(c.A, "attachment", stem);
				a.Image = image;
				a.X = F(c.A, "x", 0.0f); a.Y = F(c.A, "y", 0.0f); a.Rotation = F(c.A, "rotation", 0.0f);
				float sc[2] = { 1, 1 };
				Vec(c.A, "scale", 2, sc);
				a.ScaleX = sc[0]; a.ScaleY = sc[1];
				a.Width = F(c.A, "width", 0.0f); a.Height = F(c.A, "height", 0.0f);
				s.Attachments.push_back(a);
				s.SetupAttachment = a.Name;
				const std::string order = S(c.A, "order", "front");
				int at = (int)d.Slots.size();
				if (order == "back") at = 0;
				else if (c.A.contains("order") && c.A["order"].is_number()) at = std::clamp(I(c.A, "order", at), 0, (int)d.Slots.size());
				d.Slots.insert(d.Slots.begin() + at, s);
				d.SelectedSlot = at;
				d.SelectedBone = bi;
				Done(c, 1, "slots");
				c.R["slot"] = s.Name;
				return true; });
			add("attachment.add", "--slot <slot> --name <attachment> --image <png> [--x --y --rotation --scale --width --height]: another image for the slot (swap with slot.set --attachment or pose --slot --attachment)", true, [](Ctx& c) {
				Document& d = c.D;
				const int si = d.FindSlot(S(c.A, "slot"));
				if (si < 0) { c.E = "no slot '" + S(c.A, "slot") + "'"; return false; }
				Attachment a;
				a.Name = S(c.A, "name");
				a.Image = S(c.A, "image");
				if (a.Name.empty() || a.Image.empty()) { c.E = "need --name and --image"; return false; }
				if (d.Slots[si].Find(a.Name)) { c.E = "attachment '" + a.Name + "' exists in " + d.Slots[si].Name; return false; }
				if (!GetImage(a.Image)) { c.E = "cannot read image " + a.Image; return false; }
				// 기본 위치 = 셋업 첨부와 같게
				if (const Attachment* base = d.Slots[si].Find(d.Slots[si].SetupAttachment)) { a.X = base->X; a.Y = base->Y; a.Rotation = base->Rotation; a.ScaleX = base->ScaleX; a.ScaleY = base->ScaleY; }
				a.X = F(c.A, "x", a.X); a.Y = F(c.A, "y", a.Y); a.Rotation = F(c.A, "rotation", a.Rotation);
				float sc[2] = { a.ScaleX, a.ScaleY };
				Vec(c.A, "scale", 2, sc);
				a.ScaleX = sc[0]; a.ScaleY = sc[1];
				a.Width = F(c.A, "width", 0.0f); a.Height = F(c.A, "height", 0.0f);
				d.Slots[si].Attachments.push_back(a);
				return Done(c, 1, "attachments"); });
			add("attachment.set", "--slot <slot> [--name <attachment> (default: setup one)] [--x --y --rotation --scale --width --height] [--image <png>]: move / resize an image on its bone", true, [](Ctx& c) {
				Document& d = c.D;
				const int si = d.FindSlot(S(c.A, "slot"));
				if (si < 0) { c.E = "no slot '" + S(c.A, "slot") + "'"; return false; }
				Slot& s = d.Slots[si];
				const std::string n = S(c.A, "name", s.SetupAttachment);
				Attachment* a = nullptr;
				for (Attachment& x : s.Attachments) if (x.Name == n) a = &x;
				if (!a) { c.E = "no attachment '" + n + "' in " + s.Name; return false; }
				a->X = F(c.A, "x", a->X); a->Y = F(c.A, "y", a->Y); a->Rotation = F(c.A, "rotation", a->Rotation);
				float sc[2] = { a->ScaleX, a->ScaleY };
				Vec(c.A, "scale", 2, sc);
				a->ScaleX = sc[0]; a->ScaleY = sc[1];
				a->Width = F(c.A, "width", a->Width); a->Height = F(c.A, "height", a->Height);
				if (c.A.contains("image")) { if (!GetImage(S(c.A, "image"))) { c.E = "cannot read image " + S(c.A, "image"); return false; } a->Image = S(c.A, "image"); }
				return Done(c, 1, "attachments"); });
			add("slot.set", "--name <slot> [--attachment <name> | \"\" (hide)] [--color r,g,b,a] [--bone <bone>] [--order front|back|forward|backward|<index>] [--rename]: setup of a slot (draw order: later = in front)", true, [](Ctx& c) {
				Document& d = c.D;
				int si = d.FindSlot(S(c.A, "name"));
				if (si < 0) { c.E = "no slot '" + S(c.A, "name") + "'"; return false; }
				Slot& s = d.Slots[si];
				if (c.A.contains("attachment"))
				{
					const std::string a = S(c.A, "attachment");
					if (!a.empty() && !s.Find(a)) { c.E = "no attachment '" + a + "' in " + s.Name; return false; }
					s.SetupAttachment = a;
				}
				Vec(c.A, "color", 4, s.Color);
				if (c.A.contains("bone")) { const int b = d.FindBone(S(c.A, "bone")); if (b < 0) { c.E = "no bone '" + S(c.A, "bone") + "'"; return false; } s.Bone = b; }
				if (c.A.contains("rename")) { const std::string nn = S(c.A, "rename"); if (nn.empty() || d.FindSlot(nn) >= 0) { c.E = "bad or taken name"; return false; } for (Animation& a : d.Animations) { auto it = a.Slots.find(s.Name); if (it != a.Slots.end()) { a.Slots[nn] = it->second; a.Slots.erase(s.Name); } } s.Name = nn; }
				if (c.A.contains("order"))
				{
					Slot moved = s;
					d.Slots.erase(d.Slots.begin() + si);
					int at = si;
					const std::string o = S(c.A, "order");
					if (o == "front") at = (int)d.Slots.size();
					else if (o == "back") at = 0;
					else if (o == "forward") at = (std::min)(si + 1, (int)d.Slots.size());
					else if (o == "backward") at = (std::max)(si - 1, 0);
					else if (c.A["order"].is_number()) at = std::clamp(I(c.A, "order", si), 0, (int)d.Slots.size());
					d.Slots.insert(d.Slots.begin() + at, moved);
					si = at;
				}
				d.SelectedSlot = si;
				return Done(c, 1, "slots"); });
			add("slot.delete", "--name <slot>", true, [](Ctx& c) {
				const int si = c.D.FindSlot(S(c.A, "name"));
				if (si < 0) { c.E = "no slot '" + S(c.A, "name") + "'"; return false; }
				for (Animation& a : c.D.Animations) a.Slots.erase(c.D.Slots[si].Name);
				c.D.Slots.erase(c.D.Slots.begin() + si);
				c.D.SelectedSlot = -1;
				return Done(c, 1, "deleted"); });
			add("slot.list", "slots in draw order (back → front): name, bone, attachment (setup), current (pose now), attachments, color", false, [](Ctx& c) {
				json list = json::array();
				for (const Slot& s : c.D.Slots)
				{
					json at = json::array();
					for (const Attachment& a : s.Attachments) at.push_back({ { "name", a.Name }, { "image", a.Image }, { "x", R4(a.X) }, { "y", R4(a.Y) }, { "rotation", R4(a.Rotation) } });
					list.push_back({ { "name", s.Name }, { "bone", c.D.Bones[s.Bone].Name }, { "attachment", s.SetupAttachment }, { "current", s.Current }, { "attachments", at }, { "color", { s.Color[0], s.Color[1], s.Color[2], s.Color[3] } } });
				}
				c.R = { { "slots", list } };
				return true; });

			// ================= 애니메이션: anim.new → (pose …) → anim.key --time t → 반복
			auto animList = [](Document& d) {
				json l = json::array();
				for (const Animation& a : d.Animations) l.push_back({ { "name", a.Name }, { "length", a.Length }, { "loop", a.Loop }, { "keys", a.KeyCount() } });
				return l;
			};
			add("anim.new", "--name <anim> [--length 1] [--loop true]: new animation (active, animate mode, time 0)", true, [animList](Ctx& c) {
				Document& d = c.D;
				const std::string n = S(c.A, "name");
				if (n.empty()) { c.E = "need --name"; return false; }
				if (d.FindAnim(n) >= 0) { c.E = "animation '" + n + "' exists (anim2d anim.select)"; return false; }
				Animation a;
				a.Name = n;
				a.Length = (std::max)(0.05f, F(c.A, "length", 1.0f));
				a.Loop = B(c.A, "loop", true);
				d.Animations.push_back(a);
				d.ActiveAnim = (int)d.Animations.size() - 1;
				d.AnimateMode = true;
				d.Time = 0.0f;
				d.Posed = false;
				Done(c, 1, "animations");
				c.R["animationsList"] = animList(d);
				return true; });
			add("anim.select", "--name <anim>: active animation (animate mode)", true, [](Ctx& c) {
				const int a = c.D.FindAnim(S(c.A, "name"));
				if (a < 0) { c.E = "no animation '" + S(c.A, "name") + "'"; return false; }
				c.D.ActiveAnim = a;
				c.D.AnimateMode = true;
				c.D.Time = std::clamp(c.D.Time, 0.0f, c.D.Animations[a].Length);
				c.D.Posed = false;
				return Done(c, 1, "animations"); });
			add("anim.set", "[--name <anim> (default: active)] [--length] [--loop] [--rename]", true, [](Ctx& c) {
				const int ai = c.A.contains("name") ? c.D.FindAnim(S(c.A, "name")) : c.D.ActiveAnim;
				if (ai < 0) { c.E = "no animation"; return false; }
				Animation& a = c.D.Animations[ai];
				a.Length = (std::max)(0.05f, F(c.A, "length", a.Length));
				a.Loop = B(c.A, "loop", a.Loop);
				if (c.A.contains("rename")) a.Name = S(c.A, "rename");
				return Done(c, 1, "animations"); });
			add("anim.delete", "--name <anim>", true, [](Ctx& c) {
				const int a = c.D.FindAnim(S(c.A, "name"));
				if (a < 0) { c.E = "no animation '" + S(c.A, "name") + "'"; return false; }
				c.D.Animations.erase(c.D.Animations.begin() + a);
				c.D.ActiveAnim = c.D.Animations.empty() ? -1 : 0;
				if (c.D.ActiveAnim < 0) c.D.AnimateMode = false;
				return Done(c, 1, "deleted"); });
			add("anim.list", "animations: name, length, loop, key count", false, [animList](Ctx& c) {
				c.R = { { "animations", animList(c.D) }, { "active", c.D.Active() ? c.D.Active()->Name : "" }, { "time", c.D.Time } };
				return true; });
			add("anim.time", "--time <s>: go to this time (pose = the keys there)", false, [](Ctx& c) {
				Animation* a = c.D.Active();
				if (!a) { c.E = "no animation"; return false; }
				c.D.AnimateMode = true;
				c.D.Time = std::clamp(F(c.A, "time", 0.0f), 0.0f, a->Length);
				c.D.Repose();
				c.D.Changed();
				c.R = c.D.Summary();
				return true; });
			add("pose", "--bone <bone> [--rotation deg] [--x --y] [--scale] [--world] | --slot <slot> [--attachment <name>|\"\"] [--color r,g,b,a]: change the pose at the current time (animate mode) - then anim.key", false, [](Ctx& c) {
				Document& d = c.D;
				if (!d.Active()) { c.E = "no animation (anim2d anim.new --name idle)"; return false; }
				if (!d.AnimateMode) { d.AnimateMode = true; d.Repose(); }
				int n = 0;
				if (c.A.contains("bone"))
				{
					const int bi = BoneArg(c);
					if (bi < 0) return false;
					ApplyBoneArgs(c, bi, false);
					++n;
				}
				if (c.A.contains("slot"))
				{
					const int si = d.FindSlot(S(c.A, "slot"));
					if (si < 0) { c.E = "no slot '" + S(c.A, "slot") + "'"; return false; }
					Slot& s = d.Slots[si];
					if (c.A.contains("attachment")) { const std::string a = S(c.A, "attachment"); if (!a.empty() && !s.Find(a)) { c.E = "no attachment '" + a + "'"; return false; } s.Current = a; }
					Vec(c.A, "color", 4, s.PColor);
					++n;
				}
				d.Posed = true;
				d.UpdateWorld();
				d.Changed();
				c.R = d.Summary();
				c.R["changed"] = { { "posed", n } };
				return true; });
			add("anim.key", "[--time <s> (default: current)] [--bones a,b (default: all bones + changed slots)] [--curve linear|stepped|smooth] [--slots true]: key the current pose", true, [](Ctx& c) {
				Document& d = c.D;
				Animation* a = d.Active();
				if (!a) { c.E = "no animation (anim2d anim.new --name idle)"; return false; }
				const float t = std::clamp(F(c.A, "time", d.Time), 0.0f, a->Length);
				const std::string cv = S(c.A, "curve", "linear");
				const int n = d.KeyPose(*a, t, List(c.A, "bones"), cv == "stepped" ? Curve::Stepped : (cv == "smooth" ? Curve::Smooth : Curve::Linear), B(c.A, "slots", true));
				// 키를 찍고 나면 그 시각의 자세 = 키 (Spine 처럼)
				if (fabsf(d.Time - t) > 1e-4f) { d.Time = t; d.Repose(); }
				d.Posed = false;
				c.R = d.Summary();
				c.R["changed"] = { { "keys", n } };
				d.Changed();
				return true; });
			add("anim.unkey", "--time <s> [--bones a,b]: delete keys at this time", true, [](Ctx& c) {
				Document& d = c.D;
				Animation* a = d.Active();
				if (!a) { c.E = "no animation"; return false; }
				const float t = F(c.A, "time", d.Time);
				const std::vector<std::string> only = List(c.A, "bones");
				int n = 0;
				auto drop = [&](std::vector<Key>& k) { const size_t b = k.size(); k.erase(std::remove_if(k.begin(), k.end(), [&](const Key& x) { return fabsf(x.Time - t) < 1e-3f; }), k.end()); n += (int)(b - k.size()); };
				for (auto& [name, tl] : a->Bones) if (only.empty() || std::find(only.begin(), only.end(), name) != only.end()) { drop(tl.Rotate); drop(tl.Translate); drop(tl.Scale); }
				for (auto& [name, tl] : a->Slots)
					if (only.empty() || std::find(only.begin(), only.end(), name) != only.end())
					{
						drop(tl.Color);
						const size_t b = tl.Attach.size();
						tl.Attach.erase(std::remove_if(tl.Attach.begin(), tl.Attach.end(), [&](const auto& x) { return fabsf(x.first - t) < 1e-3f; }), tl.Attach.end());
						n += (int)(b - tl.Attach.size());
					}
				d.Posed = false;
				return Done(c, n, "keys"); });

			add("batch", "--steps [{\"op\":...}, ...] [--atomic true]: run ops as ONE undo step (CLI: nova anim2d batch <file>, one op per line)", false, Batch);
			return ops;
		}

		bool Batch(Ctx& c)
		{
			if (!c.A.contains("steps") || !c.A["steps"].is_array()) { c.E = "need --steps [...]"; return false; }
			const bool atomic = B(c.A, "atomic", true);
			c.D.PushUndo("batch");
			json results = json::array();
			int i = 0;
			for (const json& step : c.A["steps"])
			{
				++i;
				const std::string op = step.value("op", std::string());
				json args = step;
				args.erase("op");
				json r;
				std::string err;
				if (op == "batch" || op == "undo" || op == "redo" || op == "new" || op == "open") err = "not allowed inside batch";
				if (!err.empty() || !RunOpNoUndo(op, args, r, err))
				{
					c.E = "step " + std::to_string(i) + " (" + op + "): " + err + (atomic ? "  [all steps rolled back]" : "");
					if (atomic) c.D.CancelUndo();
					return false;
				}
				json brief = { { "op", op } };
				for (const char* k : { "changed", "file", "sheet", "slot", "path" }) if (r.contains(k)) brief[k] = r[k];
				results.push_back(brief);
			}
			c.R = c.D.Summary();
			c.R["steps"] = results;
			c.D.Changed();
			return true;
		}
	}

	const std::vector<OpInfo>& Ops()
	{
		static std::vector<OpInfo> infos;
		if (infos.empty()) for (const Entry& e : Registry()) infos.push_back(e.Info);
		return infos;
	}

	bool RunOpNoUndo(const std::string& name, const json& args, json& result, std::string& error)
	{
		for (Entry& e : Registry())
			if (e.Info.Name == name)
			{
				Ctx c{ Doc(), args, result, error };
				try { return e.Fn(c); }
				catch (const std::exception& ex) { error = std::string("bad arguments: ") + ex.what(); return false; }
			}
		error = "unknown anim2d op '" + name + "' (nova anim2d help)";
		return false;
	}

	bool RunOp(const std::string& name, const json& args, json& result, std::string& error)
	{
		for (Entry& e : Registry())
			if (e.Info.Name == name)
			{
				Document& d = Doc();
				if (e.Info.Mutates) d.PushUndo(name);
				Ctx c{ d, args, result, error };
				bool ok = false;
				try { ok = e.Fn(c); }
				catch (const std::exception& ex) { error = std::string("bad arguments: ") + ex.what(); ok = false; }
				if (!ok && e.Info.Mutates) d.CancelUndo();
				return ok;
			}
		error = "unknown anim2d op '" + name + "' (nova anim2d help)";
		return false;
	}
}
