#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    GtkWidget *btn;
    const char *label;
    const char *confirm;
    const char *const *cmd;
} PowerAction;

typedef struct {
    GtkApplication *app;
    GtkWidget *win;
    GtkWidget *card;
    PowerAction actions[4];
    int armed_idx;
    gboolean daemon_mode;
    gboolean open;
    gboolean hiding;
    gboolean was_active;
    guint show_timer;
    guint hide_timer;
    gint64 last_hide_us;
} State;

static const char *const CMD_LOCK[]     = { "plume-lock", NULL };
static const char *const CMD_EXIT[]     = { "sh", "-c", "kill -TERM \"$(cat \"$PLUME_RUNTIME/wm.pid\")\" 2>/dev/null || pkill -TERM -x wayfire", NULL };
static const char *const CMD_REBOOT[]   = { "systemctl", "reboot", NULL };
static const char *const CMD_POWEROFF[] = { "systemctl", "poweroff", NULL };

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

static void reset_armed(State *st) {
    for (int i = 0; i < 4; i++) {
        gtk_button_set_label(GTK_BUTTON(st->actions[i].btn), st->actions[i].label);
        gtk_widget_remove_css_class(st->actions[i].btn, "armed");
    }
    st->armed_idx = -1;
}

static gboolean reveal_cb(gpointer user_data) {
    State *st = user_data;
    st->show_timer = 0;
    if (st->open && !st->hiding) {
        gtk_widget_add_css_class(st->card, "visible");
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

static void show_power(State *st) {
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
    reset_armed(st);

    gtk_widget_remove_css_class(st->card, "visible");
    gtk_widget_set_visible(st->win, TRUE);
    gtk_window_present(GTK_WINDOW(st->win));
    st->show_timer = g_timeout_add(16, reveal_cb, st);
}

static void hide_power(State *st) {
    if (!st->open || st->hiding) return;
    if (st->show_timer) {
        g_source_remove(st->show_timer);
        st->show_timer = 0;
    }
    st->hiding = TRUE;
    st->open = FALSE;
    st->last_hide_us = g_get_monotonic_time();
    reset_armed(st);
    gtk_widget_remove_css_class(st->card, "visible");
    if (st->hide_timer) g_source_remove(st->hide_timer);
    st->hide_timer = g_timeout_add(230, finish_hide_cb, st);
}

static gboolean on_sigusr1(gpointer user_data) {
    State *st = user_data;
    if (st->open && !st->hiding) {
        hide_power(st);
    } else if (g_get_monotonic_time() - st->last_hide_us > 250000) {
        show_power(st);
    }
    return G_SOURCE_CONTINUE;
}

static void on_btn_clicked(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "action-idx"));
    PowerAction *act = &st->actions[idx];

    if (act->confirm && st->armed_idx != idx) {
        reset_armed(st);
        st->armed_idx = idx;
        gtk_button_set_label(btn, act->confirm);
        gtk_widget_add_css_class(GTK_WIDGET(btn), "armed");
        return;
    }

    GError *err = NULL;
    g_spawn_async(NULL, (char **)act->cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
    hide_power(st);
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                               GdkModifierType mods, gpointer user_data) {
    (void)ctrl; (void)keycode; (void)mods;
    State *st = user_data;
    if (keyval == GDK_KEY_Escape) {
        hide_power(st);
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
        hide_power(st);
    }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    State *st = user_data;
    st->app = app;
    st->armed_idx = -1;

    if (st->daemon_mode) {
        g_application_hold(G_APPLICATION(app));
    }

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "window.plume-win, window.background.plume-win {"
        "  background: none;"
        "  background-color: transparent;"
        "  box-shadow: none;"
        "}"
        "box.plume-power-card {"
        "  background-color: #171a21;"
        "  color: #edf0f5;"
        "  border: 1px solid #2b3342;"
        "  border-radius: 6px;"
        "  font-family: 'Onest', 'Sora', 'JetBrainsMono Nerd Font', 'Symbols Nerd Font', sans-serif;"
        "  font-size: 13px;"
        "  opacity: 0;"
        "  transform: translateY(-8px);"
        "  transition: opacity 220ms cubic-bezier(0.22, 1, 0.36, 1),"
        "              transform 220ms cubic-bezier(0.22, 1, 0.36, 1);"
        "}"
        "box.plume-power-card.visible {"
        "  opacity: 1;"
        "  transform: translateY(0px);"
        "}"
        "button.plume-power-btn {"
        "  background: transparent;"
        "  color: #edf0f5;"
        "  border: 1px solid transparent;"
        "  border-radius: 6px;"
        "  padding: 8px 12px;"
        "  box-shadow: none;"
        "}"
        "button.plume-power-btn:hover {"
        "  background: #202633;"
        "  border-color: #2b3342;"
        "}"
        "button.plume-power-btn.armed {"
        "  background: #3a1d20;"
        "  border-color: #e04f3e;"
        "  color: #ffffff;"
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
        gtk_layer_set_anchor(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_RIGHT, 8);
        gtk_layer_set_margin(GTK_WINDOW(st->win), GTK_LAYER_SHELL_EDGE_TOP, 0);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(st->win), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    }

    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(wrap, 8);
    gtk_window_set_child(GTK_WINDOW(st->win), wrap);

    st->card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(st->card, "plume-power-card");
    gtk_widget_set_size_request(st->card, 220, -1);
    gtk_box_append(GTK_BOX(wrap), st->card);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(inner, 8);
    gtk_widget_set_margin_bottom(inner, 8);
    gtk_widget_set_margin_start(inner, 8);
    gtk_widget_set_margin_end(inner, 8);
    gtk_box_append(GTK_BOX(st->card), inner);

    st->actions[0] = (PowerAction){ NULL, "󰌾   Заблокировать", NULL, CMD_LOCK };
    st->actions[1] = (PowerAction){ NULL, "󰍃   Выйти из Plume", NULL, CMD_EXIT };
    st->actions[2] = (PowerAction){ NULL, "󰜉   Перезагрузить", "Точно перезагрузить?", CMD_REBOOT };
    st->actions[3] = (PowerAction){ NULL, "󰐥   Выключить", "Точно выключить?", CMD_POWEROFF };

    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(st->actions[i].label);
        gtk_widget_add_css_class(btn, "plume-power-btn");
        gtk_widget_set_halign(btn, GTK_ALIGN_FILL);
        GtkWidget *lbl = gtk_button_get_child(GTK_BUTTON(btn));
        if (GTK_IS_LABEL(lbl)) gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        g_object_set_data(G_OBJECT(btn), "action-idx", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_btn_clicked), st);
        gtk_box_append(GTK_BOX(inner), btn);
        st->actions[i].btn = btn;
    }

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_key_pressed), st);
    gtk_widget_add_controller(st->win, key_ctrl);

    g_signal_connect(st->win, "notify::is-active", G_CALLBACK(on_active_changed), st);
    g_unix_signal_add(SIGUSR1, on_sigusr1, st);

    if (!st->daemon_mode) {
        show_power(st);
    }
}

int main(int argc, char **argv) {
    gboolean daemon_mode = (argc > 1 && strcmp(argv[1], "--daemon") == 0);
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char *pid_path = g_strdup_printf("%s/plume-power.pid", rt ? rt : "/tmp");

    if (daemon_mode) {
        send_signal_to_existing(pid_path, SIGTERM);
    } else if (send_signal_to_existing(pid_path, SIGUSR1)) {
        g_free(pid_path);
        return 0;
    }
    write_pid(pid_path);

    State st = { .daemon_mode = daemon_mode };
    GtkApplication *app = gtk_application_new(NULL, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
    int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    unlink(pid_path);
    g_free(pid_path);
    return status;
}
