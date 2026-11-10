#include "UiLayer.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi/Lua.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>

#include "UiInternal.h"
#include "UiKeyboard.h"
#include "UiLinear.h"

namespace rts::ui {

namespace {

class UiSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        static const char* names[] = {"always", "error", "assert", "warning", "info", "debug"};
        if (type <= Rml::Log::LT_INFO) std::fprintf(stderr, "[ui %s] %s\n", type < 6 ? names[type] : "?", message.c_str());
        return true;
    }
    void JoinPath(Rml::String& out, const Rml::String& documentPath, const Rml::String& path) override {
        if (!path.empty() && path[0] == '#') { out = path; return; } // dynamic texture name
        Rml::SystemInterface::JoinPath(out, documentPath, path);
    }
    int TranslateString(Rml::String& out, const Rml::String& input) override {
        // only whole "@key" strings (surrounding whitespace allowed): plain text stays untouched
        const size_t b = input.find_first_not_of(" \t\r\n"), e = input.find_last_not_of(" \t\r\n");
        if (!translate || b == Rml::String::npos || input[b] != '@' || e == b) { out = input; return 0; }
        for (size_t i = b + 1; i <= e; ++i) {
            const char c = input[i];
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) { out = input; return 0; }
        }
        out = translate(input.substr(b + 1, e - b));
        return 1;
    }
    void SetClipboardText(const Rml::String& text) override { platform::setClipboardText(text); }

    void ActivateKeyboard(Rml::Vector2f caret, float) override { if (keyboard) keyboard->activate(caret.y); }
    void DeactivateKeyboard() override { if (keyboard) keyboard->deactivate(); }

    std::function<std::string(const std::string&)> translate;
    UiKeyboard* keyboard = nullptr;
    void GetClipboardText(Rml::String& text) override { text = platform::clipboardText(); }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// Content files through the platform layer; "engine/ui/x" prefers the game's "ui/x" and vice versa
// falls back, so overrides work for documents and everything they link to.
class UiFiles final : public Rml::FileInterface {
public:
    UiLayer::FileReader reader = platform::readContentFile;
    Rml::FileHandle Open(const Rml::String& path) override {
        auto f = std::make_unique<File>();
        if (!read(path, f->data)) return 0;
        return reinterpret_cast<Rml::FileHandle>(f.release());
    }
    void Close(Rml::FileHandle h) override { delete reinterpret_cast<File*>(h); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle h) override {
        File& f = *reinterpret_cast<File*>(h);
        size = std::min(size, f.data.size() - f.pos);
        std::memcpy(buffer, f.data.data() + f.pos, size);
        f.pos += size;
        return size;
    }
    bool Seek(Rml::FileHandle h, long offset, int origin) override {
        File& f = *reinterpret_cast<File*>(h);
        long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? long(f.pos) : long(f.data.size());
        long p = base + offset;
        if (p < 0 || size_t(p) > f.data.size()) return false;
        f.pos = size_t(p);
        return true;
    }
    size_t Tell(Rml::FileHandle h) override { return reinterpret_cast<File*>(h)->pos; }
    size_t Length(Rml::FileHandle h) override { return reinterpret_cast<File*>(h)->data.size(); }
    bool LoadFile(const Rml::String& path, Rml::String& out) override { return read(path, out); }

private:
    struct File { std::string data; size_t pos = 0; };
    bool read(const std::string& path, std::string& out) {
        static const std::string kEngine = "engine/ui/", kGame = "ui/";
        if (path.rfind(kEngine, 0) == 0 && reader(kGame + path.substr(kEngine.size()), out)) return true;
        if (reader(path, out)) return true;
        if (path.rfind(kGame, 0) == 0) return reader(kEngine + path.substr(kGame.size()), out);
        return false;
    }
};

Rml::Input::KeyIdentifier toRmlKey(platform::Key k) {
    using platform::Key;
    using namespace Rml::Input;
    if (k >= Key::A && k <= Key::Z) return KeyIdentifier(KI_A + (int(k) - int(Key::A)));
    if (k >= Key::Num0 && k <= Key::Num9) return KeyIdentifier(KI_0 + (int(k) - int(Key::Num0)));
    if (k >= Key::F1 && k <= Key::F12) return KeyIdentifier(KI_F1 + (int(k) - int(Key::F1)));
    switch (k) {
    case Key::Escape: return KI_ESCAPE; case Key::Enter: return KI_RETURN; case Key::Tab: return KI_TAB;
    case Key::Backspace: return KI_BACK; case Key::Delete: return KI_DELETE; case Key::Insert: return KI_INSERT;
    case Key::Space: return KI_SPACE; case Key::Left: return KI_LEFT; case Key::Right: return KI_RIGHT;
    case Key::Up: return KI_UP; case Key::Down: return KI_DOWN; case Key::Home: return KI_HOME; case Key::End: return KI_END;
    case Key::PageUp: return KI_PRIOR; case Key::PageDown: return KI_NEXT;
    case Key::LeftShift: return KI_LSHIFT; case Key::RightShift: return KI_RSHIFT;
    case Key::LeftCtrl: return KI_LCONTROL; case Key::RightCtrl: return KI_RCONTROL;
    case Key::LeftAlt: return KI_LMENU; case Key::RightAlt: return KI_RMENU;
    case Key::LeftSuper: return KI_LMETA; case Key::RightSuper: return KI_RMETA;
    case Key::Minus: return KI_OEM_MINUS; case Key::Equal: return KI_OEM_PLUS; case Key::Comma: return KI_OEM_COMMA;
    case Key::Period: return KI_OEM_PERIOD; case Key::Slash: return KI_OEM_2; case Key::Semicolon: return KI_OEM_1;
    case Key::LeftBracket: return KI_OEM_4; case Key::RightBracket: return KI_OEM_6; case Key::Apostrophe: return KI_OEM_7;
    case Key::Backslash: return KI_OEM_5; case Key::GraveAccent: return KI_OEM_3;
    default: return KI_UNKNOWN;
    }
}

int toRmlMods(uint8_t m) {
    using namespace Rml::Input;
    return (m & platform::kModCtrl ? KM_CTRL : 0) | (m & platform::kModShift ? KM_SHIFT : 0) |
           (m & platform::kModAlt ? KM_ALT : 0) | (m & platform::kModSuper ? KM_META : 0);
}

} // namespace

struct UiLayer::Impl {
    UiSystem system;
    UiFiles files;
    UiRenderer renderer;
    UiFontEngine fonts;
    UiKeyboard keyboard;
    UiLinearComposite linear;
    gfx::TextureHandle targetColor, targetDepth;
    Rml::Context* context = nullptr;
    gfx::Device* dev = nullptr;
    int width = 0, height = 0;
    float density = 1;
    int64_t primaryTouch = -1;
    bool debugger = false;
    Rml::Element* textField = nullptr; // field the on-screen keyboard was opened for
};

UiLayer::UiLayer() = default;
UiLayer::~UiLayer() { shutdown(); }

bool UiLayer::init(gfx::Device& dev, const char* gameFont, FileReader reader) {
    impl_ = std::make_unique<Impl>();
    Impl& m = *impl_;
    m.dev = &dev;
    if (reader) m.files.reader = std::move(reader);
    if (!m.renderer.init(dev)) { impl_.reset(); return false; }
    Rml::SetSystemInterface(&m.system);
    Rml::SetFileInterface(&m.files);
    Rml::SetRenderInterface(&m.renderer);
    Rml::SetFontEngineInterface(&m.fonts);
    if (!Rml::Initialise()) { impl_.reset(); return false; }
    // Every family resolves to the first loaded font when unknown, so the game font goes first.
    std::string ttf;
    if (gameFont && m.files.reader(gameFont, ttf) && !ttf.empty())
        m.fonts.addFont(std::move(ttf), "ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto);
    if (m.files.reader("engine/fonts/ui.ttf", ttf) && !ttf.empty())
        m.fonts.addFont(std::move(ttf), "ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto);
    // headings: the game's display font as family "title" (falls back to "ui" when absent)
    if (m.files.reader("fonts/title.ttf", ttf) && !ttf.empty())
        m.fonts.addFont(std::move(ttf), "title", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto);

    m.context = Rml::CreateContext("main", {platform::framebufferWidth(), platform::framebufferHeight()});
    if (!m.context) { shutdown(); return false; }
    m.context->SetDensityIndependentPixelRatio(platform::dpiScale());
    Rml::Debugger::Initialise(m.context);
    m.system.keyboard = &m.keyboard;
    setOnScreenKeyboard(OnScreenKeyboard::Auto);
    m.keyboard.init(*m.context); // its document is created last: drawn above the game's documents
    return true;
}

void UiLayer::shutdown() {
    if (!impl_) return;
    if (impl_->context) {
        impl_->system.keyboard = nullptr;
        impl_->keyboard.shutdown();
        Rml::Debugger::Shutdown();
        Rml::Shutdown(); // releases contexts, documents and all render resources
        impl_->context = nullptr;
    }
    impl_->linear.shutdown();
    impl_->renderer.shutdown();
    impl_.reset();
}

void UiLayer::attachLua(lua_State* L) {
    if (L && context()) Rml::Lua::Initialise(L);   // RmlUi is up: the plugin initialises at once
}

Rml::Context* UiLayer::context() const { return impl_ ? impl_->context : nullptr; }

Rml::ElementDocument* UiLayer::loadDocument(const std::string& name) {
    if (!context()) return nullptr;
    Rml::ElementDocument* doc = impl_->context->LoadDocument("engine/ui/" + name);
    if (doc) doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return doc;
}

void UiLayer::setDynamicTexture(const std::string& name, gfx::TextureHandle tex, int w, int h) {
    if (impl_) impl_->renderer.setDynamicTexture(name, tex, w, h);
}

void UiLayer::setTranslator(std::function<std::string(const std::string&)> fn) {
    if (impl_) impl_->system.translate = std::move(fn);
}

void UiLayer::setOnScreenKeyboard(OnScreenKeyboard mode) {
    if (!impl_) return;
    const char* env = std::getenv("RTS_OSK");
    const bool on = mode == OnScreenKeyboard::Always ||
                    (mode == OnScreenKeyboard::Auto && (platform::isTouchDevice() || (env && env[0] == '1')));
    impl_->keyboard.setEnabled(on);
}

void UiLayer::setKeyboardLanguage(const std::string& tag) { if (impl_) impl_->keyboard.setLanguage(tag); }
bool UiLayer::keyboardVisible() const { return impl_ && impl_->keyboard.visible(); }

void UiLayer::setImageSource(ImageSource source) {
    if (impl_) impl_->renderer.setImageSource(std::move(source));
}

float UiLayer::dpRatio() const { return context() ? impl_->context->GetDensityIndependentPixelRatio() : 1.0f; }

bool UiLayer::textInputFocused() const {
    if (!context()) return false;
    Rml::Element* f = impl_->context->GetFocusElement();
    return f && (f->GetTagName() == "input" || f->GetTagName() == "textarea" || f->GetTagName() == "select");
}

namespace {
// Fields that take typed text (checkboxes, ranges, buttons and selects don't need a keyboard).
bool isTextField(Rml::Element* f) {
    if (!f) return false;
    if (f->GetTagName() == "textarea") return true;
    if (f->GetTagName() != "input") return false;
    const std::string type = f->GetAttribute<Rml::String>("type", "text");
    return type == "text" || type == "password" || type == "search" || type == "email" || type == "number";
}
} // namespace

void UiLayer::syncTextInput() {
    if (!context()) return;
    Rml::Element* f = impl_->context->GetFocusElement();
    if (!isTextField(f)) f = nullptr;
    if (f) {
        if (f != impl_->textField || !platform::textInputActive()) {
            const float s = platform::dpiScale();
            const Rml::Vector2f pos = f->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = f->GetBox().GetSize(Rml::BoxArea::Border);
            platform::startTextInput({pos.x / s, pos.y / s, size.x / s, size.y / s});
        }
    } else if (impl_->textField) {
        platform::stopTextInput();
    }
    impl_->textField = f;
}

void UiLayer::toggleDebugger() {
    if (!impl_) return;
    impl_->debugger = !impl_->debugger;
    Rml::Debugger::SetVisible(impl_->debugger);
}

bool UiLayer::pointerOverUi() const {
    auto* hover=context()?context()->GetHoverElement():nullptr;
    return hover&&hover->GetOwnerDocument();
}

bool UiLayer::pointerHasAttribute(const char* name) const {
    for (Rml::Element* el = context() ? context()->GetHoverElement() : nullptr; el; el = el->GetParentNode())
        if (el->HasAttribute(name)) return true;
    return false;
}

bool UiLayer::handleEvent(const platform::Event& e) {
    if (e.type == platform::EventType::TextInputEnded && context()) {
        // the user closed the keyboard: leave the field so the next tap opens it again
        if (Rml::Element* f = impl_->context->GetFocusElement(); f && f == impl_->textField) f->Blur();
        impl_->textField = nullptr;
        return false;
    }
    bool consumed = processEvent(e);
    // inside the input handler on purpose: mobile browsers open the keyboard only during a gesture
    syncTextInput();
    return consumed;
}

bool UiLayer::processEvent(const platform::Event& e) {
    using platform::EventType;
    if (!context()) return false;
    Impl& m = *impl_;
    Rml::Context& c = *m.context;
    const float s = platform::dpiScale();
    const int x = int(e.x * s), y = int(e.y * s), mods = toRmlMods(e.mods);
    switch (e.type) {
    case EventType::MouseMove: return !c.ProcessMouseMove(x, y, mods);
    case EventType::MouseDown:
        c.ProcessMouseMove(x, y, mods);
        return !c.ProcessMouseButtonDown(e.button, mods);
    case EventType::MouseUp:
        c.ProcessMouseMove(x, y, mods);
        return !c.ProcessMouseButtonUp(e.button, mods);
    case EventType::MouseWheel: return !c.ProcessMouseWheel(Rml::Vector2f(-e.wheelX, -e.wheelY), mods);
    case EventType::TouchBegin:
        if (m.primaryTouch != -1) return false;
        c.ProcessMouseMove(x, y, 0);
        if (c.ProcessMouseButtonDown(0, 0)) { // not on the UI: the game owns it
            c.ProcessMouseLeave();
            // a tap outside a text field closes the on-screen keyboard, as in native apps
            if (Rml::Element* f = c.GetFocusElement(); isTextField(f)) f->Blur();
            return false;
        }
        m.primaryTouch = e.touchId;
        return true;
    case EventType::TouchMove:
        if (e.touchId != m.primaryTouch) return false;
        c.ProcessMouseMove(x, y, 0);
        return true;
    case EventType::TouchEnd: case EventType::TouchCancel:
        if (e.touchId != m.primaryTouch) return false;
        m.primaryTouch = -1;
        c.ProcessMouseMove(x, y, 0);
        c.ProcessMouseButtonUp(0, 0);
        c.ProcessMouseLeave(); // no hover state on touch screens
        return true;
    case EventType::KeyDown: {
        if (e.key == platform::Key::Unknown) return false;
        bool notConsumed = c.ProcessKeyDown(toRmlKey(e.key), mods);
        return !notConsumed || textInputFocused();
    }
    case EventType::KeyUp:
        if (e.key == platform::Key::Unknown) return false;
        return !c.ProcessKeyUp(toRmlKey(e.key), mods) || textInputFocused();
    case EventType::Char:
        if (e.codepoint < 32 || e.codepoint == 127) return textInputFocused();
        return !c.ProcessTextInput(Rml::Character(e.codepoint)) || textInputFocused();
    case EventType::FocusLost: c.ProcessMouseLeave(); return false;
    default: return false;
    }
}

void UiLayer::setViewport(int width, int height, float density) {
    if (!impl_) return;
    impl_->width = width; impl_->height = height; impl_->density = std::max(.01f, density);
}

void UiLayer::update() {
    if (!context()) return;
    Rml::Context& c = *impl_->context;
    const Rml::Vector2i dims{impl_->width > 0 ? impl_->width : platform::framebufferWidth(),
                            impl_->height > 0 ? impl_->height : platform::framebufferHeight()};
    const float density = impl_->width > 0 ? impl_->density : platform::dpiScale();
    if (c.GetDimensions() != dims) c.SetDimensions(dims);
    if (c.GetDensityIndependentPixelRatio() != density) c.SetDensityIndependentPixelRatio(density);
    impl_->keyboard.update();
    c.Update();
    syncTextInput(); // focus changed by a script or a closed document
}

bool UiLayer::setLinearBlending(bool on) {
    if (!impl_) return false;
    Impl& m = *impl_;
    if (on == m.renderer.linear()) return true;
    if (on && !m.linear.ready() && !m.linear.init(*m.dev)) {
        std::fprintf(stderr, "[ui] linear blending: no sRGB render targets on this GPU, UI blends in sRGB\n");
        return false;
    }
    if (!m.renderer.setLinear(on)) { m.linear.shutdown(); return false; }
    if (!on) m.linear.shutdown();
    if (m.context) Rml::ReleaseTextures();   // loaded images change encoding: RmlUi reloads them on use
    return true;
}

bool UiLayer::linearBlending() const { return impl_ && impl_->renderer.linear(); }

void UiLayer::setTarget(gfx::TextureHandle color, gfx::TextureHandle depth) {
    if (impl_) { impl_->targetColor = color; impl_->targetDepth = depth; }
}

void UiLayer::render() {
    if (!context()) return;
    Impl& m = *impl_;
    const auto dims = m.context->GetDimensions();
    if (m.renderer.linear()) {
        m.dev->endPass();   // the caller's pass: the UI goes into the layer first
        const bool layer = m.linear.beginLayer(dims.x, dims.y);
        if (layer) {
            m.renderer.beginFrame(dims.x, dims.y);
            m.context->Render();
            m.dev->setScissor({});
            m.dev->endPass();
        }
        m.linear.composite(m.targetColor, m.targetDepth, dims.x, dims.y, layer && m.renderer.draws() > 0);
        m.dev->setScissor({});
        return;
    }
    m.renderer.beginFrame(dims.x, dims.y);
    m.context->Render();
    m.dev->setScissor({});
}

} // namespace rts::ui
