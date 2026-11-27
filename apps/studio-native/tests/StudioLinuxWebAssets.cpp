#include "../linux/StudioWebAssets.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!gtk_init_check(&argc, &argv)) return 77;
    auto* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    auto* view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    aiverse::studio::registerStudioWebAssets(webkit_web_view_get_context(view),argv[1]);
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
    g_signal_connect(view,"load-changed",G_CALLBACK(+[](WebKitWebView* web,WebKitLoadEvent event,gpointer){
        if (event != WEBKIT_LOAD_FINISHED) return;
        const char* check = R"JS((function check() {
            const root = document.getElementById('root');
            if (root?.childElementCount && !document.getElementById('studio-boot') && document.styleSheets.length) {
                window.webkit.messageHandlers.mounted.postMessage(true);
            } else { setTimeout(check, 100); }
        })())JS";
        webkit_web_view_evaluate_javascript(web,check,-1,nullptr,nullptr,nullptr,nullptr,nullptr);
    }),nullptr);
    webkit_web_view_load_uri(view,"aiverse-studio://shell/index.html");
    g_timeout_add_seconds(30,[](gpointer)->gboolean{ gtk_main_quit(); return G_SOURCE_REMOVE; },nullptr);
    gtk_main();
    gtk_widget_destroy(window);
    if (!mounted) std::cerr << "Studio did not mount from its bundled HTML, modules and styles\n";
    return mounted ? 0 : 1;
}
