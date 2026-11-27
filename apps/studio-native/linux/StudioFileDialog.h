#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <string>
#include <utility>

namespace aiverse::studio {
inline void chooseStudioPath(GtkWindow* parent, GtkFileChooserAction action, const std::string& title,
                             const std::string& directory, const std::string& name,
                             std::function<void(std::string)> done) {
    const bool saving = action == GTK_FILE_CHOOSER_ACTION_SAVE;
    auto* dialog = gtk_file_chooser_native_new(title.c_str(), parent, action,
        saving ? "_Save" : "_Open", "_Cancel");
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_local_only(chooser, TRUE);
    if (!directory.empty()) gtk_file_chooser_set_current_folder(chooser, directory.c_str());
    if (saving) {
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
        gtk_file_chooser_set_current_name(chooser, name.c_str());
    }
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), TRUE);
    using Callback = std::function<void(std::string)>;
    g_signal_connect_data(dialog, "response", G_CALLBACK(+[](GtkNativeDialog* sender, gint response, gpointer data) {
        gchar* filename = response == GTK_RESPONSE_ACCEPT
            ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(sender)) : nullptr;
        std::string path = filename ? filename : "";
        g_free(filename);
        Callback callback = std::move(*static_cast<Callback*>(data));
        gtk_native_dialog_destroy(sender);
        g_object_unref(sender);
        callback(std::move(path));
    }), new Callback(std::move(done)), [](gpointer data, GClosure*) { delete static_cast<Callback*>(data); },
        G_CONNECT_DEFAULT);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
}
