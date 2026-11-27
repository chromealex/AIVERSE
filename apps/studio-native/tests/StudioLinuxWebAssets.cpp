#include "../linux/StudioWebAssets.h"
#include "../linux/StudioWebBridge.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!gtk_init_check(&argc, &argv)) return 77;
    auto* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window),1280,800);
    auto* view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    webkit_settings_set_enable_write_console_messages_to_stdout(webkit_web_view_get_settings(view),true);
    aiverse::studio::registerStudioWebAssets(webkit_web_view_get_context(view),argv[1]);
    bool roundTrip = false;
    aiverse::studio::StudioWebBridge bridge(view,[&](const std::string& request) {
        if (request.find("studio.webDiagnostic") != std::string::npos) std::cerr << request << std::endl;
        if (request == R"({"studioWindowProbe":true})") {
            roundTrip = true;
            bridge.evaluate("window.webkit.messageHandlers.mounted.postMessage(true)");
        }
    });
    if (!bridge.registered()) return 1;
    gtk_container_add(GTK_CONTAINER(window),GTK_WIDGET(view));
    gtk_widget_show_all(window);
    bool mounted = false;
    auto* manager = webkit_web_view_get_user_content_manager(view);
    g_signal_connect(manager,"script-message-received::mounted",G_CALLBACK(+[](WebKitUserContentManager*,WebKitJavascriptResult* result,gpointer data){
        auto* value = webkit_javascript_result_get_js_value(result);
        *static_cast<bool*>(data) = jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
        gtk_main_quit();
    }),&mounted);
    if (!webkit_user_content_manager_register_script_message_handler(manager,"mounted")) return 1;
    g_signal_connect(view,"load-failed",G_CALLBACK(+[](WebKitWebView*,WebKitLoadEvent,const gchar* uri,GError* error,gpointer)->gboolean{
        std::cerr << uri << ": " << error->message << std::endl;
        return FALSE;
    }),nullptr);
    g_signal_connect(view,"web-process-terminated",G_CALLBACK(+[](WebKitWebView*,WebKitWebProcessTerminationReason reason,gpointer){
        std::cerr << "Web process terminated: " << reason << std::endl;
        gtk_main_quit();
    }),nullptr);
    g_signal_connect(view,"load-changed",G_CALLBACK(+[](WebKitWebView* web,WebKitLoadEvent event,gpointer){
        if (event != WEBKIT_LOAD_FINISHED) return;
        const char* check = R"JS((function check() {
            const root = document.querySelector('#root .studio.compact-shell');
            const css = Array.from(document.styleSheets).some(s => s.href?.startsWith('aiverse-studio://shell/assets/') && !s.disabled);
            if (root && css && getComputedStyle(root).display === 'flex') {
                window.__aiverseLinux.postMessage({studioWindowProbe:true});
            } else { setTimeout(check, 100); }
        })())JS";
        webkit_web_view_evaluate_javascript(web,check,-1,nullptr,nullptr,nullptr,nullptr,nullptr);
    }),nullptr);
    webkit_web_view_load_uri(view,"aiverse-studio://shell/index.html");
    g_timeout_add_seconds(30,[](gpointer)->gboolean{ gtk_main_quit(); return G_SOURCE_REMOVE; },nullptr);
    gtk_main();
    const char* screenshot = g_getenv("STUDIO_WINDOW_CAPTURE");
    bool captured = false;
    if (mounted && roundTrip && screenshot) {
        webkit_web_view_get_snapshot(view, WEBKIT_SNAPSHOT_REGION_VISIBLE, WEBKIT_SNAPSHOT_OPTIONS_NONE,
            nullptr, +[](GObject* object, GAsyncResult* result, gpointer data) {
                GError* error = nullptr;
                auto* surface = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(object), result, &error);
                if (surface) {
                    *static_cast<bool*>(data) = cairo_surface_write_to_png(surface, g_getenv("STUDIO_WINDOW_CAPTURE")) == CAIRO_STATUS_SUCCESS;
                    cairo_surface_destroy(surface);
                }
                if (error) { std::cerr << error->message << std::endl; g_error_free(error); }
                gtk_main_quit();
            }, &captured);
        gtk_main();
    }
    gtk_widget_destroy(window);
    if (!mounted) std::cerr << "Studio did not mount from its bundled HTML, modules and styles\n";
    return mounted && roundTrip && (!screenshot || captured) ? 0 : 1;
}
