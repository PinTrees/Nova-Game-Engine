#include "pch.h"
#include "ShaderGraph.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <map>

namespace fs = std::filesystem;

namespace ShaderGraph
{
	// ------------------------------------------------------------------ 도우미
	std::string TypeName(int width)
	{
		switch (width)
		{
		case 1: return "float";
		case 2: return "float2";
		case 3: return "float3";
		default: return "float4";
		}
	}

	std::string Cast(const std::string& expr, int from, int to)
	{
		if (from == to || to <= 0)
			return expr;
		if (from == 1)
			return "((" + TypeName(to) + ")(" + expr + "))";   // 모든 칸에
		if (from > to)
		{
			static const char* swz[] = { "", ".x", ".xy", ".xyz" };
			return "(" + expr + ")" + swz[to];
		}
		// 작은 벡터 → 큰 벡터: 나머지 칸 0
		std::string pad;
		for (int i = from; i < to; ++i) pad += ", 0";
		return TypeName(to) + "(" + expr + pad + ")";
	}

	std::string Sanitize(const std::string& s)
	{
		std::string out;
		for (char c : s)
			out += (isalnum((unsigned char)c) || c == '_') ? c : '_';
		if (out.empty() || isdigit((unsigned char)out[0]))
			out = "_" + out;
		return out;
	}

	namespace
	{
		std::string Num(float v)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%.7g", v);
			std::string s = buf;
			if (s.find_first_of(".eE") == std::string::npos && s != "inf" && s != "-inf")
				s += ".0";
			return s;
		}

		std::string Literal(const float* v, int width)
		{
			if (width <= 1)
				return Num(v[0]);
			std::string s = TypeName(width) + "(";
			for (int i = 0; i < width; ++i)
				s += (i ? ", " : "") + Num(v[i]);
			return s + ")";
		}

		int BindWidth(const std::string& bind)
		{
			if (bind == "uv") return 2;
			if (bind == "posW" || bind == "normalW" || bind == "viewW") return 3;
			if (bind == "screen") return 4;
			return 1;
		}

		std::string BindExpr(const std::string& bind)
		{
			if (bind == "uv") return "sg_uv";
			if (bind == "posW") return "sg_posW";
			if (bind == "normalW") return "sg_normalW";
			if (bind == "viewW") return "sg_viewW";
			if (bind == "screen") return "sg_screen";
			return "0.0";
		}

		PortDef P(const char* name, int width, float a = 0, float b = 0, float c = 0, float d = 0, const char* bind = "")
		{
			PortDef p;
			p.Name = name;
			p.Width = width;
			p.Default[0] = a; p.Default[1] = b; p.Default[2] = c; p.Default[3] = d;
			p.Bind = bind;
			return p;
		}

		// ---- 노드 정의 모음
		std::vector<NodeDef> BuildDefs()
		{
			std::vector<NodeDef> d;
			auto add = [&](const char* type, const char* cat, std::vector<PortDef> in, std::vector<PortDef> out, std::function<void(NodeContext&)> gen, const char* help = "", json options = json::object()) {
				NodeDef nd;
				nd.Type = type; nd.Category = cat; nd.In = std::move(in); nd.Out = std::move(out); nd.Gen = std::move(gen); nd.Help = help; nd.DefaultOptions = options;
				d.push_back(std::move(nd));
			};
			const int D = kDynamic;
			// 두 입력 → 같은 폭 (동적)
			auto binary = [&](const char* type, const char* cat, const char* op, float bDefault = 0.0f, bool func = false) {
				add(type, cat, { P("A", D, 0), P("B", D, bDefault, bDefault, bDefault, bDefault) }, { P("Out", D) }, [op, func](NodeContext& c) {
					const int w = c.DynamicWidth({ "A", "B" });
					if (func) c.Out("Out", w, std::string(op) + "(" + c.In("A", w) + ", " + c.In("B", w) + ")");
					else c.Out("Out", w, "(" + c.In("A", w) + " " + op + " " + c.In("B", w) + ")");
				});
			};
			auto unary = [&](const char* type, const char* cat, const std::string& pre, const std::string& post, float inDefault = 0.0f) {
				add(type, cat, { P("In", D, inDefault) }, { P("Out", D) }, [pre, post](NodeContext& c) {
					const int w = c.DynamicWidth({ "In" });
					c.Out("Out", w, pre + c.In("In", w) + post);
				});
			};

			// ---- Input / Basic
			add("Float", "Input/Basic", { P("X", 1, 0) }, { P("Out", 1) }, [](NodeContext& c) { c.Out("Out", 1, c.In("X", 1)); }, "a number");
			add("Vector2", "Input/Basic", { P("X", 1), P("Y", 1) }, { P("Out", 2) }, [](NodeContext& c) { c.Out("Out", 2, "float2(" + c.In("X", 1) + ", " + c.In("Y", 1) + ")"); });
			add("Vector3", "Input/Basic", { P("X", 1), P("Y", 1), P("Z", 1) }, { P("Out", 3) }, [](NodeContext& c) { c.Out("Out", 3, "float3(" + c.In("X", 1) + ", " + c.In("Y", 1) + ", " + c.In("Z", 1) + ")"); });
			add("Vector4", "Input/Basic", { P("X", 1), P("Y", 1), P("Z", 1), P("W", 1) }, { P("Out", 4) }, [](NodeContext& c) { c.Out("Out", 4, "float4(" + c.In("X", 1) + ", " + c.In("Y", 1) + ", " + c.In("Z", 1) + ", " + c.In("W", 1) + ")"); });
			add("Color", "Input/Basic", {}, { P("Out", 4) }, [](NodeContext& c) {
				float v[4] = { 1, 1, 1, 1 };
				const json o = c.Option("color");
				if (o.is_array()) for (int i = 0; i < 4 && i < (int)o.size(); ++i) v[i] = o[i].get<float>();
				c.Out("Out", 4, Literal(v, 4));
			}, "a constant color (option color: [r,g,b,a])", { { "color", { 1, 1, 1, 1 } } });
			add("Time", "Input/Basic", {}, { P("Time", 1), P("Sine Time", 1), P("Cosine Time", 1), P("Delta Time", 1) }, [](NodeContext& c) {
				c.Out("Time", 1, "gSGTime.x");
				c.Out("Sine Time", 1, "gSGTime.y");
				c.Out("Cosine Time", 1, "gSGTime.z");
				c.Out("Delta Time", 1, "gSGTime.w");
			}, "seconds since the editor / game started");
			// ---- Input / Geometry
			add("UV", "Input/Geometry", {}, { P("Out", 2) }, [](NodeContext& c) { c.Out("Out", 2, "sg_uv"); }, "mesh UV0");
			add("Position", "Input/Geometry", {}, { P("Out", 3) }, [](NodeContext& c) { c.Out("Out", 3, "sg_posW"); }, "world position");
			add("Normal Vector", "Input/Geometry", {}, { P("Out", 3) }, [](NodeContext& c) { c.Out("Out", 3, "sg_normalW"); }, "world normal");
			add("View Direction", "Input/Geometry", {}, { P("Out", 3) }, [](NodeContext& c) { c.Out("Out", 3, "sg_viewW"); }, "world direction to the camera (normalized)");
			add("Screen Position", "Input/Geometry", {}, { P("Out", 4) }, [](NodeContext& c) { c.Out("Out", 4, "sg_screen"); }, "0..1 screen UV in xy");
			// ---- Input / Texture
			add("Sample Texture 2D", "Input/Texture", { P("Texture", kTexture), P("UV", 2, 0, 0, 0, 0, "uv") },
				{ P("RGBA", 4), P("R", 1), P("G", 1), P("B", 1), P("A", 1) }, [](NodeContext& c) {
				const std::string tex = c.TextureIn("Texture");
				const std::string rgba = c.OutVar(*c.N, "RGBA");
				if (tex.empty())
					*c.Code += "    float4 " + rgba + " = float4(1, 1, 1, 1);\n";
				else
				{
					*c.Code += "    float4 " + rgba + " = " + tex + ".Sample(samSG, " + c.In("UV", 2) + ");\n";
					if (c.Option("type").is_string() && c.Option("type").get<std::string>() == "Normal")
						*c.Code += "    " + rgba + ".rgb = normalize(" + rgba + ".rgb * 2.0 - 1.0);\n";
				}
				c.Out("R", 1, rgba + ".r");
				c.Out("G", 1, rgba + ".g");
				c.Out("B", 1, rgba + ".b");
				c.Out("A", 1, rgba + ".a");
			}, "Texture = a Texture2D property or option texture (path); option type: Default | Normal (unpacked tangent normal)", { { "type", "Default" }, { "texture", "" } });
			// ---- Math / Basic
			binary("Add", "Math/Basic", "+");
			binary("Subtract", "Math/Basic", "-");
			binary("Multiply", "Math/Basic", "*", 1.0f);
			binary("Divide", "Math/Basic", "/", 1.0f);
			binary("Power", "Math/Basic", "pow", 2.0f, true);
			unary("Square Root", "Math/Basic", "sqrt(", ")");
			// ---- Math / Advanced
			unary("Absolute", "Math/Advanced", "abs(", ")");
			unary("Negate", "Math/Advanced", "(-", ")");
			unary("One Minus", "Math/Advanced", "(1.0 - ", ")");
			unary("Reciprocal", "Math/Advanced", "(1.0 / ", ")", 1.0f);
			unary("Exponential", "Math/Advanced", "exp(", ")");
			unary("Log", "Math/Advanced", "log(", ")", 1.0f);
			binary("Modulo", "Math/Advanced", "fmod", 1.0f, true);
			add("Posterize", "Math/Advanced", { P("In", D), P("Steps", D, 4, 4, 4, 4) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "In", "Steps" });
				c.Out("Out", w, "(floor(" + c.In("In", w) + " * " + c.In("Steps", w) + ") / " + c.In("Steps", w) + ")");
			});
			// ---- Math / Range
			unary("Saturate", "Math/Range", "saturate(", ")");
			unary("Fraction", "Math/Range", "frac(", ")");
			binary("Minimum", "Math/Range", "min", 0.0f, true);
			binary("Maximum", "Math/Range", "max", 0.0f, true);
			add("Clamp", "Math/Range", { P("In", D), P("Min", D, 0), P("Max", D, 1, 1, 1, 1) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "In", "Min", "Max" });
				c.Out("Out", w, "clamp(" + c.In("In", w) + ", " + c.In("Min", w) + ", " + c.In("Max", w) + ")");
			});
			add("Remap", "Math/Range", { P("In", D), P("In Min Max", 2, -1, 1), P("Out Min Max", 2, 0, 1) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "In" });
				const std::string i = c.In("In Min Max", 2), o = c.In("Out Min Max", 2);
				c.Out("Out", w, "(" + o + ".x + (" + c.In("In", w) + " - " + i + ".x) * (" + o + ".y - " + o + ".x) / (" + i + ".y - " + i + ".x))");
			});
			// ---- Math / Round
			unary("Floor", "Math/Round", "floor(", ")");
			unary("Ceiling", "Math/Round", "ceil(", ")");
			unary("Round", "Math/Round", "round(", ")");
			add("Step", "Math/Round", { P("Edge", D, 0.5f, 0.5f, 0.5f, 0.5f), P("In", D) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "Edge", "In" });
				c.Out("Out", w, "step(" + c.In("Edge", w) + ", " + c.In("In", w) + ")");
			});
			// ---- Math / Interpolation
			add("Lerp", "Math/Interpolation", { P("A", D, 0), P("B", D, 1, 1, 1, 1), P("T", D, 0.5f, 0.5f, 0.5f, 0.5f) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "A", "B", "T" });
				c.Out("Out", w, "lerp(" + c.In("A", w) + ", " + c.In("B", w) + ", " + c.In("T", w) + ")");
			});
			add("Smoothstep", "Math/Interpolation", { P("Edge1", D, 0), P("Edge2", D, 1, 1, 1, 1), P("In", D) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "Edge1", "Edge2", "In" });
				c.Out("Out", w, "smoothstep(" + c.In("Edge1", w) + ", " + c.In("Edge2", w) + ", " + c.In("In", w) + ")");
			});
			// ---- Math / Trigonometry
			unary("Sine", "Math/Trigonometry", "sin(", ")");
			unary("Cosine", "Math/Trigonometry", "cos(", ")");
			unary("Tangent", "Math/Trigonometry", "tan(", ")");
			// ---- Math / Vector
			add("Dot Product", "Math/Vector", { P("A", D), P("B", D) }, { P("Out", 1) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "A", "B" });
				c.Out("Out", 1, "dot(" + c.In("A", w) + ", " + c.In("B", w) + ")");
			});
			add("Cross Product", "Math/Vector", { P("A", 3), P("B", 3, 0, 1, 0) }, { P("Out", 3) }, [](NodeContext& c) { c.Out("Out", 3, "cross(" + c.In("A", 3) + ", " + c.In("B", 3) + ")"); });
			unary("Normalize", "Math/Vector", "normalize(", ")", 1.0f);
			add("Length", "Math/Vector", { P("In", D) }, { P("Out", 1) }, [](NodeContext& c) { c.Out("Out", 1, "length(" + c.In("In", c.DynamicWidth({ "In" })) + ")"); });
			add("Distance", "Math/Vector", { P("A", D), P("B", D) }, { P("Out", 1) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "A", "B" });
				c.Out("Out", 1, "distance(" + c.In("A", w) + ", " + c.In("B", w) + ")");
			});
			// ---- Channel
			add("Split", "Channel", { P("In", 4) }, { P("R", 1), P("G", 1), P("B", 1), P("A", 1) }, [](NodeContext& c) {
				const std::string v = c.In("In", 4);
				c.Out("R", 1, "(" + v + ").x");
				c.Out("G", 1, "(" + v + ").y");
				c.Out("B", 1, "(" + v + ").z");
				c.Out("A", 1, "(" + v + ").w");
			});
			add("Combine", "Channel", { P("R", 1), P("G", 1), P("B", 1), P("A", 1) }, { P("RGBA", 4), P("RGB", 3), P("RG", 2) }, [](NodeContext& c) {
				const std::string r = c.In("R", 1), g = c.In("G", 1), b = c.In("B", 1), a = c.In("A", 1);
				c.Out("RGBA", 4, "float4(" + r + ", " + g + ", " + b + ", " + a + ")");
				c.Out("RGB", 3, "float3(" + r + ", " + g + ", " + b + ")");
				c.Out("RG", 2, "float2(" + r + ", " + g + ")");
			});
			add("Swizzle", "Channel", { P("In", 4) }, { P("Out", D) }, [](NodeContext& c) {
				std::string mask = c.Option("mask").is_string() ? c.Option("mask").get<std::string>() : "xyzw";
				std::string clean;
				for (char ch : mask)
				{
					const char m = ch == 'r' ? 'x' : ch == 'g' ? 'y' : ch == 'b' ? 'z' : ch == 'a' ? 'w' : ch;
					if (m == 'x' || m == 'y' || m == 'z' || m == 'w') clean += m;
				}
				if (clean.empty()) clean = "x";
				if (clean.size() > 4) clean.resize(4);
				c.Out("Out", (int)clean.size(), "(" + c.In("In", 4) + ")." + clean);
			}, "option mask: e.g. xxy, bgr", { { "mask", "xyzw" } });
			// ---- UV
			add("Tiling And Offset", "UV", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Tiling", 2, 1, 1), P("Offset", 2, 0, 0) }, { P("Out", 2) }, [](NodeContext& c) {
				c.Out("Out", 2, "(" + c.In("UV", 2) + " * " + c.In("Tiling", 2) + " + " + c.In("Offset", 2) + ")");
			});
			add("Rotate", "UV", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Center", 2, 0.5f, 0.5f), P("Rotation", 1, 0) }, { P("Out", 2) }, [](NodeContext& c) {
				c.Out("Out", 2, "SG_Rotate(" + c.In("UV", 2) + ", " + c.In("Center", 2) + ", " + c.In("Rotation", 1) + ")");
			}, "Rotation in radians");
			add("Polar Coordinates", "UV", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Center", 2, 0.5f, 0.5f), P("Radial Scale", 1, 1), P("Length Scale", 1, 1) }, { P("Out", 2) }, [](NodeContext& c) {
				c.Out("Out", 2, "SG_Polar(" + c.In("UV", 2) + ", " + c.In("Center", 2) + ", " + c.In("Radial Scale", 1) + ", " + c.In("Length Scale", 1) + ")");
			});
			// ---- Procedural
			add("Simple Noise", "Procedural/Noise", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Scale", 1, 500) }, { P("Out", 1) }, [](NodeContext& c) {
				c.Out("Out", 1, "SG_SimpleNoise(" + c.In("UV", 2) + ", " + c.In("Scale", 1) + ")");
			});
			add("Gradient Noise", "Procedural/Noise", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Scale", 1, 10) }, { P("Out", 1) }, [](NodeContext& c) {
				c.Out("Out", 1, "SG_GradientNoise(" + c.In("UV", 2) + ", " + c.In("Scale", 1) + ")");
			});
			add("Voronoi", "Procedural/Noise", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Angle Offset", 1, 2), P("Cell Density", 1, 5) }, { P("Out", 1), P("Cells", 1) }, [](NodeContext& c) {
				const std::string v = c.OutVar(*c.N, "Voronoi");
				*c.Code += "    float2 " + v + " = SG_Voronoi(" + c.In("UV", 2) + ", " + c.In("Angle Offset", 1) + ", " + c.In("Cell Density", 1) + ");\n";
				c.Out("Out", 1, v + ".x");
				c.Out("Cells", 1, v + ".y");
			});
			add("Checkerboard", "Procedural", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Color A", 3, 0.2f, 0.2f, 0.2f), P("Color B", 3, 0.7f, 0.7f, 0.7f), P("Frequency", 2, 1, 1) }, { P("Out", 3) }, [](NodeContext& c) {
				c.Out("Out", 3, "SG_Checker(" + c.In("UV", 2) + ", " + c.In("Color A", 3) + ", " + c.In("Color B", 3) + ", " + c.In("Frequency", 2) + ")");
			});
			add("Ellipse", "Procedural/Shapes", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Width", 1, 0.5f), P("Height", 1, 0.5f) }, { P("Out", 1) }, [](NodeContext& c) {
				c.Out("Out", 1, "SG_Ellipse(" + c.In("UV", 2) + ", " + c.In("Width", 1) + ", " + c.In("Height", 1) + ")");
			});
			add("Rectangle", "Procedural/Shapes", { P("UV", 2, 0, 0, 0, 0, "uv"), P("Width", 1, 0.5f), P("Height", 1, 0.5f) }, { P("Out", 1) }, [](NodeContext& c) {
				c.Out("Out", 1, "SG_Rectangle(" + c.In("UV", 2) + ", " + c.In("Width", 1) + ", " + c.In("Height", 1) + ")");
			});
			// ---- Artistic / Utility
			add("Fresnel Effect", "Math/Vector", { P("Normal", 3, 0, 0, 0, 0, "normalW"), P("View Dir", 3, 0, 0, 0, 0, "viewW"), P("Power", 1, 1) }, { P("Out", 1) }, [](NodeContext& c) {
				c.Out("Out", 1, "pow(1.0 - saturate(dot(normalize(" + c.In("Normal", 3) + "), normalize(" + c.In("View Dir", 3) + "))), " + c.In("Power", 1) + ")");
			});
			add("Normal Strength", "Artistic/Normal", { P("In", 3, 0, 0, 1), P("Strength", 1, 1) }, { P("Out", 3) }, [](NodeContext& c) {
				const std::string i = c.In("In", 3), s = c.In("Strength", 1);
				c.Out("Out", 3, "float3((" + i + ").rg * " + s + ", lerp(1.0, (" + i + ").b, saturate(" + s + ")))");
			});
			add("Normal Blend", "Artistic/Normal", { P("A", 3, 0, 0, 1), P("B", 3, 0, 0, 1) }, { P("Out", 3) }, [](NodeContext& c) {
				const std::string a = c.In("A", 3), b = c.In("B", 3);
				c.Out("Out", 3, "normalize(float3((" + a + ").rg + (" + b + ").rg, (" + a + ").b * (" + b + ").b))");
			});
			add("Contrast", "Artistic/Adjustment", { P("In", 3, 0.5f, 0.5f, 0.5f), P("Contrast", 1, 1) }, { P("Out", 3) }, [](NodeContext& c) {
				c.Out("Out", 3, "((" + c.In("In", 3) + " - 0.5) * " + c.In("Contrast", 1) + " + 0.5)");
			});
			add("Saturation", "Artistic/Adjustment", { P("In", 3, 0.5f, 0.5f, 0.5f), P("Saturation", 1, 1) }, { P("Out", 3) }, [](NodeContext& c) {
				const std::string i = c.In("In", 3);
				c.Out("Out", 3, "lerp((float3)dot(" + i + ", float3(0.2126729, 0.7151522, 0.0721750)), " + i + ", " + c.In("Saturation", 1) + ")");
			});
			add("Branch", "Utility/Logic", { P("Predicate", 1, 1), P("True", D, 1, 1, 1, 1), P("False", D, 0) }, { P("Out", D) }, [](NodeContext& c) {
				const int w = c.DynamicWidth({ "True", "False" });
				c.Out("Out", w, "(" + c.In("Predicate", 1) + " > 0.5 ? " + c.In("True", w) + " : " + c.In("False", w) + ")");
			}, "Predicate > 0.5 → True");
			// Property (Blackboard): 코드 생성은 Generate 가 따로
			add("Property", "Property", {}, { P("Out", D) }, [](NodeContext&) {}, "a Blackboard property (option ref)", { { "ref", "" } });
			return d;
		}

		const char* kHelpers = R"(
SamplerState samSG
{
    Filter = ANISOTROPIC;
    MaxAnisotropy = 4;
    AddressU = WRAP;
    AddressV = WRAP;
};

float2 SG_Rotate(float2 uv, float2 center, float r)
{
    uv -= center;
    float s = sin(r), c = cos(r);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c) + center;
}

float2 SG_Polar(float2 uv, float2 center, float radialScale, float lengthScale)
{
    float2 d = uv - center;
    float radius = length(d) * 2.0 * radialScale;
    float angle = atan2(d.x, d.y) * 1.0 / 6.28318530718 * lengthScale;
    return float2(radius, angle);
}

// Unity Shader Graph 의 Simple Noise (값 노이즈 3 옥타브)
float SG_RandomValue(float2 uv) { return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453); }
float SG_Interp(float a, float b, float t) { return (1.0 - t) * a + t * b; }
float SG_ValueNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    float r0 = SG_RandomValue(i);
    float r1 = SG_RandomValue(i + float2(1, 0));
    float r2 = SG_RandomValue(i + float2(0, 1));
    float r3 = SG_RandomValue(i + float2(1, 1));
    return SG_Interp(SG_Interp(r0, r1, f.x), SG_Interp(r2, r3, f.x), f.y);
}
float SG_SimpleNoise(float2 uv, float scale)
{
    float t = 0.0;
    float freq = pow(2.0, 0.0); float amp = pow(0.5, 3.0);
    t += SG_ValueNoise(float2(uv.x * scale / freq, uv.y * scale / freq)) * amp;
    freq = pow(2.0, 1.0); amp = pow(0.5, 2.0);
    t += SG_ValueNoise(float2(uv.x * scale / freq, uv.y * scale / freq)) * amp;
    freq = pow(2.0, 2.0); amp = pow(0.5, 1.0);
    t += SG_ValueNoise(float2(uv.x * scale / freq, uv.y * scale / freq)) * amp;
    return t;
}

// Gradient Noise (펄린)
float2 SG_GradientDir(float2 p)
{
    p = p % 289.0;
    float x = (34.0 * p.x + 1.0) * p.x % 289.0 + p.y;
    x = (34.0 * x + 1.0) * x % 289.0;
    x = frac(x / 41.0) * 2.0 - 1.0;
    return normalize(float2(x - floor(x + 0.5), abs(x) - 0.5));
}
float SG_GradientNoise(float2 uv, float scale)
{
    float2 p = uv * scale;
    float2 ip = floor(p);
    float2 fp = frac(p);
    float d00 = dot(SG_GradientDir(ip), fp);
    float d01 = dot(SG_GradientDir(ip + float2(0, 1)), fp - float2(0, 1));
    float d10 = dot(SG_GradientDir(ip + float2(1, 0)), fp - float2(1, 0));
    float d11 = dot(SG_GradientDir(ip + float2(1, 1)), fp - float2(1, 1));
    fp = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);
    return lerp(lerp(d00, d01, fp.y), lerp(d10, d11, fp.y), fp.x) + 0.5;
}

float2 SG_VoronoiOffset(float2 uv, float offset)
{
    float2x2 m = float2x2(15.27, 47.63, 99.41, 89.98);
    uv = frac(sin(mul(uv, m)) * 46839.32);
    return float2(sin(uv.y * offset) * 0.5 + 0.5, cos(uv.x * offset) * 0.5 + 0.5);
}
float2 SG_Voronoi(float2 uv, float angleOffset, float cellDensity)
{
    float2 g = floor(uv * cellDensity);
    float2 f = frac(uv * cellDensity);
    float3 res = float3(8.0, 0.0, 0.0);
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 lattice = float2(x, y);
            float2 offset = SG_VoronoiOffset(lattice + g, angleOffset);
            float d = distance(lattice + offset, f);
            if (d < res.x)
                res = float3(d, offset.x, offset.y);
        }
    }
    return float2(res.x, res.y);
}

float3 SG_Checker(float2 uv, float3 a, float3 b, float2 freq)
{
    uv = (uv.xy + 0.5) * freq;
    float4 derivatives = float4(ddx(uv), ddy(uv));
    float2 duv = length(derivatives.xz) + length(derivatives.yw);
    float width = 1.0;
    float2 distance3 = 4.0 * abs(frac(uv + 0.25) - 0.5) - width;
    float2 scale = 0.35 / max(duv, 1e-5);
    float freqLimiter = sqrt(clamp(1.1 - max(duv.x, duv.y), 0.0, 1.0));
    float2 vectorAlpha = clamp(distance3 * scale.xy, -1.0, 1.0);
    float alpha = saturate(0.5 + 0.5 * vectorAlpha.x * vectorAlpha.y * freqLimiter);
    return lerp(b, a, alpha.xxx);
}

float SG_Ellipse(float2 uv, float w, float h)
{
    float d = length((uv * 2 - 1) / float2(w, h));
    return saturate((1 - d) / max(fwidth(d), 1e-5));
}

float SG_Rectangle(float2 uv, float w, float h)
{
    float2 d = abs(uv * 2 - 1) - float2(w, h);
    d = 1 - d / max(fwidth(d), 1e-5);
    return saturate(min(d.x, d.y));
}
)";
	}

	const std::vector<NodeDef>& NodeDefs()
	{
		static const std::vector<NodeDef> defs = BuildDefs();
		return defs;
	}

	const NodeDef* FindDef(const std::string& type)
	{
		for (const NodeDef& d : NodeDefs())
			if (d.Type == type)
				return &d;
		return nullptr;
	}

	const std::vector<PortDef>& MasterInputs(const std::string& material)
	{
		static const std::vector<PortDef> lit = {
			P("Base Color", 3, 0.5f, 0.5f, 0.5f), P("Normal", 3, 0, 0, 1), P("Metallic", 1, 0), P("Smoothness", 1, 0.5f),
			P("Emission", 3, 0, 0, 0), P("Ambient Occlusion", 1, 1), P("Alpha", 1, 1) };
		static const std::vector<PortDef> unlit = { P("Base Color", 3, 0.5f, 0.5f, 0.5f), P("Alpha", 1, 1) };
		return material == "Unlit" ? unlit : lit;
	}

	int Property::Width() const
	{
		if (Type == "Float") return 1;
		if (Type == "Vector2") return 2;
		if (Type == "Vector3") return 3;
		if (Type == "Texture2D") return kTexture;
		return 4;   // Color, Vector4
	}

	// ------------------------------------------------------------------ 그래프
	Node* Graph::FindNode(int id)
	{
		for (Node& n : Nodes) if (n.Id == id) return &n;
		return nullptr;
	}

	const Node* Graph::FindNode(int id) const
	{
		for (const Node& n : Nodes) if (n.Id == id) return &n;
		return nullptr;
	}

	const Property* Graph::FindProperty(const std::string& ref) const
	{
		for (const Property& p : Properties) if (p.Ref == ref) return &p;
		return nullptr;
	}

	int Graph::AddNode(const std::string& type, float x, float y)
	{
		const NodeDef* d = FindDef(type);
		if (!d)
			return 0;
		Node n;
		n.Id = NextId++;
		n.Type = type;
		n.X = x;
		n.Y = y;
		n.Options = d->DefaultOptions;
		Nodes.push_back(n);
		return n.Id;
	}

	void Graph::RemoveNode(int id)
	{
		Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(), [&](const Node& n) { return n.Id == id; }), Nodes.end());
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [&](const Edge& e) { return e.FromNode == id || e.ToNode == id; }), Edges.end());
	}

	void Graph::Disconnect(int toNode, const std::string& toPort)
	{
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [&](const Edge& e) { return e.ToNode == toNode && e.ToPort == toPort; }), Edges.end());
	}

	std::string Graph::UniqueRef(const std::string& name) const
	{
		std::string base = "_" + Sanitize(name);
		base.erase(std::remove(base.begin(), base.end(), ' '), base.end());
		std::string ref = base;
		for (int i = 1; FindProperty(ref); ++i)
			ref = base + std::to_string(i);
		return ref;
	}

	namespace
	{
		const PortDef* FindPort(const std::vector<PortDef>& ports, const std::string& name)
		{
			for (const PortDef& p : ports) if (p.Name == name) return &p;
			return nullptr;
		}

		// from 이 to 의 위쪽 (to 의 입력을 거슬러 올라가 from 에 닿나)
		bool Reaches(const Graph& g, int start, int target, std::set<int>& seen)
		{
			if (start == target) return true;
			if (!seen.insert(start).second) return false;
			for (const Edge& e : g.Edges)
				if (e.ToNode == start && Reaches(g, e.FromNode, target, seen))
					return true;
			return false;
		}
	}

	bool Graph::Connect(int fromNode, const std::string& fromPort, int toNode, const std::string& toPort, std::string& error)
	{
		const Node* from = FindNode(fromNode);
		if (!from) { error = "no node " + std::to_string(fromNode); return false; }
		const NodeDef* fd = FindDef(from->Type);
		if (!fd || !FindPort(fd->Out, fromPort)) { error = "node " + std::to_string(fromNode) + " (" + from->Type + ") has no output '" + fromPort + "'"; return false; }
		const PortDef* tp = nullptr;
		if (toNode == 0)
			tp = FindPort(MasterInputs(Material), toPort);
		else if (const Node* to = FindNode(toNode))
		{
			if (const NodeDef* td = FindDef(to->Type)) tp = FindPort(td->In, toPort);
		}
		else { error = "no node " + std::to_string(toNode); return false; }
		if (!tp) { error = "no input '" + toPort + "' on " + (toNode == 0 ? std::string("Master") : "node " + std::to_string(toNode)); return false; }
		const bool fromTex = OutputWidth(*this, *from, fromPort) == kTexture;
		if (fromTex != (tp->Width == kTexture)) { error = "a Texture2D connects only to a Texture input"; return false; }
		std::set<int> seen;
		if (toNode != 0 && Reaches(*this, fromNode, toNode, seen)) { error = "that link would make a loop"; return false; }
		Disconnect(toNode, toPort);
		Edges.push_back({ fromNode, fromPort, toNode, toPort });
		return true;
	}

	int OutputWidth(const Graph& g, const Node& n, const std::string& port)
	{
		if (n.Type == "Property")
		{
			const std::string ref = n.Options.value("ref", std::string());
			const Property* p = g.FindProperty(ref);
			return p ? p->Width() : 1;
		}
		if (n.Type == "Swizzle")
		{
			const std::string mask = n.Options.value("mask", std::string("xyzw"));
			int count = 0;
			for (char c : mask) if (strchr("xyzwrgba", c)) ++count;
			return std::clamp(count, 1, 4);
		}
		const NodeDef* d = FindDef(n.Type);
		if (!d) return 1;
		const PortDef* out = FindPort(d->Out, port);
		if (!out) return 1;
		if (out->Width != kDynamic) return out->Width;
		// 동적 출력: 동적 입력들 중 가장 큰 폭
		int w = 1;
		for (const PortDef& in : d->In)
		{
			if (in.Width != kDynamic) continue;
			int iw = 1;
			bool connected = false;
			for (const Edge& e : g.Edges)
				if (e.ToNode == n.Id && e.ToPort == in.Name)
					if (const Node* src = g.FindNode(e.FromNode)) { iw = OutputWidth(g, *src, e.FromPort); connected = true; }
			if (!connected && n.Values.contains(in.Name) && n.Values[in.Name].is_array())
				iw = std::clamp((int)n.Values[in.Name].size(), 1, 4);
			w = (std::max)(w, iw == kTexture ? 1 : iw);
		}
		return w;
	}

	// ------------------------------------------------------------------ NodeContext
	namespace
	{
		const Edge* FindEdge(const Graph& g, int node, const std::string& port)
		{
			for (const Edge& e : g.Edges)
				if (e.ToNode == node && e.ToPort == port)
					return &e;
			return nullptr;
		}
	}

	int NodeContext::InWidth(const std::string& port) const
	{
		if (const Edge* e = FindEdge(*G, N->Id, port))
			if (const Node* src = G->FindNode(e->FromNode))
				return OutputWidth(*G, *src, e->FromPort);
		const PortDef* p = FindPort(D->In, port);
		if (!p) return 1;
		if (!p->Bind.empty()) return BindWidth(p->Bind);
		if (p->Width == kDynamic)
			return N->Values.contains(port) && N->Values[port].is_array() ? std::clamp((int)N->Values[port].size(), 1, 4) : 1;
		return p->Width;
	}

	int NodeContext::DynamicWidth(std::initializer_list<const char*> ports) const
	{
		int w = 1;
		for (const char* p : ports)
		{
			const int iw = InWidth(p);
			if (iw != kTexture) w = (std::max)(w, iw);
		}
		return w;
	}

	std::string NodeContext::In(const std::string& port, int width) const
	{
		const int have = InWidth(port);
		const int want = width > 0 ? width : have;
		if (const Edge* e = FindEdge(*G, N->Id, port))
			if (const Node* src = G->FindNode(e->FromNode))
				return Cast(OutVar(*src, e->FromPort), have, want);
		const PortDef* p = FindPort(D->In, port);
		if (!p) return Cast("0.0", 1, want);
		if (!p->Bind.empty())
			return Cast(BindExpr(p->Bind), BindWidth(p->Bind), want);
		float v[4] = { p->Default[0], p->Default[1], p->Default[2], p->Default[3] };
		int w = p->Width == kDynamic ? 1 : p->Width;
		if (N->Values.contains(port))
		{
			const json& val = N->Values[port];
			if (val.is_number()) { v[0] = val.get<float>(); w = p->Width == kDynamic ? 1 : p->Width; if (p->Width != kDynamic) for (int i = 1; i < 4; ++i) v[i] = v[0]; }
			else if (val.is_array())
			{
				for (int i = 0; i < 4 && i < (int)val.size(); ++i) v[i] = val[i].get<float>();
				if (p->Width == kDynamic) w = std::clamp((int)val.size(), 1, 4);
			}
		}
		else if (p->Width == kDynamic)
		{
			// 동적 입력의 기본값: 요청한 폭으로 (모든 칸 같은 값)
			for (int i = 1; i < 4; ++i) v[i] = p->Default[i];
			return Literal(v, want);
		}
		return Cast(Literal(v, w), w, want);
	}

	std::string NodeContext::TextureIn(const std::string& port) const
	{
		if (const Edge* e = FindEdge(*G, N->Id, port))
			if (const Node* src = G->FindNode(e->FromNode); src && src->Type == "Property")
			{
				const Property* p = G->FindProperty(src->Options.value("ref", std::string()));
				if (p && p->Type == "Texture2D")
					return "gSG_" + Sanitize(p->Ref);
			}
		// 노드에 바로 넣은 그림 (option texture)
		const std::string inlinePath = N->Options.value("texture", std::string());
		if (!inlinePath.empty())
			return "gSG_NodeTex" + std::to_string(N->Id);
		return std::string();
	}

	void NodeContext::Out(const std::string& port, int width, const std::string& expr) const
	{
		*Code += "    " + TypeName(width) + " " + OutVar(*N, port) + " = " + expr + ";\n";
	}

	json NodeContext::Option(const char* key) const
	{
		return N->Options.contains(key) ? N->Options[key] : json();
	}

	// ------------------------------------------------------------------ 코드 생성
	CodeResult Generate(const Graph& g)
	{
		CodeResult r;
		// Master 에서 거슬러 올라가며 쓰는 노드 (뒤 순서 = 위상 순서), 고리 검사
		std::vector<int> order;
		std::map<int, int> state;   // 1 방문 중, 2 끝
		std::function<bool(int)> visit = [&](int id) -> bool {
			if (state[id] == 2) return true;
			if (state[id] == 1) { r.Error = "the graph has a loop at node " + std::to_string(id); return false; }
			state[id] = 1;
			for (const Edge& e : g.Edges)
				if (e.ToNode == id && !visit(e.FromNode))
					return false;
			state[id] = 2;
			order.push_back(id);
			return true;
		};
		for (const Edge& e : g.Edges)
			if (e.ToNode == 0 && !visit(e.FromNode))
				return r;
		auto outVar = [](const Node& n, const std::string& port) { return "n" + std::to_string(n.Id) + "_" + Sanitize(port); };

		std::string body;
		for (int id : order)
		{
			const Node* n = g.FindNode(id);
			if (!n) { r.Error = "missing node " + std::to_string(id); return r; }
			if (n->Type == "Property")
			{
				const std::string ref = n->Options.value("ref", std::string());
				const Property* p = g.FindProperty(ref);
				if (!p) { r.Error = "node " + std::to_string(id) + ": no property '" + ref + "' on the Blackboard"; return r; }
				if (p->Type != "Texture2D")
					body += "    " + TypeName(p->Width()) + " " + outVar(*n, "Out") + " = " + Cast("gSG_" + Sanitize(p->Ref), 4, p->Width()) + ";\n";
				r.Used.push_back(id);
				continue;
			}
			const NodeDef* d = FindDef(n->Type);
			if (!d) { r.Error = "node " + std::to_string(id) + ": unknown type '" + n->Type + "'"; return r; }
			NodeContext ctx;
			ctx.G = &g; ctx.N = n; ctx.D = d; ctx.Code = &body; ctx.OutVar = outVar;
			body += "    // " + std::to_string(id) + " " + n->Type + "\n";
			d->Gen(ctx);
			r.Used.push_back(id);
		}

		// Master 입력
		auto master = [&](const char* name, int width) -> std::string {
			for (const Edge& e : g.Edges)
				if (e.ToNode == 0 && e.ToPort == name)
					if (const Node* src = g.FindNode(e.FromNode))
						return Cast(outVar(*src, e.FromPort), OutputWidth(g, *src, e.FromPort), width);
			for (const PortDef& p : MasterInputs(g.Material))
				if (p.Name == name)
					return Literal(p.Default, width);
			return Cast("0.0", 1, width);
		};

		std::ostringstream fx;
		fx << "//=============================================================================\n";
		fx << "// Shader Graph 가 만든 셰이더 (손으로 고치지 말 것 — .shadergraph 를 고치면 다시 만들어진다)\n";
		fx << "//=============================================================================\n";
		fx << "#include \"32. InstancedBasic.fx\"\n\n";
		fx << "cbuffer cbShaderGraph\n{\n    float4 gSGTime;   // x 시간, y sin, z cos, w 프레임 간격\n";
		for (const Property& p : g.Properties)
			if (p.Type != "Texture2D")
				fx << "    float4 gSG_" << Sanitize(p.Ref) << ";   // " << p.Name << " (" << p.Type << ")\n";
		fx << "};\n";
		for (const Property& p : g.Properties)
			if (p.Type == "Texture2D")
				fx << "Texture2D gSG_" << Sanitize(p.Ref) << ";   // " << p.Name << "\n";
		for (int id : order)
			if (const Node* n = g.FindNode(id); n && !n->Options.value("texture", std::string()).empty())
				fx << "Texture2D gSG_NodeTex" << id << ";   // " << n->Options.value("texture", std::string()) << "\n";
		fx << kHelpers << "\n";
		fx << "float4 PS_Graph(VertexOut pin) : SV_Target\n{\n";
		fx << "    float3 sg_normalW = normalize(pin.NormalW);\n";
		fx << "    float3 sg_posW = pin.PosW.xyz;\n";
		fx << "    float3 sg_toEye = gEyePosW - sg_posW;\n";
		fx << "    float sg_dist = length(sg_toEye);\n";
		fx << "    float3 sg_viewW = sg_toEye / max(sg_dist, 0.0001);\n";
		fx << "    float2 sg_uv = pin.Tex;\n";
		fx << "    float4 sg_screen = float4(pin.SsaoPosH.xy / max(pin.SsaoPosH.w, 1e-5), 0, 1);\n\n";
		fx << body << "\n";
		fx << "    float3 baseColor = " << master("Base Color", 3) << ";\n";
		fx << "    float alpha = " << master("Alpha", 1) << ";\n";
		if (g.Material == "Unlit")
		{
			fx << "    return float4(ToGamma(ToLinear(baseColor)), alpha);\n";
		}
		else
		{
			fx << "    float3 normalTS = " << master("Normal", 3) << ";\n";
			fx << "    float3 N = sg_normalW;\n";
			bool normalConnected = false;
			for (const Edge& e : g.Edges) normalConnected |= e.ToNode == 0 && e.ToPort == "Normal";
			if (normalConnected)
				fx << "    N = NormalSampleToWorldSpace(normalize(normalTS) * 0.5 + 0.5, sg_normalW, pin.TangentW);\n";
			fx << "    LitSurface surf;\n";
			fx << "    surf.Albedo = ToLinear(saturate(baseColor));\n";
			fx << "    surf.Metallic = saturate(" << master("Metallic", 1) << ");\n";
			fx << "    surf.Smoothness = saturate(" << master("Smoothness", 1) << ");\n";
			fx << "    surf.Occlusion = saturate(" << master("Ambient Occlusion", 1) << ");\n";
			fx << "    surf.Emission = max(" << master("Emission", 3) << ", 0);\n";
			fx << "    surf.Transmission = float3(0, 0, 0);\n";
			fx << "    surf.Highlights = gPbr.SpecularHighlights != 0;   // 재질의 Advanced Options · Receive Shadows\n";
			fx << "    surf.Reflections = gPbr.EnvironmentReflections != 0;\n";
			fx << "    surf.ReceiveShadows = gPbr.ReceiveShadows != 0;\n";
			fx << "    return FinishLit(ShadeLit(surf, sg_posW, N, sg_viewW, pin.SsaoPosH), alpha, sg_dist);\n";
		}
		fx << "}\n\n";
		fx << "technique11 GraphBatchTech\n{\n    pass P0\n    {\n        SetVertexShader(CompileShader(vs_5_0, VS_Batch()));\n        SetGeometryShader(NULL);\n        SetPixelShader(CompileShader(ps_5_0, PS_Graph()));\n    }\n}\n\n";
		fx << "technique11 GraphSkinnedTech\n{\n    pass P0\n    {\n        SetVertexShader(CompileShader(vs_5_0, VS_Skinned()));\n        SetGeometryShader(NULL);\n        SetPixelShader(CompileShader(ps_5_0, PS_Graph()));\n    }\n}\n";
		r.Hlsl = fx.str();
		return r;
	}

	// ------------------------------------------------------------------ JSON
	json Graph::ToJson() const
	{
		json props = json::array();
		for (const Property& p : Properties)
		{
			json jp = { { "name", p.Name }, { "ref", p.Ref }, { "type", p.Type }, { "value", { p.Value[0], p.Value[1], p.Value[2], p.Value[3] } } };
			if (p.Type == "Texture2D") jp["texture"] = p.Texture;
			if (p.Range) jp["range"] = { p.Min, p.Max };
			props.push_back(jp);
		}
		json nodes = json::array();
		for (const Node& n : Nodes)
			nodes.push_back({ { "id", n.Id }, { "type", n.Type }, { "pos", { n.X, n.Y } }, { "values", n.Values }, { "options", n.Options } });
		json edges = json::array();
		for (const Edge& e : Edges)
			edges.push_back({ { "from", { e.FromNode, e.FromPort } }, { "to", { e.ToNode, e.ToPort } } });
		return { { "format", "nova-shadergraph" }, { "version", 1 }, { "material", Material }, { "properties", props }, { "nodes", nodes }, { "edges", edges }, { "nextId", NextId } };
	}

	bool Graph::FromJson(const json& j, std::string& error)
	{
		if (!j.is_object() || j.value("format", std::string()) != "nova-shadergraph")
		{
			error = "not a NOVA shader graph";
			return false;
		}
		Material = j.value("material", std::string("Lit"));
		Properties.clear();
		Nodes.clear();
		Edges.clear();
		if (j.contains("properties"))
			for (const json& jp : j["properties"])
			{
				Property p;
				p.Name = jp.value("name", std::string("Property"));
				p.Ref = jp.value("ref", std::string("_Property"));
				p.Type = jp.value("type", std::string("Float"));
				if (jp.contains("value") && jp["value"].is_array())
					for (int i = 0; i < 4 && i < (int)jp["value"].size(); ++i) p.Value[i] = jp["value"][i].get<float>();
				p.Texture = jp.value("texture", std::string());
				if (jp.contains("range") && jp["range"].is_array() && jp["range"].size() == 2)
				{
					p.Range = true;
					p.Min = jp["range"][0].get<float>();
					p.Max = jp["range"][1].get<float>();
				}
				Properties.push_back(p);
			}
		int maxId = 0;
		if (j.contains("nodes"))
			for (const json& jn : j["nodes"])
			{
				Node n;
				n.Id = jn.value("id", 0);
				n.Type = jn.value("type", std::string());
				if (jn.contains("pos") && jn["pos"].is_array() && jn["pos"].size() == 2) { n.X = jn["pos"][0].get<float>(); n.Y = jn["pos"][1].get<float>(); }
				n.Values = jn.value("values", json::object());
				n.Options = jn.value("options", json::object());
				maxId = (std::max)(maxId, n.Id);
				Nodes.push_back(n);
			}
		if (j.contains("edges"))
			for (const json& je : j["edges"])
				if (je.contains("from") && je.contains("to"))
					Edges.push_back({ je["from"][0].get<int>(), je["from"][1].get<std::string>(), je["to"][0].get<int>(), je["to"][1].get<std::string>() });
		NextId = (std::max)(j.value("nextId", 1), maxId + 1);
		return true;
	}

	bool Graph::Save(const std::wstring& fullPath, std::string& error) const
	{
		std::error_code ec;
		fs::create_directories(fs::path(fullPath).parent_path(), ec);
		std::ofstream os(fullPath, std::ios::binary | std::ios::trunc);
		if (!os) { error = "cannot write " + wstring_to_string(fullPath); return false; }
		os << ToJson().dump(2);
		return true;
	}

	bool Graph::Load(const std::wstring& fullPath, std::string& error)
	{
		std::ifstream in(fullPath, std::ios::binary);
		if (!in) { error = "cannot read " + wstring_to_string(fullPath); return false; }
		const json j = json::parse(in, nullptr, false);
		if (j.is_discarded()) { error = "bad JSON in " + wstring_to_string(fullPath); return false; }
		return FromJson(j, error);
	}
}
