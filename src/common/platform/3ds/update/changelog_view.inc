void DrawUpdateTopBackground(unsigned char *pixels, int pitch, int width, int height)
{
    // Select the title artwork explicitly, even when the idle sequence is on
    // ZSTORY or the update menu was opened from a paused game.
    const auto id = TexMan.CheckForTexture("TITLEPIC", ETextureType::Any,
        FTextureManager::TEXMAN_TryAny);
    auto *art = BottomMapTexture(id);
    const unsigned brightness = NativeMenuCurrentBrightness();
    for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
        auto *target = pixels + y * pitch + x * 4;
        const auto *source = art ? art->Pixels.data() +
            (y * art->Height / height) * art->Pitch + (x * art->Width / width) * 4 : nullptr;
        for (int channel = 0; channel < 3; ++channel)
            target[channel] = source ? NativeMenuDimChannel(source[channel], brightness) : 0;
        target[3] = 255;
    }
}

bool DrawUpdateChangelogTop(unsigned char *pixels, int pitch, int width, int height)
{
    if (!CurrentMenu || !CurrentMenu->IsKindOf("LegendUpdateMenu")) return false;
    // The NES font of the native menus (native_menu_3ds.inc), x1: 8 pixels a character.
    LoadNesFont();
    if (!NesFont.Valid) return false;
    const size_t columns = static_cast<size_t>(std::min(42, (width - 32) / 8));
    static char bundled[12289] = {}, cached[UPDATE_HISTORY_MAX] = {}, lines[2048][43];
    static bool loaded = false;
    static unsigned count = 0;
    static int cachedWidth = 0;
    if (!loaded)
    {
        loaded = true;
        if (FILE *file = std::fopen("romfs:/update-changelog.txt", "rb"))
        {
            bundled[std::fread(bundled, 1, sizeof(bundled)-1, file)] = 0;
            std::fclose(file);
        }
    }
    // CHANGELOG: ALL (NativeUpdateAll) shows every version; the start-up offer always shows
    // what is new since the installed one.
    const bool all = CurrentMenu->IntVar(FName("NativeUpdateAll")) != 0 &&
        CurrentMenu->IntVar(FName("NativeUpdateConfirm")) != 2;
    static char notes[UPDATE_HISTORY_MAX], saved[UPDATE_HISTORY_MAX];
    notes[0] = 0;
    if (all) Updater_GetHistory(notes, sizeof(notes));
    else Updater_GetNotes(notes, sizeof(notes));
    const bool onlineNotes = notes[0] != 0;
    // The notes of the last answer are kept on the SD card (one file per CHANGELOG setting), so
    // the screen has them with no network, or while the check runs; the bundled ones (the
    // installed version's, put in the romfs at build time) come last.
    const char *savedPath = all ? LOD3DS_APP_DIR "/update-notes-all.txt"
                                : LOD3DS_APP_DIR "/update-notes-new.txt";
    static const char *loadedPath = nullptr;
    static char writtenPath[96] = {};
    if (onlineNotes)
    {
        // Written once per different text.
        if (std::strcmp(writtenPath, savedPath) || std::strcmp(saved, notes))
        {
            std::snprintf(saved, sizeof(saved), "%s", notes);
            std::snprintf(writtenPath, sizeof(writtenPath), "%s", savedPath);
            if (FILE *file = std::fopen(savedPath, "wb"))
            {
                std::fwrite(notes, 1, std::strlen(notes), file);
                std::fclose(file);
            }
        }
    }
    else if (loadedPath != savedPath)
    {
        loadedPath = savedPath;
        saved[0] = 0;
        if (FILE *file = std::fopen(savedPath, "rb"))
        {
            saved[std::fread(saved, 1, sizeof(saved)-1, file)] = 0;
            std::fclose(file);
        }
    }
    const char *body = onlineNotes ? notes : saved[0] ? saved : bundled;
    if (!count || std::strcmp(body, cached) || cachedWidth != width)
    {
        std::snprintf(cached, sizeof(cached), "%s", body);
        cachedWidth = width;
        static char formatted[1024][43];
        const unsigned formattedCount = Update_FormatNotes(cached, formatted, 1024);
        count = 0;
        for (unsigned row = 0; row < formattedCount && count < 2048; ++row)
        {
            const char *remaining = formatted[row];
            do
            {
                size_t take = std::strlen(remaining);
                char candidate[43];
                std::strcpy(candidate, remaining);
                while (take > 1 && take > columns)
                    candidate[--take] = 0;
                if (take < std::strlen(remaining))
                {
                    size_t word = take;
                    while (word && remaining[word] != ' ') --word;
                    if (word) take = word;
                }
                std::memcpy(lines[count], remaining, take);
                lines[count++][take] = 0;
                remaining += take;
                while (*remaining == ' ') ++remaining;
            } while (*remaining && count < 2048);
        }
        CurrentMenu->IntVar(FName("NativeUpdatePage")) = 0;
    }
    unsigned first = 0;
    if (count && !std::strcmp(lines[0], "Changelog"))
        while (++first < count && !lines[first][0]) {}
    const unsigned rowHeight = 11;
    const unsigned rows = std::max(1, (height - 78) / int(rowHeight));
    const unsigned pages = std::max(1u, (count-first+rows-1)/rows);
    int &page = CurrentMenu->IntVar(FName("NativeUpdatePage"));
    page = std::clamp(page, 0, int(pages)-1);
    CurrentMenu->IntVar(FName("NativeUpdatePages")) = pages;
    DrawUpdateTopBackground(pixels, pitch, width, height);
    auto centered = [&](int y, const char *text, FOverlayColor color)
    {
        NesTopText(pixels, pitch, width, height, (width - NesTextWidth(text, 1)) / 2, y, text, 1, color);
    };
    centered(10, Tr("CHANGELOG"), NesRed);
    UpdateStatus status;
    Updater_GetStatus(&status);
    const char *version = all && body != bundled ? Tr("ALL VERSIONS")
        : onlineNotes && status.version[0] ? status.version
        : body == saved ? Tr("LAST NOTES SAVED") : LOD3DS_PORT_VERSION_LABEL;
    centered(29, version, OverlayIvory);
    for (unsigned i = first + page*rows; i < count && i < first+(page+1)*rows; ++i)
        NesTopText(pixels, pitch, width, height, 16,
            52+(i-first-page*rows)*rowHeight, lines[i], 1, NesWhite);
    char footer[64];
    std::snprintf(footer, sizeof(footer), "%s %d / %u", Tr("L R PAGE"), page+1, pages);
    centered(height-20, footer, OverlayIvory);
    return true;
}
