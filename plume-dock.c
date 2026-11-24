#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <signal.h>
#include <gio/gdesktopappinfo.h>
#include <gdk/wayland/gdkwayland.h>
#include <gtk4-layer-shell.h>
#include <wayland-client.h>
#include "wlr-foreign-toplevel-unstable-v1-client-protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DockState DockState;

typedef struct {
    DockState *dock;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    char *title;
    char *app_id;
    gboolean active;
    gboolean minimized;
    GtkWidget *btn;
} ToplevelItem;

struct DockState {
    GtkApplication *app;
    GtkWidget *win;
    GtkWidget *bar;
    GtkWidget *box;
    GtkCssProvider *css_provider;
    GFileMonitor *theme_mon;
    struct wl_display *wl_dpy;
    struct wl_registry *registry;
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    GPtrArray *items;
    gboolean shown;
    guint hide_timer;
    guint anim_timer;
};

static void rebuild_dock_ui(DockState *d);

static void free_item(gpointer data) {
    ToplevelItem *it = data;
    if (it->handle) zwlr_foreign_toplevel_handle_v1_destroy(it->handle);
    g_free(it->title);
    g_free(it->app_id);
    g_free(it);
}

static gboolean finish_hide_cb(gpointer user_data) {
    DockState *d = user_data;
    d->hide_timer = 0;
    if (!d->shown && d->win) {
        gtk_widget_set_visible(d->win, FALSE);
    }
    return G_SOURCE_REMOVE;
}

static gboolean start_show_cb(gpointer user_data) {
    DockState *d = user_data;
    d->anim_timer = 0;
    if (d->shown && d->bar) {
        gtk_widget_add_css_class(d->bar, "visible");
    }
    return G_SOURCE_REMOVE;
}

static void sync_visibility(DockState *d) {
    gboolean has_windows = (d->items->len > 0);
    if (has_windows) {
        if (d->hide_timer) {
            g_source_remove(d->hide_timer);
            d->hide_timer = 0;
        }
        if (!d->shown) {
            d->shown = TRUE;
            gtk_widget_remove_css_class(d->bar, "visible");
            gtk_widget_set_visible(d->win, TRUE);
            gtk_window_present(GTK_WINDOW(d->win));
            if (d->anim_timer) g_source_remove(d->anim_timer);
            d->anim_timer = g_timeout_add(16, start_show_cb, d);
        }
    } else {
        if (d->anim_timer) {
            g_source_remove(d->anim_timer);
            d->anim_timer = 0;
        }
        if (d->shown) {
            d->shown = FALSE;
            gtk_widget_remove_css_class(d->bar, "visible");
            if (d->hide_timer) g_source_remove(d->hide_timer);
            d->hide_timer = g_timeout_add(220, finish_hide_cb, d);
        }
    }
}

static void on_item_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    ToplevelItem *it = user_data;
    if (!it || !it->handle || !it->dock->wl_dpy) return;

    struct wl_seat *seat = NULL;
    GdkDisplay *gdpy = gdk_display_get_default();
    if (GDK_IS_WAYLAND_DISPLAY(gdpy)) {
        GdkSeat *gseat = gdk_display_get_default_seat(gdpy);
        if (GDK_IS_WAYLAND_SEAT(gseat)) {
            seat = gdk_wayland_seat_get_wl_seat(gseat);
        }
    }

    if (it->minimized) {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(it->handle);
        if (seat) zwlr_foreign_toplevel_handle_v1_activate(it->handle, seat);
    } else if (it->active) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(it->handle);
    } else {
        if (seat) zwlr_foreign_toplevel_handle_v1_activate(it->handle, seat);
    }
    wl_display_flush(it->dock->wl_dpy);
}

static GtkWidget *make_icon_for_app(const char *app_id) {
    GtkWidget *img = NULL;
    if (app_id && *app_id) {
        char *did = g_str_has_suffix(app_id, ".desktop")
            ? g_strdup(app_id)
            : g_strdup_printf("%s.desktop", app_id);
        GDesktopAppInfo *dinfo = g_desktop_app_info_new(did);
        if (!dinfo) {
            char *low_did = g_ascii_strdown(did, -1);
            dinfo = g_desktop_app_info_new(low_did);
            g_free(low_did);
        }
        g_free(did);

        if (dinfo) {
            GIcon *gicon = g_app_info_get_icon(G_APP_INFO(dinfo));
            if (gicon) img = gtk_image_new_from_gicon(gicon);
            g_object_unref(dinfo);
        }

        if (!img) {
            GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
            char *low = g_ascii_strdown(app_id, -1);
            const char *dot = strrchr(low, '.');
            const char *cand = (dot && *(dot + 1)) ? (dot + 1) : low;
            if (gtk_icon_theme_has_icon(theme, app_id)) {
                img = gtk_image_new_from_icon_name(app_id);
            } else if (gtk_icon_theme_has_icon(theme, cand)) {
                img = gtk_image_new_from_icon_name(cand);
            } else if (gtk_icon_theme_has_icon(theme, low)) {
                img = gtk_image_new_from_icon_name(low);
            }
            g_free(low);
        }
    }
    if (!img) {
        img = gtk_image_new_from_icon_name("application-x-executable");
    }
    gtk_image_set_pixel_size(GTK_IMAGE(img), 22);
    return img;
}

static void rebuild_dock_ui(DockState *d) {
    if (!d->box) return;
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(d->box)) != NULL) {
        gtk_box_remove(GTK_BOX(d->box), ch);
    }

    for (guint i = 0; i < d->items->len; i++) {
        ToplevelItem *it = d->items->pdata[i];
        GtkWidget *btn = gtk_button_new();
        gtk_widget_add_css_class(btn, "plume-dock-item");
        if (it->active && !it->minimized) gtk_widget_add_css_class(btn, "active");
        if (it->minimized) gtk_widget_add_css_class(btn, "minimized");

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_box_append(GTK_BOX(vbox), make_icon_for_app(it->app_id));

        GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(dot, "plume-dock-dot");
        gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(vbox), dot);

        gtk_button_set_child(GTK_BUTTON(btn), vbox);
        if (it->title && *it->title) {
            gtk_widget_set_tooltip_text(btn, it->title);
        } else if (it->app_id && *it->app_id) {
            gtk_widget_set_tooltip_text(btn, it->app_id);
        }
        g_signal_connect(btn, "clicked", G_CALLBACK(on_item_clicked), it);
        it->btn = btn;
        gtk_box_append(GTK_BOX(d->box), btn);
    }

    sync_visibility(d);
}

static void tl_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *title) {
    (void)h;
    ToplevelItem *it = data;
    g_free(it->title);
    it->title = g_strdup(title ? title : "");
}

static void tl_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *app_id) {
    (void)h;
    ToplevelItem *it = data;
    g_free(it->app_id);
    it->app_id = g_strdup(app_id ? app_id : "");
}

static void tl_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *state) {
    (void)h;
    ToplevelItem *it = data;
    gboolean active = FALSE;
    gboolean minimized = FALSE;
    uint32_t *entry;
    wl_array_for_each(entry, state) {
        if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) active = TRUE;
        if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) minimized = TRUE;
    }
    it->active = active;
    it->minimized = minimized;
}

static void tl_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;
    ToplevelItem *it = data;
    rebuild_dock_ui(it->dock);
}

static void tl_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)h;
    ToplevelItem *it = data;
    DockState *d = it->dock;
    g_ptr_array_remove(d->items, it);
    rebuild_dock_ui(d);
}

static void tl_noop_output(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) { (void)d; (void)h; (void)o; }
static void tl_noop_parent(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct zwlr_foreign_toplevel_handle_v1 *p) { (void)d; (void)h; (void)p; }

static const struct zwlr_foreign_toplevel_handle_v1_listener TL_LISTENER = {
    .title = tl_title,
    .app_id = tl_app_id,
    .output_enter = tl_noop_output,
    .output_leave = tl_noop_output,
    .state = tl_state,
    .done = tl_done,
    .closed = tl_closed,
    .parent = tl_noop_parent,
};

static void mgr_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *mgr,
                         struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)mgr;
    DockState *d = data;
    ToplevelItem *it = g_new0(ToplevelItem, 1);
    it->dock = d;
    it->handle = handle;
    g_ptr_array_add(d->items, it);
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &TL_LISTENER, it);
}

static void mgr_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *mgr) {
    (void)data; (void)mgr;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener MGR_LISTENER = {
    .toplevel = mgr_toplevel,
    .finished = mgr_finished,
};

static void reg_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t ver) {
    DockState *d = data;
    if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        uint32_t v = ver < 3 ? ver : 3;
        d->manager = wl_registry_bind(reg, name, &zwlr_foreign_toplevel_manager_v1_interface, v);
        zwlr_foreign_toplevel_manager_v1_add_listener(d->manager, &MGR_LISTENER, d);
    }
}

static void reg_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener REG_LISTENER = {
    .global = reg_global,
    .global_remove = reg_global_remove,
};


static void apply_dock_theme(DockState *d) {
    char mode[16] = "dark";
    char accent[16] = "#2f7fe6";

    char *ini = g_build_filename(g_get_user_config_dir(), "plume", "theme.ini", NULL);
    char *content = NULL;
    if (g_file_get_contents(ini, &content, NULL, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "mode=")) {
                const char *v = lines[i] + 5;
                if (strcmp(v, "light") == 0 || strcmp(v, "dark") == 0)
                    g_strlcpy(mode, v, sizeof(mode));
            } else if (g_str_has_prefix(lines[i], "accent=")) {
                const char *v = lines[i] + 7;
                if (v[0] == '#' && strlen(v) == 7)
                    g_strlcpy(accent, v, sizeof(accent));
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(ini);

    gboolean light = (strcmp(mode, "light") == 0);
    const char *bg       = light ? "#e8ecf3" : "#13161c";
    const char *border   = light ? "#cdd5e2" : "#252b37";
    const char *hover_bg = light ? "#dce2ec" : "#1c212b";
    const char *hover_bd = light ? "#b8c3d4" : "#2b3342";
    const char *dot_dim  = light ? "#8b96a8" : "#5b6577";

    char *css = g_strdup_printf(
        "window.plume-dock-win, window.background.plume-dock-win {"
        "  background: none; background-color: transparent; box-shadow: none;"
        "}"
        "box.plume-dock-bar {"
        "  background-color: %s;"
        "  border: 1px solid %s;"
        "  border-radius: 10px;"
        "  padding: 5px 8px;"
        "  opacity: 0;"
        "  transform: translateY(10px);"
        "  transition: opacity 200ms cubic-bezier(0.22, 1, 0.36, 1),"
        "              transform 200ms cubic-bezier(0.22, 1, 0.36, 1);"
        "}"
        "box.plume-dock-bar.visible {"
        "  opacity: 1;"
        "  transform: translateY(0px);"
        "}"
        "button.plume-dock-item {"
        "  background: transparent; background-image: none; border: 1px solid transparent;"
        "  border-radius: 7px; padding: 5px 7px; box-shadow: none;"
        "}"
        "button.plume-dock-item:hover {"
        "  background: %s; border-color: %s;"
        "}"
        "button.plume-dock-item.active {"
        "  background: %s; border-color: %s;"
        "}"
        "button.plume-dock-item.minimized {"
        "  opacity: 0.6;"
        "}"
        "box.plume-dock-dot {"
        "  min-width: 5px; min-height: 5px; border-radius: 3px;"
        "  background-color: %s;"
        "}"
        "button.plume-dock-item.active box.plume-dock-dot {"
        "  min-width: 12px; background-color: %s;"
        "}",
        bg, border, hover_bg, hover_bd, hover_bg, accent, dot_dim, accent
    );
    gtk_css_provider_load_from_string(d->css_provider, css);
    g_free(css);
}

static void on_theme_file_changed(GFileMonitor *mon, GFile *file, GFile *other,
                                  GFileMonitorEvent ev, gpointer user_data) {
    (void)mon; (void)file; (void)other;
    if (ev == G_FILE_MONITOR_EVENT_CHANGED ||
        ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
        ev == G_FILE_MONITOR_EVENT_CREATED ||
        ev == G_FILE_MONITOR_EVENT_MOVED_IN) {
        apply_dock_theme((DockState *)user_data);
    }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    DockState *d = user_data;
    d->app = app;
    d->items = g_ptr_array_new_with_free_func(free_item);

    d->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(d->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );
    apply_dock_theme(d);
    {
        char *cfg_dir = g_build_filename(g_get_user_config_dir(), "plume", NULL);
        g_mkdir_with_parents(cfg_dir, 0755);
        GFile *gf = g_file_new_for_path(cfg_dir);
        d->theme_mon = g_file_monitor_directory(gf, G_FILE_MONITOR_NONE, NULL, NULL);
        if (d->theme_mon) {
            g_signal_connect(d->theme_mon, "changed", G_CALLBACK(on_theme_file_changed), d);
        }
        g_object_unref(gf);
        g_free(cfg_dir);
    }

    d->win = gtk_application_window_new(app);
    gtk_widget_add_css_class(d->win, "plume-dock-win");
    gtk_window_set_decorated(GTK_WINDOW(d->win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(d->win), FALSE);

    if (gtk_layer_is_supported()) {
        gtk_layer_init_for_window(GTK_WINDOW(d->win));
        gtk_layer_set_layer(GTK_WINDOW(d->win), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_anchor(GTK_WINDOW(d->win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(d->win), GTK_LAYER_SHELL_EDGE_BOTTOM, 10);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(d->win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    }

    d->bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(d->bar, "plume-dock-bar");
    gtk_window_set_child(GTK_WINDOW(d->win), d->bar);

    d->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(d->bar), d->box);

    GdkDisplay *gdpy = gdk_display_get_default();
    if (GDK_IS_WAYLAND_DISPLAY(gdpy)) {
        d->wl_dpy = gdk_wayland_display_get_wl_display(gdpy);
        d->registry = wl_display_get_registry(d->wl_dpy);
        wl_registry_add_listener(d->registry, &REG_LISTENER, d);
        wl_display_roundtrip(d->wl_dpy);
        /* Не вешаем свой g_unix_fd_add: GDK4 сам диспатчит d->wl_dpy */
    }

    rebuild_dock_ui(d);
    if (d->items->len == 0) {
        gtk_widget_set_visible(d->win, FALSE);
    }
}

int main(int argc, char **argv) {
    DockState d = { 0 };
    GtkApplication *app = gtk_application_new("org.ouvert.plume.dock", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &d);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (d.items) g_ptr_array_unref(d.items);
    g_object_unref(app);
    return status;
}
