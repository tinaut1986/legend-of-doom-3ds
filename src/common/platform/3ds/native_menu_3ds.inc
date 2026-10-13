// Native menus for the touch screen (PLAN U1), included by diagnostics_3ds.cpp.
//
// The engine keeps every menu's logic: selection, what a button does to an item, sounds,
// cvars. What changes is the drawing. Instead of rendering the engine's menu on the top
// canvas, pixel-diffing it out and rescaling it on the CPU (DrawNativeMenuBottomFrame), the
// menus listed in NativeMenuSupported() are drawn here, straight into the bottom LCD, with
// the game's own NES font (the SMALLFNT lump) at integer scales: x2 for the short list menus
// (main, pause, skill...), x1 with 20-pixel rows for OPTIONS. OPTIONS is one screen with tabs
// (the four Legend*Options menus; L/R in their ZScript, a tap here). The top screen shows the
// game as it was when the menu opened, dimmed and frozen: the 3D view is not rendered again
// while a native menu is open (I_3DSNativeMenuFrozen, D_Display).
//
// Save/load and yes/no message boxes are drawn here too (list and dialogs below, preview and
// description on the top screen). Menus this does not know (update, controls, the keyboard)
// keep the old route.

constexpr FOverlayColor NesWhite{ 252, 252, 252 };
constexpr FOverlayColor NesRed{ 216, 40, 0 };
constexpr FOverlayColor NesGold{ 252, 188, 60 };
constexpr FOverlayColor NesBlue{ 0, 88, 248 };
constexpr FOverlayColor NesGray{ 116, 116, 116 };
constexpr FOverlayColor NesPanel{ 36, 24, 140 };
constexpr FOverlayColor NesGreen{ 0, 168, 0 };
constexpr FOverlayColor NesOff{ 60, 60, 60 };
constexpr FOverlayColor NesTabIdle{ 20, 20, 20 };
constexpr FOverlayColor NesHintBar{ 16, 16, 16 };

// Defined further down in diagnostics_3ds.cpp.
void NativeTopRect(unsigned char *pixels, int pitchBytes, int width, int height,
	int x, int y, int rectWidth, int rectHeight, FOverlayColor color, unsigned alpha);

// ---- The NES font ----------------------------------------------------------------------

// 8x8 cells, one byte per row, bit 7 the leftmost pixel.
struct FNesFont
{
	bool Loaded = false;
	bool Valid = false;
	uint8_t Rows[128][8] = {};
};
FNesFont NesFont;

// Shapes the NES font does not have (its '<', '>' and '%' are other symbols).
const struct { char Character; uint8_t Rows[8]; } NesExtraGlyphs[] = {
	{ '<', { 0x0c, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0c, 0x00 } },
	{ '>', { 0x60, 0x30, 0x18, 0x0c, 0x18, 0x30, 0x60, 0x00 } },
	{ '%', { 0xc6, 0xcc, 0x18, 0x30, 0x60, 0xcc, 0x8c, 0x00 } },
};

// FON2 (ZDoom's font lump): header, widths, palette, then each glyph ByteRun1-packed.
void LoadNesFont()
{
	if (NesFont.Loaded) return;
	NesFont.Loaded = true;
	const int lump = fileSystem.CheckNumForName("SMALLFNT");
	if (lump < 0) return;
	FileData file = fileSystem.ReadFile(lump);
	const auto *data = static_cast<const uint8_t *>(file.GetMem());
	const size_t size = file.GetSize();
	if (data == nullptr || size < 12 || std::memcmp(data, "FON2", 4) != 0) return;
	const int height = data[4] | (data[5] << 8);
	const int first = data[6];
	const int last = data[7];
	const bool constantWidth = data[8] != 0;
	const int paletteSize = data[10];
	const bool kerning = (data[11] & 1) != 0;
	if (height <= 0 || height > 64 || last < first) return;
	size_t at = 12 + (kerning ? 2 : 0);
	const int count = last - first + 1;
	int widths[256] = {};
	for (int i = 0; i < count; ++i)
	{
		if (constantWidth && i > 0) { widths[i] = widths[0]; continue; }
		if (at + 2 > size) return;
		widths[i] = data[at] | (data[at + 1] << 8);
		at += 2;
	}
	at += static_cast<size_t>(paletteSize + 1) * 3;
	std::vector<uint8_t> pixels;
	for (int i = 0; i < count; ++i)
	{
		const int width = widths[i];
		if (width == 0) continue;
		const size_t total = static_cast<size_t>(width) * height;
		pixels.clear();
		while (pixels.size() < total)
		{
			if (at >= size) return;
			const int code = static_cast<int8_t>(data[at++]);
			if (code >= 0)
			{
				for (int n = 0; n <= code && at < size; ++n) pixels.push_back(data[at++]);
			}
			else if (code != -128)
			{
				if (at >= size) return;
				pixels.insert(pixels.end(), static_cast<size_t>(1 - code), data[at++]);
			}
		}
		const int character = first + i;
		if (character < 0 || character >= 128) continue;
		for (int y = 0; y < std::min(height, 8); ++y)
			for (int x = 0; x < std::min(width, 8); ++x)
				if (pixels[static_cast<size_t>(y) * width + x] != 0)
					NesFont.Rows[character][y] |= static_cast<uint8_t>(0x80u >> x);
	}
	for (const auto &glyph : NesExtraGlyphs)
		std::memcpy(NesFont.Rows[static_cast<int>(glyph.Character)], glyph.Rows, 8);
	NesFont.Valid = true;
}

int NesTextWidth(const char *text, int scale)
{
	return text == nullptr ? 0 : static_cast<int>(std::strlen(text)) * 8 * scale;
}

void NesText(unsigned char *framebuffer, int x, int y, const char *text, int scale,
	FOverlayColor color)
{
	if (text == nullptr) return;
	for (; *text != '\0'; ++text, x += 8 * scale)
	{
		int character = std::toupper(static_cast<unsigned char>(*text));
		if (character < 0 || character >= 128) continue;
		const uint8_t *rows = NesFont.Rows[character];
		for (int row = 0; row < 8; ++row)
			for (int column = 0; column < 8; ++column)
				if (rows[row] & (0x80u >> column))
					OverlayRect(framebuffer, x + column * scale, y + row * scale, scale, scale, color);
	}
}

void NesCenteredText(unsigned char *framebuffer, int y, const char *text, int scale,
	FOverlayColor color)
{
	NesText(framebuffer, (static_cast<int>(BottomScreenWidth) - NesTextWidth(text, scale)) / 2,
		y, text, scale, color);
}

void NesHeart(unsigned char *framebuffer, int x, int y, int scale)
{
	static constexpr uint8_t Rows[] = { 0x6c, 0xfe, 0xfe, 0x7c, 0x38, 0x10 };
	for (int row = 0; row < 6; ++row)
		for (int column = 0; column < 7; ++column)
			if (Rows[row] & (0x80u >> column))
				OverlayRect(framebuffer, x + column * scale, y + row * scale, scale, scale, NesRed);
}

void NesBox(unsigned char *framebuffer, int x, int y, int width, int height, FOverlayColor fill,
	FOverlayColor border)
{
	OverlayRect(framebuffer, x, y, width, height, fill);
	OverlayFrame(framebuffer, x, y, width, height, 1, border);
}

void NesHint(unsigned char *framebuffer, const char *text)
{
	OverlayRect(framebuffer, 0, 228, BottomScreenWidth, 12, NesHintBar);
	NesCenteredText(framebuffer, 230, text, 1, NesGray);
}

// ---- Touch zones -----------------------------------------------------------------------

enum class ENativeTouch : uint8_t
{
	Activate, // select the item and press A on it
	Left,     // select the item and press left
	Right,    // select the item and press right
	Slider,   // select the item and set it from where the bar was touched
	Tab,      // open another OPTIONS tab
	Save,     // load/save list: select the save and press A
	Choice,   // a dialog's choice (Item 0 or 1): set it and press A
};

struct FNativeTouchZone
{
	int16_t X0, Y0, X1, Y1;
	ENativeTouch Action;
	int16_t Item;
};

std::array<FNativeTouchZone, 48> NativeTouchZones;
unsigned NativeTouchZoneCount;
// The last bottom frame was drawn here, so taps go through the zones above.
bool NativeMenuDrawnHere;

void AddNativeTouchZone(int x0, int y0, int x1, int y1, ENativeTouch action, int item)
{
	if (NativeTouchZoneCount >= NativeTouchZones.size()) return;
	NativeTouchZones[NativeTouchZoneCount++] = { static_cast<int16_t>(x0), static_cast<int16_t>(y0),
		static_cast<int16_t>(x1), static_cast<int16_t>(y1), action, static_cast<int16_t>(item) };
}

// ---- What a menu shows -------------------------------------------------------------------

constexpr const char *OptionTabMenus[] = {
	"LegendDisplayOptions", "LegendSoundOptions", "LegendControllerOptions", "LegendDeveloperOptions" };
constexpr const char *OptionTabTitles[] = { "VIDEO", "SOUND", "CONTROL", "DEBUG" };
constexpr int OptionTabCount = 4;

int OptionTabIndex(FName menu)
{
	for (int tab = 0; tab < OptionTabCount; ++tab)
		if (menu == FName(OptionTabMenus[tab])) return tab;
	return -1;
}

const char *Localized(const FString &text)
{
	return GStrings.localize(text.GetChars());
}

enum class ENativeRowKind : uint8_t { Header, Toggle, Choice, Slider, Submenu, Command };

struct FNativeRow
{
	ENativeRowKind Kind;
	int Item;
	FString Label;
	FString Value;
	float Fraction; // sliders
};

FBaseCVar *ItemCVar(DMenuItemBase *item)
{
	return item->mAction == NAME_None ? nullptr : FindCVar(item->mAction.GetChars(), nullptr);
}

float CVarFloat(FBaseCVar *cvar)
{
	return cvar == nullptr ? 0.0f : cvar->GetGenericRep(CVAR_Float).Float;
}

// One option item as a row; false when the item is of a kind not drawn here.
bool DescribeOptionItem(DMenuItemBase *item, int index, FNativeRow &row)
{
	row.Item = index;
	row.Label = Localized(item->StringVar(FName("mLabel")));
	row.Fraction = 0.0f;
	FBaseCVar *cvar = ItemCVar(item);
	if (item->IsKindOf("OptionMenuItemStaticText"))
	{
		row.Kind = ENativeRowKind::Header;
		return true;
	}
	if (item->IsKindOf("OptionMenuItemRenderScaleSlider"))
	{
		row.Kind = ENativeRowKind::Choice;
		row.Label = "RENDER";
		row.Value.Format("%d%%", static_cast<int>(std::lround(CVarFloat(cvar) * 10.0f)));
		return true;
	}
	if (item->IsKindOf("OptionMenuItemRenderDistanceSlider"))
	{
		static constexpr const char *Levels[] = { "LOW", "NORMAL", "HIGH" };
		row.Kind = ENativeRowKind::Choice;
		row.Label = "DISTANCE";
		row.Value = Levels[std::clamp(static_cast<int>(std::lround(CVarFloat(cvar))), 0, 2)];
		return true;
	}
	if (item->IsKindOf("OptionMenuSliderBase"))
	{
		const double minimum = item->FloatVar(FName("mMin"));
		const double maximum = item->FloatVar(FName("mMax"));
		row.Kind = ENativeRowKind::Slider;
		row.Fraction = maximum > minimum ? static_cast<float>(std::clamp(
			(CVarFloat(cvar) - minimum) / (maximum - minimum), 0.0, 1.0)) : 0.0f;
		return true;
	}
	if (item->IsKindOf("OptionMenuItemOption"))
	{
		const FName valuesName = item->NameVar(FName("mValues"));
		FOptionValues **values = OptionValues.CheckKey(valuesName);
		const float current = CVarFloat(cvar);
		if (values == nullptr || *values == nullptr || (*values)->mValues.Size() == 0) return false;
		const auto &pairs = (*values)->mValues;
		unsigned best = 0;
		for (unsigned i = 1; i < pairs.Size(); ++i)
			if (std::fabs(pairs[i].Value - current) < std::fabs(pairs[best].Value - current)) best = i;
		row.Value = Localized(pairs[best].Text);
		row.Value.ToUpper();
		const bool onOff = pairs.Size() == 2 && (row.Value.CompareNoCase("ON") == 0 ||
			row.Value.CompareNoCase("OFF") == 0 || row.Value.CompareNoCase("YES") == 0 ||
			row.Value.CompareNoCase("NO") == 0);
		if (onOff)
		{
			row.Kind = ENativeRowKind::Toggle;
			row.Fraction = current != 0.0f ? 1.0f : 0.0f;
			row.Value = current != 0.0f ? "ON" : "OFF";
		}
		else
		{
			row.Kind = ENativeRowKind::Choice;
		}
		return true;
	}
	if (item->IsKindOf("OptionMenuItemCommand"))
	{
		row.Kind = ENativeRowKind::Command;
		return true;
	}
	if (item->IsKindOf("OptionMenuItemSubmenu"))
	{
		row.Kind = ENativeRowKind::Submenu;
		return true;
	}
	return false;
}

// A list menu's text: its selectable text items and its static text (the title). False when
// it has items drawn as pictures, which only the engine can show.
bool DescribeListMenu(DListMenuDescriptor *descriptor, TArray<FNativeRow> &rows, FString &title)
{
	for (unsigned i = 0; i < descriptor->mItems.Size(); ++i)
	{
		DMenuItemBase *item = descriptor->mItems[i];
		if (item == nullptr) continue;
		if (item->IsKindOf("ListMenuItemPatchItem")) return false;
		if (item->IsKindOf("ListMenuItemTextItem"))
		{
			FNativeRow row{ ENativeRowKind::Command, static_cast<int>(i),
				Localized(item->StringVar(FName("mText"))), "", 0.0f };
			rows.Push(row);
		}
		else if (item->IsKindOf("ListMenuItemStaticText") && title.IsEmpty())
		{
			title = Localized(item->StringVar(FName("mText")));
		}
	}
	return rows.Size() > 0;
}

bool DescribeOptionMenu(DOptionMenuDescriptor *descriptor, TArray<FNativeRow> &rows)
{
	for (unsigned i = 0; i < descriptor->mItems.Size(); ++i)
	{
		DMenuItemBase *item = descriptor->mItems[i];
		if (item == nullptr) continue;
		FNativeRow row;
		if (!DescribeOptionItem(item, static_cast<int>(i), row)) return false;
		rows.Push(row);
	}
	return rows.Size() > 0;
}

// Whether the open menu is one drawn here.
bool NativeMenuSupported()
{
	if (CurrentMenu == nullptr || menuactive == MENU_Off || DiagnosticProgressActive) return false;
	if (CurrentMenu->IsKindOf("ListMenu"))
	{
		auto *descriptor = CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc"));
		if (descriptor == nullptr) return false;
		TArray<FNativeRow> rows;
		FString title;
		return DescribeListMenu(descriptor, rows, title);
	}
	if (CurrentMenu->IsKindOf("LoadSaveMenu")) return true;
	if (CurrentMenu->IsKindOf("MessageBoxMenu"))
		return CurrentMenu->IntVar(FName("mMessageMode")) == 0;
	if (CurrentMenu->GetClass()->TypeName == FName("LegendSplitOptionMenu"))
	{
		auto *descriptor = CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
		if (descriptor == nullptr || OptionTabIndex(descriptor->mMenuName) < 0) return false;
		TArray<FNativeRow> rows;
		return DescribeOptionMenu(descriptor, rows);
	}
	return false;
}

// ---- Drawing -------------------------------------------------------------------------------

void DrawNativeListMenu(unsigned char *framebuffer, DListMenuDescriptor *descriptor)
{
	TArray<FNativeRow> rows;
	FString title;
	DescribeListMenu(descriptor, rows, title);
	if (descriptor->mMenuName == FName("LegendPauseMenu")) title = "PAUSED";

	if (gamestate == GS_DEMOSCREEN) DrawMenuBottomScreen(framebuffer, 71u);

	// x2 unless the longest label does not fit; then x1 with the same row pitch.
	int widest = 0;
	for (const auto &row : rows) widest = std::max(widest, NesTextWidth(row.Label.GetChars(), 2));
	const int scale = widest <= 200 ? 2 : 1;
	if (scale == 1) widest /= 2;
	const int rowPitch = scale == 2 ? 25 : 20;
	const int listHeight = static_cast<int>(rows.Size()) * rowPitch;
	const int titleHeight = title.IsNotEmpty() ? 30 : 0;
	int y = std::max(8, (226 - listHeight - titleHeight) / 2) + 5;
	if (title.IsNotEmpty())
	{
		NesCenteredText(framebuffer, y, title.GetChars(), 2, NesRed);
		y += titleHeight;
	}
	const int boxWidth = std::min(304, std::max(240, widest + 64));
	const int boxX = (static_cast<int>(BottomScreenWidth) - boxWidth) / 2;
	const int textX = boxX + 36;
	for (const auto &row : rows)
	{
		const bool selected = descriptor->mSelectedItem == row.Item;
		const int glyphHeight = 8 * scale;
		if (selected)
		{
			NesBox(framebuffer, boxX, y - 5, boxWidth, glyphHeight + 10, NesPanel, NesBlue);
			NesHeart(framebuffer, boxX + 12, y + (glyphHeight - 6 * scale) / 2, scale);
		}
		NesText(framebuffer, textX, y, row.Label.GetChars(), scale, selected ? NesGold : NesWhite);
		AddNativeTouchZone(boxX, y - 5, boxX + boxWidth, y - 5 + rowPitch, ENativeTouch::Activate, row.Item);
		y += rowPitch;
	}
	NesHint(framebuffer, descriptor->mMenuName == FName("LegendPauseMenu") ?
		"A SELECT   B RESUME" : "A SELECT   B BACK");
}

void DrawNativeOptionMenu(unsigned char *framebuffer, DOptionMenuDescriptor *descriptor)
{
	TArray<FNativeRow> rows;
	DescribeOptionMenu(descriptor, rows);
	const int currentTab = OptionTabIndex(descriptor->mMenuName);

	for (int tab = 0; tab < OptionTabCount; ++tab)
	{
		const bool active = tab == currentTab;
		const int x = 8 + tab * 76;
		NesBox(framebuffer, x, 6, 72, 16, active ? NesPanel : NesTabIdle, active ? NesBlue : NesGray);
		NesText(framebuffer, x + (72 - NesTextWidth(OptionTabTitles[tab], 1)) / 2, 10,
			OptionTabTitles[tab], 1, active ? NesGold : NesGray);
		AddNativeTouchZone(x, 0, x + 76, 28, ENativeTouch::Tab, tab);
	}

	// Nine rows fit; past that the list scrolls to keep the selection in view.
	constexpr int FirstRowY = 36;
	constexpr int RowPitch = 20;
	constexpr int VisibleRows = 9;
	int selectedRow = 0;
	for (unsigned i = 0; i < rows.Size(); ++i)
		if (rows[i].Item == descriptor->mSelectedItem) selectedRow = static_cast<int>(i);
	const int firstRow = std::clamp(selectedRow - VisibleRows + 1, 0,
		std::max(0, static_cast<int>(rows.Size()) - VisibleRows));

	for (int i = firstRow; i < std::min<int>(rows.Size(), firstRow + VisibleRows); ++i)
	{
		const FNativeRow &row = rows[i];
		const int y = FirstRowY + (i - firstRow) * RowPitch;
		const bool selected = row.Item == descriptor->mSelectedItem;
		if (row.Kind == ENativeRowKind::Header)
		{
			NesText(framebuffer, 16, y, row.Label.GetChars(), 1, NesGray);
			continue;
		}
		if (selected) NesBox(framebuffer, 8, y - 5, 304, 18, NesPanel, NesBlue);
		NesText(framebuffer, 16, y, row.Label.GetChars(), 1, selected ? NesGold : NesWhite);
		const int zoneTop = y - 5;
		const int zoneBottom = y - 5 + RowPitch;
		switch (row.Kind)
		{
		case ENativeRowKind::Toggle:
		{
			const bool on = row.Fraction > 0.5f;
			OverlayRect(framebuffer, 270, y - 2, 34, 12, on ? NesGreen : NesOff);
			NesText(framebuffer, 270 + (34 - NesTextWidth(row.Value.GetChars(), 1)) / 2, y,
				row.Value.GetChars(), 1, NesWhite);
			AddNativeTouchZone(8, zoneTop, 312, zoneBottom, ENativeTouch::Activate, row.Item);
			break;
		}
		case ENativeRowKind::Choice:
		{
			const int width = NesTextWidth(row.Value.GetChars(), 1);
			const int leftArrow = 296 - width - 14;
			NesText(framebuffer, 296 - width, y, row.Value.GetChars(), 1, NesWhite);
			NesText(framebuffer, leftArrow, y, "<", 1, NesGray);
			NesText(framebuffer, 298, y, ">", 1, NesGray);
			AddNativeTouchZone(leftArrow - 12, zoneTop, leftArrow + 12, zoneBottom, ENativeTouch::Left, row.Item);
			AddNativeTouchZone(290, zoneTop, 320, zoneBottom, ENativeTouch::Right, row.Item);
			AddNativeTouchZone(8, zoneTop, 312, zoneBottom, ENativeTouch::Right, row.Item);
			break;
		}
		case ENativeRowKind::Slider:
		{
			// Ten cells in half steps: a volume step (0.05) is half a cell.
			constexpr int Segments = 10;
			const int halves = static_cast<int>(std::lround(row.Fraction * Segments * 2));
			for (int segment = 0; segment < Segments; ++segment)
			{
				const int x = 224 + segment * 8;
				OverlayRect(framebuffer, x, y, 6, 8, NesOff);
				if (halves >= (segment + 1) * 2) OverlayRect(framebuffer, x, y, 6, 8, NesRed);
				else if (halves == segment * 2 + 1) OverlayRect(framebuffer, x, y, 3, 8, NesRed);
			}
			AddNativeTouchZone(220, zoneTop, 308, zoneBottom, ENativeTouch::Slider, row.Item);
			AddNativeTouchZone(8, zoneTop, 312, zoneBottom, ENativeTouch::Activate, row.Item);
			break;
		}
		case ENativeRowKind::Submenu:
		case ENativeRowKind::Command:
			NesText(framebuffer, 298, y, ">", 1, NesGray);
			AddNativeTouchZone(8, zoneTop, 312, zoneBottom, ENativeTouch::Activate, row.Item);
			break;
		default:
			break;
		}
	}
	NesHint(framebuffer, "L R TAB   A CHANGE   B BACK");
}

// Splits `text` into lines of at most `columns` characters, at spaces where it can.
TArray<FString> NesWrap(const char *text, unsigned columns, unsigned maximumLines)
{
	TArray<FString> lines;
	FString current;
	FString word;
	auto flushWord = [&]()
	{
		if (word.IsEmpty()) return;
		if (current.Len() + (current.IsEmpty() ? 0 : 1) + word.Len() > columns && current.IsNotEmpty())
		{
			lines.Push(current);
			current = "";
		}
		while (word.Len() > columns)
		{
			lines.Push(word.Left(columns));
			word = word.Mid(columns);
		}
		if (current.IsNotEmpty()) current += " ";
		current += word;
		word = "";
	};
	for (const char *c = text; c != nullptr && *c != '\0'; ++c)
	{
		if (*c == ' ' || *c == '\n')
		{
			flushWord();
			if (*c == '\n' && current.IsNotEmpty())
			{
				lines.Push(current);
				current = "";
			}
		}
		else word += *c;
	}
	flushWord();
	if (current.IsNotEmpty()) lines.Push(current);
	if (lines.Size() > maximumLines) lines.Resize(maximumLines);
	return lines;
}

// A two-choice dialog in the middle of the touch screen.
void DrawNesDialog(unsigned char *framebuffer, const char *message, const char *left,
	const char *right, int selected)
{
	TArray<FString> lines = NesWrap(message, 34, 5);
	const int boxHeight = 64 + static_cast<int>(lines.Size()) * 12;
	const int boxTop = (226 - boxHeight) / 2;
	NesBox(framebuffer, 16, boxTop, 288, boxHeight, NesTabIdle, NesBlue);
	int y = boxTop + 14;
	for (const auto &line : lines)
	{
		NesCenteredText(framebuffer, y, line.GetChars(), 1, NesWhite);
		y += 12;
	}
	const char *labels[] = { left, right };
	for (int choice = 0; choice < 2; ++choice)
	{
		const int x = choice == 0 ? 52 : 172;
		const int top = boxTop + boxHeight - 38;
		const bool active = choice == selected;
		NesBox(framebuffer, x, top, 96, 24, active ? NesPanel : NesTabIdle, active ? NesBlue : NesGray);
		NesText(framebuffer, x + (96 - NesTextWidth(labels[choice], 1)) / 2, top + 8, labels[choice], 1,
			active ? NesGold : NesWhite);
		AddNativeTouchZone(x, top, x + 96, top + 24, ENativeTouch::Choice, choice);
	}
}

void DrawNativeSaveLoadMenu(unsigned char *framebuffer)
{
	const bool saving = CurrentMenu->IsKindOf("SaveMenu");
	NesCenteredText(framebuffer, 8, saving ? "SAVE GAME" : "LOAD GAME", 2, NesRed);

	const int count = static_cast<int>(savegameManager.SavegameCount());
	const int selected = CurrentMenu->IntVar(FName("Selected"));
	if (count == 0)
	{
		NesCenteredText(framebuffer, 112, "NO SAVED GAMES", 1, NesGray);
		NesHint(framebuffer, "B BACK");
		return;
	}
	constexpr int FirstRowY = 36;
	constexpr int RowPitch = 20;
	constexpr int VisibleRows = 9;
	constexpr unsigned Columns = 36;
	const int firstRow = std::clamp(selected - VisibleRows / 2, 0, std::max(0, count - VisibleRows));
	for (int item = firstRow; item < std::min(count, firstRow + VisibleRows); ++item)
	{
		const int y = FirstRowY + (item - firstRow) * RowPitch;
		const bool active = item == selected;
		FSaveGameNode *node = savegameManager.GetSavegame(item);
		FString label = node != nullptr ? node->SaveTitle : FString();
		if (label.Len() > Columns) label = label.Left(Columns - 3) + "...";
		if (active) NesBox(framebuffer, 8, y - 5, 304, 18, NesPanel, NesBlue);
		NesText(framebuffer, 16, y, label.GetChars(), 1, active ? NesGold : NesWhite);
		AddNativeTouchZone(8, y - 5, 312, y - 5 + RowPitch, ENativeTouch::Save, item);
	}
	if (firstRow > 0) NesText(framebuffer, 300, 24, "-", 1, NesGray);

	const int stage = saving ? 0 : CurrentMenu->IntVar(FName("NativeActionStage"));
	if (stage != 0)
	{
		NativeTouchZoneCount = 0; // only the dialog takes taps
		const int choice = CurrentMenu->IntVar(FName("NativeActionChoice"));
		if (stage == 2) DrawNesDialog(framebuffer, "DELETE THIS SAVE?", "YES", "NO", choice);
		else if (stage == 3) DrawNesDialog(framebuffer, "COULD NOT DELETE THE SAVE", "BACK", "BACK", choice);
		else DrawNesDialog(framebuffer, "WHAT TO DO WITH THIS SAVE?", "LOAD", "DELETE", choice);
		NesHint(framebuffer, "LEFT RIGHT CHOOSE   A OK   B BACK");
		return;
	}
	NesHint(framebuffer, saving ? "A SAVE   B BACK" : "A CHOOSE   B BACK");
}

void DrawNativeMessageBox(unsigned char *framebuffer)
{
	FString message = CurrentMenu->StringVar(FName("NativeMessageText"));
	message.ToUpper();
	DrawNesDialog(framebuffer, message.GetChars(), "YES", "NO",
		CurrentMenu->IntVar(FName("messageSelection")));
	NesHint(framebuffer, "LEFT RIGHT CHOOSE   A OK   B BACK");
}

// The top screen's part of a native menu, drawn each frame over the frozen picture: for
// save/load, the selected save's picture and description.
struct FNesPreviewCache
{
	FGameTexture *Texture = nullptr;
	int Width = 0, Height = 0;
	std::vector<uint32_t> Pixels; // BGRA, already scaled
};
FNesPreviewCache NesPreview;

void NesTopText(unsigned char *pixels, int pitchBytes, int width, int height, int x, int y,
	const char *text, int scale, FOverlayColor color)
{
	if (text == nullptr) return;
	for (; *text != '\0'; ++text, x += 8 * scale)
	{
		const int character = std::toupper(static_cast<unsigned char>(*text));
		if (character < 0 || character >= 128) continue;
		const uint8_t *rows = NesFont.Rows[character];
		for (int row = 0; row < 8; ++row)
			for (int column = 0; column < 8; ++column)
				if (rows[row] & (0x80u >> column))
					NativeTopRect(pixels, pitchBytes, width, height, x + column * scale,
						y + row * scale, scale, scale, color, 255u);
	}
}

void DrawNativeMenuTop(unsigned char *pixels, int pitchBytes, int width, int height)
{
	if (CurrentMenu == nullptr || !CurrentMenu->IsKindOf("LoadSaveMenu") || !NesFont.Valid) return;
	const int count = static_cast<int>(savegameManager.SavegameCount());
	const int selected = CurrentMenu->IntVar(FName("Selected"));
	if (selected < 0 || selected >= count) return;

	// The picture, scaled once per save (GetBgraBitmap is too slow for every frame).
	const int previewWidth = std::min(240, width - 40);
	const int previewHeight = previewWidth * 3 / 4;
	const int previewX = (width - previewWidth) / 2;
	constexpr int PreviewY = 8;
	FGameTexture *picture = savegameManager.GetSavePic();
	if (picture != NesPreview.Texture || NesPreview.Width != previewWidth)
	{
		NesPreview.Texture = picture;
		NesPreview.Width = previewWidth;
		NesPreview.Height = previewHeight;
		NesPreview.Pixels.clear();
		if (picture != nullptr && picture->GetTexture() != nullptr)
		{
			FBitmap bitmap = picture->GetTexture()->GetBgraBitmap(nullptr);
			const int sourceWidth = bitmap.GetWidth();
			const int sourceHeight = bitmap.GetHeight();
			if (sourceWidth > 0 && sourceHeight > 0 && bitmap.GetPixels() != nullptr)
			{
				NesPreview.Pixels.resize(static_cast<size_t>(previewWidth) * previewHeight);
				for (int y = 0; y < previewHeight; ++y)
					for (int x = 0; x < previewWidth; ++x)
					{
						const uint8_t *source = bitmap.GetPixels() +
							(y * sourceHeight / previewHeight) * bitmap.GetPitch() +
							(x * sourceWidth / previewWidth) * 4;
						NesPreview.Pixels[static_cast<size_t>(y) * previewWidth + x] =
							source[0] | (source[1] << 8) | (source[2] << 16) | 0xff000000u;
					}
			}
		}
	}
	NativeTopRect(pixels, pitchBytes, width, height, previewX - 2, PreviewY - 2,
		previewWidth + 4, previewHeight + 4, NesBlue, 255u);
	if (!NesPreview.Pixels.empty())
	{
		for (int y = 0; y < previewHeight && PreviewY + y < height; ++y)
			std::memcpy(pixels + static_cast<size_t>(PreviewY + y) * pitchBytes + previewX * 4,
				&NesPreview.Pixels[static_cast<size_t>(y) * previewWidth],
				static_cast<size_t>(previewWidth) * 4);
	}
	else
	{
		NativeTopRect(pixels, pitchBytes, width, height, previewX, PreviewY,
			previewWidth, previewHeight, OverlayInk, 255u);
	}

	// The description under it: the save's title, then the game's comment lines.
	int y = PreviewY + previewHeight + 8;
	FSaveGameNode *node = savegameManager.GetSavegame(selected);
	if (node != nullptr)
	{
		FString title = node->SaveTitle;
		if (title.Len() > 46) title = title.Left(43) + "...";
		NesTopText(pixels, pitchBytes, width, height, 16, y, title.GetChars(), 1, NesGold);
		y += 11;
	}
	FString comment = savegameManager.SaveCommentString;
	comment.ToUpper();
	for (const auto &line : NesWrap(comment.GetChars(), 46, (height - y) / 10))
	{
		NesTopText(pixels, pitchBytes, width, height, 16, y, line.GetChars(), 1, NesWhite);
		y += 10;
	}
}

// Draws the open native menu on the bottom LCD. False when it is not one drawn here.
bool DrawNativeMenu(unsigned char *framebuffer)
{
	LoadNesFont();
	NativeTouchZoneCount = 0;
	NativeMenuDrawnHere = false;
	if (!NesFont.Valid || !NativeMenuSupported()) return false;
	OverlayRect(framebuffer, 0, 0, BottomScreenWidth, BottomScreenHeight, OverlayInk);
	if (CurrentMenu->IsKindOf("LoadSaveMenu"))
		DrawNativeSaveLoadMenu(framebuffer);
	else if (CurrentMenu->IsKindOf("MessageBoxMenu"))
		DrawNativeMessageBox(framebuffer);
	else if (CurrentMenu->IsKindOf("ListMenu"))
		DrawNativeListMenu(framebuffer, CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc")));
	else
		DrawNativeOptionMenu(framebuffer, CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc")));
	NativeMenuDrawnHere = true;
	return true;
}

// ---- Touch -------------------------------------------------------------------------------

void SetSliderFromTouch(DMenuItemBase *item, float fraction)
{
	FBaseCVar *cvar = ItemCVar(item);
	if (cvar == nullptr) return;
	const double minimum = item->FloatVar(FName("mMin"));
	const double maximum = item->FloatVar(FName("mMax"));
	const double step = item->FloatVar(FName("mStep"));
	double value = minimum + (maximum - minimum) * std::clamp(fraction, 0.0f, 1.0f);
	if (step > 0.0) value = minimum + std::round((value - minimum) / step) * step;
	UCVarValue rep;
	rep.Float = static_cast<float>(std::clamp(value, minimum, maximum));
	cvar->SetGenericRep(rep, CVAR_Float);
}

// A tap on a native menu. True when the frame on screen was drawn here (the tap is used).
bool NativeMenuTouch(int x, int y)
{
	if (!NativeMenuDrawnHere || CurrentMenu == nullptr) return false;
	for (unsigned i = 0; i < NativeTouchZoneCount; ++i)
	{
		const FNativeTouchZone &zone = NativeTouchZones[i];
		if (x < zone.X0 || x >= zone.X1 || y < zone.Y0 || y >= zone.Y1) continue;
		if (zone.Action == ENativeTouch::Save)
		{
			CurrentMenu->IntVar(FName("Selected")) = zone.Item;
			CurrentMenu->CallMenuEvent(MKEY_Enter, true);
			return true;
		}
		if (zone.Action == ENativeTouch::Choice)
		{
			if (CurrentMenu->IsKindOf("MessageBoxMenu"))
				CurrentMenu->IntVar(FName("messageSelection")) = zone.Item;
			else if (CurrentMenu->IsKindOf("LoadMenu"))
				CurrentMenu->IntVar(FName("NativeActionChoice")) = zone.Item;
			CurrentMenu->CallMenuEvent(MKEY_Enter, true);
			return true;
		}
		if (zone.Action == ENativeTouch::Tab)
		{
			auto *descriptor = CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
			if (descriptor != nullptr && OptionTabIndex(descriptor->mMenuName) != zone.Item)
			{
				CurrentMenu->Close();
				M_SetMenu(FName(OptionTabMenus[zone.Item]), -1);
			}
			return true;
		}
		if (CurrentMenu->IsKindOf("ListMenu"))
		{
			auto *descriptor = CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc"));
			if (descriptor == nullptr || zone.Item < 0 ||
				static_cast<unsigned>(zone.Item) >= descriptor->mItems.Size()) return true;
			descriptor->mSelectedItem = zone.Item;
			CurrentMenu->CallMenuEvent(MKEY_Enter, true);
			return true;
		}
		auto *descriptor = CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
		if (descriptor == nullptr || zone.Item < 0 ||
			static_cast<unsigned>(zone.Item) >= descriptor->mItems.Size()) return true;
		descriptor->mSelectedItem = zone.Item;
		switch (zone.Action)
		{
		case ENativeTouch::Activate: CurrentMenu->CallMenuEvent(MKEY_Enter, true); break;
		case ENativeTouch::Left: CurrentMenu->CallMenuEvent(MKEY_Left, true); break;
		case ENativeTouch::Right: CurrentMenu->CallMenuEvent(MKEY_Right, true); break;
		case ENativeTouch::Slider:
			SetSliderFromTouch(descriptor->mItems[zone.Item],
				static_cast<float>(x - 224) / 80.0f);
			break;
		default: break;
		}
		return true;
	}
	return true;
}

// ---- The top screen while a native menu is open -------------------------------------------

std::vector<unsigned char> NativeMenuFrozenTop;
int NativeMenuFrozenWidth, NativeMenuFrozenHeight, NativeMenuFrozenPitch;

void ReleaseNativeMenuFreeze()
{
	NativeMenuFrozenTop.clear();
	NativeMenuFrozenTop.shrink_to_fit();
	NativeMenuFrozenWidth = NativeMenuFrozenHeight = NativeMenuFrozenPitch = 0;
}
