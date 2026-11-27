#pragma once
#include <webkit2/webkit2.h>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

// One bridge per view. The shell supplies StudioHost::handleMessage as the receiver.
namespace aiverse::studio {
class StudioWebBridge {
public:
    StudioWebBridge(WebKitWebView* view, std::function<void(std::string)> receive)
        : view_(WEBKIT_WEB_VIEW(g_object_ref(view))), receive_(std::move(receive)) {
        auto* manager=webkit_web_view_get_user_content_manager(view_);
        // Every Studio WebView, including detached docks, uses the same navigation policy.
        policy_=g_signal_connect(view_,"decide-policy",G_CALLBACK(decidePolicy),nullptr);
        char* uuid=g_uuid_string_random();
        token_=uuid;
        g_free(uuid);
        const std::string source=
            "if(location.href.split(/[?#]/)[0]==='aiverse-studio://shell/index.html'){"
            "const channel=window.webkit.messageHandlers.studioRequest;"
            "const send=channel.postMessage.bind(channel);"
            "Object.defineProperty(window,'__aiverseLinux',{value:Object.freeze({postMessage:"
            "(message)=>send({key:'"+token_+"',message})})});}";
        script_=webkit_user_script_new(source.c_str(),WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,nullptr,nullptr);
        signal_=g_signal_connect(manager,"script-message-received::studioRequest",G_CALLBACK(onMessage),this);
        registered_=webkit_user_content_manager_register_script_message_handler(manager,"studioRequest");
        if(registered_)webkit_user_content_manager_add_script(manager,script_);
    }
    ~StudioWebBridge() {
        auto* manager=webkit_web_view_get_user_content_manager(view_);
        if(registered_){
            webkit_user_content_manager_remove_script(manager,script_);
            webkit_user_content_manager_unregister_script_message_handler(manager,"studioRequest");
        }
        g_signal_handler_disconnect(view_,policy_);
        g_signal_handler_disconnect(manager,signal_);
        webkit_user_script_unref(script_);
        g_object_unref(view_);
    }
    StudioWebBridge(const StudioWebBridge&)=delete;
    StudioWebBridge& operator=(const StudioWebBridge&)=delete;
    bool registered() const { return registered_; }
    void evaluate(const std::string& script) const {
        const char* uri=webkit_web_view_get_uri(view_);
        if(uri && std::string_view(uri).substr(0,std::strcspn(uri,"?#"))=="aiverse-studio://shell/index.html")
            webkit_web_view_evaluate_javascript(view_,script.c_str(),script.size(),nullptr,nullptr,nullptr,nullptr,nullptr);
    }
private:
    static gboolean decidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                                 WebKitPolicyDecisionType type, gpointer) {
        if (type==WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return FALSE;
        if (type==WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
            auto* nav=webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
            const char* uri=webkit_uri_request_get_uri(webkit_navigation_action_get_request(nav));
            if (uri && std::string_view(uri).substr(0,std::strcspn(uri,"?#"))=="aiverse-studio://shell/index.html") return FALSE;
        }
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    static void onMessage(WebKitUserContentManager*,WebKitJavascriptResult* result,gpointer data) {
        auto& self=*static_cast<StudioWebBridge*>(data);
        auto* envelope=webkit_javascript_result_get_js_value(result);
        if(!jsc_value_is_object(envelope))return;
        auto* key=jsc_value_object_get_property(envelope,"key");
        char* value=jsc_value_is_string(key)?jsc_value_to_string(key):nullptr;
        g_object_unref(key);
        const bool trusted=value && self.token_==value;
        g_free(value);
        if(!trusted)return;
        auto* message=jsc_value_object_get_property(envelope,"message");
        char* json=jsc_value_is_object(message)?jsc_value_to_json(message,0):nullptr;
        g_object_unref(message);
        if(json && std::strlen(json)<=1024*1024)self.receive_(json);
        g_free(json);
    }
    WebKitWebView* view_;
    std::function<void(std::string)> receive_;
    std::string token_;
    WebKitUserScript* script_;
    gulong signal_, policy_;
    bool registered_;
};
}
