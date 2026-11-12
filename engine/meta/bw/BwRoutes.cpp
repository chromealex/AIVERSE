// Route catalog: engine meta (service, action) -> Bunker Wars API path. Paths come from the Unity
// client's Connector/NetworkManager calls; kChunk marks controllers with @ChunkedMethod in
// BunkerWars-Server mw2-server-web (only those accept {path}/chunk).
#include "BwBackend.h"

#include <cctype>

namespace aiverse::meta::bw {

namespace {
constexpr unsigned M = kMutates, C = kChunk, R = kRead, E = kCache;
} // namespace

const std::vector<Route>& routes() {
    static const std::vector<Route> table = {
        // auth / session
        {"auth", "entryPoint", "v/entryPoint", R | kNoAuth},
        {"auth", "login", "v/profile/auth", M | kNoAuth},
        {"auth", "checkServer", "v/checkServer", R | kNoAuth},
        // profile
        {"profile", "get", "v/profile/get", M | C},   // the server recalculates and stamps the login time
        {"profile", "byId", "v/profile/byId", R | C},
        {"profile", "saveName", "v/profile/saveName", M | C},
        {"profile", "saveAvatar", "v/profile/saveAvatar", M | C},
        {"profile", "saveAvatarBorder", "v/profile/saveAvatarBorder", M | C},
        {"profile", "updateLang", "v/profile/updateLang", M | C},
        {"profile", "updateApn", "v/profile/updateApn", M | C},
        {"profile", "setValue", "v/profile/setValue", M | C},
        {"profile", "setSharedData", "v/profile/setSharedData", M | C},
        {"profile", "binData", "v/profile/binData", R | C},
        {"profile", "binDataUpdate", "v/profile/binDataUpdate", M | C},
        {"profile", "clientInfo", "v/profile/clientInfo", M | C},
        {"profile", "addSocial", "v/profile/addSocial", M | C},
        {"profile", "removeSocial", "v/profile/removeSocial", M | C},
        {"profile", "linkGameCenter", "v/profile/linkgc", M | C},
        {"profile", "merge", "v/profile/merge", M | C},
        {"profile", "delete", "v/profile/delete", M | C},
        {"profile", "rateUs", "v/profile/rateUs", M | C},
        {"profile", "rateUsV2", "v/profile/rateUsV2", M | C},
        {"profile", "dailyBonus", "v/profile/getDailyBonusReward", M},
        {"profile", "placementReward", "v/profile/getPlacementReward", M | C},
        {"profile", "clan", "v/profile/clan", R | C},
        {"profile", "seasonStatistic", "v/profile/season/statistic", R},
        {"profile", "heroStatistic", "v/profile/season/statistic/hero", R},
        {"profile", "transferCode", "v/profile/transfer/code", M},
        {"profile", "transferPreview", "v/profile/transfer/preview", R},
        {"profile", "transferComplete", "v/profile/transfer/complete", M},
        {"profile", "adRevenue", "v/profile/adRevenue", M},
        {"profile", "survey", "v/profile/takeFeiyuSurvey", M},
        // inventory
        {"inventory", "setIntoSlot", "v/profile/things/setIntoSlot", M | C},
        {"inventory", "buyCell", "v/inventory/buy-cell", M},
        {"inventory", "sync", "v/inventory/sync-inventory", M},
        {"inventory", "storage", "v/inventory/storage/get", R},
        // heroes, skills, talents
        {"heroes", "buy", "v/hero/buyHero", M},
        {"heroes", "save", "v/hero/saveHero", M | C},
        {"heroes", "setSkin", "v/hero/setHeroSkin", M | C},
        {"heroes", "setUnitSkin", "v/hero/setHeroUnitSkin", M | C},
        {"heroes", "setBuildingSkin", "v/hero/setHeroBuildingSkin", M | C},
        {"heroes", "setEmotion", "v/hero/setEmotion", M | C},
        {"heroes", "charge", "v/hero/charge", M},
        {"heroes", "seasonEndReward", "v/hero/getRewardForSeasonEnd", M | C},
        {"heroes", "trophyRoadClaim", "v/hero/trophyRoad/claim", M | C},
        {"heroes", "skillEquip", "v/hero/skill/equip", M},
        {"heroes", "skillUnequip", "v/hero/skill/unequip", M},
        {"heroes", "talentSelect", "v/hero/talent/select", M},
        {"heroes", "skillUpgrade", "v/skill/upgrade", M},
        // chests and cargoes
        {"chests", "start", "v/profile/startchest", M},
        {"chests", "open", "v/profile/openchest", M},
        {"chests", "openCargo", "v/profile/openCargo", M | C},
        {"chests", "openAllCargoes", "v/profile/openAllCargoes", M | C},
        {"chests", "openCampaignChest", "v/profile/openCampaignChest", M | C},
        {"chests", "openTierChest", "v/profile/openTierChest", M | C},
        {"chests", "premature", "v/market/chest/premature", M | C},
        // craft, forge, passive skills
        {"craft", "craft", "v/profile/craft", M | C},
        {"craft", "charge", "v/profile/charge", M},
        {"craft", "passiveSkill", "v/profile/craftPassiveSkill", M},
        {"craft", "passiveBuy", "v/profile/passiveSkill/buy", M},
        {"craft", "passiveReset", "v/profile/passiveSkill/reset", M},
        {"craft", "forgeUpgrade", "v/forge/upgrade", M},
        {"craft", "artifactTake", "v/forge/artifact/take", M},
        {"craft", "artifactSpeedup", "v/forge/artifact/speedup", M},
        {"craft", "artifactDecompose", "v/forge/artifact/decompose", M},
        // shop (soft currency, fair, merchants, roulettes, ads rewards)
        {"shop", "get", "v/market/get", R | C | E},
        {"shop", "getWithProducts", "v/market/getWithProducts", R | C | E},
        {"shop", "view", "v/market/shop", R | C | E},
        {"shop", "fair", "v/market/fair", R | C},
        {"shop", "fairBuy", "v/market/fair/buy", M | C},
        {"shop", "fairBuyChallenge", "v/market/fair/buyChallenge", M | C},
        {"shop", "fairRoll", "v/market/roll", M | C},
        {"shop", "buyGold", "v/market/buyGold", M | C},
        {"shop", "replenishBooster", "v/market/replenishBooster", M | C},
        {"shop", "offerActivate", "v/market/offer/activate", M | C},
        {"shop", "cinema", "v/market/getCinema", R | C},
        {"shop", "claimCinema", "v/market/claimCinemaReward", M | C},
        {"shop", "video", "v/market/video", M | C},
        {"shop", "videoCharges", "v/market/videoCharges", M},
        {"shop", "mines", "v/market/mines/get", R},
        {"shop", "minesClaim", "v/market/mines/claimDailyReward", M},
        {"shop", "merchant", "v/profile/merchant/get", R | C},
        {"shop", "merchantWithProducts", "v/profile/merchant/getWithProducts", R | C},
        {"shop", "merchantSpin", "v/profile/merchant/spin", M},
        {"shop", "markerWithProducts", "v/marker/getWithProducts", R},
        {"shop", "rouletteInit", "v/profile/rouletteInit", M},
        {"shop", "rouletteSpin", "v/profile/rouletteSpin", M},
        {"shop", "rouletteClose", "v/profile/rouletteClose", M},
        {"shop", "miniRouletteExclude", "v/miniRoulette/excludeRewards", M | C},
        // real-money purchases: receipts are validated by the BW server, never locally
        {"purchase", "receipt", "v/market/receipt", M | C},
        {"purchase", "receiptProduct", "v/market/receiptProduct", M | C},
        {"purchase", "restore", "v/market/restore", M | C},
        {"purchase", "products", "v/market/fy/getProducts", R | C},
        {"purchase", "initFeiyu", "v/market/initFeiyu", M | C},
        {"purchase", "receiptFeiyu", "v/market/receiptFeiyu", M | C},
        {"purchase", "initXsolla", "v/market/initXsolla", M},
        {"purchase", "initYookassa", "v/market/initYookassa", M},
        {"purchase", "offerwall", "v/offerwall/tapjoy", M},
        // clans
        {"clan", "create", "v/clans/create", M | C},
        {"clan", "createCheck", "v/clans/createCheck", R | C},
        {"clan", "list", "v/clans/list", R | C},
        {"clan", "leaderboardPosition", "v/clans/getLeaderboardPosition", R | C},
        {"clan", "membershipRequest", "v/clans/membershipRequest", M | C},
        {"clan", "private", "v/clans/private", M | C},
        {"clan", "iconKit", "v/clans/iconCreationKit", R | C},
        {"clan", "commonData", "v/clans/commonData", R | C},
        {"clan", "userRequests", "v/clans/userMembershipRequests", R | C},
        {"clan", "kick", "v/clans/kickUser", M | C},
        {"clan", "resolveRequest", "v/clans/resolveMembershipRequest", M | C},
        {"clan", "participants", "v/clans/participants", R | C},
        {"clan", "role", "v/clans/userRole", M | C},
        {"clan", "edit", "v/clans/edit", M | C},
        {"clan", "shopBuy", "v/clans/shop/buy", M | C},
        {"clan", "takeGift", "v/clans/takeGift", M | C},
        {"clan", "takeSeasonReward", "v/clans/takeSeasonReward", M | C},
        // leagues, boards, trophy road
        {"leagues", "season", "v/hero/leagues/season", R | C},
        {"leagues", "progress", "v/hero/leagues/progress", R | C},
        {"leagues", "participants", "v/hero/leagues/participants", R | C},
        {"leagues", "lastSeason", "v/hero/leagues/lastSeasonResults", R | C},
        {"leagues", "collectLastSeason", "v/hero/leagues/collectLastSeasonRewards", M | C},
        {"board", "get", "v/board/get", R},
        {"board", "hero", "v/board/getHero", R},
        {"board", "league", "v/board/getLeague", R},
        {"board", "heroes", "v/board/heroes", R},
        {"board", "leagueBoard", "v/board/league", R},
        {"board", "arena", "v/arena/get", R},
        {"trophyroad", "data", "v/trophyRoad/getData", R | C},
        {"trophyroad", "track", "v/trophyRoad/getSeasonRewardsTrack", R | C},
        {"trophyroad", "board", "v/trophyRoad/getBoard", R | C},
        {"trophyroad", "status", "v/trophyRoad/getSeasonRewardsStatus", R | C},
        {"trophyroad", "claim", "v/trophyRoad/claimReward", M | C},
        {"trophyroad", "claimRace", "v/trophyRoad/claimRaceReward", M | C},
        // hero battle pass
        {"battlepass", "summary", "v/hero/battlePass/summary", R | C},
        {"battlepass", "levels", "v/hero/battlePass/levels", R | C},
        {"battlepass", "progress", "v/hero/battlePass/progress", R | C},
        {"battlepass", "buyLevels", "v/hero/battlePass/progress/buyLevels", M | C},
        {"battlepass", "rewards", "v/hero/battlePass/rewards", R | C},
        {"battlepass", "claim", "v/hero/battlePass/rewards/claim", M | C},
        {"battlepass", "tasks", "v/hero/battlePass/tasks", R | C},
        {"battlepass", "claimTask", "v/hero/battlePass/tasks/claimReward", M | C},
        {"battlepass", "shop", "v/hero/battlePass/shop", R | C},
        {"battlepass", "shopBuy", "v/hero/battlePass/shop/buy", M | C},
        {"battlepass", "offers", "v/hero/battlePass/offers", R | C},
        {"battlepass", "useOffer", "v/hero/battlePass/offers/use", M | C},
        // quests, meta quests, live activities
        {"quests", "get", "v/profile/quest/get", R | C},
        {"quests", "replace", "v/profile/quest/replace", M | C},
        {"quests", "reward", "v/profile/quest/getReward", M | C},
        {"quests", "reroll", "v/profile/quest/reroll", M},
        {"quests", "daily", "v/profile/quest/rewardDaily", M},
        {"quests", "league", "v/profile/quest/rewardLeague", M},
        {"metaquest", "get", "v/profile/meta-quest", R | C},
        {"metaquest", "claim", "v/profile/meta-quest/claim-reward", M | C},
        {"metaquest", "claimChest", "v/profile/meta-quest/claim-chest", M | C},
        {"liveops", "get", "v/profile/liveActivity", R | C},
        {"liveops", "questReward", "v/profile/liveActivity/quest/reward", M | C},
        {"liveops", "rouletteSpin", "v/roulette/spin", M | C},
        {"liveops", "rouletteVideo", "v/roulette/video", M | C},
        // challenges and tasks
        {"challenges", "get", "v/profile/challenge/get", R | C},
        {"challenges", "reward", "v/profile/challenge/reward", M | C},
        {"challenges", "rewardAll", "v/profile/challenge/rewardAll", M | C},
        {"challenges", "rewardGraph", "v/profile/challenge/rewardGraph", M | C},
        {"challenges", "previousSeason", "v/profile/challenge/previousSeason/rewardAll", M | C},
        {"challenges", "buyVictory", "v/profile/challenge/buyVictoryHeroChallenge", M},
        {"challenges", "reset", "v/profile/challenge/resetHeroChallenge", M},
        {"challenges", "customRules", "v/profile/customRules/get", R},
        {"challenges", "customRulesReward", "v/profile/customRules/reward", M},
        {"tasks", "event", "v/task/event", M},
        {"tasks", "award", "v/award/complete", M},
        {"tasks", "trigger", "v/trigger", M},
        // social
        {"friends", "list", "v/friend/getAll", R | C},
        {"friends", "search", "v/friend/search", R | C},
        {"friends", "add", "v/friend/add", M | C},
        {"friends", "remove", "v/friend/remove", M | C},
        {"friends", "push", "v/friend/push", M | C},
        {"friends", "inviteRewards", "v/deeplink/rewards", R | C},
        {"friends", "inviteLink", "v/deeplink/link", M | C},
        {"friends", "inviteCollect", "v/deeplink/collect", M | C},
        {"friends", "invite", "v/deeplink/invite", M | C},
        {"code", "redeem", "v/code/redeem", M | C},
        {"news", "get", "v/news/get", R | C | E},
        {"notifications", "markRead", "v/feiyuNotifications/markAsRead", M},
        // battle history and replays (replay packets come from the battle node's GameReport)
        {"history", "list", "v/history/get", R | C},
        {"history", "best", "v/history/getBest", R | C},
        {"history", "byId", "v/history/getById", R | C},
        {"history", "replay", "v/history/game", R | C},
        // campaign
        {"campaign", "game", "v/campaign/game", M | C},
        {"campaign", "games", "v/campaign/games", M | C},
        {"campaign", "seasons", "v/campaign/seasons", R | C},
        // matchmaking entry: which battle front to connect to (see BwMatch.h)
        {"match", "choose", "v/game/choose", M | C},
        {"match", "find", "v/game/find", M | C},
        {"match", "like", "v/game/like", M | C},
        {"match", "decline", "v/game/decline", M | C},
        {"match", "opponentInfo", "v/match/getOpponentInfo", R},
        {"match", "buyRaise", "v/match/buyRaiseBattleHero", M},
    };
    return table;
}

const Route* findRoute(const std::string& service, const std::string& action) {
    if (service == "statics") {
        // v/db/<name>: static tables (heroes, skills, chests, craft, ...). Names are path segments only.
        static thread_local std::string path;
        static thread_local Route dyn;
        if (action.empty() || action.size() > 64) return nullptr;
        for (char c : action)
            if (!(std::isalnum(uint8_t(c)) || c == '/' || c == '_' || c == '-') || action.find("..") != std::string::npos) return nullptr;
        path = "v/db/" + action;
        dyn = {"statics", "", path.c_str(), kRead | kChunk | kStatic | kCache};
        return &dyn;
    }
    for (auto& r : routes())
        if (service == r.service && action == r.action) return &r;
    return nullptr;
}

} // namespace aiverse::meta::bw
