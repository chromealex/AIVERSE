#pragma once
#include <RmlUi/Core.h>
#include <string>
#include <vector>

namespace rts::client {
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
    bool operator==(const UiAction&) const = default;
};
struct UiSection {             // built per section, flattened into UiState::card for the document
    std::string title, kind;
    std::vector<UiAction> actions;
};
struct UiState {
    std::string screen = "menu";   // menu | lobby | playing | paused | over
    std::string gameName, error, platformInfo;
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
    std::vector<UiAction> topActions;
    std::string clock;
    std::vector<std::string> notices;
    bool hasSelection = false, ownSelection = false;
    std::string selTitle, emptyHint;
    std::vector<std::string> selInfo;
    std::vector<UiAction> scriptCards, scriptStatus, scriptSupport, scriptTop;
    std::string scriptTitle;
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
        h.RegisterMember("alt", &UiAction::alt); h.RegisterMember("altLabel", &UiAction::altLabel);
    }
    c.RegisterArray<std::vector<UiAction>>();
    c.RegisterArray<std::vector<UiItem>>();
    c.RegisterArray<std::vector<std::string>>();

    c.Bind("screen", &b.screen); c.Bind("gameName", &b.gameName); c.Bind("error", &b.error);
    c.Bind("platformInfo", &b.platformInfo); c.Bind("maps", &b.maps);
    c.Bind("worldCounters", &b.worldCounters); c.Bind("entityActions", &b.entityActions); c.Bind("top", &b.top); c.Bind("topActions", &b.topActions); c.Bind("clock", &b.clock); c.Bind("notices", &b.notices);
    c.Bind("hasSelection", &b.hasSelection); c.Bind("ownSelection", &b.ownSelection);
    c.Bind("selTitle", &b.selTitle); c.Bind("emptyHint", &b.emptyHint); c.Bind("selInfo", &b.selInfo);
    c.Bind("scriptTop", &b.scriptTop); c.Bind("scriptCards", &b.scriptCards); c.Bind("scriptStatus", &b.scriptStatus); c.Bind("scriptSupport", &b.scriptSupport); c.Bind("scriptTitle", &b.scriptTitle);
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
#define RTS_SYNC(name)                                       \
    if (!(s.name == bound_.name)) {                          \
        bound_.name = std::move(s.name);                     \
        model_.DirtyVariable(#name);                         \
    }
        RTS_SYNC(screen) RTS_SYNC(gameName) RTS_SYNC(error) RTS_SYNC(platformInfo) RTS_SYNC(maps)
        RTS_SYNC(worldCounters) RTS_SYNC(entityActions) RTS_SYNC(top) RTS_SYNC(topActions) RTS_SYNC(clock) RTS_SYNC(notices) RTS_SYNC(hasSelection) RTS_SYNC(ownSelection)
        RTS_SYNC(scriptTop) RTS_SYNC(scriptCards) RTS_SYNC(scriptStatus) RTS_SYNC(scriptSupport) RTS_SYNC(scriptTitle)
        RTS_SYNC(selTitle) RTS_SYNC(emptyHint) RTS_SYNC(selInfo) RTS_SYNC(card) RTS_SYNC(overTitle) RTS_SYNC(overTime)
        RTS_SYNC(dispatchCount) RTS_SYNC(dispatchCaption) RTS_SYNC(dispatchRatio) RTS_SYNC(dispatchStyle)
        RTS_SYNC(modeHint) RTS_SYNC(hintStyle) RTS_SYNC(boxStyle) RTS_SYNC(minimapStyle) RTS_SYNC(lines) RTS_SYNC(touch) RTS_SYNC(showCard)
        RTS_SYNC(languages) RTS_SYNC(lang) RTS_SYNC(tools) RTS_SYNC(hasHowto) RTS_SYNC(howtoOpen)
        RTS_SYNC(howtoLike) RTS_SYNC(howtoGoal) RTS_SYNC(howtoTip) RTS_SYNC(howtoSteps) RTS_SYNC(lobby) RTS_SYNC(netStatus) RTS_SYNC(netError) RTS_SYNC(roomTitle) RTS_SYNC(roomInfo)
        RTS_SYNC(spectators) RTS_SYNC(waitHint) RTS_SYNC(queues) RTS_SYNC(rooms) RTS_SYNC(createMaps) RTS_SYNC(slots) RTS_SYNC(chat)
        RTS_SYNC(isHost) RTS_SYNC(myReady) RTS_SYNC(canStart) RTS_SYNC(netMatch) RTS_SYNC(searching)
#undef RTS_SYNC
}
} // namespace rts::client
