#pragma once
#include <gtk/gtk.h>
#include <functional>
#include <filesystem>
#include <vector>
#include <string>
#include <utility>

namespace aiverse::studio {
inline bool confirmStudioDiscard(GtkWindow* parent, const std::string& title,
                                 const std::string& message, const std::string& discardLabel) {
    auto* dialog = gtk_message_dialog_new(parent,
        GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", title.c_str());
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message.c_str());
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), discardLabel.c_str(), GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    // Keep a reference if the parent is destroyed during the nested GTK loop.
    g_object_ref_sink(dialog);
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    return response == GTK_RESPONSE_ACCEPT;
}

using StudioPathSelection = std::function<void(std::vector<std::filesystem::path>)>;

inline void chooseStudioPaths(GtkWindow* parent, GtkFileChooserAction action, const std::string& title,
                             const std::string& directory, const std::string& name,
                             bool multiple, StudioPathSelection done) {
    const bool saving = action == GTK_FILE_CHOOSER_ACTION_SAVE;
    auto* dialog = gtk_file_chooser_native_new(title.c_str(), parent, action,
        saving ? "_Save" : "_Open", "_Cancel");
    auto* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_select_multiple(chooser, multiple);
    gtk_file_chooser_set_local_only(chooser, TRUE);
    if (!directory.empty()) gtk_file_chooser_set_current_folder(chooser, directory.c_str());
    if (saving) {
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
        gtk_file_chooser_set_current_name(chooser, name.c_str());
    }
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), TRUE);
    using Callback = StudioPathSelection;
    g_signal_connect_data(dialog, "response", G_CALLBACK(+[](GtkNativeDialog* sender, gint response, gpointer data) {
        std::vector<std::filesystem::path> paths;
        if (response == GTK_RESPONSE_ACCEPT) {
            GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(sender));
            for (auto* file = files; file; file = file->next)
                paths.emplace_back(static_cast<const char*>(file->data));
            g_slist_free_full(files, g_free);
        }
        Callback callback = std::move(*static_cast<Callback*>(data));
        gtk_native_dialog_destroy(sender);
        g_object_unref(sender);
        callback(std::move(paths));
    }), new Callback(std::move(done)), [](gpointer data, GClosure*) { delete static_cast<Callback*>(data); },
        G_CONNECT_DEFAULT);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

inline void chooseStudioPath(GtkWindow* parent, GtkFileChooserAction action, const std::string& title,
                             const std::string& directory, const std::string& name,
                             std::function<void(std::string)> done) {
    chooseStudioPaths(parent, action, title, directory, name, false,
        [done = std::move(done)](std::vector<std::filesystem::path> paths) {
            done(paths.empty() ? std::string{} : paths.front().string());
        });
}
}
