// Auto Frame Window
// Replays a bot macro over and over at max speed, nudging one input at a time,
// to measure how many frames of leeway every input really has.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/ui/MDPopup.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/fmod/fmod.hpp>

#include <gdr/gdr.hpp>
#include <gdr_convert.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>

#include "Measurer.hpp"
#include "LStar.hpp"
#include <thread>
#include <atomic>

using namespace geode::prelude;

struct MyReplay : gdr::Replay<MyReplay, gdr::Input<>> {
    MyReplay() : gdr::Replay<MyReplay, gdr::Input<>>("AutoFrameWindow", 1) {}
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

namespace {

using Clock = std::chrono::steady_clock;

struct Loaded {
    std::vector<afw::Event> events;
    double fps = 240.0;
    std::string botName;
    std::string fileName;
    std::string path;
    bool scaled = false;
};

struct Run {
    bool active = false;
    bool needReset = true;
    bool injecting = false;
    bool died = false;
    bool reachedEnd = false;
    int deathFrame = 0;
    size_t next = 0;
    bool held[2][4] = {};     // [player2][button]
    struct Split { bool armed = false; int button = 1; bool down = true; bool p2 = false; double frac = 0; } split;
    int attempts = 0;
    int lastFrame = -1;
    int stuckTicks = 0;
    bool stuck = false;
    Clock::time_point start;
    double fps = 240.0;
    std::string fileName;
    std::string levelName;
    std::string macroPath;
    int rangeMin = 0;
    int rangeMax = INT_MAX;
};

afw::Measurer g_meas;
Run g_run;
Ref<CCLabelBMFont> g_label;

int frameOf(PlayLayer* pl, afw::FrameSource src) {
    if (src == afw::FrameSource::LevelTime) {
        return static_cast<int>(pl->m_gameState.m_levelTime * 240.0) + 1;
    }
    return static_cast<int>(pl->m_gameState.m_currentProgress);
}

int macroFrame(PlayLayer* pl) {
    auto c = g_meas.attempt().cand;
    return frameOf(pl, c.source) + c.offset;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string fmtTime(double secs) {
    int s = static_cast<int>(secs);
    if (s >= 3600) return fmt::format("{}h{:02}m", s / 3600, (s / 60) % 60);
    return fmt::format("{}:{:02}", s / 60, s % 60);
}

// ---------------------------------------------------------------------------
// Macro loading (.gdr / .gdr.json / .gdr2)
// ---------------------------------------------------------------------------

Result<Loaded> loadMacro(std::filesystem::path const& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Err("Couldn't open the file.");
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return Err("The file is empty.");

    std::optional<MyReplay> replay;
    // New binary GDR format first, then the older json / msgpack one.
    {
        auto res = MyReplay::importData(std::span<uint8_t>(data));
        if (res.isOk()) replay = std::move(res).unwrap();
    }
    if (!replay) {
        auto res = gdr::convert<MyReplay, gdr::Input<>>(std::span<uint8_t>(data));
        if (res.isOk()) replay = std::move(res).unwrap();
    }
    if (!replay) return Err("Not a GDR macro this mod can read. Supported: .gdr, .gdr.json, .gdr2");

    Loaded out;
    out.fps = replay->framerate > 0 ? replay->framerate : 240.0;
    out.botName = replay->botInfo.name;
    out.fileName = path.filename().string();
    out.path = path.string();
    double scale = 240.0 / out.fps;
    out.scaled = std::abs(out.fps - 240.0) > 0.5;

    for (auto const& in : replay->inputs) {
        afw::Event e;
        e.frame = static_cast<int>(out.scaled ? std::llround(in.frame * scale) : (long long)in.frame);
        e.button = in.button == 0 ? 1 : in.button;
        e.p2 = in.player2;
        e.down = in.down;
        out.events.push_back(e);
    }
    if (out.events.empty()) return Err("The macro has no inputs.");
    return Ok(std::move(out));
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

std::filesystem::path writeResults(std::vector<afw::Result> const& results) {
    auto dir = Mod::get()->getSaveDir() / "results";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::string base = g_run.levelName.empty() ? "level" : g_run.levelName;
    for (auto& c : base) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&t));
    auto stem = dir / fmt::format("{}_{}", base, stamp);

    // .fwc (Frame Window Counter binary format, "FWC2")
    {
        std::ofstream f(stem.string() + ".fwc", std::ios::binary);
        f.write("FWC2", 4);
        double fps = 240.0;
        f.write(reinterpret_cast<char const*>(&fps), sizeof(double));
        uint32_t count = static_cast<uint32_t>(results.size());
        f.write(reinterpret_cast<char const*>(&count), sizeof(uint32_t));
        for (auto const& r : results) {
            int32_t frame = r.ev.frame;
            double window = static_cast<double>(r.window());
            uint8_t flags = 1 | (r.ev.p2 ? 2 : 0);
            int32_t ifCount = 1;
            f.write(reinterpret_cast<char const*>(&frame), sizeof(int32_t));
            f.write(reinterpret_cast<char const*>(&window), sizeof(double));
            f.write(reinterpret_cast<char const*>(&flags), sizeof(uint8_t));
            f.write(reinterpret_cast<char const*>(&ifCount), sizeof(int32_t));
        }
    }

    // .csv for humans
    {
        std::ofstream f(stem.string() + ".csv");
        f << "input,frame,type,player,button,early,late,window,early_capped,late_capped\n";
        int n = 1;
        for (auto const& r : results) {
            f << n++ << ',' << r.ev.frame << ',' << (r.ev.down ? "press" : "release") << ','
              << (r.ev.p2 ? 2 : 1) << ',' << r.ev.button << ',' << r.early << ',' << r.late << ','
              << r.window() << ',' << (r.earlyCapped ? 1 : 0) << ',' << (r.lateCapped ? 1 : 0) << '\n';
        }
    }
    return stem.string() + ".fwc";
}


// ---------------------------------------------------------------------------
// Overlay: click markers + top-right HUD (uses saved results for the level)
// ---------------------------------------------------------------------------

struct Mark {
    int frame = 0;
    double window = 1;
    double early = 0;
    double late = 0;
    bool p2 = false;
    bool down = true;
    bool capped = false;
};

struct LevelMarks {
    bool loaded = false;
    bool hasCand = false;
    afw::Candidate cand;
    std::string macroPath;     // last macro used on this level
    std::vector<Mark> marks;   // sorted by frame
};

struct Overlay {
    size_t next = 0;
    int lastFrame = -1;
    int maxRow = 25;                 // one HUD row per window 1..maxRow
    std::vector<int> counts;         // index k-1 = clicks with window k passed so far
    std::vector<int> totals;
    Ref<CCNode> hud;
    std::vector<CCLabelBMFont*> rows;
    CCLabelBMFont* nextRow = nullptr;
    CCLabelBMFont* lstarRow = nullptr;
    int passed = 0;   // presses passed so far
};

LevelMarks g_marks;
Overlay g_ov;

ccColor3B const kBucketColors[4] = {
    { 255, 70, 70 },    // 1f
    { 255, 160, 50 },   // 2f
    { 255, 235, 70 },   // 3f
    { 90, 235, 100 },   // 4f+
};
int maxShownSetting() { return (int)std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("max-shown-window"), 1, 25); }

// HUD row for a window (1..max), or -1 if it's above the max and should be hidden.
int rowOf(double window, int maxRow) {
    int k = std::max(1, (int)std::floor(window + 1e-9));
    return k > maxRow ? -1 : k - 1;
}

// Red (tight) -> orange -> yellow -> green (loose). 1, 2, 3, 4+ keep the classic colors.
ccColor3B colorForWindow(double window) {
    int k = std::max(1, (int)std::floor(window + 1e-9));
    if (k <= 4) return kBucketColors[k - 1];
    float t = std::min(1.f, (k - 4) / 12.f);  // 4 -> green, 16+ -> cyan
    return { (GLubyte)(90 - 30 * t), (GLubyte)(235 - 15 * t), (GLubyte)(100 + 155 * t) };
}

int bucketOf(double window) { return std::clamp((int)std::floor(window + 1e-9), 1, 4) - 1; }

// 2 -> "2", 1.25 -> "1.25", 1.5 -> "1.5"
std::string fmtW(double v) {
    auto s = fmt::format("{:.3f}", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string levelKey(GJGameLevel* level) {
    if (!level) return "unknown";
    int id = level->m_levelID.value();
    if (id > 0) return fmt::format("id_{}", id);
    std::string name = level->m_levelName;
    for (auto& c : name) if (!std::isalnum((unsigned char)c)) c = '_';
    return "local_" + name;
}

std::filesystem::path levelFile(GJGameLevel* level) {
    return Mod::get()->getSaveDir() / "levels" / (levelKey(level) + ".json");
}

LevelMarks readMarks(GJGameLevel* level) {
    LevelMarks out;
    std::ifstream f(levelFile(level));
    if (!f) return out;
    std::stringstream ss;
    ss << f.rdbuf();
    auto res = matjson::parse(ss.str());
    if (!res) return out;
    auto root = std::move(res).unwrap();
    out.hasCand = root.contains("source");
    out.cand.source = root["source"].asInt().unwrapOr(0) == 1 ? afw::FrameSource::LevelTime : afw::FrameSource::Progress;
    out.cand.offset = (int)root["offset"].asInt().unwrapOr(0);
    out.macroPath = root["macro"].asString().unwrapOr("");
    if (root["marks"].isArray()) {
        for (auto const& m : root["marks"]) {
            Mark mk;
            mk.frame = (int)m["f"].asInt().unwrapOr(0);
            mk.window = m["w"].asDouble().unwrapOr(1.0);
            mk.early = m["e"].asDouble().unwrapOr(0.0);
            mk.late = m["l"].asDouble().unwrapOr(0.0);
            mk.p2 = m["p2"].asBool().unwrapOr(false);
            mk.down = m["d"].asBool().unwrapOr(true);
            mk.capped = m["c"].asBool().unwrapOr(false);
            out.marks.push_back(mk);
        }
    }
    std::stable_sort(out.marks.begin(), out.marks.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    out.loaded = !out.marks.empty();
    return out;
}

void writeMarks(GJGameLevel* level, LevelMarks const& lm) {
    std::vector<matjson::Value> arr;
    for (auto const& m : lm.marks) {
        arr.push_back(matjson::makeObject({
            { "f", m.frame }, { "w", m.window }, { "e", m.early }, { "l", m.late },
            { "p2", m.p2 }, { "d", m.down }, { "c", m.capped },
        }));
    }
    auto root = matjson::makeObject({
        { "source", lm.cand.source == afw::FrameSource::LevelTime ? 1 : 0 },
        { "offset", lm.cand.offset },
        { "macro", lm.macroPath },
        { "marks", arr },
    });
    auto path = levelFile(level);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    f << root.dump(2);
}

// Merge new results into what's saved: inputs inside the measured frame range are
// replaced, everything else is kept (so a level can be measured in sections).
void saveMarks(GJGameLevel* level, std::vector<afw::Result> const& results, afw::Candidate cand,
    std::string const& macroPath, int rangeMin, int rangeMax) {
    LevelMarks lm = readMarks(level);
    // A different sync means old frames may not line up; start fresh.
    if (lm.hasCand && (lm.cand.source != cand.source || lm.cand.offset != cand.offset)) lm.marks.clear();
    if (!lm.macroPath.empty() && lm.macroPath != macroPath) lm.marks.clear();
    std::erase_if(lm.marks, [&](Mark const& m) { return m.frame >= rangeMin && m.frame <= rangeMax; });
    for (auto const& r : results) {
        Mark m;
        m.frame = r.ev.frame;
        m.window = r.window();
        m.early = r.early;
        m.late = r.late;
        m.p2 = r.ev.p2;
        m.down = r.ev.down;
        m.capped = r.earlyCapped || r.lateCapped;
        lm.marks.push_back(m);
    }
    std::stable_sort(lm.marks.begin(), lm.marks.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    lm.cand = cand;
    lm.hasCand = true;
    lm.macroPath = macroPath;
    writeMarks(level, lm);
}

// ---------------------------------------------------------------------------
// L* (Nerve / Fatigue / CPS precision), computed in the background
// ---------------------------------------------------------------------------

struct LStarState {
    bool ready = false;
    bool computing = false;
    afw::lstar::Output out;
};
LStarState g_lstar;
std::atomic<int> g_lstarGen{ 0 };
std::atomic<float> g_lstarProgress{ 0.f };

void refreshLStarRow();

afw::lstar::Params lstarParams() {
    afw::lstar::Params p;
    p.target = Mod::get()->getSettingValue<double>("lstar-target-hours") * 3600.0;
    p.respawn = Mod::get()->getSettingValue<double>("lstar-respawn");
    p.kT = Mod::get()->getSettingValue<double>("lstar-kt");
    p.kU = Mod::get()->getSettingValue<double>("lstar-ku");
    p.kC = Mod::get()->getSettingValue<double>("lstar-kc");
    if (p.target <= 0) p.target = 86400.0;
    return p;
}

void startLStar() {
    int gen = ++g_lstarGen;
    g_lstar = LStarState{};
    if (!g_marks.loaded) return;
    std::vector<afw::lstar::Click> clicks;
    for (auto const& m : g_marks.marks) if (m.down) clicks.push_back({ (double)m.frame, (double)m.window });
    if (clicks.empty()) return;
    g_lstar.computing = true;
    g_lstarProgress = 0.f;
    auto params = lstarParams();
    std::thread([gen, clicks = std::move(clicks), params] {
        auto out = afw::lstar::compute(clicks, params, [gen] { return g_lstarGen.load() != gen; }, &g_lstarProgress);
        geode::queueInMainThread([gen, out = std::move(out)]() mutable {
            if (g_lstarGen.load() != gen) return;
            g_lstar.out = std::move(out);
            g_lstar.ready = true;
            g_lstar.computing = false;
            refreshLStarRow();
        });
    }).detach();
}

int lstarMetricSetting() {
    auto v = Mod::get()->getSettingValue<std::string>("lstar-hud");
    for (int m = 0; m < afw::lstar::MetricCount; m++) if (v == afw::lstar::metricName(m)) return m;
    return -1;  // Off
}

std::string lstarSummary(bool markup) {
    if (!g_lstar.ready) return g_lstar.computing ? "L*: calculating..." : "";
    using namespace afw::lstar;
    if (markup) {
        return fmt::format("L*  Base <cy>{:.2f}</c>  Nerve <cy>{:.2f}</c>  Fatigue <cy>{:.2f}</c>  CPS <cy>{:.2f}</c>  All <cr>{:.2f}</c>",
            g_lstar.out.final(Base), g_lstar.out.final(Nerve), g_lstar.out.final(Fatigue),
            g_lstar.out.final(Cps), g_lstar.out.final(All));
    }
    return fmt::format("L* Base {:.2f} | Nerve {:.2f} | Fatigue {:.2f} | CPS {:.2f} | All {:.2f}",
        g_lstar.out.final(Base), g_lstar.out.final(Nerve), g_lstar.out.final(Fatigue),
        g_lstar.out.final(Cps), g_lstar.out.final(All));
}

void loadMarks(GJGameLevel* level) {
    g_marks = readMarks(level);
    startLStar();
}

void refreshHud() {
    if (!g_ov.hud) return;
    g_ov.hud->setVisible(!g_run.active);
    for (size_t k = 0; k < g_ov.rows.size(); k++) {
        if (!g_ov.rows[k]) continue;
        g_ov.rows[k]->setString(fmt::format("{}: {}", k + 1, g_ov.counts[k]).c_str());
    }
}

// "Next 1f in 12" line: the upcoming click and how many frames away it is.
void refreshNext(int frame) {
    if (!g_ov.nextRow) return;
    auto const& marks = g_marks.marks;
    size_t i = g_ov.next;
    while (i < marks.size() && (!marks[i].down || rowOf(marks[i].window, g_ov.maxRow) < 0)) i++;
    if (i >= marks.size()) {
        g_ov.nextRow->setString("Next: -");
        g_ov.nextRow->setColor({ 200, 200, 200 });
        return;
    }
    auto const& m = marks[i];
    g_ov.nextRow->setString(fmt::format("Next {}{} in {}", fmtW(m.window), m.capped ? "+" : "",
        std::max(0, m.frame - frame)).c_str());
    g_ov.nextRow->setColor(colorForWindow(m.window));
}

// "L* All 412.3 / 1240.0": precision needed so far vs. for the whole level.
void refreshLStarRow() {
    if (!g_ov.lstarRow) return;
    int m = lstarMetricSetting();
    if (m < 0) return;
    if (!g_lstar.ready) {
        g_ov.lstarRow->setString(g_lstar.computing
            ? fmt::format("L* {:.0f}%", g_lstarProgress.load() * 100.f).c_str() : "");
        return;
    }
    auto const& s = g_lstar.out.series[m];
    double now = (g_ov.passed > 0 && g_ov.passed <= (int)s.size()) ? s[g_ov.passed - 1] : 0.0;
    g_ov.lstarRow->setString(fmt::format("L* {} {:.1f} / {:.1f}", afw::lstar::metricName(m), now, s.back()).c_str());
}

void removeHud() {
    if (g_ov.hud) g_ov.hud->removeFromParent();
    g_ov.hud = nullptr;
    g_ov.rows.clear();
    g_ov.nextRow = nullptr;
    g_ov.lstarRow = nullptr;
}

void buildOverlay(PlayLayer* pl) {
    removeHud();
    g_ov = Overlay{};
    if (!g_marks.loaded) return;

    g_ov.maxRow = maxShownSetting();
    g_ov.counts.assign(g_ov.maxRow, 0);
    g_ov.totals.assign(g_ov.maxRow, 0);
    for (auto const& m : g_marks.marks) {
        if (!m.down) continue;
        int r = rowOf(m.window, g_ov.maxRow);
        if (r >= 0) g_ov.totals[r]++;
    }

    if (!Mod::get()->getSettingValue<bool>("show-hud")) return;

    auto win = CCDirector::get()->getWinSize();
    auto hud = CCNode::create();
    hud->setID("hud"_spr);
    hud->setPosition({ win.width - 6.f, win.height - 6.f });

    auto title = CCLabelBMFont::create("Frame windows", "goldFont.fnt");
    title->setScale(0.45f);
    title->setAnchorPoint({ 1.f, 1.f });
    hud->addChild(title);

    // Shrink rows so up to 25 still fit on screen.
    float spacing = std::min(12.f, (win.height - 70.f) / g_ov.maxRow);
    float scale = 0.35f * spacing / 12.f;
    float y = -16.f;
    g_ov.rows.assign(g_ov.maxRow, nullptr);
    for (int k = 0; k < g_ov.maxRow; k++) {
        auto row = CCLabelBMFont::create("", "bigFont.fnt");
        row->setScale(scale);
        row->setAnchorPoint({ 1.f, 1.f });
        row->setPosition({ 0.f, y });
        row->setColor(colorForWindow(k + 1));
        hud->addChild(row);
        g_ov.rows[k] = row;
        y -= spacing;
    }
    if (Mod::get()->getSettingValue<bool>("show-next")) {
        auto row = CCLabelBMFont::create("", "bigFont.fnt");
        row->setScale(0.35f);
        row->setAnchorPoint({ 1.f, 1.f });
        row->setPosition({ 0.f, y - 3.f });
        hud->addChild(row);
        g_ov.nextRow = row;
    }
    if (lstarMetricSetting() >= 0) {
        auto row = CCLabelBMFont::create("", "bigFont.fnt");
        row->setScale(0.3f);
        row->setAnchorPoint({ 1.f, 1.f });
        row->setPosition({ 0.f, y - 16.f });
        row->setColor({ 200, 220, 255 });
        hud->addChild(row);
        g_ov.lstarRow = row;
    }
    pl->addChild(hud, 9998);
    g_ov.hud = hud;
    refreshHud();
    refreshNext(0);
    refreshLStarRow();
}

// ---------------------------------------------------------------------------
// Custom click sounds (one per window bucket, picked in settings)
// ---------------------------------------------------------------------------

std::unordered_map<std::string, FMOD::Sound*> g_soundCache;
FMOD::ChannelGroup* g_sfxGroup = nullptr;

void playSoundFile(std::filesystem::path const& path) {
    if (path.empty()) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;
    auto sys = engine->m_system;
    if (!g_sfxGroup && sys->createChannelGroup("afw-sfx", &g_sfxGroup) != FMOD_OK) return;

    float vol = (float)Mod::get()->getSettingValue<double>("sound-volume");
    if (Mod::get()->getSettingValue<bool>("sound-follow-sfx")) vol *= engine->m_sfxVolume;
    if (vol <= 0.f) return;
    g_sfxGroup->setVolume(vol);

    auto key = utils::string::pathToString(path);
    FMOD::Sound* sound = nullptr;
    if (auto it = g_soundCache.find(key); it != g_soundCache.end()) sound = it->second;
    else {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;
        if (sys->createSound(key.c_str(), FMOD_DEFAULT | FMOD_LOOP_OFF | FMOD_CREATESAMPLE, nullptr, &sound) != FMOD_OK || !sound) return;
        g_soundCache[key] = sound;
    }
    FMOD::Channel* ch = nullptr;
    sys->playSound(sound, g_sfxGroup, false, &ch);
}

void playClickSound(Mark const& m) {
    if (!Mod::get()->getSettingValue<bool>("sound-enabled")) return;
    static char const* keys[4] = { "sound-1f", "sound-2f", "sound-3f", "sound-4f" };
    auto path = Mod::get()->getSettingValue<std::filesystem::path>(keys[bucketOf(m.window)]);
    if (path.empty()) path = Mod::get()->getSettingValue<std::filesystem::path>("sound-any");
    playSoundFile(path);
}

// Hide markers/sounds for clicks with a window above this (1 to 25).
bool shownWindow(Mark const& m) {
    auto maxShown = (double)Mod::get()->getSettingValue<int64_t>("max-shown-window");
    return m.window <= maxShown + 1e-9;
}

void spawnMarker(PlayLayer* pl, Mark const& m) {
    auto player = m.p2 ? pl->m_player2 : pl->m_player1;
    if (!player || !player->getParent()) return;

    auto color = colorForWindow(m.window);
    ccColor4F fill = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.35f };
    ccColor4F ring = { color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f };

    auto node = CCNode::create();
    node->setID("click-marker"_spr);
    node->setPosition(player->getPosition());

    auto draw = CCDrawNode::create();
    draw->drawCircle({ 0.f, 0.f }, 16.f, fill, 1.5f, ring, 32);
    node->addChild(draw);

    auto label = CCLabelBMFont::create(fmt::format("{}{}", fmtW(m.window), m.capped ? "+" : "").c_str(), "bigFont.fnt");
    label->setScale(0.45f);
    label->setColor(color);
    node->addChild(label);

    // Early / late split under the circle, e.g. "-1 +0".
    if (Mod::get()->getSettingValue<bool>("marker-detail")) {
        auto detail = CCLabelBMFont::create(fmt::format("-{} +{}", fmtW(m.early), fmtW(m.late)).c_str(), "bigFont.fnt");
        detail->setScale(0.25f);
        detail->setPosition({ 0.f, -22.f });
        detail->setColor(color);
        node->addChild(detail);
    }

    node->setScale(0.6f);
    node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.15f, 1.f)));
    node->runAction(CCSequence::create(CCDelayTime::create(1.0f), CCRemoveSelf::create(), nullptr));
    player->getParent()->addChild(node, 10000);
}

// Called every tick during normal play.
void tickOverlay(PlayLayer* pl) {
    if (!g_marks.loaded || g_run.active) return;
    int frame = frameOf(pl, g_marks.cand.source) + g_marks.cand.offset;
    auto const& marks = g_marks.marks;

    // Restart / respawn: recount everything up to the current frame.
    if (frame < g_ov.lastFrame) {
        g_ov.next = 0;
        g_ov.passed = 0;
        std::fill(g_ov.counts.begin(), g_ov.counts.end(), 0);
        while (g_ov.next < marks.size() && marks[g_ov.next].frame < frame) {
            if (marks[g_ov.next].down) {
                int r = rowOf(marks[g_ov.next].window, g_ov.maxRow);
                if (r >= 0 && r < (int)g_ov.counts.size()) g_ov.counts[r]++;
                g_ov.passed++;
            }
            g_ov.next++;
        }
        refreshHud();
        refreshLStarRow();
    }
    int prevFrame = g_ov.lastFrame;
    g_ov.lastFrame = frame;

    bool changed = false;
    bool showMarkers = Mod::get()->getSettingValue<bool>("show-markers");
    while (g_ov.next < marks.size() && frame >= marks[g_ov.next].frame) {
        auto const& m = marks[g_ov.next++];
        if (!m.down) continue;
        if (int r = rowOf(m.window, g_ov.maxRow); r >= 0 && r < (int)g_ov.counts.size()) g_ov.counts[r]++;
        g_ov.passed++;
        changed = true;
        if (shownWindow(m)) {
            if (showMarkers) spawnMarker(pl, m);
            playClickSound(m);
        }
    }
    if (changed) { refreshHud(); refreshLStarRow(); }
    else if (g_lstar.computing && g_ov.lstarRow && (frame & 15) == 0) refreshLStarRow();
    if (frame != prevFrame) refreshNext(frame);
}

// ---------------------------------------------------------------------------
// Run control
// ---------------------------------------------------------------------------

void releaseHeld(PlayLayer* pl) {
    g_run.injecting = true;
    for (int p = 0; p < 2; p++) {
        for (int b = 1; b < 4; b++) {
            if (g_run.held[p][b]) pl->handleButton(false, b, p == 0);
            g_run.held[p][b] = false;
        }
    }
    g_run.injecting = false;
}

void removeLabel() {
    if (g_label) g_label->removeFromParent();
    g_label = nullptr;
}

void updateLabel(PlayLayer* pl) {
    if (!g_label) {
        g_label = CCLabelBMFont::create("", "bigFont.fnt");
        g_label->setScale(0.35f);
        g_label->setAnchorPoint({ 0.5f, 1.f });
        g_label->setID("status"_spr);
        auto win = CCDirector::get()->getWinSize();
        g_label->setPosition({ win.width / 2.f, win.height - 6.f });
        pl->addChild(g_label, 9999);
    }
    double elapsed = std::chrono::duration<double>(Clock::now() - g_run.start).count();
    std::string text;
    if (g_meas.phase() == afw::Measurer::Phase::Calibrating) {
        text = fmt::format("Auto Frame Window: syncing macro (try {}/{})",
            g_meas.calibrationTry(), g_meas.calibrationTotal());
    }
    else {
        int done = g_meas.targetsDone();
        int total = g_meas.targetCount();
        std::string eta = "--";
        if (done > 0) eta = fmtTime(elapsed / done * (total - done));
        double shift = g_meas.currentShift();
        text = fmt::format("Input {}/{}  shift {}{}  |  {} elapsed  |  ~{} left\nPause to stop (keeps finished inputs)",
            std::min(done + 1, total), total, shift > 0 ? "+" : "", fmtW(shift), fmtTime(elapsed), eta);
    }
    g_label->setString(text.c_str());
}

// Shown next frame so it sits on top of the pause menu.
void showResultPopup(std::string title, std::string body, std::optional<std::filesystem::path> folder) {
    geode::queueInMainThread([title, body, folder] {
        if (folder) {
            auto dir = folder->parent_path();
            createQuickPopup(title.c_str(), body, "OK", "Open folder", 380.f, [dir](FLAlertLayer*, bool open) {
                if (open) file::openFolder(dir);
            });
        }
        else {
            FLAlertLayer::create(title.c_str(), gd::string(body), "OK")->show();
        }
    });
}

void endRun(PlayLayer* pl, bool cancelled) {
    if (!g_run.active) return;
    releaseHeld(pl);
    g_run.active = false;
    removeLabel();
    refreshHud();

    double elapsed = std::chrono::duration<double>(Clock::now() - g_run.start).count();

    if (g_run.stuck) {
        showResultPopup("Stopped", "The level stopped advancing during replay, so measuring was stopped. "
            "Try again from a fresh level load without other bots or speedhacks on.", std::nullopt);
        return;
    }

    if (g_meas.phase() == afw::Measurer::Phase::Failed) {
        showResultPopup("Couldn't sync macro", g_meas.error(), std::nullopt);
        return;
    }

    auto results = cancelled ? g_meas.completedResults() : g_meas.results();
    if (results.empty()) {
        showResultPopup("Stopped", cancelled ? "No inputs were finished, nothing saved."
            : "No clicks in the chosen section. Check Section start / end in settings.", std::nullopt);
        return;
    }

    auto path = writeResults(results);
    // If stopped early, only replace old results up to the last input that finished.
    int rangeMax = g_run.rangeMax;
    if (cancelled) {
        rangeMax = 0;
        for (auto const& r : results) rangeMax = std::max(rangeMax, r.ev.frame);
    }
    saveMarks(pl->m_level, results, g_meas.chosen(), g_run.macroPath, g_run.rangeMin, rangeMax);
    loadMarks(pl->m_level);
    buildOverlay(pl);

    auto tight = std::min_element(results.begin(), results.end(),
        [](auto const& a, auto const& b) { return a.window() < b.window(); });
    int oneFrame = (int)std::count_if(results.begin(), results.end(), [](auto const& r) { return r.window() < 1.0 + 1e-9; });

    std::string body = fmt::format(
        "{} {} inputs in {} ({} attempts).\n"
        "1-frame inputs: <cr>{}</c>. Tightest: frame {} (window {}).\n\n"
        "Markers, HUD and L* (Nerve / Fatigue / CPS) are now on for this level; see <cg>View results</c>. "
        "Also saved <cy>{}</c> for Frame Window Counter.",
        cancelled ? "Stopped early. Measured" : "Measured",
        results.size(), fmtTime(elapsed), g_run.attempts,
        oneFrame, tight->ev.frame, fmtW(tight->window()),
        path.filename().string());
    showResultPopup(cancelled ? "Stopped" : "Done", body, path);
}

void finishAttempt(PlayLayer* pl, bool survived, int frame) {
    g_meas.report(survived, frame);
    g_run.attempts++;
    if (g_meas.finished()) {
        endRun(pl, false);
        pl->resetLevel();
        pl->pauseGame(false);
        return;
    }
    g_run.needReset = true;
}

void beginAttempt(PlayLayer* pl) {
    releaseHeld(pl);
    pl->resetLevel();
    g_run.next = 0;
    g_run.died = false;
    g_run.reachedEnd = false;
    g_run.deathFrame = 0;
    g_run.needReset = false;
    g_run.lastFrame = -1;
    g_run.stuckTicks = 0;
    g_run.split = {};
}

void startRun(Loaded const& macro) {
    auto pl = PlayLayer::get();
    if (!pl) return;

    afw::Settings s;
    s.maxShift = (int)Mod::get()->getSettingValue<int64_t>("max-shift");
    s.horizon = (int)Mod::get()->getSettingValue<int64_t>("horizon");
    s.measureReleases = Mod::get()->getSettingValue<bool>("measure-releases");
    auto src = Mod::get()->getSettingValue<std::string>("frame-source");
    s.autoSource = src == "Auto";
    s.forcedSource = src == "Level time" ? afw::FrameSource::LevelTime : afw::FrameSource::Progress;
    s.preferLevelTime = lower(macro.botName).find("xdbot") != std::string::npos;
    {
        auto cbf = Mod::get()->getSettingValue<std::string>("cbf-beta");
        s.subdiv = cbf == "1/2 frame" ? 2 : cbf == "1/4 frame" ? 4 : cbf == "1/8 frame" ? 8 : 1;
    }

    // Same macro as last time on this level: try the known sync first (usually 1 run).
    if (g_marks.hasCand && g_marks.macroPath == macro.path) s.preferred = g_marks.cand;

    // Section: percentages of the macro's length.
    int lastFrame = 0;
    for (auto const& e : macro.events) lastFrame = std::max(lastFrame, e.frame);
    int startPct = (int)Mod::get()->getSettingValue<int64_t>("section-start");
    int endPct = (int)Mod::get()->getSettingValue<int64_t>("section-end");
    if (endPct < startPct) std::swap(startPct, endPct);
    s.minFrame = startPct <= 0 ? 0 : (int)((int64_t)lastFrame * startPct / 100);
    s.maxFrame = endPct >= 100 ? INT_MAX : (int)(((int64_t)lastFrame * endPct + 99) / 100);

    g_meas.init(macro.events, s);
    g_run = Run{};
    g_run.active = true;
    g_run.needReset = true;
    g_run.start = Clock::now();
    g_run.fps = macro.fps;
    g_run.fileName = macro.fileName;
    g_run.macroPath = macro.path;
    g_run.rangeMin = s.minFrame;
    g_run.rangeMax = s.maxFrame;
    g_run.levelName = pl->m_level ? std::string(pl->m_level->m_levelName) : "level";

    if (pl->m_isPracticeMode) pl->togglePracticeMode(false);
    refreshHud();
    updateLabel(pl);
}

void closePauseMenu() {
    auto scene = CCDirector::get()->getRunningScene();
    if (auto pause = scene ? scene->getChildByType<PauseLayer>(0) : nullptr) {
        pause->onResume(nullptr);
    }
}

void confirmAndStart(Loaded macro) {
    int presses = (int)std::count_if(macro.events.begin(), macro.events.end(), [](auto& e) { return e.down; });
    bool rel = Mod::get()->getSettingValue<bool>("measure-releases");
    int startPct = (int)Mod::get()->getSettingValue<int64_t>("section-start");
    int endPct = (int)Mod::get()->getSettingValue<int64_t>("section-end");
    if (endPct < startPct) std::swap(startPct, endPct);
    std::string section = (startPct <= 0 && endPct >= 100) ? "the whole macro"
        : fmt::format("<cy>{}% to {}%</c> of the macro (merged into saved results)", startPct, endPct);

    std::string body = fmt::format(
        "<cy>{}</c>: {} presses, {} inputs total{}.\n\n"
        "Measures {} on {}. Don't touch anything; pause to stop early.{}",
        macro.fileName, presses, macro.events.size(),
        macro.botName.empty() ? "" : fmt::format(" ({})", macro.botName),
        rel ? "presses and releases" : "presses", section,
        macro.scaled ? fmt::format("\n<cr>Macro is {} TPS, converted to 240. Results may be off.</c>", macro.fps) : "");

    createQuickPopup("Auto Frame Window", body, "Cancel", "Start", 380.f, [macro](FLAlertLayer*, bool start) {
        if (!start || !PlayLayer::get()) return;
        closePauseMenu();
        startRun(macro);
    });
}

void pickAndStart() {
    file::FilePickOptions options;
    options.filters.push_back({ "GD Replay (*.gdr, *.gdr2, *.json)", { "*.gdr", "*.gdr2", "*.json" } });

    async::spawn(file::pick(file::PickMode::OpenFile, options), [](file::PickResult res) {
        if (!res.isOk()) return;
        auto opt = res.unwrap();
        if (!opt) return;
        auto loaded = loadMacro(*opt);
        if (loaded.isErr()) {
            FLAlertLayer::create("Can't load macro", gd::string(loaded.unwrapErr()), "OK")->show();
            return;
        }
        confirmAndStart(std::move(loaded).unwrap());
    });
}

void remeasure() {
    if (g_marks.macroPath.empty()) {
        FLAlertLayer::create("Re-measure", "No macro remembered for this level yet. Use <cg>Measure</c> first.", "OK")->show();
        return;
    }
    auto loaded = loadMacro(g_marks.macroPath);
    if (loaded.isErr()) {
        FLAlertLayer::create("Can't load macro",
            gd::string(fmt::format("{}\n\n{}", g_marks.macroPath, loaded.unwrapErr())), "OK")->show();
        return;
    }
    confirmAndStart(std::move(loaded).unwrap());
}

void showResultsList() {
    auto pl = PlayLayer::get();
    if (!g_marks.loaded) {
        FLAlertLayer::create("Results", "This level hasn't been measured yet.", "OK")->show();
        return;
    }
    int counts[4] = {};
    int total = 0;
    for (auto const& m : g_marks.marks) if (m.down) { counts[bucketOf(m.window)]++; total++; }

    std::string md = fmt::format(
        "**{} clicks**: <cr>{} 1f</c> · <co>{} 2f</c> · <cy>{} 3f</c> · <cg>{} 4f+</c>\n\n"
        "{}\n\n"
        "`+` = hit the test cap, could be bigger. Time is from level start at 240 TPS.\n\n"
        "| # | Frame | Time | Window | Early | Late |\n|---|---|---|---|---|---|\n",
        total, counts[0], counts[1], counts[2], counts[3], lstarSummary(true));
    int n = 1;
    char const* tags[4] = { "<cr>", "<co>", "<cy>", "<cg>" };
    for (auto const& m : g_marks.marks) {
        if (!m.down) continue;
        md += fmt::format("| {}{} | {} | {:.2f}s | {}{}{}</c> | {} | {} |\n",
            n++, m.p2 ? " (P2)" : "", m.frame, m.frame / 240.0,
            tags[bucketOf(m.window)], fmtW(m.window), m.capped ? "+" : "", fmtW(m.early), fmtW(m.late));
    }
    std::string title = pl && pl->m_level ? std::string(pl->m_level->m_levelName) : "Results";
    MDPopup::create(title, md, "OK")->show();
}

void clearResults() {
    createQuickPopup("Clear results", "Delete the saved frame windows for this level?", "Cancel", "Delete",
        [](FLAlertLayer*, bool yes) {
            if (!yes) return;
            auto pl = PlayLayer::get();
            if (!pl) return;
            std::error_code ec;
            std::filesystem::remove(levelFile(pl->m_level), ec);
            loadMarks(pl->m_level);
            buildOverlay(pl);
        });
}

// Menu opened from the AFW pause button.
class AFWMenu : public geode::Popup {
protected:
    bool init() {
        if (!Popup::init(260.f, 230.f)) return false;
        this->setTitle("Auto Frame Window");

        this->setID("menu"_spr);
        auto menu = CCMenu::create();
        menu->setID("menu-buttons"_spr);
        menu->setContentSize({ 220.f, 180.f });
        menu->setLayout(ColumnLayout::create()->setGap(6.f)->setAxisReverse(true)->setAxisAlignment(AxisAlignment::Center));

        auto add = [&](char const* text, SEL_MenuHandler sel, bool enabled = true) {
            auto spr = ButtonSprite::create(text, 180, 180, 0.7f, true, "bigFont.fnt", enabled ? "GJ_button_01.png" : "GJ_button_04.png");
            auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
            menu->addChild(btn);
        };
        add("Measure", menu_selector(AFWMenu::onMeasure));
        add("Re-measure", menu_selector(AFWMenu::onRemeasure), !g_marks.macroPath.empty());
        add("View results", menu_selector(AFWMenu::onView), g_marks.loaded);
        add("Clear results", menu_selector(AFWMenu::onClear), g_marks.loaded);
        add("Settings", menu_selector(AFWMenu::onSettings));
        menu->updateLayout();

        m_mainLayer->addChildAtPosition(menu, Anchor::Center, { 0.f, -12.f });
        return true;
    }

    void onMeasure(CCObject*) { this->onClose(nullptr); pickAndStart(); }
    void onRemeasure(CCObject*) { this->onClose(nullptr); remeasure(); }
    void onView(CCObject*) { showResultsList(); }
    void onClear(CCObject*) { this->onClose(nullptr); clearResults(); }
    void onSettings(CCObject*) { openSettingsPopup(Mod::get()); }

public:
    static AFWMenu* create() {
        auto ret = new AFWMenu();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

class $modify(AFWBaseLayer, GJBaseGameLayer) {
    // Inputs fire right after the game processes a tick, same as xdBot does.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return;
        if (!g_run.active) { tickOverlay(pl); return; }
        if (g_run.needReset) return;
        if (g_run.died || g_run.reachedEnd) return;

        auto const& sched = g_meas.attempt().schedule;
        int frame = macroFrame(pl);
        while (g_run.next < sched.size() && frame >= sched[g_run.next].frame) {
            auto const& ev = sched[g_run.next++];
            if (ev.frac > 1e-9) {
                // CBF beta: fire partway through this tick's player update.
                g_run.split = { true, ev.button, ev.down, ev.p2, ev.frac };
                continue;
            }
            g_run.injecting = true;
            GJBaseGameLayer::handleButton(ev.down, ev.button, !ev.p2);
            g_run.injecting = false;
            if (ev.button >= 1 && ev.button <= 3) g_run.held[ev.p2 ? 1 : 0][ev.button] = ev.down;
        }
    }

    // Block real clicks while measuring.
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_run.active && !g_run.injecting && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Fast forward: run as many physics ticks as fit in the frame budget.
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!g_run.active || !pl || static_cast<GJBaseGameLayer*>(pl) != this) {
            return GJBaseGameLayer::update(dt);
        }

        auto budget = std::chrono::milliseconds(Mod::get()->getSettingValue<int64_t>("frame-budget"));
        auto start = Clock::now();

        while (g_run.active) {
            if (g_run.needReset) beginAttempt(pl);

            // 4 ticks per call, same as normal 60fps play. Inputs and deaths are
            // still handled per tick in processCommands / destroyPlayer.
            GJBaseGameLayer::update(4.f / 240.f);

            int nowFrame = macroFrame(pl);
            if (nowFrame == g_run.lastFrame) {
                if (++g_run.stuckTicks > 20000) {
                    g_run.stuck = true;
                    endRun(pl, true);
                    break;
                }
            }
            else {
                g_run.lastFrame = nowFrame;
                g_run.stuckTicks = 0;
            }

            auto const& at = g_meas.attempt();
            if (g_run.died) {
                finishAttempt(pl, false, g_run.deathFrame);
            }
            else if (g_run.reachedEnd || pl->m_levelEndAnimationStarted) {
                finishAttempt(pl, true, macroFrame(pl));
            }
            else if (macroFrame(pl) >= at.stopFrame) {
                finishAttempt(pl, true, macroFrame(pl));
            }

            if (Clock::now() - start >= budget) break;
        }

        if (g_run.active) updateLabel(pl);
    }
};

class $modify(AFWPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_ov = Overlay{};
        loadMarks(level);
        buildOverlay(this);
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (!g_run.active) return PlayLayer::destroyPlayer(player, object);
        // GD "kills" the player with this at level start as an anticheat check.
        if (object == m_anticheatSpike) return PlayLayer::destroyPlayer(player, object);
        // Deaths after the survive line (can happen inside the same 4-tick batch) don't count.
        if (!g_run.died && !g_run.needReset && macroFrame(this) < g_meas.attempt().stopFrame) {
            g_run.died = true;
            g_run.deathFrame = macroFrame(this);
        }
        // Skip the real death: no death effect, no progress saved.
    }

    void levelComplete() {
        if (g_run.active) { g_run.reachedEnd = true; return; }
        PlayLayer::levelComplete();
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (g_run.active) { g_run.reachedEnd = true; return; }
        PlayLayer::playEndAnimationToPos(pos);
    }

    // Don't add thousands of attempts to the level.
    void updateAttempts() {
        if (g_run.active) return;
        PlayLayer::updateAttempts();
    }

    void pauseGame(bool unfocused) {
        // Alt-tabbing shouldn't cancel a long run.
        if (g_run.active && unfocused) return;
        if (g_run.active) {
            endRun(this, true);
            this->resetLevel();
        }
        PlayLayer::pauseGame(unfocused);
    }

    void onQuit() {
        if (g_run.active) {
            g_run.active = false;
            removeLabel();
        }
        removeHud();
        PlayLayer::onQuit();
    }
};

class $modify(AFWPlayerObject, PlayerObject) {
    // CBF beta: run the tick in two parts with the input in between, like Click Between Frames.
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!g_run.active || !g_run.split.armed || !pl) return PlayerObject::update(dt);
        auto target = g_run.split.p2 ? pl->m_player2 : pl->m_player1;
        if (this != target) return PlayerObject::update(dt);

        auto sp = g_run.split;
        g_run.split.armed = false;
        float first = dt * (float)sp.frac;
        bool startedOnGround = m_isOnGround;

        PlayerObject::update(first);
        if ((m_yVelocity < 0) ^ m_isUpsideDown) m_isOnGround = startedOnGround;
        if (!m_isOnSlope || m_isDart) pl->checkCollisions(this, 0.f, true);
        else pl->checkCollisions(this, dt, true);

        g_run.injecting = true;
        pl->handleButton(sp.down, sp.button, !sp.p2);
        g_run.injecting = false;
        if (sp.button >= 1 && sp.button <= 3) g_run.held[sp.p2 ? 1 : 0][sp.button] = sp.down;

        PlayerObject::update(dt - first);
    }

    void incrementJumps() {
        if (g_run.active) return;
        PlayerObject::incrementJumps();
    }
};

class $modify(AFWStats, GameStatsManager) {
    void incrementStat(char const* key, int amount) {
        if (g_run.active) return;
        GameStatsManager::incrementStat(key, amount);
    }
};

class $modify(AFWPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) return;

        auto spr = CircleButtonSprite::createWithSprite("afw_icon.png"_spr, 1.15f, CircleBaseColor::Cyan, CircleBaseSize::Small);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AFWPauseLayer::onAutoFrameWindow));
        btn->setID("start-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onAutoFrameWindow(CCObject*) {
        if (auto menu = AFWMenu::create()) menu->show();
    }
};
