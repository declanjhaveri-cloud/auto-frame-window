// Update checker: looks at the latest GitHub release once per session (on the main
// menu). If it's newer, offers a one-click download into geode/mods + restart.
// Not used once the mod is installed from the Geode index (Geode updates it itself).

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>

using namespace geode::prelude;

namespace {

constexpr char const* kLatestUrl =
    "https://api.github.com/repos/declanjhaveri-cloud/auto-frame-window/releases/latest";

bool g_checked = false;

void installUpdate(std::string url, std::string version) {
    auto req = web::WebRequest().userAgent("auto-frame-window").followRedirects(true).get(url);
    async::spawn(std::move(req), [version](web::WebResponse res) {
        if (!res.ok()) {
            FLAlertLayer::create("Update failed",
                gd::string(fmt::format("Download failed (HTTP {}). Get it from the GitHub Releases page instead.", res.code())),
                "OK")->show();
            return;
        }
        auto path = dirs::getModsDir() / (Mod::get()->getID() + ".geode");
        if (auto w = res.into(path); w.isErr()) {
            FLAlertLayer::create("Update failed", gd::string(w.unwrapErr()), "OK")->show();
            return;
        }
        createQuickPopup("Updated", fmt::format("Auto Frame Window <cg>{}</c> is installed. Restart GD to use it.", version),
            "Later", "Restart", [](FLAlertLayer*, bool restart) {
                if (restart) utils::game::restart(true);
            });
    });
}

void checkForUpdate() {
    if (g_checked || !Mod::get()->getSettingValue<bool>("check-updates")) return;
    g_checked = true;

    auto req = web::WebRequest()
        .userAgent("auto-frame-window")
        .header("Accept", "application/vnd.github+json")
        .timeout(std::chrono::seconds(10))
        .get(kLatestUrl);

    async::spawn(std::move(req), [](web::WebResponse res) {
        if (!res.ok()) return;
        auto json = res.json();
        if (json.isErr()) return;
        auto root = std::move(json).unwrap();

        std::string tag = root["tag_name"].asString().unwrapOr("");
        auto latest = VersionInfo::parse(tag);
        if (latest.isErr()) return;
        auto latestV = latest.unwrap();
        if (!(Mod::get()->getVersion() < latestV)) return;

        std::string url;
        if (root["assets"].isArray()) {
            for (auto const& a : root["assets"]) {
                auto name = a["name"].asString().unwrapOr("");
                if (name.ends_with(".geode")) { url = a["browser_download_url"].asString().unwrapOr(""); break; }
            }
        }
        if (url.empty()) return;

        std::string notes = root["body"].asString().unwrapOr("");
        if (notes.size() > 500) notes = notes.substr(0, 500) + "...";
        std::string body = fmt::format("<cy>{}</c> -> <cg>{}</c>\n\n{}", Mod::get()->getVersion().toVString(), tag, notes);

        geode::queueInMainThread([body, url, tag] {
            createQuickPopup("Auto Frame Window update", body, "Later", "Update", 400.f,
                [url, tag](FLAlertLayer*, bool update) {
                    if (update) installUpdate(url, tag);
                });
        });
    });
}

} // namespace

class $modify(AFWMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        checkForUpdate();
        return true;
    }
};
