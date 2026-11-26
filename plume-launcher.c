#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    GtkApplication *app;
    GtkCssProvider *accent_css;
    GFileMonitor *theme_monitor;
    guint theme_timer;
    GtkWidget *win;
    GtkWidget *card;
    GtkWidget *entry;
    GtkWidget *scroll;
    GtkWidget *list;
    char *pid_path;
    gboolean daemon_mode;
    gboolean open;
    gboolean hiding;
    gboolean was_active;
    guint show_timer;
    guint hide_timer;
    gint64 last_hide_us;
    gboolean search_descriptions;
    gboolean search_commands;
} State;

static gboolean send_signal_to_existing(const char *pid_path, int sig) {
    FILE *f = fopen(pid_path, "r");
    if (!f) return FALSE;
    int pid = 0;
    int ok = fscanf(f, "%d", &pid);
    fclose(f);
    if (ok == 1 && pid > 1 && kill(pid, sig) == 0) {
        return TRUE;
    }
    unlink(pid_path);
    return FALSE;
}

static void write_pid(const char *pid_path) {
    FILE *f = fopen(pid_path, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

static int cmp_app(gconstpointer a, gconstpointer b) {
    const char *na = g_app_info_get_display_name(G_APP_INFO(a));
    const char *nb = g_app_info_get_display_name(G_APP_INFO(b));
    return g_utf8_collate(na ? na : "", nb ? nb : "");
}

static void load_search_prefs(State *st) {
    st->search_descriptions = TRUE;
    st->search_commands = FALSE;
    char *path = g_build_filename(
        g_get_user_config_dir(), "plume", "search.ini", NULL);
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gboolean value = g_key_file_get_boolean(
            key, "Search", "descriptions", &error);
        if (!error) st->search_descriptions = value;
        g_clear_error(&error);
        value = g_key_file_get_boolean(key, "Search", "commands", &error);
        if (!error) st->search_commands = value;
        g_clear_error(&error);
    }
    g_key_file_unref(key);
    g_free(path);
}

static gboolean folded_contains(const char *text, const char *query_folded) {
    if (!text || !*text) return FALSE;
    char *text_folded = g_utf8_casefold(text, -1);
    gboolean match = strstr(text_folded, query_folded) != NULL;
    g_free(text_folded);
    return match;
}

static gboolean filter_row(GtkListBoxRow *row, gpointer user_data) {
    State *st = user_data;
    const char *q = gtk_editable_get_text(GTK_EDITABLE(st->entry));
    if (!q || !*q) return TRUE;
    char *q_fold = g_utf8_casefold(q, -1);
    const char *name = g_object_get_data(G_OBJECT(row), "app-name");
    const char *description = g_object_get_data(
        G_OBJECT(row), "app-description");
    const char *command = g_object_get_data(G_OBJECT(row), "app-command");
    gboolean match = folded_contains(name, q_fold) ||
        (st->search_descriptions && folded_contains(description, q_fold)) ||
        (st->search_commands && folded_contains(command, q_fold));
    g_free(q_fold);
    return match;
}

static void select_first_visible(State *st) {
    for (GtkWidget *w = gtk_widget_get_first_child(st->list); w; w = gtk_widget_get_next_sibling(w)) {
        if (GTK_IS_LIST_BOX_ROW(w) && gtk_widget_get_child_visible(w)) {
            gtk_list_box_select_row(GTK_LIST_BOX(st->list), GTK_LIST_BOX_ROW(w));
            return;
        }
    }
    gtk_list_box_unselect_all(GTK_LIST_BOX(st->list));
}

static gboolean reveal_cb(gpointer user_data) {
    State *st = user_data;
    st->show_timer = 0;
    if (st->open && !st->hiding) {
        gtk_widget_add_css_class(st->card, "visible");
        gtk_widget_grab_focus(st->entry);
    }
    return G_SOURCE_REMOVE;
}

static gboolean finish_hide_cb(gpointer user_data) {
    State *st = user_data;
    st->hide_timer = 0;
    if (st->hiding) {
        st->hiding = FALSE;
        gtk_widget_set_visible(st->win, FALSE);
        if (!st->daemon_mode) {
            g_application_quit(G_APPLICATION(st->app));
        }
    }
    return G_SOURCE_REMOVE;
}

/* Refresh on every opening: the launcher daemon survives theme changes. */
static void refresh_accent(State *st) {
    char accent[8] = "#2f7fe6";
    gboolean light = FALSE;
    char *path = g_build_filename(g_get_user_config_dir(), "plume", "theme.ini", NULL);
    char *content = NULL;
    if (g_file_get_contents(path, &content, NULL, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            char *line = g_strstrip(lines[i]);
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = g_strstrip(line);
            if (strcmp(key, "mode") == 0) {
                light = strcmp(g_strstrip(eq + 1), "light") == 0;
                continue;
            }
            if (strcmp(key, "accent") != 0) continue;
            char *value = g_strstrip(eq + 1);
            gboolean valid = strlen(value) == 7 && value[0] == '#';
            for (guint j = 1; valid && j < 7; j++)
                valid = g_ascii_isxdigit(value[j]);
            if (valid) g_strlcpy(accent, value, sizeof accent);
        }
        g_strfreev(lines);
    }
    g_free(content);
    g_free(path);
    char *css = g_strdup_printf(
        "@define-color plume_launcher_accent %s;"
        "@define-color plume_launcher_bg %s;"
        "@define-color plume_launcher_field %s;"
        "@define-color plume_launcher_fg %s;"
        "@define-color plume_launcher_border %s;"
        "@define-color plume_launcher_hover %s;"
        "@define-color plume_launcher_selected %s;"
        "@define-color plume_launcher_muted %s;",
        accent, light ? "#f3f5f9" : "#171a21",
        light ? "#e8ecf3" : "#1c212b",
        light ? "#171c26" : "#edf0f5",
        light ? "#b8c3d4" : "#2b3342",
        light ? "#dce2ec" : "#202633",
        light ? "#cdd5e2" : "#252b37",
        light ? "#667185" : "#8b96a8");
    gtk_css_provider_load_from_string(st->accent_css, css);
    g_free(css);
}

static gboolean refresh_accent_cb(gpointer user_data) {
    State *st = user_data;
    st->theme_timer = 0;
    refresh_accent(st);
    return G_SOURCE_REMOVE;
}

static void on_theme_changed(GFileMonitor *monitor, GFile *file,
                             GFile *other_file, GFileMonitorEvent event,
                             gpointer user_data) {
    (void)monitor;
    (void)event;
    State *st = user_data;
    char *name = file ? g_file_get_basename(file) : NULL;
    char *other_name = other_file ? g_file_get_basename(other_file) : NULL;
    gboolean relevant = g_strcmp0(name, "theme.ini") == 0 ||
                        g_strcmp0(other_name, "theme.ini") == 0;
    g_free(name);
    g_free(other_name);
    if (!relevant) return;
    /* Coalesce writes and atomic replacements before reading the new file. */
    if (st->theme_timer) g_source_remove(st->theme_timer);
    st->theme_timer = g_timeout_add(80, refresh_accent_cb, st);
}

static void watch_theme(State *st) {
    char *dir = g_build_filename(g_get_user_config_dir(), "plume", NULL);
    if (g_mkdir_with_parents(dir, 0700) == 0) {
        GFile *file = g_file_new_for_path(dir);
        st->theme_monitor = g_file_monitor_directory(
            file, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
        if (st->theme_monitor)
            g_signal_connect(st->theme_monitor, "changed",
                             G_CALLBACK(on_theme_changed), st);
        g_object_unref(file);
    }
    g_free(dir);
}

static void show_launcher(State *st) {
    refresh_accent(st);
    load_search_prefs(st);
    if (st->hide_timer) {
        g_source_remove(st->hide_timer);
        st->hide_timer = 0;
    }
    if (st->show_timer) {
        g_source_remove(st->show_timer);
        st->show_timer = 0;
    }
    st->open = TRUE;
    st->hiding = FALSE;
    st->was_active = FALSE;

    gtk_editable_set_text(GTK_EDITABLE(st->entry), "");
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(st->list));
    select_first_visible(st);
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(st->scroll));
    if (adj) gtk_adjustment_set_value(adj, 0);

    gtk_widget_remove_css_class(st->card, "visible");
    gtk_widget_set_visible(st->win, TRUE);
    gtk_window_present(GTK_WINDOW(st->win));
    st->show_timer = g_timeout_add(16, reveal_cb, st);
}

static void hide_launcher(State *st) {
    if (!st->open || st->hiding) return;
    if (st->show_timer) {
        g_source_remove(st->show_timer);
        st->show_timer = 0;
    }
    st->hiding = TRUE;
    st->open = FALSE;
    st->last_hide_us = g_get_monotonic_time();
    gtk_widget_remove_css_class(st->card, "visible");
    if (st->hide_timer) g_source_remove(st->hide_timer);
    st->hide_timer = g_timeout_add(230, finish_hide_cb, st);
}

static gboolean on_sigusr1(gpointer user_data) {
    State *st = user_data;
    if (st->open && !st->hiding) {
        hide_launcher(st);
    } else if (g_get_monotonic_time() - st->last_hide_us > 250000) {
        show_launcher(st);
    }
    return G_SOURCE_CONTINUE;
}


static gboolean is_pinned_desktop(const char *desktop_id) {
    if (!desktop_id || !*desktop_id) return FALSE;
    char *pf = g_build_filename(g_get_user_config_dir(), "plume", "pinned", NULL);
    char *content = NULL;
    gboolean found = FALSE;
    if (g_file_get_contents(pf, &content, NULL, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            g_strstrip(lines[i]);
            if (strcmp(lines[i], desktop_id) == 0) {
                found = TRUE;
                break;
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(pf);
    return found;
}

static void on_pin_clicked(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    const char *did = g_object_get_data(G_OBJECT(btn), "desktop-id");
    if (!did || !*did) return;
    char *argv[] = { (char *)"plume-pinned", (char *)"toggle", (char *)did, NULL };
    GError *err = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);

    gboolean now_pinned = !is_pinned_desktop(did);
    gtk_button_set_label(btn, now_pinned ? "󰐃" : "󰐂");
    if (now_pinned)
        gtk_widget_add_css_class(GTK_WIDGET(btn), "pinned");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(btn), "pinned");
}

static void launch_row(State *st, GtkListBoxRow *row) {
    if (!row) return;
    GAppInfo *info = g_object_get_data(G_OBJECT(row), "app-info");
    if (info) {
        GError *err = NULL;
        g_app_info_launch(info, NULL, NULL, &err);
        if (err) g_error_free(err);
    }
    hide_launcher(st);
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    launch_row(user_data, row);
}

static void on_search_changed(GtkEditable *ed, gpointer user_data) {
    (void)ed;
    State *st = user_data;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(st->list));
    select_first_visible(st);
}

static void move_selection(State *st, int dir) {
    GtkListBoxRow *cur = gtk_list_box_get_selected_row(GTK_LIST_BOX(st->list));
    GtkWidget *w = cur ? GTK_WIDGET(cur) : NULL;
    while (w) {
        w = (dir > 0) ? gtk_widget_get_next_sibling(w) : gtk_widget_get_prev_sibling(w);
        if (w && GTK_IS_LIST_BOX_ROW(w) && gtk_widget_get_child_visible(w)) {
            gtk_list_box_select_row(GTK_LIST_BOX(st->list), GTK_LIST_BOX_ROW(w));
            gtk_widget_grab_focus(st->entry);
            return;
        }
    }
    if (!cur) select_first_visible(st);
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                               GdkModifierType mods, gpointer user_data) {
    (void)ctrl; (void)keycode; (void)mods;
    State *st = user_data;
    if (keyval == GDK_KEY_Escape) {
        hide_launcher(st);
        return TRUE;
    }
    if (keyval == GDK_KEY_Down) {
        move_selection(st, 1);
        return TRUE;
    }
    if (keyval == GDK_KEY_Up) {
        move_selection(st, -1);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(st->list));
        launch_row(st, row);
        return TRUE;
    }
    return FALSE;
}

static void on_active_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    State *st = user_data;
    gboolean active = gtk_window_is_active(GTK_WINDOW(obj));
    if (active) {
        st->was_active = TRUE;
    } else if (st->was_active && st->open && !st->hiding) {
        hide_launcher(st);
    }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    State *st = user_data;
    st->app = app;

    if (st->daemon_mode) {
        g_application_hold(G_APPLICATION(app));
    }

    st->accent_css = gtk_css_provider_new();
    refresh_accent(st);
    watch_theme(st);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(st->accent_css),
        GTK_STYLE_PROVIDER_PRIORITY_USER);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "window.plume-win, window.background.plume-win {"
        "  background: none;"
        "  background-color: transparent;"
        "  box-shadow: none;"
        "}"
        "box.plume-card {"
        "  background-color: @plume_launcher_bg;"
        "  color: @plume_launcher_fg;"
        "  border: 1px solid @plume_launcher_accent;"
        "  border-radius: 6px;"
        "  font-family: 'Onest', 'Sora', sans-serif;"
        "  font-size: 13px;"
        "  opacity: 0;"
        "  transform: translateY(-8px);"
        "  transition: opacity 220ms cubic-bezier(0.22, 1, 0.36, 1),"
        "              transform 220ms cubic-bezier(0.22, 1, 0.36, 1);"
        "}"
        "box.plume-card.visible {"
        "  opacity: 1;"
        "  transform: translateY(0px);"
        "}"
        "entry.plume-search {"
        "  background: @plume_launcher_field;"
        "  color: @plume_launcher_fg;"
        "  border: 1px solid @plume_launcher_border;"
        "  border-radius: 6px;"
        "  padding: 8px 10px;"
        "  min-height: 20px;"
        "  box-shadow: none; outline: none;"
        "}"
        "entry.plume-search:focus, entry.plume-search:focus-within {"
        "  border-color: @plume_launcher_accent;"
        "  outline: none; box-shadow: none;"
        "}"
        "entry.plume-search text, entry.plume-search text:focus {"
        "  outline: none; box-shadow: none;"
        "}"
        "list.plume-list {"
        "  background: transparent;"
        "}"
        "list.plume-list row {"
        "  padding: 6px 8px;"
        "  border-radius: 6px;"
        "  color: @plume_launcher_fg;"
        "}"
        "list.plume-list row:hover {"
        "  background: @plume_launcher_hover;"
        "}"
        "button.plume-pin {"
        "  background: transparent; border: none; color: @plume_launcher_muted;"
        "  padding: 2px 6px; min-height: 20px; border-radius: 4px; box-shadow: none;"
        "}"
        "button.plume-pin:hover, button.plume-pin.pinned {"
        "  color: @plume_launcher_accent; background: @plume_launcher_selected;"
        "}"
        "list.plume-list row:selected {"
        "  background: @plume_launcher_selected;"
        "  color: @plume_launcher_fg;"
        "}"
        "scrollbar slider {"
        "  background: @plume_launcher_border;"
        "  border-radius: 3px;"
        "  min-width: 6px;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );
    g_object_unref(css);

    st->win = gtk_application_window_new(app);
    gtk_widget_add_css_class(st->win, "plume-win");
    gtk_window_set_decorated(GTK_WINDOW(st->win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(st->win), FALSE);

    if (gtk_layer_is_supported()) {
        gtk_layer_init_for_window(GTK_WINDOW(st->win));
        gtk_layer_set_layer(GTK_WINDOW(st->win), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_anchor(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_LEFT, 8);
        gtk_layer_set_margin(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_TOP, 0);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(st->win), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    }

    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(wrap, 8);
    gtk_window_set_child(GTK_WINDOW(st->win), wrap);

    st->card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(st->card, "plume-card");
    gtk_widget_set_size_request(st->card, 340, 440);
    gtk_box_append(GTK_BOX(wrap), st->card);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(inner, 10);
    gtk_widget_set_margin_bottom(inner, 10);
    gtk_widget_set_margin_start(inner, 10);
    gtk_widget_set_margin_end(inner, 10);
    gtk_widget_set_vexpand(inner, TRUE);
    gtk_box_append(GTK_BOX(st->card), inner);

    st->entry = gtk_entry_new();
    gtk_widget_add_css_class(st->entry, "plume-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->entry), "Поиск приложений…");
    g_signal_connect(st->entry, "changed", G_CALLBACK(on_search_changed), st);
    gtk_box_append(GTK_BOX(inner), st->entry);

    st->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(st->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(st->scroll, TRUE);
    gtk_box_append(GTK_BOX(inner), st->scroll);

    st->list = gtk_list_box_new();
    gtk_widget_add_css_class(st->list, "plume-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(st->list), TRUE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(st->list), filter_row, st, NULL);
    g_signal_connect(st->list, "row-activated", G_CALLBACK(on_row_activated), st);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(st->scroll), st->list);

    GList *apps = g_list_sort(g_app_info_get_all(), cmp_app);
    for (GList *l = apps; l; l = l->next) {
        GAppInfo *info = G_APP_INFO(l->data);
        if (!g_app_info_should_show(info)) continue;
        const char *name = g_app_info_get_display_name(info);
        if (!name || !*name) continue;

        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GIcon *gicon = g_app_info_get_icon(info);
        GtkWidget *img = gicon ? gtk_image_new_from_gicon(gicon) : gtk_image_new();
        gtk_image_set_pixel_size(GTK_IMAGE(img), 22);
        gtk_box_append(GTK_BOX(row_box), img);

        GtkWidget *lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(row_box), lbl);

        const char *desktop_id = g_app_info_get_id(info);
        if (desktop_id && *desktop_id) {
            gboolean pinned = is_pinned_desktop(desktop_id);
            GtkWidget *pin_btn = gtk_button_new_with_label(pinned ? "󰐃" : "󰐂");
            gtk_widget_add_css_class(pin_btn, "plume-pin");
            if (pinned) gtk_widget_add_css_class(pin_btn, "pinned");
            g_object_set_data_full(G_OBJECT(pin_btn), "desktop-id", g_strdup(desktop_id), g_free);
            g_signal_connect(pin_btn, "clicked", G_CALLBACK(on_pin_clicked), st);
            gtk_box_append(GTK_BOX(row_box), pin_btn);
        }

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        g_object_set_data_full(G_OBJECT(row), "app-info", g_object_ref(info), g_object_unref);
        g_object_set_data_full(G_OBJECT(row), "app-name", g_strdup(name), g_free);
        g_object_set_data_full(G_OBJECT(row), "app-description",
            g_strdup(g_app_info_get_description(info)), g_free);
        g_object_set_data_full(G_OBJECT(row), "app-command",
            g_strdup(g_app_info_get_commandline(info)), g_free);
        gtk_list_box_append(GTK_LIST_BOX(st->list), row);
    }
    g_list_free_full(apps, g_object_unref);

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_key_pressed), st);
    gtk_widget_add_controller(st->win, key_ctrl);

    g_signal_connect(st->win, "notify::is-active", G_CALLBACK(on_active_changed), st);
    g_unix_signal_add(SIGUSR1, on_sigusr1, st);

    if (!st->daemon_mode) {
        show_launcher(st);
    }
}

int main(int argc, char **argv) {
    gboolean daemon_mode = (argc > 1 && strcmp(argv[1], "--daemon") == 0);
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char *pid_path = g_strdup_printf("%s/plume-launcher.pid", rt ? rt : "/tmp");

    if (daemon_mode) {
        send_signal_to_existing(pid_path, SIGTERM);
    } else if (send_signal_to_existing(pid_path, SIGUSR1)) {
        g_free(pid_path);
        return 0;
    }
    write_pid(pid_path);

    State st = { .pid_path = pid_path, .daemon_mode = daemon_mode };
    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
    int status = g_application_run(G_APPLICATION(app), 1, argv);
    if (st.theme_timer) g_source_remove(st.theme_timer);
    if (st.theme_monitor) g_file_monitor_cancel(st.theme_monitor);
    g_clear_object(&st.theme_monitor);
    if (st.accent_css) {
        gtk_style_context_remove_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(st.accent_css));
        g_clear_object(&st.accent_css);
    }
    g_object_unref(app);
    unlink(pid_path);
    g_free(pid_path);
    return status;
}
