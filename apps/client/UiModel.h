#pragma once
#include <algorithm>
#include "engine/data/Json.h"
#include <RmlUi/Core.h>
#include <string>
#include <vector>

namespace aiverse::client {
// Values bound to the "game" data model (see engine/assets/ui/*.rml). Compared every frame so only
// changed variables are marked dirty.
struct UiItem {          // resource bar entry
    std::string text, sub, style;  // style: inline css (player colour)
    bool warn = false;
    float progress = -1;
    bool operator==(const UiItem&) const = default;
};
struct UiAction {        // command card button; `id` indexes GameClient::actions_
    std::string label, cost, hotkey, tip, image, frame;
    std::string kind;         // layout of the section it belongs to: css class ("list", "row", "grid", "queue")
    bool header = false;      // section start: shows `label` as a title (may be empty: just a separator)
    bool enabled = true, active = false;
    float progress = -1;  // >= 0: shows a progress bar (production queue)
    int id = 0;
    int alt = -1;             // optional second button of the row (e.g. "watch" next to "join")
    std::string altLabel;
    std::string page, cellStyle;
    int column = -1, row = -1;
    bool positioned = false;
    bool operator==(const UiAction&) const = default;
};
inline void applyCardSlot(UiAction& action, const aiverse::Json& slot) {
    if (!slot.isObject()) return;
    action.positioned = true;
    action.page = slot.getStr("page");
    action.column = int(std::clamp<int64_t>(slot.getInt("column", -1), -1, 15));
    action.row = int(std::clamp<int64_t>(slot.getInt("row", -1), -1, 31));
}

struct SelectedUnit {
    std::string type, name, icon, subgroup, unitClass, damageType, armorType;
    float hp = 0, maxHp = 0, hpFraction = 0, energy = 0, attackMin = 0, attackMax = 0, armor = 0;
    bool invulnerable = false, hasInventory = false;
    int selectAction = -1;
    bool operator==(const SelectedUnit&) const = default;
};

struct UiSection {             // built per section, flattened into UiState::card for the document
    std::string title, kind;
    std::vector<UiAction> actions;
};
struct UiState {
    std::string screen = "menu";   // menu | lobby | playing | paused | over
    std::string gameName, error, platformInfo;
    bool lanSupported = false;
    std::vector<UiAction> lanHosts;
    std::vector<std::string> saveSlots, replaySlots;
    bool canSaveMatch = false, watchingReplay = false, replayPaused = false;
    std::string replaySpeed;
    std::vector<std::string> maps;
    std::vector<UiAction> languages;
    std::string lang;                  // active language: argument of the `tr` transform in documents
    // lobby (Lobby.cpp)
    std::string lobby;             // connect | connecting | browse | room
    std::string netStatus, netError, roomTitle, roomInfo, spectators, waitHint;
    std::vector<UiAction> queues, rooms, createMaps, slots;
    std::vector<std::string> chat;
    bool isHost = false, myReady = false, canStart = false, netMatch = false, searching = false;
    std::vector<UiItem> top, entityActions, worldCounters;
    std::vector<UiAction> topActions, selectionShortcuts;
    std::string clock;
    std::vector<std::string> notices;
    std::vector<UiItem> noticeRows, messageHistory;
    bool messageLogOpen = false, hasMessageLog = false, canChat = false;
    bool hasSelection = false, ownSelection = false, hasPortrait = false, showPortrait = false;
    std::string selTitle, emptyHint;
    std::vector<std::string> selInfo, selectedStatuses;
    std::vector<SelectedUnit> selectedUnits;
    std::vector<UiAction> scriptCards, scriptStatus, scriptSupport, scriptTop;
    std::string scriptTitle;
    std::string cardGridStyle, cardPage;
    std::vector<UiAction> card;       // flat: one nesting level keeps RmlUi's data-for updates simple
    std::string overTitle, overTime;
    std::string modeHint, hintStyle, boxStyle, minimapStyle;
    std::string dispatchCount, dispatchCaption, dispatchRatio, dispatchStyle;
    std::vector<std::string> lines;   // send-drag lines as inline styles of rotated bars
    bool touch = false, showCard = true;
    std::vector<UiAction> tools;      // skills bar: player abilities, dig / room / road tools, hand, possession
    // "How to play" (game lang keys howto.like / goal / step1.. / tip)
    bool hasHowto = false, howtoOpen = false;
    std::string howtoLike, howtoGoal, howtoTip;
    std::vector<std::string> howtoSteps;
};

inline void bindUiState(Rml::DataModelConstructor& c, UiState& b) {
    if (auto h = c.RegisterStruct<SelectedUnit>()) {
        h.RegisterMember("type", &SelectedUnit::type);
        h.RegisterMember("name", &SelectedUnit::name);
        h.RegisterMember("icon", &SelectedUnit::icon);
        h.RegisterMember("subgroup", &SelectedUnit::subgroup);
        h.RegisterMember("unitClass", &SelectedUnit::unitClass);
        h.RegisterMember("damageType", &SelectedUnit::damageType);
        h.RegisterMember("armorType", &SelectedUnit::armorType);
        h.RegisterMember("hp", &SelectedUnit::hp);
        h.RegisterMember("maxHp", &SelectedUnit::maxHp);
        h.RegisterMember("hpFraction", &SelectedUnit::hpFraction);
        h.RegisterMember("energy", &SelectedUnit::energy);
        h.RegisterMember("attackMin", &SelectedUnit::attackMin);
        h.RegisterMember("attackMax", &SelectedUnit::attackMax);
        h.RegisterMember("armor", &SelectedUnit::armor);
        h.RegisterMember("invulnerable", &SelectedUnit::invulnerable);
        h.RegisterMember("hasInventory", &SelectedUnit::hasInventory);
        h.RegisterMember("selectAction", &SelectedUnit::selectAction);
    }
    c.RegisterArray<std::vector<SelectedUnit>>();
    if (auto h = c.RegisterStruct<UiItem>()) {
        h.RegisterMember("text", &UiItem::text); h.RegisterMember("sub", &UiItem::sub);
        h.RegisterMember("style", &UiItem::style); h.RegisterMember("warn", &UiItem::warn);
        h.RegisterMember("progress", &UiItem::progress);
    }
    if (auto h = c.RegisterStruct<UiAction>()) {
        h.RegisterMember("frame", &UiAction::frame); h.RegisterMember("image", &UiAction::image); h.RegisterMember("label", &UiAction::label); h.RegisterMember("cost", &UiAction::cost);
        h.RegisterMember("hotkey", &UiAction::hotkey); h.RegisterMember("tip", &UiAction::tip);
        h.RegisterMember("enabled", &UiAction::enabled); h.RegisterMember("active", &UiAction::active);
        h.RegisterMember("progress", &UiAction::progress); h.RegisterMember("id", &UiAction::id);
        h.RegisterMember("kind", &UiAction::kind); h.RegisterMember("header", &UiAction::header);
        h.RegisterMember("page", &UiAction::page); h.RegisterMember("column", &UiAction::column);
        h.RegisterMember("row", &UiAction::row); h.RegisterMember("positioned", &UiAction::positioned);
        h.RegisterMember("cellStyle", &UiAction::cellStyle);
        h.RegisterMember("alt", &UiAction::alt); h.RegisterMember("altLabel", &UiAction::altLabel);
    }
    c.RegisterArray<std::vector<UiAction>>();
    c.RegisterArray<std::vector<UiItem>>();
    c.RegisterArray<std::vector<std::string>>();

    c.Bind("screen", &b.screen); c.Bind("gameName", &b.gameName); c.Bind("error", &b.error);
    c.Bind("lanSupported",&b.lanSupported); c.Bind("lanHosts",&b.lanHosts);
    c.Bind("saveSlots",&b.saveSlots); c.Bind("replaySlots",&b.replaySlots); c.Bind("canSaveMatch",&b.canSaveMatch);
    c.Bind("watchingReplay",&b.watchingReplay); c.Bind("replayPaused",&b.replayPaused); c.Bind("replaySpeed",&b.replaySpeed);
    c.Bind("platformInfo", &b.platformInfo); c.Bind("maps", &b.maps);
    c.Bind("worldCounters", &b.worldCounters); c.Bind("entityActions", &b.entityActions); c.Bind("top", &b.top); c.Bind("topActions", &b.topActions); c.Bind("clock", &b.clock); c.Bind("notices", &b.notices);
    c.Bind("noticeRows", &b.noticeRows); c.Bind("messageHistory", &b.messageHistory);
    c.Bind("messageLogOpen", &b.messageLogOpen); c.Bind("hasMessageLog", &b.hasMessageLog); c.Bind("canChat", &b.canChat);
    c.Bind("selectedUnits", &b.selectedUnits); c.Bind("selectedStatuses", &b.selectedStatuses);
    c.Bind("selectionShortcuts", &b.selectionShortcuts);
    c.Bind("hasPortrait",&b.hasPortrait); c.Bind("showPortrait",&b.showPortrait);
    c.Bind("hasSelection", &b.hasSelection); c.Bind("ownSelection", &b.ownSelection);
    c.Bind("selTitle", &b.selTitle); c.Bind("emptyHint", &b.emptyHint); c.Bind("selInfo", &b.selInfo);
    c.Bind("scriptTop", &b.scriptTop); c.Bind("scriptCards", &b.scriptCards); c.Bind("scriptStatus", &b.scriptStatus); c.Bind("scriptSupport", &b.scriptSupport); c.Bind("scriptTitle", &b.scriptTitle);
    c.Bind("cardGridStyle", &b.cardGridStyle); c.Bind("cardPage", &b.cardPage);
    c.Bind("card", &b.card); c.Bind("overTitle", &b.overTitle); c.Bind("overTime", &b.overTime);
    c.Bind("dispatchCount", &b.dispatchCount); c.Bind("dispatchCaption", &b.dispatchCaption);
    c.Bind("dispatchRatio", &b.dispatchRatio); c.Bind("dispatchStyle", &b.dispatchStyle);
    c.Bind("modeHint", &b.modeHint); c.Bind("hintStyle", &b.hintStyle); c.Bind("boxStyle", &b.boxStyle);
    c.Bind("minimapStyle", &b.minimapStyle); c.Bind("lines", &b.lines); c.Bind("touch", &b.touch); c.Bind("showCard", &b.showCard);
    c.Bind("languages", &b.languages); c.Bind("lang", &b.lang);
    c.Bind("tools", &b.tools); c.Bind("hasHowto", &b.hasHowto); c.Bind("howtoOpen", &b.howtoOpen);
    c.Bind("howtoLike", &b.howtoLike); c.Bind("howtoGoal", &b.howtoGoal); c.Bind("howtoTip", &b.howtoTip); c.Bind("howtoSteps", &b.howtoSteps);
    c.Bind("lobby", &b.lobby); c.Bind("netStatus", &b.netStatus); c.Bind("netError", &b.netError);
    c.Bind("roomTitle", &b.roomTitle); c.Bind("roomInfo", &b.roomInfo); c.Bind("spectators", &b.spectators); c.Bind("waitHint", &b.waitHint);
    c.Bind("queues", &b.queues); c.Bind("rooms", &b.rooms); c.Bind("createMaps", &b.createMaps); c.Bind("slots", &b.slots);
    c.Bind("chat", &b.chat); c.Bind("isHost", &b.isHost); c.Bind("myReady", &b.myReady); c.Bind("canStart", &b.canStart);
    c.Bind("netMatch", &b.netMatch); c.Bind("searching", &b.searching);
}

inline void syncUiState(Rml::DataModelHandle model_, UiState& bound_, UiState s) {
#define AIVERSE_SYNC(name)                                       \
    if (!(s.name == bound_.name)) {                          \
        bound_.name = std::move(s.name);                     \
        model_.DirtyVariable(#name);                         \
    }
        AIVERSE_SYNC(screen) AIVERSE_SYNC(gameName) AIVERSE_SYNC(error) AIVERSE_SYNC(platformInfo) AIVERSE_SYNC(maps) AIVERSE_SYNC(saveSlots) AIVERSE_SYNC(replaySlots) AIVERSE_SYNC(lanSupported) AIVERSE_SYNC(lanHosts)
        AIVERSE_SYNC(canSaveMatch) AIVERSE_SYNC(watchingReplay) AIVERSE_SYNC(replayPaused) AIVERSE_SYNC(replaySpeed)
        AIVERSE_SYNC(worldCounters) AIVERSE_SYNC(entityActions) AIVERSE_SYNC(top) AIVERSE_SYNC(topActions) AIVERSE_SYNC(clock) AIVERSE_SYNC(notices) AIVERSE_SYNC(hasSelection) AIVERSE_SYNC(ownSelection) AIVERSE_SYNC(hasPortrait) AIVERSE_SYNC(showPortrait)
        AIVERSE_SYNC(selectedUnits) AIVERSE_SYNC(selectedStatuses) AIVERSE_SYNC(selectionShortcuts) AIVERSE_SYNC(noticeRows) AIVERSE_SYNC(messageHistory) AIVERSE_SYNC(messageLogOpen) AIVERSE_SYNC(hasMessageLog) AIVERSE_SYNC(canChat)
        AIVERSE_SYNC(scriptTop) AIVERSE_SYNC(scriptCards) AIVERSE_SYNC(scriptStatus) AIVERSE_SYNC(scriptSupport) AIVERSE_SYNC(scriptTitle)
        AIVERSE_SYNC(selTitle) AIVERSE_SYNC(emptyHint) AIVERSE_SYNC(selInfo) AIVERSE_SYNC(card) AIVERSE_SYNC(cardGridStyle) AIVERSE_SYNC(cardPage) AIVERSE_SYNC(overTitle) AIVERSE_SYNC(overTime)
        AIVERSE_SYNC(dispatchCount) AIVERSE_SYNC(dispatchCaption) AIVERSE_SYNC(dispatchRatio) AIVERSE_SYNC(dispatchStyle)
        AIVERSE_SYNC(modeHint) AIVERSE_SYNC(hintStyle) AIVERSE_SYNC(boxStyle) AIVERSE_SYNC(minimapStyle) AIVERSE_SYNC(lines) AIVERSE_SYNC(touch) AIVERSE_SYNC(showCard)
        AIVERSE_SYNC(languages) AIVERSE_SYNC(lang) AIVERSE_SYNC(tools) AIVERSE_SYNC(hasHowto) AIVERSE_SYNC(howtoOpen)
        AIVERSE_SYNC(howtoLike) AIVERSE_SYNC(howtoGoal) AIVERSE_SYNC(howtoTip) AIVERSE_SYNC(howtoSteps) AIVERSE_SYNC(lobby) AIVERSE_SYNC(netStatus) AIVERSE_SYNC(netError) AIVERSE_SYNC(roomTitle) AIVERSE_SYNC(roomInfo)
        AIVERSE_SYNC(spectators) AIVERSE_SYNC(waitHint) AIVERSE_SYNC(queues) AIVERSE_SYNC(rooms) AIVERSE_SYNC(createMaps) AIVERSE_SYNC(slots) AIVERSE_SYNC(chat)
        AIVERSE_SYNC(isHost) AIVERSE_SYNC(myReady) AIVERSE_SYNC(canStart) AIVERSE_SYNC(netMatch) AIVERSE_SYNC(searching)
#undef AIVERSE_SYNC
}
} // namespace aiverse::client
