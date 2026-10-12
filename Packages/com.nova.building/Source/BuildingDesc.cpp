#include "pch.h"
#include "BuildingDesc.h"
#include <random>

using json = nlohmann::json;

namespace
{
	const char* const kStyles[] = {
		"Medieval Cottage", "Tudor Townhouse", "Tavern", "Nordic Longhouse", "Stave Church", "Gothic Chapel", "Roman Villa", "Roman Temple",
		"Baroque Palace", "Castle Keep", "Watchtower", "Wizard Tower", "Alpine Chalet", "Mediterranean House", "Market Hall", "Blacksmith", "Windmill",
	"Northern Merchant House", "Northern Gatehouse", "Riverland Townhouse", "Highland Manor", "Canal House", "Canal Merchant Hall", "Bridge House",
	"Stone Canal Bridge" };
	const char* const kDescriptions[] = {
		"Stone ground walls, small windows, steep thatched roof and a chimney",
		"Stone ground floor, jettied half-timbered upper floors (Tudor bracing), steep tiled gable, dormers",
		"L-shaped inn: half-timbered floors over stone, dormers and two chimneys",
		"Long low hall of logs under a steep turf roof, carved dragon heads on the gables",
		"Norwegian stave church: tiered shingle roofs, dragon heads and a spire",
		"Long stone nave, pointed lancet windows, buttresses, steep slate roof, rose window",
		"Stucco courtyard villa with low clay-tile hip roofs and a columned portico",
		"Marble temple on a podium, columns all round, pediment roof",
		"Symmetric three-floor palace: pilasters, cornice, ornate windows, mansard roof and copper dome",
		"Square stone keep with crenellations and round corner towers with conical roofs",
		"Round stone tower with arrow slits and a conical slate roof",
		"Tall tapering round tower with a crooked spire roof",
		"Stone base, timber upper floors, wide low roof, balcony and painted shutters",
		"Whitewashed stucco house, arched windows, blue shutters, low clay roof",
		"Open stone arcade below, half-timbered hall above, hipped roof",
		"Low workshop of planks and stone with a big chimney",
		"Tapering stone mill tower with a turning cap and four sails",
	"Rubble ground floor with a stone-arched door, plank upper floor on heavy posts, steep shingle roof on brackets, carved and painted gables, banners",
	"Timber gate hall over a stone archway: the road runs through, steep shingle gable to the front, carved bargeboards and banners",
	"Tall river-town house: rubble ground floor, jettied half-timbered floors on curved braces, oriel window, corner turret, steep bellcast tile roof",
	"Highland manor of cut stone and timber: L-plan, round corner turrets with flared witch-hat roofs, stair tower, curved slate roofs, carved gables",
	"Narrow canal-town house standing in the water: stone base with a landing, water steps and mooring posts, close-studded jettied floors, oriel, steep mossy tiles",
	"Wide merchant hall on a canal: big leaded shop windows, close studding over grey rubble, three jettied floors, oriels, long shakes, landing and mooring posts",
	"Town house built over a passage: a stone arch carries the street through, close-studded floors above on brackets, oriel, steep tiled gable",
	"Hump-backed stone bridge over a canal: three arches with voussoirs, cutwaters, parapets with coping stones, cobbled deck" };
	constexpr int kStyleCount = (int)(sizeof(kStyles) / sizeof(kStyles[0]));

	void Rgb(float* d, float r, float g, float b) { d[0] = r; d[1] = g; d[2] = b; }
}

const char* const* BuildingDesc::StyleNames(int& count)
{
	count = kStyleCount;
	return kStyles;
}

const char* BuildingDesc::StyleDescription(int style)
{
	return style >= 0 && style < kStyleCount ? kDescriptions[style] : "";
}

int BuildingDesc::FindStyle(const std::string& name)
{
	for (int i = 0; i < kStyleCount; ++i)
		if (_stricmp(kStyles[i], name.c_str()) == 0)
			return i;
	return -1;
}

void BuildingDesc::ApplyStyle(int style)
{
	const int seed = Seed;
	*this = BuildingDesc();
	Seed = seed;
	Style = std::clamp(style, 0, kStyleCount - 1);
	switch (Style)
	{
	default:
	case 0:   // Medieval Cottage
		Shape = Plan::Rectangle; Width = 8.0f; Depth = 6.0f; Floors = 1; FloorHeight = 2.8f; Plinth = 0.35f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Rubble;
		WindowStyle = GroundWindowStyle = Window::Rectangular; WindowWidth = 0.7f; WindowHeight = 0.9f; WindowSpacing = 2.6f; SillHeight = 0.95f; Shutters = true;
		DoorStyle = Door::Plank; DoorWidth = 1.0f; DoorHeight = 1.95f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Thatch; RoofPitch = 52.0f; Overhang = 0.6f; Chimneys = 1;
		Rgb(WallColor, 0.64f, 0.6f, 0.54f); Rgb(TrimColor, 0.55f, 0.52f, 0.47f); Rgb(RoofColor, 0.76f, 0.64f, 0.42f);
		Rgb(ShutterColor, 0.42f, 0.3f, 0.2f); Rgb(DoorColor, 0.38f, 0.26f, 0.16f);
		Weathering = 0.75f; Bargeboards = 1; Irregularity = 0.85f; FlowerBoxes = true; Props = 1; Annex = true; RoofCurve = 0.15f;
		break;
	case 1:   // Tudor Townhouse
		Shape = Plan::Rectangle; Width = 8.0f; Depth = 7.0f; Floors = 3; FloorHeight = 2.8f; Plinth = 0.4f; Jetty = 0.35f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Plaster; Frame = Timber::Tudor; TimberWidth = 0.2f;
		WindowStyle = Window::Mullioned; GroundWindowStyle = Window::Mullioned; WindowWidth = 1.1f; WindowHeight = 1.1f; WindowSpacing = 2.4f; SillHeight = 0.9f;
		DoorStyle = Door::Arched; DoorWidth = 1.1f; DoorHeight = 2.1f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 55.0f; Overhang = 0.35f; Dormers = 2; Chimneys = 2;
		Rgb(WallColor, 0.66f, 0.62f, 0.55f); Rgb(UpperColor, 0.93f, 0.88f, 0.76f); Rgb(TimberColor, 0.2f, 0.14f, 0.1f); Rgb(RoofColor, 0.58f, 0.3f, 0.22f);
		Rgb(FrameColor, 0.2f, 0.14f, 0.1f);
		Weathering = 0.5f; Lanterns = true; Finials = true; Irregularity = 0.65f; FlowerBoxes = true; ShopSign = true; RoofCurve = 0.2f; Oriels = 1;
		break;
	case 2:   // Tavern
		Shape = Plan::LShape; Width = 14.0f; Depth = 8.0f; WingWidth = 0.9f; WingLength = 0.55f; Floors = 2; FloorHeight = 3.0f; Plinth = 0.45f; Jetty = 0.25f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Plaster; Frame = Timber::Cross;
		WindowStyle = Window::Mullioned; GroundWindowStyle = Window::Arched; WindowWidth = 1.0f; WindowHeight = 1.2f; WindowSpacing = 2.5f; Shutters = true;
		DoorStyle = Door::Double; DoorWidth = 1.8f; DoorHeight = 2.3f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Shingles; RoofPitch = 50.0f; Overhang = 0.45f; Dormers = 3; Chimneys = 2;
		Rgb(WallColor, 0.6f, 0.56f, 0.5f); Rgb(UpperColor, 0.9f, 0.84f, 0.7f); Rgb(TimberColor, 0.26f, 0.17f, 0.11f); Rgb(RoofColor, 0.42f, 0.34f, 0.28f);
		Rgb(ShutterColor, 0.52f, 0.2f, 0.16f); Rgb(DoorColor, 0.32f, 0.2f, 0.12f);
		Weathering = 0.65f; Brackets = true; Lanterns = true; Banners = 1; Bargeboards = 1; CornerPosts = true;
		Irregularity = 0.6f; Terrace = true; FlowerBoxes = true; ShopSign = true; Props = 2; RoofCurve = 0.25f; Turrets = 1;
		break;
	case 3:   // Nordic Longhouse
		Shape = Plan::Rectangle; Width = 20.0f; Depth = 7.0f; Floors = 1; FloorHeight = 2.6f; Plinth = 0.4f;
		GroundWall = WallMat::Logs; UpperWall = WallMat::Logs;
		WindowStyle = GroundWindowStyle = Window::Slit; WindowWidth = 0.35f; WindowHeight = 0.6f; WindowSpacing = 4.5f; SillHeight = 1.3f;
		DoorStyle = Door::Plank; DoorWidth = 1.5f; DoorHeight = 2.1f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Turf; RoofPitch = 48.0f; Overhang = 0.7f; Chimneys = 0; DragonHeads = true;
		Rgb(WallColor, 0.5f, 0.38f, 0.26f); Rgb(UpperColor, 0.5f, 0.38f, 0.26f); Rgb(TrimColor, 0.5f, 0.48f, 0.44f); Rgb(RoofColor, 0.5f, 0.62f, 0.32f);
		Rgb(TimberColor, 0.3f, 0.2f, 0.12f); Rgb(DoorColor, 0.32f, 0.22f, 0.14f);
		Weathering = 0.6f; Brackets = true; Bargeboards = 2; PaintedGables = true; Porch = true; Irregularity = 0.7f; Props = 1; Annex = true;
		break;
	case 4:   // Stave Church
		Shape = Plan::Cross; Width = 12.0f; Depth = 6.0f; WingWidth = 1.0f; WingLength = 0.75f; Floors = 2; FloorHeight = 3.2f; Plinth = 0.6f;
		GroundWall = WallMat::Planks; UpperWall = WallMat::Planks;
		WindowStyle = GroundWindowStyle = Window::Round; WindowWidth = 0.5f; WindowHeight = 0.5f; WindowSpacing = 4.0f; SillHeight = 1.8f;
		DoorStyle = Door::Arched; DoorWidth = 1.3f; DoorHeight = 2.4f;
		RoofStyle = Roof::Tiered; RoofMaterial = RoofMat::Shingles; RoofPitch = 55.0f; Overhang = 0.5f; Chimneys = 0; DragonHeads = true; SpireHeight = 7.0f;
		Rgb(WallColor, 0.28f, 0.2f, 0.14f); Rgb(UpperColor, 0.28f, 0.2f, 0.14f); Rgb(TrimColor, 0.45f, 0.42f, 0.38f); Rgb(RoofColor, 0.22f, 0.18f, 0.15f);
		Rgb(TimberColor, 0.18f, 0.13f, 0.1f); Rgb(DoorColor, 0.3f, 0.2f, 0.12f);
		Weathering = 0.5f; Bargeboards = 2; Finials = true; Brackets = true; Irregularity = 0.35f; RoofCurve = 0.3f;
		break;
	case 5:   // Gothic Chapel
		Shape = Plan::Cross; Width = 22.0f; Depth = 9.0f; WingWidth = 0.95f; WingLength = 0.6f; Floors = 2; FloorHeight = 4.5f; Plinth = 0.7f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Ashlar; Buttresses = true; Cornice = true;
		WindowStyle = GroundWindowStyle = Window::Gothic; WindowWidth = 1.3f; WindowHeight = 4.2f; WindowSpacing = 3.8f; SillHeight = 2.2f; Lattice = true; RoseWindow = true;
		DoorStyle = Door::Grand; DoorWidth = 2.2f; DoorHeight = 4.0f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Slate; RoofPitch = 58.0f; Overhang = 0.3f; Chimneys = 0;
		Towers = 1; TowerKind = TowerShape::Square; TowerRadius = 2.8f; TowerExtra = 9.0f; TowerRoof = Roof::Pyramid; SpireHeight = 12.0f;
		Rgb(WallColor, 0.72f, 0.69f, 0.62f); Rgb(UpperColor, 0.72f, 0.69f, 0.62f); Rgb(TrimColor, 0.66f, 0.63f, 0.57f); Rgb(RoofColor, 0.34f, 0.36f, 0.4f);
		Rgb(GlassColor, 0.28f, 0.3f, 0.5f); Rgb(DoorColor, 0.3f, 0.2f, 0.13f); Rgb(MetalColor, 0.4f, 0.42f, 0.44f);
		Weathering = 0.55f; Irregularity = 0.2f;
		break;
	case 6:   // Roman Villa
		Shape = Plan::UShape; Width = 22.0f; Depth = 9.0f; WingWidth = 0.75f; WingLength = 0.55f; Floors = 1; FloorHeight = 3.8f; Plinth = 0.5f;
		GroundWall = WallMat::Stucco; UpperWall = WallMat::Stucco; Cornice = true; Portico = true; PorticoColumns = 8;
		WindowStyle = GroundWindowStyle = Window::Rectangular; WindowWidth = 0.8f; WindowHeight = 1.2f; WindowSpacing = 3.5f; SillHeight = 1.4f; Shutters = true; Lattice = false;
		DoorStyle = Door::Double; DoorWidth = 2.0f; DoorHeight = 2.8f;
		RoofStyle = Roof::Hip; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 22.0f; Overhang = 0.6f; Chimneys = 0;
		Rgb(WallColor, 0.93f, 0.86f, 0.74f); Rgb(UpperColor, 0.93f, 0.86f, 0.74f); Rgb(TrimColor, 0.92f, 0.9f, 0.85f); Rgb(RoofColor, 0.7f, 0.36f, 0.22f);
		Rgb(ShutterColor, 0.5f, 0.24f, 0.16f); Rgb(DoorColor, 0.38f, 0.24f, 0.14f);
		Weathering = 0.25f; Irregularity = 0.15f; Terrace = true; FlowerBoxes = true;
		break;
	case 7:   // Roman Temple
		Shape = Plan::Rectangle; Width = 12.0f; Depth = 22.0f; Floors = 1; FloorHeight = 8.0f; Plinth = 2.0f;
		GroundWall = WallMat::Marble; UpperWall = WallMat::Marble; Cornice = true; Peristyle = true; PorticoColumns = 6;
		WindowStyle = GroundWindowStyle = Window::None;
		DoorStyle = Door::Grand; DoorWidth = 3.0f; DoorHeight = 5.5f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 20.0f; Overhang = 0.4f; Chimneys = 0;
		Rgb(WallColor, 0.9f, 0.88f, 0.84f); Rgb(UpperColor, 0.9f, 0.88f, 0.84f); Rgb(TrimColor, 0.92f, 0.9f, 0.86f); Rgb(RoofColor, 0.68f, 0.38f, 0.26f);
		Rgb(DoorColor, 0.5f, 0.36f, 0.18f);
		Weathering = 0.2f; Irregularity = 0.05f;
		break;
	case 8:   // Baroque Palace
		Shape = Plan::UShape; Width = 40.0f; Depth = 12.0f; WingWidth = 1.0f; WingLength = 0.45f; Floors = 3; FloorHeight = 4.2f; Plinth = 0.9f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Stucco; Quoins = true; StringCourse = true; Cornice = true; Pilasters = true; Portico = true; PorticoColumns = 4;
		WindowStyle = Window::Baroque; GroundWindowStyle = Window::Arched; WindowWidth = 1.3f; WindowHeight = 2.6f; WindowSpacing = 3.2f; SillHeight = 0.8f;
		WindowPediments = true; Lattice = false;
		DoorStyle = Door::Grand; DoorWidth = 2.6f; DoorHeight = 3.6f;
		RoofStyle = Roof::Mansard; RoofMaterial = RoofMat::Slate; RoofPitch = 30.0f; Overhang = 0.2f; Dormers = 6; Chimneys = 4; CentralDome = true;
		Rgb(WallColor, 0.84f, 0.8f, 0.72f); Rgb(UpperColor, 0.95f, 0.88f, 0.68f); Rgb(TrimColor, 0.95f, 0.94f, 0.9f); Rgb(RoofColor, 0.3f, 0.32f, 0.36f);
		Rgb(FrameColor, 0.94f, 0.93f, 0.9f); Rgb(DoorColor, 0.24f, 0.32f, 0.26f); Rgb(MetalColor, 0.36f, 0.6f, 0.52f);
		Weathering = 0.2f; Lanterns = true; Irregularity = 0.05f;
		break;
	case 9:   // Castle Keep
		Shape = Plan::Rectangle; Width = 16.0f; Depth = 14.0f; Floors = 4; FloorHeight = 4.0f; Plinth = 1.2f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Ashlar; Crenellations = true; StringCourse = true;
		WindowStyle = Window::Arched; GroundWindowStyle = Window::Slit; WindowWidth = 0.6f; WindowHeight = 1.4f; WindowSpacing = 4.0f; SillHeight = 1.2f;
		DoorStyle = Door::Arched; DoorWidth = 2.0f; DoorHeight = 3.0f;
		RoofStyle = Roof::Flat; RoofMaterial = RoofMat::Lead; Overhang = 0.0f; Chimneys = 0;
		Towers = 4; TowerKind = TowerShape::Round; TowerRadius = 3.0f; TowerExtra = 5.0f; TowerRoof = Roof::Cone;
		Rgb(WallColor, 0.56f, 0.54f, 0.5f); Rgb(UpperColor, 0.62f, 0.6f, 0.55f); Rgb(TrimColor, 0.6f, 0.58f, 0.53f); Rgb(RoofColor, 0.3f, 0.32f, 0.38f);
		Rgb(DoorColor, 0.3f, 0.2f, 0.12f);
		Weathering = 0.7f; Banners = 2; Lanterns = true; Irregularity = 0.3f; Props = 1; RoofCurve = 0.35f;
		break;
	case 10:  // Watchtower
		Shape = Plan::Round; Width = 6.0f; Depth = 6.0f; Floors = 4; FloorHeight = 3.2f; Plinth = 0.8f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Rubble; StringCourse = true;
		WindowStyle = GroundWindowStyle = Window::Slit; WindowWidth = 0.3f; WindowHeight = 1.0f; WindowSpacing = 3.5f; SillHeight = 1.2f;
		DoorStyle = Door::Arched; DoorWidth = 1.1f; DoorHeight = 2.2f;
		RoofStyle = Roof::Cone; RoofMaterial = RoofMat::Slate; RoofPitch = 60.0f; Overhang = 0.4f; Chimneys = 0;
		Rgb(WallColor, 0.58f, 0.56f, 0.52f); Rgb(UpperColor, 0.58f, 0.56f, 0.52f); Rgb(RoofColor, 0.3f, 0.33f, 0.4f);
		Weathering = 0.75f; Irregularity = 0.45f; Props = 1; RoofCurve = 0.3f;
		break;
	case 11:  // Wizard Tower
		Shape = Plan::Round; Width = 7.0f; Depth = 7.0f; Floors = 6; FloorHeight = 3.0f; Plinth = 0.9f; Taper = 0.3f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Ashlar; StringCourse = true;
		WindowStyle = GroundWindowStyle = Window::Gothic; WindowWidth = 0.6f; WindowHeight = 1.3f; WindowSpacing = 3.0f; SillHeight = 1.0f;
		DoorStyle = Door::Arched; DoorWidth = 1.2f; DoorHeight = 2.3f;
		RoofStyle = Roof::Cone; RoofMaterial = RoofMat::Slate; RoofPitch = 74.0f; Overhang = 0.6f; Chimneys = 0; SpireHeight = 2.5f;
		Rgb(WallColor, 0.5f, 0.5f, 0.56f); Rgb(UpperColor, 0.5f, 0.5f, 0.56f); Rgb(TrimColor, 0.62f, 0.6f, 0.66f); Rgb(RoofColor, 0.34f, 0.24f, 0.52f);
		Rgb(GlassColor, 0.5f, 0.42f, 0.7f); Rgb(MetalColor, 0.8f, 0.66f, 0.3f); Rgb(DoorColor, 0.26f, 0.18f, 0.32f);
		Weathering = 0.5f; Lanterns = true; Rgb(BannerColor, 0.3f, 0.2f, 0.5f); Irregularity = 1.0f; Banners = 1; RoofCurve = 0.5f;
		break;
	case 12:  // Alpine Chalet
		Shape = Plan::Rectangle; Width = 11.0f; Depth = 9.0f; Floors = 3; FloorHeight = 2.8f; Plinth = 0.5f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Planks; Balcony = true;
		WindowStyle = GroundWindowStyle = Window::Rectangular; WindowWidth = 0.9f; WindowHeight = 1.2f; WindowSpacing = 2.4f; SillHeight = 0.9f; Shutters = true; Lattice = false;
		DoorStyle = Door::Plank; DoorWidth = 1.1f; DoorHeight = 2.1f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Shingles; RoofPitch = 24.0f; Overhang = 1.3f; Chimneys = 1;
		Rgb(WallColor, 0.66f, 0.64f, 0.6f); Rgb(UpperColor, 0.5f, 0.34f, 0.2f); Rgb(RoofColor, 0.36f, 0.3f, 0.26f); Rgb(TimberColor, 0.42f, 0.28f, 0.17f);
		Rgb(ShutterColor, 0.24f, 0.42f, 0.26f); Rgb(DoorColor, 0.4f, 0.26f, 0.14f); Rgb(FrameColor, 0.92f, 0.9f, 0.86f);
		Weathering = 0.4f; Brackets = true; Bargeboards = 1; CornerPosts = true; Irregularity = 0.3f; FlowerBoxes = true; Props = 1; RoofCurve = 0.1f;
		break;
	case 13:  // Mediterranean House
		Shape = Plan::LShape; Width = 12.0f; Depth = 8.0f; WingWidth = 0.8f; WingLength = 0.45f; Floors = 2; FloorHeight = 3.0f; Plinth = 0.3f;
		GroundWall = WallMat::Stucco; UpperWall = WallMat::Stucco;
		WindowStyle = GroundWindowStyle = Window::Arched; WindowWidth = 0.9f; WindowHeight = 1.4f; WindowSpacing = 2.8f; SillHeight = 0.9f; Shutters = true; Lattice = false;
		DoorStyle = Door::Arched; DoorWidth = 1.2f; DoorHeight = 2.3f;
		RoofStyle = Roof::Hip; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 20.0f; Overhang = 0.35f; Chimneys = 1;
		Rgb(WallColor, 0.96f, 0.95f, 0.92f); Rgb(UpperColor, 0.96f, 0.95f, 0.92f); Rgb(TrimColor, 0.9f, 0.86f, 0.78f); Rgb(RoofColor, 0.74f, 0.4f, 0.24f);
		Rgb(ShutterColor, 0.16f, 0.36f, 0.62f); Rgb(DoorColor, 0.18f, 0.32f, 0.55f); Rgb(FrameColor, 0.9f, 0.9f, 0.88f);
		Weathering = 0.3f; Irregularity = 0.4f; FlowerBoxes = true; Terrace = true;
		break;
	case 14:  // Market Hall
		Shape = Plan::Rectangle; Width = 16.0f; Depth = 9.0f; Floors = 2; FloorHeight = 3.6f; Plinth = 0.3f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Plaster; Frame = Timber::Chevron; Arcade = true;
		WindowStyle = Window::Mullioned; GroundWindowStyle = Window::Arched; WindowWidth = 1.2f; WindowHeight = 1.3f; WindowSpacing = 2.8f;
		DoorStyle = Door::Arched; DoorWidth = 2.4f; DoorHeight = 3.0f;
		RoofStyle = Roof::Hip; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 50.0f; Overhang = 0.5f; Dormers = 2; Chimneys = 0;
		Rgb(WallColor, 0.7f, 0.66f, 0.58f); Rgb(UpperColor, 0.92f, 0.86f, 0.7f); Rgb(TimberColor, 0.3f, 0.18f, 0.1f); Rgb(RoofColor, 0.55f, 0.28f, 0.2f);
		Weathering = 0.5f; Banners = 3; Lanterns = true; Brackets = true; Irregularity = 0.4f; Props = 2; ShopSign = true; RoofCurve = 0.2f;
		break;
	case 15:  // Blacksmith
		Shape = Plan::Rectangle; Width = 9.0f; Depth = 7.0f; Floors = 1; FloorHeight = 3.2f; Plinth = 0.3f;
		GroundWall = WallMat::Planks; UpperWall = WallMat::Planks;
		WindowStyle = GroundWindowStyle = Window::Rectangular; WindowWidth = 1.2f; WindowHeight = 0.9f; WindowSpacing = 3.2f; SillHeight = 1.2f; Shutters = true; Lattice = false;
		DoorStyle = Door::Double; DoorWidth = 2.4f; DoorHeight = 2.5f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Shingles; RoofPitch = 35.0f; Overhang = 0.6f; Chimneys = 1;
		Rgb(WallColor, 0.44f, 0.34f, 0.24f); Rgb(UpperColor, 0.44f, 0.34f, 0.24f); Rgb(TrimColor, 0.5f, 0.48f, 0.44f); Rgb(RoofColor, 0.34f, 0.3f, 0.28f);
		Rgb(ShutterColor, 0.36f, 0.26f, 0.18f); Rgb(DoorColor, 0.3f, 0.2f, 0.13f);
		Weathering = 0.85f; Porch = true; Brackets = true; CornerPosts = true; Bargeboards = 1; Irregularity = 0.7f; Props = 2; Annex = true; ShopSign = true;
		break;
	case 16:  // Windmill
		Shape = Plan::Round; Width = 7.0f; Depth = 7.0f; Floors = 4; FloorHeight = 3.0f; Plinth = 0.5f; Taper = 0.28f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Plaster;
		WindowStyle = GroundWindowStyle = Window::Rectangular; WindowWidth = 0.6f; WindowHeight = 0.9f; WindowSpacing = 3.6f; SillHeight = 1.1f; Lattice = false;
		DoorStyle = Door::Plank; DoorWidth = 1.2f; DoorHeight = 2.1f;
		RoofStyle = Roof::Dome; RoofMaterial = RoofMat::Shingles; RoofPitch = 45.0f; Overhang = 0.3f; Chimneys = 0; Sails = true;
		Rgb(WallColor, 0.66f, 0.62f, 0.56f); Rgb(UpperColor, 0.92f, 0.9f, 0.84f); Rgb(RoofColor, 0.34f, 0.26f, 0.2f); Rgb(TimberColor, 0.38f, 0.26f, 0.16f);
		Rgb(DoorColor, 0.36f, 0.22f, 0.14f);
		Weathering = 0.5f; Irregularity = 0.4f; Props = 1;
		break;
	case 17:  // Northern Merchant House (북쪽 상업 도시 — 돌 아래층 · 판자 위층 · 가파른 너와 · 조각 박공)
		Shape = Plan::Rectangle; Width = 9.0f; Depth = 11.0f; Floors = 2; FloorHeight = 3.2f; Plinth = 0.6f; Jetty = 0.3f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Planks; CornerPosts = true;
		WindowStyle = Window::Rectangular; GroundWindowStyle = Window::Arched; WindowWidth = 0.9f; WindowHeight = 1.25f; WindowSpacing = 2.5f; SillHeight = 0.95f;
		Shutters = true; Lattice = true;
		DoorStyle = Door::Arched; DoorWidth = 1.4f; DoorHeight = 2.5f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Shingles; RoofPitch = 57.0f; Overhang = 0.85f; Dormers = 0; Chimneys = 1;
		Brackets = true; Bargeboards = 2; Finials = true; PaintedGables = true; Lanterns = true; Banners = 2; Weathering = 0.7f;
		Irregularity = 0.6f; Terrace = true; FlowerBoxes = true; ShopSign = true; Props = 2; RoofCurve = 0.3f;
		Rgb(WallColor, 0.62f, 0.6f, 0.56f); Rgb(UpperColor, 0.46f, 0.33f, 0.22f); Rgb(TrimColor, 0.66f, 0.64f, 0.6f); Rgb(TimberColor, 0.26f, 0.18f, 0.12f);
		Rgb(RoofColor, 0.42f, 0.35f, 0.29f); Rgb(FrameColor, 0.2f, 0.42f, 0.42f); Rgb(ShutterColor, 0.18f, 0.42f, 0.42f); Rgb(DoorColor, 0.3f, 0.2f, 0.13f);
		Rgb(MetalColor, 0.26f, 0.25f, 0.24f); Rgb(BannerColor, 0.56f, 0.12f, 0.1f);
		break;
	case 18:  // Northern Gatehouse (길이 지나가는 문루 — 돌 아치 위 목조 방)
		Shape = Plan::Rectangle; Width = 7.0f; Depth = 9.0f; Floors = 2; FloorHeight = 3.8f; Plinth = 0.2f; Jetty = 0.35f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Planks; Arcade = true; CornerPosts = true;
		WindowStyle = Window::Rectangular; GroundWindowStyle = Window::Arched; WindowWidth = 1.0f; WindowHeight = 1.2f; WindowSpacing = 6.0f; SillHeight = 1.1f;
		Shutters = true; Lattice = true;
		DoorStyle = Door::Arched; DoorWidth = 3.2f; DoorHeight = 3.4f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Shingles; RoofPitch = 52.0f; Overhang = 1.0f; Chimneys = 0;
		Brackets = true; Bargeboards = 2; Finials = true; PaintedGables = true; Lanterns = true; Banners = 2; Weathering = 0.6f;
		Irregularity = 0.45f; Props = 1;
		Rgb(WallColor, 0.66f, 0.64f, 0.6f); Rgb(UpperColor, 0.48f, 0.35f, 0.23f); Rgb(TrimColor, 0.68f, 0.66f, 0.62f); Rgb(TimberColor, 0.24f, 0.17f, 0.11f);
		Rgb(RoofColor, 0.4f, 0.34f, 0.28f); Rgb(FrameColor, 0.2f, 0.42f, 0.42f); Rgb(ShutterColor, 0.55f, 0.16f, 0.12f); Rgb(MetalColor, 0.26f, 0.25f, 0.24f);
		Rgb(BannerColor, 0.15f, 0.32f, 0.5f); RoofCurve = 0.3f;
		break;
	case 19:  // Riverland Townhouse (강가 도시의 좁고 높은 집 — 돌 1 층 · 돌출 반목조 · 내민 창 · 모서리 탑 · 종 모양 기와 지붕)
		Shape = Plan::Rectangle; Width = 8.5f; Depth = 10.0f; Floors = 3; FloorHeight = 2.9f; Plinth = 0.5f; Jetty = 0.4f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Plaster; Frame = Timber::Tudor; TimberWidth = 0.22f; CornerPosts = true;
		WindowStyle = Window::Rectangular; GroundWindowStyle = Window::Arched; WindowWidth = 0.85f; WindowHeight = 1.15f; WindowSpacing = 2.3f; SillHeight = 0.9f;
		Shutters = true; Lattice = true;
		DoorStyle = Door::Arched; DoorWidth = 1.3f; DoorHeight = 2.3f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 58.0f; Overhang = 0.55f; Dormers = 1; Chimneys = 1; RoofCurve = 0.32f;
		Brackets = true; Bargeboards = 1; Finials = true; Turrets = 1; Oriels = 1; Lanterns = true; ShopSign = true; Banners = 1; FlowerBoxes = true; Props = 2;
		Weathering = 0.7f; Irregularity = 0.75f;
		Rgb(WallColor, 0.6f, 0.57f, 0.52f); Rgb(UpperColor, 0.88f, 0.82f, 0.68f); Rgb(TrimColor, 0.66f, 0.63f, 0.58f); Rgb(TimberColor, 0.22f, 0.15f, 0.1f);
		Rgb(RoofColor, 0.6f, 0.3f, 0.2f); Rgb(FrameColor, 0.24f, 0.17f, 0.11f); Rgb(ShutterColor, 0.48f, 0.18f, 0.14f); Rgb(DoorColor, 0.32f, 0.2f, 0.12f);
		Rgb(MetalColor, 0.27f, 0.26f, 0.25f); Rgb(BannerColor, 0.5f, 0.12f, 0.1f);
		break;
	case 20:  // Highland Manor (산골 영주 저택 — 마름돌 · 목조, L 자, 둥근 모서리 탑 · 계단 탑, 굽은 슬레이트 지붕)
		Shape = Plan::LShape; Width = 16.0f; Depth = 9.0f; WingWidth = 0.85f; WingLength = 0.5f; Floors = 2; FloorHeight = 3.4f; Plinth = 0.7f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Planks; Quoins = true; StringCourse = true; CornerPosts = true;
		WindowStyle = Window::Arched; GroundWindowStyle = Window::Mullioned; WindowWidth = 1.0f; WindowHeight = 1.5f; WindowSpacing = 2.8f; SillHeight = 1.0f;
		Shutters = true; Lattice = true;
		DoorStyle = Door::Grand; DoorWidth = 1.8f; DoorHeight = 2.8f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Slate; RoofPitch = 55.0f; Overhang = 0.7f; Dormers = 2; Chimneys = 2; RoofCurve = 0.38f;
		Brackets = true; Bargeboards = 2; Finials = true; PaintedGables = false; Turrets = 2; StairTower = true; Lanterns = true; Banners = 2; Props = 1;
		Weathering = 0.55f; Irregularity = 0.35f;
		Rgb(WallColor, 0.7f, 0.67f, 0.61f); Rgb(UpperColor, 0.4f, 0.29f, 0.19f); Rgb(TrimColor, 0.74f, 0.71f, 0.65f); Rgb(TimberColor, 0.22f, 0.15f, 0.1f);
		Rgb(RoofColor, 0.3f, 0.32f, 0.37f); Rgb(FrameColor, 0.26f, 0.18f, 0.12f); Rgb(ShutterColor, 0.2f, 0.3f, 0.26f); Rgb(DoorColor, 0.28f, 0.18f, 0.11f);
		Rgb(MetalColor, 0.3f, 0.3f, 0.3f); Rgb(BannerColor, 0.16f, 0.24f, 0.42f);
		break;
	case 21:  // Canal House (물 위 도시 — 돌 기초가 물에 서고, 촘촘한 샛기둥 위층이 돌출, 가파른 이끼 기와)
		Shape = Plan::Rectangle; Width = 6.8f; Depth = 9.5f; Floors = 3; FloorHeight = 2.75f; Plinth = 0.45f; Jetty = 0.38f; Canal = 1.1f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Plaster; Frame = Timber::CloseStud; TimberWidth = 0.18f; CornerPosts = true;
		WindowStyle = Window::Mullioned; GroundWindowStyle = Window::Mullioned; WindowWidth = 1.0f; WindowHeight = 1.15f; WindowSpacing = 2.2f; SillHeight = 0.85f; Lattice = true;
		DoorStyle = Door::Plank; DoorWidth = 1.1f; DoorHeight = 2.15f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 56.0f; Overhang = 0.4f; Dormers = 0; Chimneys = 1;
		Brackets = true; Bargeboards = 1; Oriels = 1; Lanterns = true; Props = 1; Weathering = 0.8f; Irregularity = 0.7f; Rustic = 0.85f;
		Rgb(WallColor, 0.6f, 0.59f, 0.56f); Rgb(UpperColor, 0.8f, 0.79f, 0.75f); Rgb(TrimColor, 0.62f, 0.61f, 0.58f); Rgb(TimberColor, 0.24f, 0.2f, 0.17f);
		Rgb(RoofColor, 0.48f, 0.36f, 0.28f); Rgb(FrameColor, 0.2f, 0.17f, 0.14f); Rgb(DoorColor, 0.26f, 0.2f, 0.15f); Rgb(MetalColor, 0.25f, 0.25f, 0.25f);
		Materials = { { "roof", "roof/flat-tiles-red-mossy" }, { "wallLower", "stone/ashlar-weathered" }, { "wallUpper", "plaster/rough-plaster" }, { "timber", "wood/rough-beam-grey" } };
		break;
	case 22:  // Canal Merchant Hall (물가 상인 회관 — 넓은 가게 창, 막돌 1 층, 세 층 돌출, 내민 창 둘)
		Shape = Plan::Rectangle; Width = 11.5f; Depth = 10.0f; Floors = 3; FloorHeight = 2.9f; Plinth = 0.5f; Jetty = 0.45f; Canal = 1.1f;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Plaster; Frame = Timber::CloseStud; TimberWidth = 0.2f; CornerPosts = true; ShopFront = true;
		WindowStyle = Window::Mullioned; GroundWindowStyle = Window::Mullioned; WindowWidth = 1.1f; WindowHeight = 1.2f; WindowSpacing = 2.4f; SillHeight = 0.85f; Lattice = true;
		DoorStyle = Door::Double; DoorWidth = 1.6f; DoorHeight = 2.3f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::Shingles; RoofPitch = 54.0f; Overhang = 0.5f; Dormers = 2; Chimneys = 2;
		Brackets = true; Bargeboards = 1; Oriels = 2; Lanterns = true; ShopSign = true; Banners = 1; Props = 2; Weathering = 0.7f; Irregularity = 0.6f; Rustic = 0.85f;
		Rgb(WallColor, 0.58f, 0.57f, 0.54f); Rgb(UpperColor, 0.82f, 0.8f, 0.74f); Rgb(TrimColor, 0.62f, 0.61f, 0.58f); Rgb(TimberColor, 0.22f, 0.18f, 0.15f);
		Rgb(RoofColor, 0.4f, 0.33f, 0.27f); Rgb(FrameColor, 0.2f, 0.17f, 0.14f); Rgb(DoorColor, 0.28f, 0.2f, 0.14f); Rgb(MetalColor, 0.25f, 0.25f, 0.25f);
		Rgb(BannerColor, 0.42f, 0.14f, 0.12f);
		Materials = { { "roof", "roof/wood-shakes-long" }, { "wallLower", "stone/rubble-wall-grey" }, { "wallUpper", "plaster/patched-plaster" }, { "timber", "wood/weathered-beam" } };
		break;
	case 23:  // Bridge House (길 위에 걸친 집 — 돌 아치로 길이 지나가고, 위는 촘촘한 샛기둥 층)
		Shape = Plan::Rectangle; Width = 7.5f; Depth = 8.0f; Floors = 3; FloorHeight = 2.8f; Plinth = 0.2f; Jetty = 0.4f;
		GroundWall = WallMat::Ashlar; UpperWall = WallMat::Plaster; Frame = Timber::CloseStud; TimberWidth = 0.19f; Arcade = true; CornerPosts = true;
		WindowStyle = Window::Mullioned; GroundWindowStyle = Window::Arched; WindowWidth = 1.0f; WindowHeight = 1.1f; WindowSpacing = 5.5f; SillHeight = 0.85f; Lattice = true;
		DoorStyle = Door::Arched; DoorWidth = 3.2f; DoorHeight = 3.0f;
		RoofStyle = Roof::Gable; RoofMaterial = RoofMat::ClayTiles; RoofPitch = 55.0f; Overhang = 0.45f; Dormers = 1; Chimneys = 1;
		Brackets = true; Bargeboards = 1; Oriels = 1; Lanterns = true; Weathering = 0.65f; Irregularity = 0.55f; Rustic = 0.85f;
		Rgb(WallColor, 0.62f, 0.61f, 0.58f); Rgb(UpperColor, 0.8f, 0.78f, 0.73f); Rgb(TrimColor, 0.64f, 0.63f, 0.6f); Rgb(TimberColor, 0.23f, 0.19f, 0.16f);
		Rgb(RoofColor, 0.46f, 0.34f, 0.27f); Rgb(FrameColor, 0.2f, 0.17f, 0.14f); Rgb(MetalColor, 0.25f, 0.25f, 0.25f);
		Materials = { { "roof", "roof/flat-tiles-red-weathered" }, { "wallLower", "stone/ashlar-weathered" }, { "wallUpper", "plaster/rough-plaster" }, { "timber", "wood/rough-beam-grey" } };
		break;
	case 24:  // Stone Canal Bridge (돌 아치 다리 — 아치 셋, 쐐기돌, 물가름돌, 난간 · 갓돌, 포석 길)
		Shape = Plan::Bridge; Width = 15.0f; Depth = 4.6f; Floors = 1; FloorHeight = 2.0f; Plinth = 0.0f; Canal = 1.2f; Arches = 3;
		GroundWall = WallMat::Rubble; UpperWall = WallMat::Rubble; Chimneys = 0; WindowStyle = GroundWindowStyle = Window::None;
		Weathering = 0.6f; Irregularity = 0.0f; Rustic = 0.9f;
		Rgb(WallColor, 0.6f, 0.59f, 0.56f); Rgb(TrimColor, 0.64f, 0.63f, 0.6f);
		Materials = { { "wallLower", "stone/rubble-wall-grey" }, { "trim", "stone/dressed-granite" }, { "wallUpper", "ground/cobblestone" } };
		break;
	}
	// 고전 양식 (로마 · 바로크 · 지중해) 은 반듯하게 다듬은 돌
	if (Style == 6 || Style == 7 || Style == 8 || Style == 13)
		Rustic = 0.3f;
	// 한 층짜리 (같은 벽 재질) 는 박공도 벽과 같은 색
	if (Floors == 1 && UpperWall == GroundWall)
		Rgb(UpperColor, WallColor[0], WallColor[1], WallColor[2]);
}

void BuildingDesc::Randomize(int seed)
{
	// 같은 양식으로 돌린 뒤 크기 · 층 · 비율 · 색을 조금씩 (마을을 만들 때 같은 집이 줄지어 보이지 않게)
	const auto materials = Materials;   // 고른 재질은 그대로
	ApplyStyle(Style);
	Materials = materials;
	Seed = seed;
	std::mt19937 rng((uint32_t)seed * 2654435761u + 977u);
	std::uniform_real_distribution<float> u(0.0f, 1.0f);
	auto jitter = [&](float v, float amount) { return v * (1.0f + (u(rng) * 2.0f - 1.0f) * amount); };
	Width = jitter(Width, 0.18f);
	Depth = jitter(Depth, 0.12f);
	FloorHeight = jitter(FloorHeight, 0.05f);
	if (Shape != Plan::Round && Floors >= 2 && u(rng) < 0.35f)
		Floors += u(rng) < 0.5f ? -1 : 1;
	RoofPitch = std::clamp(jitter(RoofPitch, 0.1f), 10.0f, 80.0f);
	Dormers = Dormers > 0 ? (std::max)(0, Dormers + (int)(u(rng) * 3.0f) - 1) : 0;
	if (Chimneys > 0)
		Chimneys = (std::max)(1, Chimneys + (u(rng) < 0.3f ? 1 : 0));
	auto tint = [&](float* c, float amount) {
		const float k = 1.0f + (u(rng) * 2.0f - 1.0f) * amount;
		for (int i = 0; i < 3; ++i)
			c[i] = std::clamp(c[i] * k * (1.0f + (u(rng) * 2.0f - 1.0f) * amount * 0.4f), 0.0f, 1.0f);
	};
	tint(WallColor, 0.1f); tint(UpperColor, 0.1f); tint(RoofColor, 0.12f); tint(TimberColor, 0.15f);
	// 덧창 · 문 색은 몇 가지 중 하나 (중세 마을 — 붉은 · 초록 · 파랑 · 나무색)
	static const float kPaint[][3] = { { 0.52f, 0.2f, 0.16f }, { 0.22f, 0.38f, 0.24f }, { 0.2f, 0.32f, 0.5f }, { 0.4f, 0.28f, 0.18f }, { 0.55f, 0.46f, 0.2f } };
	const int pick = (int)(u(rng) * 5.0f) % 5;
	if (Shutters)
		Rgb(ShutterColor, kPaint[pick][0], kPaint[pick][1], kPaint[pick][2]);
	if (u(rng) < 0.5f)
		Rgb(DoorColor, kPaint[(pick + 1) % 5][0] * 0.85f, kPaint[(pick + 1) % 5][1] * 0.85f, kPaint[(pick + 1) % 5][2] * 0.85f);
}

// ================================================================== 저장
namespace
{
	json C(const float* c) { return { c[0], c[1], c[2] }; }
	void ReadC(const json& j, const char* key, float* c)
	{
		if (j.contains(key) && j[key].is_array() && j[key].size() >= 3)
			for (int i = 0; i < 3; ++i)
				c[i] = j[key][i].get<float>();
	}
}

json BuildingDesc::ToJson() const
{
	json j;
	j["style"] = Style; j["seed"] = Seed;
	j["shape"] = { { "plan", (int)Shape }, { "width", Width }, { "depth", Depth }, { "wingWidth", WingWidth }, { "wingLength", WingLength },
		{ "floors", Floors }, { "floorHeight", FloorHeight }, { "plinth", Plinth }, { "jetty", Jetty }, { "taper", Taper } };
	j["towers"] = { { "count", Towers }, { "kind", (int)TowerKind }, { "radius", TowerRadius }, { "extra", TowerExtra }, { "roof", (int)TowerRoof } };
	j["walls"] = { { "ground", (int)GroundWall }, { "upper", (int)UpperWall }, { "timber", (int)Frame }, { "timberWidth", TimberWidth },
		{ "quoins", Quoins }, { "stringCourse", StringCourse }, { "cornice", Cornice }, { "pilasters", Pilasters }, { "buttresses", Buttresses },
		{ "crenellations", Crenellations }, { "portico", Portico }, { "porticoColumns", PorticoColumns }, { "peristyle", Peristyle },
		{ "arcade", Arcade }, { "balcony", Balcony } };
	j["windows"] = { { "style", (int)WindowStyle }, { "groundStyle", (int)GroundWindowStyle }, { "width", WindowWidth }, { "height", WindowHeight },
		{ "spacing", WindowSpacing }, { "sill", SillHeight }, { "shutters", Shutters }, { "lattice", Lattice }, { "pediments", WindowPediments },
		{ "rose", RoseWindow }, { "door", (int)DoorStyle }, { "doorWidth", DoorWidth }, { "doorHeight", DoorHeight } };
	j["roof"] = { { "style", (int)RoofStyle }, { "material", (int)RoofMaterial }, { "pitch", RoofPitch }, { "overhang", Overhang },
		{ "crowSteps", CrowSteps }, { "dormers", Dormers }, { "chimneys", Chimneys }, { "dragonHeads", DragonHeads }, { "spire", SpireHeight },
		{ "dome", CentralDome }, { "sails", Sails }, { "brackets", Brackets }, { "bargeboards", Bargeboards }, { "finials", Finials },
		{ "paintedGables", PaintedGables } };
	j["details"] = { { "cornerPosts", CornerPosts }, { "porch", Porch }, { "lanterns", Lanterns }, { "banners", Banners }, { "weathering", Weathering },
		{ "irregularity", Irregularity }, { "annex", Annex }, { "terrace", Terrace }, { "flowerBoxes", FlowerBoxes }, { "shopSign", ShopSign }, { "props", Props },
		{ "turrets", Turrets }, { "stairTower", StairTower }, { "oriels", Oriels } };
	j["roof"]["curve"] = RoofCurve;
	j["shape"]["canal"] = Canal; j["shape"]["arches"] = Arches;
	j["windows"]["shopFront"] = ShopFront;
	j["details"]["rustic"] = Rustic;
	j["colors"] = { { "wall", C(WallColor) }, { "upper", C(UpperColor) }, { "trim", C(TrimColor) }, { "timber", C(TimberColor) }, { "roof", C(RoofColor) },
		{ "frame", C(FrameColor) }, { "shutter", C(ShutterColor) }, { "door", C(DoorColor) }, { "glass", C(GlassColor) }, { "metal", C(MetalColor) },
		{ "ornament", C(OrnamentColor) }, { "banner", C(BannerColor) } };
	j["materials"] = json::object();
	for (const auto& kv : Materials)
		if (!kv.second.empty())
			j["materials"][kv.first] = kv.second;
	return j;
}

void BuildingDesc::FromJson(const json& j)
{
	// 양식만 있으면 (손으로 쓴 값) 그 양식에서 시작
	if (j.contains("style") && !j.contains("shape"))
	{
		Seed = j.value("seed", Seed);
		ApplyStyle(j.value("style", 0));
		return;
	}
	Style = j.value("style", Style); Seed = j.value("seed", Seed);
	auto e = [](const json& o, const char* k, int v, int count) { return std::clamp(o.value(k, v), 0, count - 1); };
	if (j.contains("shape"))
	{
		const json& s = j["shape"];
		Shape = (Plan)e(s, "plan", (int)Shape, (int)Plan::Count);
		Width = s.value("width", Width); Depth = s.value("depth", Depth); WingWidth = s.value("wingWidth", WingWidth); WingLength = s.value("wingLength", WingLength);
		Floors = s.value("floors", Floors); FloorHeight = s.value("floorHeight", FloorHeight); Plinth = s.value("plinth", Plinth);
		Jetty = s.value("jetty", Jetty); Taper = s.value("taper", Taper);
		Canal = std::clamp(s.value("canal", Canal), 0.0f, 6.0f); Arches = std::clamp(s.value("arches", Arches), 1, 7);
	}
	if (j.contains("towers"))
	{
		const json& t = j["towers"];
		Towers = t.value("count", Towers); TowerKind = (TowerShape)e(t, "kind", (int)TowerKind, (int)TowerShape::Count);
		TowerRadius = t.value("radius", TowerRadius); TowerExtra = t.value("extra", TowerExtra); TowerRoof = (Roof)e(t, "roof", (int)TowerRoof, (int)Roof::Count);
	}
	if (j.contains("walls"))
	{
		const json& w = j["walls"];
		GroundWall = (WallMat)e(w, "ground", (int)GroundWall, (int)WallMat::Count); UpperWall = (WallMat)e(w, "upper", (int)UpperWall, (int)WallMat::Count);
		Frame = (Timber)e(w, "timber", (int)Frame, (int)Timber::Count); TimberWidth = w.value("timberWidth", TimberWidth);
		Quoins = w.value("quoins", Quoins); StringCourse = w.value("stringCourse", StringCourse); Cornice = w.value("cornice", Cornice);
		Pilasters = w.value("pilasters", Pilasters); Buttresses = w.value("buttresses", Buttresses); Crenellations = w.value("crenellations", Crenellations);
		Portico = w.value("portico", Portico); PorticoColumns = w.value("porticoColumns", PorticoColumns); Peristyle = w.value("peristyle", Peristyle);
		Arcade = w.value("arcade", Arcade); Balcony = w.value("balcony", Balcony);
	}
	if (j.contains("windows"))
	{
		const json& w = j["windows"];
		WindowStyle = (Window)e(w, "style", (int)WindowStyle, (int)Window::Count); GroundWindowStyle = (Window)e(w, "groundStyle", (int)GroundWindowStyle, (int)Window::Count);
		WindowWidth = w.value("width", WindowWidth); WindowHeight = w.value("height", WindowHeight); WindowSpacing = w.value("spacing", WindowSpacing);
		SillHeight = w.value("sill", SillHeight); Shutters = w.value("shutters", Shutters); Lattice = w.value("lattice", Lattice);
		WindowPediments = w.value("pediments", WindowPediments); RoseWindow = w.value("rose", RoseWindow); ShopFront = w.value("shopFront", ShopFront);
		DoorStyle = (Door)e(w, "door", (int)DoorStyle, (int)Door::Count); DoorWidth = w.value("doorWidth", DoorWidth); DoorHeight = w.value("doorHeight", DoorHeight);
	}
	if (j.contains("roof"))
	{
		const json& r = j["roof"];
		RoofStyle = (Roof)e(r, "style", (int)RoofStyle, (int)Roof::Count); RoofMaterial = (RoofMat)e(r, "material", (int)RoofMaterial, (int)RoofMat::Count);
		RoofPitch = r.value("pitch", RoofPitch); Overhang = r.value("overhang", Overhang); CrowSteps = r.value("crowSteps", CrowSteps);
		Dormers = r.value("dormers", Dormers); Chimneys = r.value("chimneys", Chimneys); DragonHeads = r.value("dragonHeads", DragonHeads);
		SpireHeight = r.value("spire", SpireHeight); CentralDome = r.value("dome", CentralDome); Sails = r.value("sails", Sails);
		Brackets = r.value("brackets", Brackets); Bargeboards = std::clamp(r.value("bargeboards", Bargeboards), 0, 2); Finials = r.value("finials", Finials);
		PaintedGables = r.value("paintedGables", PaintedGables); RoofCurve = std::clamp(r.value("curve", RoofCurve), 0.0f, 0.6f);
	}
	if (j.contains("details"))
	{
		const json& t = j["details"];
		CornerPosts = t.value("cornerPosts", CornerPosts); Porch = t.value("porch", Porch); Lanterns = t.value("lanterns", Lanterns);
		Banners = std::clamp(t.value("banners", Banners), 0, 8); Weathering = std::clamp(t.value("weathering", Weathering), 0.0f, 1.0f);
		Irregularity = std::clamp(t.value("irregularity", Irregularity), 0.0f, 1.0f); Annex = t.value("annex", Annex); Terrace = t.value("terrace", Terrace);
		FlowerBoxes = t.value("flowerBoxes", FlowerBoxes); ShopSign = t.value("shopSign", ShopSign); Props = std::clamp(t.value("props", Props), 0, 2);
		Turrets = std::clamp(t.value("turrets", Turrets), 0, 2); StairTower = t.value("stairTower", StairTower); Oriels = std::clamp(t.value("oriels", Oriels), 0, 3);
		Rustic = std::clamp(t.value("rustic", Rustic), 0.0f, 1.0f);
	}
	if (j.contains("colors"))
	{
		const json& c = j["colors"];
		ReadC(c, "wall", WallColor); ReadC(c, "upper", UpperColor); ReadC(c, "trim", TrimColor); ReadC(c, "timber", TimberColor); ReadC(c, "roof", RoofColor);
		ReadC(c, "frame", FrameColor); ReadC(c, "shutter", ShutterColor); ReadC(c, "door", DoorColor); ReadC(c, "glass", GlassColor); ReadC(c, "metal", MetalColor);
		ReadC(c, "ornament", OrnamentColor); ReadC(c, "banner", BannerColor);
	}
	if (j.contains("materials") && j["materials"].is_object())
	{
		Materials.clear();
		for (auto it = j["materials"].begin(); it != j["materials"].end(); ++it)
			if (it.value().is_string() && !it.value().get<std::string>().empty())
				Materials[it.key()] = it.value().get<std::string>();
	}
}

size_t BuildingDesc::Hash() const
{
	return std::hash<std::string>()(ToJson().dump());
}
