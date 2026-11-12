#include "engine/meta/Client.h"
int main() {
    using namespace aiverse::meta;
    // Replace this transport with HTTPS. This executable has no server policy code.
    RemoteBackend backend([](const ServiceRequest& r,Backend::Callback reply) {
        if(r.service=="economy"&&r.action=="buy"&&r.arguments.getStr("id")=="sword")
            reply({{},aiverse::Json::object()});
        else reply({{Code::Unsupported,"demo transport"},aiverse::Json::object()});
    });
    aiverse::Json outbox=aiverse::Json::object();
    Client client(backend,[&](const auto& value){outbox=value;return true;});
    if(!client.begin(requests::buy("sword","demo-operation")))return 1;
    client.pump();
    return client.pending()||!client.reply().result||outbox.size()?2:0;
}
