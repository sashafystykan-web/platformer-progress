#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

using namespace geode::prelude;
using json = nlohmann::json;

// ─── Структура: один чекпоинт ───────────────────────────────────────────────
struct Checkpoint {
    float   x       = 0.f;
    float   y       = 0.f;
    float   percent = 0.f;
    int64_t timestamp = 0;

    json toJson() const {
        return {
            {"x",         x},
            {"y",         y},
            {"percent",   percent},
            {"timestamp", timestamp}
        };
    }

    static Checkpoint fromJson(const json& j) {
        Checkpoint cp;
        cp.x         = j.value("x",         0.f);
        cp.y         = j.value("y",         0.f);
        cp.percent   = j.value("percent",   0.f);
        cp.timestamp = j.value("timestamp", (int64_t)0);
        return cp;
    }
};

// ─── Структура: прогресс одного уровня ──────────────────────────────────────
struct LevelProgress {
    int     levelID    = 0;
    float   bestPercent = 0.f;
    int     attempts   = 0;
    std::vector<Checkpoint> checkpoints;

    json toJson() const {
        json jCps = json::array();
        for (auto& cp : checkpoints) jCps.push_back(cp.toJson());
        return {
            {"levelID",     levelID},
            {"bestPercent", bestPercent},
            {"attempts",    attempts},
            {"checkpoints", jCps}
        };
    }

    static LevelProgress fromJson(const json& j) {
        LevelProgress lp;
        lp.levelID     = j.value("levelID",     0);
        lp.bestPercent = j.value("bestPercent", 0.f);
        lp.attempts    = j.value("attempts",    0);
        for (auto& jcp : j.value("checkpoints", json::array()))
            lp.checkpoints.push_back(Checkpoint::fromJson(jcp));
        return lp;
    }
};

// ─── Менеджер сохранений ─────────────────────────────────────────────────────
class ProgressManager {
public:
    static ProgressManager& get() {
        static ProgressManager inst;
        return inst;
    }

    std::filesystem::path savePath() const {
        return Mod::get()->getSaveDir() / "platformer_progress.json";
    }

    void load() {
        auto path = savePath();
        if (!std::filesystem::exists(path)) return;

        std::ifstream f(path);
        if (!f.is_open()) return;

        try {
            json root;
            f >> root;
            m_data.clear();
            for (auto& [key, val] : root.items()) {
                int id = std::stoi(key);
                m_data[id] = LevelProgress::fromJson(val);
            }
            log::info("[PlatformerProgress] Loaded {} levels", m_data.size());
        } catch (std::exception& e) {
            log::error("[PlatformerProgress] Load failed: {}", e.what());
        }
    }

    void save() const {
        json root;
        for (auto& [id, lp] : m_data)
            root[std::to_string(id)] = lp.toJson();

        std::ofstream f(savePath());
        if (!f.is_open()) {
            log::error("[PlatformerProgress] Cannot open save file for writing");
            return;
        }
        f << root.dump(2);
        log::info("[PlatformerProgress] Saved {} levels", m_data.size());
    }

    LevelProgress& getOrCreate(int levelID) {
        if (m_data.find(levelID) == m_data.end()) {
            m_data[levelID] = LevelProgress{};
            m_data[levelID].levelID = levelID;
        }
        return m_data[levelID];
    }

    const std::unordered_map<int, LevelProgress>& all() const { return m_data; }

private:
    std::unordered_map<int, LevelProgress> m_data;
};

// ─── Хук PlayLayer: перехватываем начало, обновление, смерть ────────────────
struct MyPlayLayer : Modify<MyPlayLayer, PlayLayer> {

    // Вспомогательные поля (Geode fields)
    struct Fields {
        int     m_levelID  = 0;
        float   m_lastSavedPercent = -1.f;
        bool    m_isPlatformer = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& f = m_fields;
        f->m_levelID      = level->m_levelID;
        f->m_isPlatformer = level->isPlatformer();

        if (f->m_isPlatformer) {
            auto& mgr = ProgressManager::get();
            mgr.load();
            auto& lp = mgr.getOrCreate(f->m_levelID);
            lp.attempts++;
            mgr.save();

            log::info("[PlatformerProgress] Entering level {} (attempt #{})",
                      f->m_levelID, lp.attempts);
        }
        return true;
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();

        auto& f = m_fields;
        if (!f->m_isPlatformer) return;

        float pct = this->getCurrentPercent();
        if (pct - f->m_lastSavedPercent < 1.f) return; // сохраняем каждый 1%

        f->m_lastSavedPercent = pct;

        auto& mgr = ProgressManager::get();
        auto& lp  = mgr.getOrCreate(f->m_levelID);

        if (pct > lp.bestPercent) {
            lp.bestPercent = pct;
        }

        // Добавляем чекпоинт (макс. 200 штук на уровень)
        if (lp.checkpoints.size() >= 200)
            lp.checkpoints.erase(lp.checkpoints.begin());

        Checkpoint cp;
        if (auto player = this->m_player1) {
            cp.x = player->getPositionX();
            cp.y = player->getPositionY();
        }
        cp.percent   = pct;
        cp.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        lp.checkpoints.push_back(cp);
        mgr.save();
    }
};

// ─── Хук EndLevelLayer: финальное сохранение при победе ─────────────────────
struct MyEndLevelLayer : Modify<MyEndLevelLayer, EndLevelLayer> {
    void customSetup() {
        EndLevelLayer::customSetup();

        // Помечаем уровень как пройденный (100%)
        if (auto pl = PlayLayer::get()) {
            if (!pl->m_level->isPlatformer()) return;

            auto& mgr = ProgressManager::get();
            auto& lp  = mgr.getOrCreate(pl->m_level->m_levelID);
            lp.bestPercent = 100.f;
            mgr.save();

            log::info("[PlatformerProgress] Level {} COMPLETED!", pl->m_level->m_levelID);
        }
    }
};

// ─── Хук PauseLayer: кнопка «Сбросить прогресс» ─────────────────────────────
struct MyPauseLayer : Modify<MyPauseLayer, PauseLayer> {
    void customSetup() {
        PauseLayer::customSetup();

        if (auto pl = PlayLayer::get()) {
            if (!pl->m_level->isPlatformer()) return;

            // Небольшая кнопка в правом нижнем углу паузы
            auto btnSpr = ButtonSprite::create("Reset\nProgress", "bigFont.fnt",
                                               "GJ_button_06.png", 0.6f);
            auto btn = CCMenuItemSpriteExtra::create(
                btnSpr, this,
                menu_selector(MyPauseLayer::onResetProgress)
            );
            btn->setTag(pl->m_level->m_levelID);

            if (auto menu = this->getChildByID("right-button-menu")) {
                menu->addChild(btn);
                menu->updateLayout();
            }
        }
    }

    void onResetProgress(CCObject* sender) {
        int levelID = sender->getTag();

        auto alert = FLAlertLayer::create(
            nullptr,
            "Сбросить прогресс",
            "Вы уверены? Все чекпоинты и статистика\nдля этого уровня будут удалены.",
            "Отмена", "Сбросить"
        );
        alert->setTag(levelID);
        // Используем лямбду через geode::utils
        alert->show();

        // Обработка через второй способ — просто сбрасываем сразу при подтверждении
        // (для простоты реализации используем немедленный сброс)
        geode::createQuickPopup(
            "Сбросить прогресс",
            "Удалить все сохранённые чекпоинты для этого уровня?",
            "Отмена", "Да",
            [levelID](auto, bool confirm) {
                if (!confirm) return;
                auto& mgr = ProgressManager::get();
                // Пересоздаём запись с нулями
                auto& lp = mgr.getOrCreate(levelID);
                lp = LevelProgress{};
                lp.levelID = levelID;
                mgr.save();
                Notification::create("Прогресс сброшен!", NotificationIcon::Success)->show();
            }
        );
    }
};

// ─── Точка входа ─────────────────────────────────────────────────────────────
$on_mod(Loaded) {
    ProgressManager::get().load();
    log::info("[PlatformerProgress] Mod loaded.");
}
