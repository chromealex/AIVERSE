#include "StudioWebAssets.h"
#include "engine/platform/EngineDir.h"
#include "apps/editor/core/Net.h"
#include "StudioWebBridge.h"
#include "StudioScenePointer.h"
#include "StudioPreviewPointer.h"
#include "StudioGamePointer.h"
#include "StudioSurfacePlatform.h"
#include <gdk/gdkx.h>
#include <iostream>
#include <map>
#include <regex>

namespace aiverse::studio {
class GtkStudio final : public StudioSurfacePlatform {
public:
    GtkStudio(GtkOverlay* overlay, GtkWindow* window, WebKitWebView* web, const std::string& root)
        : StudioSurfacePlatform(overlay), window_(window),
          bridge_(web,[this](std::string message){ host_.handleMessage(message); }), host_(*this,root,false),
          pointer_(widget(StudioSurface::Scene), host_),
          gamePointer_(widget(StudioSurface::Game), [this](const platform::Event& event){ host_.gameEvent(event); }) {
        for (auto surface : {StudioSurface::Character, StudioSurface::Appearance, StudioSurface::Nature,
                             StudioSurface::Fx, StudioSurface::EffectFile, StudioSurface::GpuEmitter})
            previewPointers_.push_back(std::make_unique<StudioPreviewPointer>(widget(surface), surface, host_));
        if (!bridge_.registered()) throw std::runtime_error("Studio web bridge unavailable");
    }
    ~GtkStudio() override {
        stopBackgroundWork();
        while (!floats_.empty()) {
            auto& panel = floats_.begin()->second;
            g_signal_handlers_disconnect_by_data(panel.window,this);
            gtk_widget_destroy(GTK_WIDGET(panel.window));
            floats_.erase(floats_.begin());
        }
        host_.shutdown();
    }
    void evaluateScript(const std::string& js, bool currentOnly) override {
        if (currentOnly && current_) { current_->bridge->evaluate(js); return; }
        bridge_.evaluate(js);
        if (!currentOnly) for (auto& entry : floats_) entry.second.bridge->evaluate(js);
    }
    Json windowRequest(const std::string& method,const Json& params) override {
        Json result=Json::object();
        auto* target=current_ ? current_->window : window_;
        if (method=="studio.windowDrag") {
            auto* widget = GTK_WIDGET(target);
            auto* seat = gdk_display_get_default_seat(gtk_widget_get_display(widget));
            auto* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
            auto* surface = gtk_widget_get_window(widget);
            if (pointer && surface) {
                GdkModifierType state{};
                gdk_window_get_device_position(surface, pointer, nullptr, nullptr, &state);
                if (state & GDK_BUTTON1_MASK) {
                    int x, y;
                    gdk_device_get_position(pointer, nullptr, &x, &y);
                    gtk_window_begin_move_drag(target, 1, x, y, gdk_x11_get_server_time(surface));
                }
            }
        } else if (method=="studio.windowZoom") {
            if (gtk_window_is_maximized(target)) gtk_window_unmaximize(target);
            else gtk_window_maximize(target);
        } else if (method=="studio.windowMinimize") gtk_window_iconify(target);
        else if (method=="studio.windowOpen") result.set("opened",Json::fromBool(openFloat(params)));
        else if (method=="studio.windowTitle" && current_) gtk_window_set_title(target,params.getStr("title").c_str());
        else if (method=="studio.windowClose") {
            const auto key=params.getStr("window");
            auto it=floats_.find(key);
            auto* closing=it!=floats_.end() ? it->second.window : key.empty() ? target : nullptr;
            if (closing) {
                // Finish the web request before destroying its bridge.
                g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer data)->gboolean {
                    gtk_window_close(GTK_WINDOW(data));
                    return G_SOURCE_REMOVE;
                },g_object_ref(closing),g_object_unref);
            }
            result.set("closed",Json::fromBool(closing!=nullptr));
        }
        return result;
    }
    StudioHost& host() { return host_; }
private:
    struct Floating {
        GtkWindow* window{};
        std::unique_ptr<StudioWebBridge> bridge;
    };
    std::map<std::string,Floating> floats_;
    Floating* current_{};
    bool openFloat(const Json& params) {
        const auto key=params.getStr("window"), title=params.getStr("title");
        static const std::regex keyPattern("[a-z0-9-]{1,40}"), panelPattern("[a-z][a-z0-9_]{0,31}");
        if (!std::regex_match(key,keyPattern) || title.size()>256) throw std::runtime_error("Invalid floating window");
        auto existing=floats_.find(key);
        if (existing!=floats_.end()) { gtk_window_present(existing->second.window); return true; }
        std::string panels;
        for (const auto& item : params["panels"].items()) {
            const auto id=item.str();
            if (!std::regex_match(id,panelPattern) || panels.size()>512) throw std::runtime_error("Invalid floating panel");
            panels+=(panels.empty()?"":",")+id;
        }
        if (panels.empty()) throw std::runtime_error("A floating window needs a panel");
        if (params.getBool("ifOutside")) {
            auto* seat=gdk_display_get_default_seat(gtk_widget_get_display(GTK_WIDGET(window_)));
            auto* pointer=seat?gdk_seat_get_pointer(seat):nullptr;
            if (pointer && gdk_device_get_window_at_position(pointer,nullptr,nullptr)) return false;
        }
        auto* web=WEBKIT_WEB_VIEW(webkit_web_view_new());
        auto bridge=std::make_unique<StudioWebBridge>(web,[this,key](std::string message) {
            auto* previous=current_;
            current_=&floats_.at(key);
            host_.handleMessage(message);
            current_=previous;
        });
        if (!bridge->registered()) { g_object_ref_sink(web);g_object_unref(web);throw std::runtime_error("Floating bridge unavailable"); }
        auto* win=GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
        gtk_window_set_title(win,title.empty()?"AIVERSE Studio":title.c_str());
        gtk_window_set_default_size(win,std::clamp<int64_t>(params.getInt("width",640),220,6000),std::clamp<int64_t>(params.getInt("height",560),160,4000));
        if (params.has("x") && params.has("y")) gtk_window_move(win,params.getInt("x"),params.getInt("y"));
        gtk_container_add(GTK_CONTAINER(win),GTK_WIDGET(web));
        floats_.emplace(key,Floating{win,std::move(bridge)});
        g_signal_connect(win,"destroy",G_CALLBACK(+[](GtkWidget* widget,gpointer data) {
            auto& self=*static_cast<GtkStudio*>(data);
            for (auto it=self.floats_.begin();it!=self.floats_.end();++it) {
                if (GTK_WIDGET(it->second.window)!=widget) continue;
                const auto key=it->first;
                self.floats_.erase(it);
                self.host_.sendEvent("window.floatClosed",key);
                break;
            }
        }),this);
        gtk_widget_show_all(GTK_WIDGET(win));
        webkit_web_view_load_uri(web,("aiverse-studio://shell/index.html?window="+key+"&panels="+panels).c_str());
        return true;
    }
    GtkWindow* window_;
    StudioWebBridge bridge_;
    StudioHost host_;
    StudioScenePointer pointer_;
    StudioGamePointer gamePointer_;
    std::vector<std::unique_ptr<StudioPreviewPointer>> previewPointers_;
};
}

int aiverseMain(int argc,char** argv) {
    using namespace aiverse::studio;
    gdk_set_allowed_backends("x11");
    if (!gtk_init_check(&argc,&argv)) { std::cerr << "Studio requires an X11 display\n"; return 1; }
    std::string project=loadStudioLastProject(), root=aiverse::platform::resolveEngineDir(AIVERSE_STUDIO_SOURCE_DIR,argv[0]);
    int listenPort=0;
    for (int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if (i+1<argc && option=="--project") project=argv[++i];
        else if (i+1<argc && option=="--listen") {
            listenPort=aiverse::editor::net::parseListenPort(argv[++i]);
            if (!listenPort) { std::cerr << "Invalid Studio listener port\n"; return 2; }
        }
        else if (i+1<argc && option=="--engine") root=argv[++i];
        else { std::cerr << "Unexpected Studio argument: " << option << '\n'; return 2; }
    }
    auto* window=GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_title(window,"AIVERSE Studio");
    gtk_window_set_default_size(window,1280,800);
    auto* overlay=GTK_OVERLAY(gtk_overlay_new());
    auto* web=WEBKIT_WEB_VIEW(webkit_web_view_new());
    gtk_container_add(GTK_CONTAINER(overlay),GTK_WIDGET(web));
    gtk_container_add(GTK_CONTAINER(window),GTK_WIDGET(overlay));
    gtk_widget_show_all(GTK_WIDGET(window));
    int status=0;
    try {
        const auto dist=std::filesystem::read_symlink("/proc/self/exe").parent_path()/"studio-web";
        if (!std::filesystem::is_regular_file(dist/"index.html")) throw std::runtime_error("Studio HTML bundle missing");
        registerStudioWebAssets(webkit_web_view_get_context(web),dist);
        aiverse::platform::setEngineAssetsDir((dist.parent_path()/"engine").string());
        GtkStudio studio(overlay,window,web,root);
        studio.host().session().setMcpTool((dist.parent_path()/"aiversemcp").string());
        if (listenPort && !studio.host().session().listenForAgent(listenPort))
            throw std::runtime_error("Could not start the authenticated Studio agent listener");
        studio.host().setPendingProject(project);
        g_signal_connect(web,"load-failed",G_CALLBACK(+[](WebKitWebView*,WebKitLoadEvent,const gchar*,GError* error,gpointer data)->gboolean {
            std::cerr << "Studio page load failed: " << error->message << '\n';
            *static_cast<int*>(data)=1;
            gtk_main_quit();
            return TRUE;
        }),&status);
        g_signal_connect(web,"load-changed",G_CALLBACK(+[](WebKitWebView*,WebKitLoadEvent event,gpointer data){
            if (event==WEBKIT_LOAD_FINISHED && gtk_main_level()>0) static_cast<GtkStudio*>(data)->host().webReady();
        }),&studio);
        g_signal_connect(window,"delete-event",G_CALLBACK(+[](GtkWidget*,GdkEvent*,gpointer data)->gboolean{
            if (static_cast<GtkStudio*>(data)->host().confirmClose()) gtk_main_quit();
            return TRUE;
        }),&studio);
        const auto timer=g_timeout_add(16,+[](gpointer data)->gboolean{
            static_cast<GtkStudio*>(data)->host().tick();
            return G_SOURCE_CONTINUE;
        },&studio);
        webkit_web_view_load_uri(web,"aiverse-studio://shell/index.html");
        gtk_main();
        g_source_remove(timer);
        g_signal_handlers_disconnect_by_data(window,&studio);
        g_signal_handlers_disconnect_by_data(web,&studio);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; status=1; }
    gtk_widget_destroy(GTK_WIDGET(window));
    return status;
}
