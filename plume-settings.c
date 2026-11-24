#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *code;
    const char *name;
} LayoutDef;

static const LayoutDef ALL_LAYOUTS[] = {
    { "us", "Английская (US)" },
    { "ru", "Русская (RU)" },
    { "de", "Немецкая (DE)" },
    { "fr", "Французская (FR)" },
    { "es", "Испанская (ES)" },
    { "it", "Итальянская (IT)" },
    { "pl", "Польская (PL)" },
    { "ee", "Эстонская (EE)" },
    { "fi", "Финская (FI)" },
    { "by", "Белорусская (BY)" },
    { "kz", "Казахская (KZ)" },
    { "tr", "Турецкая (TR)" },
    { "gb", "Английская (UK)" },
    { NULL, NULL }
};

static const char *const TOGGLE_IDS[] = {
    "grp:alt_shift_toggle",
    "grp:win_space_toggle",
    "grp:ctrl_shift_toggle",
    "grp:caps_toggle",
    NULL
};
static const char *const TOGGLE_LABELS[] = {
    "Alt + Shift",
    "Super + Пробел",
    "Ctrl + Shift",
    "Caps Lock",
    NULL
};

static const char *const ACCENT_PRESETS[] = {
    "#2f7fe6", "#5a8dee", "#22b573", "#e59a24", "#e04f3e", "#8b5cf6"
};
#define N_ACCENTS 6

typedef struct {
    const char *name;
    const char *mode;
    const char *accent;
} BuiltinProfile;

static const BuiltinProfile BUILTIN_PROFILES[] = {
    { "Plume Graphite", "dark",  "#2f7fe6" },
    { "Plume Light",    "light", "#2f7fe6" },
    { "Midnight",       "dark",  "#5a8dee" },
    { "Emerald",        "dark",  "#22b573" },
};
#define N_BUILTINS 4

typedef struct {
    GtkWidget *win;
    GtkWidget *stack;
    GtkCssProvider *css_provider;

    /* Внешний вид */
    char mode[16];
    char accent[16];
    char cur_wall[1024];
    GtkWidget *btn_dark;
    GtkWidget *btn_light;
    GtkWidget *swatch_btns[N_ACCENTS];
    GtkWidget *accent_hex_lbl;
    GtkWidget *wall_pic;
    GtkWidget *wall_path_lbl;
    GtkWidget *profiles_flow;
    GtkWidget *profile_entry;

    /* Клавиатура */
    GPtrArray *layouts;
    GPtrArray *addable_codes;
    GtkWidget *kbd_list;
    GtkWidget *dd_add_layout;
    GtkWidget *btn_add_layout;
    GtkWidget *dd_toggle;
    GtkWidget *kbd_status_lbl;
    gboolean loading;
} State;

static char *get_plume_root(void) {
    const char *env = getenv("PLUME_ROOT");
    if (env && *env) return g_strdup(env);
    if (g_file_test("/usr/share/plume/assets", G_FILE_TEST_IS_DIR))
        return g_strdup("/usr/share/plume");
    return g_build_filename(g_get_home_dir(), "plume", NULL);
}

static char *get_default_wallpaper(void) {
    char *root = get_plume_root();
    char *p = g_build_filename(root, "assets", "wallpaper.png", NULL);
    g_free(root);
    return p;
}

static char *get_config_dir(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "plume", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static char *get_themes_dir(void) {
    char *cfg = get_config_dir();
    char *dir = g_build_filename(cfg, "themes", NULL);
    g_free(cfg);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static const char *layout_display_name(const char *code) {
    for (int i = 0; ALL_LAYOUTS[i].code; i++) {
        if (strcmp(ALL_LAYOUTS[i].code, code) == 0)
            return ALL_LAYOUTS[i].name;
    }
    return code;
}

static int clamp_byte(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static void shift_hex_color(const char *hex, int delta, char out[16]) {
    unsigned int r = 47, g = 127, b = 230;
    if (hex && hex[0] == '#' && strlen(hex) == 7) {
        sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
    }
    g_snprintf(out, 16, "#%02x%02x%02x",
        clamp_byte((int)r + delta),
        clamp_byte((int)g + delta),
        clamp_byte((int)b + delta));
}

static void apply_window_css(State *st) {
    gboolean light = (strcmp(st->mode, "light") == 0);
    const char *bg        = light ? "#f3f5f9" : "#171a21";
    const char *bg_alt    = light ? "#e8ecf3" : "#13161c";
    const char *bg_hover  = light ? "#dce2ec" : "#1c212b";
    const char *border    = light ? "#cdd5e2" : "#252b37";
    const char *border_s  = light ? "#b8c3d4" : "#2b3342";
    const char *fg        = light ? "#171c26" : "#edf0f5";
    const char *fg_dim    = light ? "#434d5e" : "#cfd5df";
    const char *muted     = light ? "#667185" : "#8b96a8";

    char acc_hover[16];
    shift_hex_color(st->accent, light ? -16 : 18, acc_hover);

    GString *css = g_string_new("");
    g_string_append_printf(css,
        "window, window.background, window.csd {"
        "  background-color: %s; color: %s;"
        "  font-family: 'Onest', 'Sora', 'JetBrainsMono Nerd Font', 'Symbols Nerd Font', sans-serif;"
        "  font-size: 13px;"
        "}"
        "window headerbar {"
        "  background-color: %s; background-image: none; color: %s;"
        "  border-bottom: 1px solid %s; box-shadow: none;"
        "}"
        "box.ps-sidebar {"
        "  background-color: %s; border-right: 1px solid %s; padding: 12px 8px;"
        "}"
        "list.ps-nav { background: transparent; }"
        "list.ps-nav row { padding: 9px 12px; margin: 2px 0; border-radius: 6px; color: %s; }"
        "list.ps-nav row:hover { background-color: %s; color: %s; }"
        "list.ps-nav row:selected { background-color: %s; color: #ffffff; }"
        ".ps-nav-icon { font-size: 15px; min-width: 18px; }"
        "box.ps-page { padding: 20px 24px; }"
        ".ps-title { font-size: 18px; font-weight: 600; color: %s; }"
        ".ps-hero-title { font-size: 20px; font-weight: 600; color: %s; }"
        ".ps-card { background-color: %s; border: 1px solid %s; border-radius: 8px; padding: 14px 16px; }"
        ".ps-card-title { font-weight: 600; color: %s; }"
        ".ps-muted { color: %s; font-size: 12px; }"
        ".ps-tool-icon { font-size: 20px; color: %s; min-width: 28px; }"
        ".ps-preview-wrap { background-color: %s; border: 1px solid %s; border-radius: 8px; padding: 4px; }"
        "button.ps-btn {"
        "  background: %s; background-image: none; color: %s;"
        "  border: 1px solid %s; border-radius: 6px; padding: 6px 12px; box-shadow: none;"
        "}"
        "button.ps-btn:hover { background: %s; background-image: none; border-color: %s; }"
        "button.ps-btn-primary {"
        "  background: %s; background-image: none; color: #ffffff;"
        "  border: 1px solid %s; border-radius: 6px; padding: 6px 12px; box-shadow: none;"
        "}"
        "button.ps-btn-primary:hover { background: %s; background-image: none; }"
        "button.ps-btn-sm { padding: 3px 8px; min-height: 24px; min-width: 24px; }"
        "button.ps-swatch { min-width: 28px; min-height: 28px; padding: 0; border-radius: 14px; border: 2px solid transparent; background-image: none; box-shadow: none; }"
        "button.ps-swatch-active { border: 2px solid %s; }"
        "entry.ps-entry {"
        "  background: %s; color: %s; border: 1px solid %s; border-radius: 6px; padding: 5px 10px;"
        "}"
        "entry.ps-entry:focus-within { border-color: %s; }"
        "list.ps-kbd-list { background: transparent; }"
        "list.ps-kbd-list row { background: %s; border: 1px solid %s; border-radius: 6px; padding: 8px 10px; margin-bottom: 6px; }"
        "dropdown > button {"
        "  background: %s; background-image: none; color: %s;"
        "  border: 1px solid %s; border-radius: 6px; padding: 6px 12px; box-shadow: none;"
        "}"
        "dropdown > button:hover { border-color: %s; }"
        "popover > contents {"
        "  background: %s; background-image: none; border: 1px solid %s;"
        "  border-radius: 6px; color: %s; padding: 4px;"
        "}"
        "popover listview { background: transparent; color: %s; }"
        "popover listview > row { padding: 6px 10px; border-radius: 4px; color: %s; }"
        "popover listview > row:hover, popover listview > row:selected { background: %s; color: #ffffff; }",
        bg, fg,
        bg_alt, fg, border,
        bg_alt, border,
        fg_dim, bg_hover, fg, st->accent,
        fg, fg,
        bg_alt, border,
        fg, muted, st->accent,
        bg_alt, border,
        bg_hover, fg, border_s,
        border, st->accent,
        st->accent, st->accent, acc_hover,
        fg,
        bg, fg, border_s, st->accent,
        bg, border,
        bg_hover, fg, border_s, st->accent,
        bg, border_s, fg, fg, fg, st->accent
    );

    for (int i = 0; i < N_ACCENTS; i++) {
        g_string_append_printf(css,
            "button.ps-swatch-%d { background: %s; background-image: none; }",
            i, ACCENT_PRESETS[i]);
    }

    gtk_css_provider_load_from_string(st->css_provider, css->str);
    g_string_free(css, TRUE);

    if (st->btn_dark && st->btn_light) {
        gtk_widget_remove_css_class(st->btn_dark, "ps-btn-primary");
        gtk_widget_remove_css_class(st->btn_dark, "ps-btn");
        gtk_widget_remove_css_class(st->btn_light, "ps-btn-primary");
        gtk_widget_remove_css_class(st->btn_light, "ps-btn");
        gtk_widget_add_css_class(st->btn_dark, light ? "ps-btn" : "ps-btn-primary");
        gtk_widget_add_css_class(st->btn_light, light ? "ps-btn-primary" : "ps-btn");
    }
    for (int i = 0; i < N_ACCENTS; i++) {
        if (!st->swatch_btns[i]) continue;
        if (g_ascii_strcasecmp(st->accent, ACCENT_PRESETS[i]) == 0)
            gtk_widget_add_css_class(st->swatch_btns[i], "ps-swatch-active");
        else
            gtk_widget_remove_css_class(st->swatch_btns[i], "ps-swatch-active");
    }
    if (st->accent_hex_lbl) {
        gtk_label_set_text(GTK_LABEL(st->accent_hex_lbl), st->accent);
    }
}

static void save_and_apply_theme(State *st) {
    char *cfg = get_config_dir();
    char *ini = g_build_filename(cfg, "theme.ini", NULL);
    g_free(cfg);

    char *body = g_strdup_printf("mode=%s\naccent=%s\n", st->mode, st->accent);
    g_file_set_contents(ini, body, -1, NULL);
    g_free(body);
    g_free(ini);

    apply_window_css(st);

    char *argv[] = { (char *)"plume-apply-theme", NULL };
    GError *err = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
}

static void load_theme_prefs(State *st) {
    g_strlcpy(st->mode, "dark", sizeof(st->mode));
    g_strlcpy(st->accent, "#2f7fe6", sizeof(st->accent));

    char *cfg = get_config_dir();
    char *ini = g_build_filename(cfg, "theme.ini", NULL);
    g_free(cfg);

    char *content = NULL;
    if (g_file_get_contents(ini, &content, NULL, NULL)) {
        char **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "mode=")) {
                const char *v = lines[i] + 5;
                if (strcmp(v, "light") == 0 || strcmp(v, "dark") == 0)
                    g_strlcpy(st->mode, v, sizeof(st->mode));
            } else if (g_str_has_prefix(lines[i], "accent=")) {
                const char *v = lines[i] + 7;
                if (v[0] == '#' && strlen(v) == 7)
                    g_strlcpy(st->accent, v, sizeof(st->accent));
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(ini);
}

static char *read_current_wallpaper(void) {
    char *cfg = get_config_dir();
    char *wf = g_build_filename(cfg, "wallpaper", NULL);
    g_free(cfg);

    char *content = NULL;
    if (g_file_get_contents(wf, &content, NULL, NULL)) {
        g_strstrip(content);
        if (*content && g_file_test(content, G_FILE_TEST_IS_REGULAR)) {
            g_free(wf);
            return content;
        }
        g_free(content);
    }
    g_free(wf);
    return get_default_wallpaper();
}

static void apply_wallpaper(State *st, const char *path, gboolean save_custom) {
    char *cfg = get_config_dir();
    char *wf = g_build_filename(cfg, "wallpaper", NULL);
    g_free(cfg);

    if (save_custom) {
        g_file_set_contents(wf, path, -1, NULL);
    } else {
        g_unlink(wf);
    }
    g_free(wf);

    if (path && g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_strlcpy(st->cur_wall, path, sizeof(st->cur_wall));
        gtk_picture_set_filename(GTK_PICTURE(st->wall_pic), path);
        gtk_label_set_text(GTK_LABEL(st->wall_path_lbl), path);

        char *argv[] = {
            (char *)"sh", (char *)"-c",
            (char *)"pkill -x swaybg 2>/dev/null || true; exec swaybg -i \"$1\" -m fill",
            (char *)"_", (char *)path, NULL
        };
        GError *err = NULL;
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
        if (err) g_error_free(err);
    }
}

static void on_wall_picked(GObject *src, GAsyncResult *res, gpointer user_data) {
    State *st = user_data;
    GError *err = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, &err);
    if (err) { g_error_free(err); return; }
    if (file) {
        char *path = g_file_get_path(file);
        if (path) {
            apply_wallpaper(st, path, TRUE);
            g_free(path);
        }
        g_object_unref(file);
    }
}

static void on_choose_wall(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Выбор обоев");

    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, "Изображения (PNG, JPG, WebP)");
    gtk_file_filter_add_mime_type(flt, "image/png");
    gtk_file_filter_add_mime_type(flt, "image/jpeg");
    gtk_file_filter_add_mime_type(flt, "image/webp");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, flt);
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));

    gtk_file_dialog_open(dlg, GTK_WINDOW(st->win), NULL, on_wall_picked, st);
    g_object_unref(flt);
    g_object_unref(filters);
    g_object_unref(dlg);
}

static void on_reset_wall(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    char *def = get_default_wallpaper();
    apply_wallpaper(st, def, FALSE);
    g_free(def);
}

static void on_mode_dark(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    g_strlcpy(st->mode, "dark", sizeof(st->mode));
    save_and_apply_theme(st);
}

static void on_mode_light(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    g_strlcpy(st->mode, "light", sizeof(st->mode));
    save_and_apply_theme(st);
}

static void on_swatch_clicked(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    const char *hex = g_object_get_data(G_OBJECT(btn), "accent-hex");
    if (!hex) return;
    g_strlcpy(st->accent, hex, sizeof(st->accent));
    save_and_apply_theme(st);
}

static void on_color_picked(GObject *src, GAsyncResult *res, gpointer user_data) {
    State *st = user_data;
    GError *err = NULL;
    GdkRGBA *rgba = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src), res, &err);
    if (err) { g_error_free(err); return; }
    if (rgba) {
        int r = clamp_byte((int)(rgba->red * 255.0 + 0.5));
        int g = clamp_byte((int)(rgba->green * 255.0 + 0.5));
        int b = clamp_byte((int)(rgba->blue * 255.0 + 0.5));
        g_snprintf(st->accent, sizeof(st->accent), "#%02x%02x%02x", r, g, b);
        gdk_rgba_free(rgba);
        save_and_apply_theme(st);
    }
}

static void on_choose_custom_color(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    GtkColorDialog *dlg = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dlg, "Выбор акцентного цвета");
    gtk_color_dialog_set_with_alpha(dlg, FALSE);
    GdkRGBA cur = { 0.18f, 0.50f, 0.90f, 1.0f };
    gdk_rgba_parse(&cur, st->accent);
    gtk_color_dialog_choose_rgba(dlg, GTK_WINDOW(st->win), &cur, NULL, on_color_picked, st);
    g_object_unref(dlg);
}

static void refresh_profiles_ui(State *st);

static void on_apply_builtin_profile(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "builtin-idx"));
    if (idx >= N_BUILTINS) return;
    g_strlcpy(st->mode, BUILTIN_PROFILES[idx].mode, sizeof(st->mode));
    g_strlcpy(st->accent, BUILTIN_PROFILES[idx].accent, sizeof(st->accent));
    save_and_apply_theme(st);
}

static void on_apply_custom_profile(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    const char *path = g_object_get_data(G_OBJECT(btn), "profile-path");
    if (!path) return;
    char *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) return;

    char wall[1024] = { 0 };
    char **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "mode=")) {
            g_strlcpy(st->mode, lines[i] + 5, sizeof(st->mode));
        } else if (g_str_has_prefix(lines[i], "accent=")) {
            g_strlcpy(st->accent, lines[i] + 7, sizeof(st->accent));
        } else if (g_str_has_prefix(lines[i], "wallpaper=")) {
            g_strlcpy(wall, lines[i] + 10, sizeof(wall));
        }
    }
    g_strfreev(lines);
    g_free(content);

    save_and_apply_theme(st);
    if (wall[0] && g_file_test(wall, G_FILE_TEST_IS_REGULAR)) {
        char *def = get_default_wallpaper();
        gboolean is_custom = (strcmp(wall, def) != 0);
        g_free(def);
        apply_wallpaper(st, wall, is_custom);
    }
}

static void on_delete_custom_profile(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    const char *path = g_object_get_data(G_OBJECT(btn), "profile-path");
    if (path) g_unlink(path);
    refresh_profiles_ui(st);
}

static void on_save_profile(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    const char *raw = gtk_editable_get_text(GTK_EDITABLE(st->profile_entry));
    char *name = g_strdup(raw ? raw : "");
    g_strstrip(name);
    if (!*name) {
        g_free(name);
        return;
    }

    char *tdir = get_themes_dir();
    char *fname = g_strdup_printf("theme-%" G_GINT64_FORMAT ".ini", g_get_real_time());
    char *fpath = g_build_filename(tdir, fname, NULL);
    g_free(fname);
    g_free(tdir);

    char *body = g_strdup_printf(
        "name=%s\nmode=%s\naccent=%s\nwallpaper=%s\n",
        name, st->mode, st->accent, st->cur_wall
    );
    g_file_set_contents(fpath, body, -1, NULL);
    g_free(body);
    g_free(fpath);
    g_free(name);

    gtk_editable_set_text(GTK_EDITABLE(st->profile_entry), "");
    refresh_profiles_ui(st);
}

static void refresh_profiles_ui(State *st) {
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(st->profiles_flow)) != NULL) {
        gtk_flow_box_remove(GTK_FLOW_BOX(st->profiles_flow), ch);
    }

    for (guint i = 0; i < N_BUILTINS; i++) {
        GtkWidget *btn = gtk_button_new_with_label(BUILTIN_PROFILES[i].name);
        gtk_widget_add_css_class(btn, "ps-btn");
        g_object_set_data(G_OBJECT(btn), "builtin-idx", GUINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_apply_builtin_profile), st);
        gtk_flow_box_append(GTK_FLOW_BOX(st->profiles_flow), btn);
    }

    char *tdir = get_themes_dir();
    GDir *dir = g_dir_open(tdir, 0, NULL);
    if (dir) {
        const char *entry;
        while ((entry = g_dir_read_name(dir)) != NULL) {
            if (!g_str_has_suffix(entry, ".ini")) continue;
            char *fpath = g_build_filename(tdir, entry, NULL);
            char *content = NULL;
            char *pname = NULL;
            if (g_file_get_contents(fpath, &content, NULL, NULL)) {
                char **lines = g_strsplit(content, "\n", -1);
                for (int i = 0; lines[i]; i++) {
                    if (g_str_has_prefix(lines[i], "name=")) {
                        pname = g_strdup(lines[i] + 5);
                        break;
                    }
                }
                g_strfreev(lines);
                g_free(content);
            }
            if (!pname || !*pname) {
                g_free(pname);
                pname = g_strdup(entry);
            }

            GtkWidget *pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget *abtn = gtk_button_new_with_label(pname);
            gtk_widget_add_css_class(abtn, "ps-btn");
            g_object_set_data_full(G_OBJECT(abtn), "profile-path", g_strdup(fpath), g_free);
            g_signal_connect(abtn, "clicked", G_CALLBACK(on_apply_custom_profile), st);

            GtkWidget *dbtn = gtk_button_new_with_label("×");
            gtk_widget_add_css_class(dbtn, "ps-btn");
            gtk_widget_add_css_class(dbtn, "ps-btn-sm");
            g_object_set_data_full(G_OBJECT(dbtn), "profile-path", g_strdup(fpath), g_free);
            g_signal_connect(dbtn, "clicked", G_CALLBACK(on_delete_custom_profile), st);

            gtk_box_append(GTK_BOX(pill), abtn);
            gtk_box_append(GTK_BOX(pill), dbtn);
            gtk_flow_box_append(GTK_FLOW_BOX(st->profiles_flow), pill);

            g_free(pname);
            g_free(fpath);
        }
        g_dir_close(dir);
    }
    g_free(tdir);
}

/* ==================== Клавиатура ==================== */

static gboolean has_layout(State *st, const char *code) {
    for (guint i = 0; i < st->layouts->len; i++) {
        if (strcmp((const char *)st->layouts->pdata[i], code) == 0)
            return TRUE;
    }
    return FALSE;
}

static void parse_env_file_for_kbd(const char *path, char **out_layout, char **out_opts) {
    char *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) return;
    char **lines = g_strsplit(content, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        if (!*out_layout && g_str_has_prefix(lines[i], "XKB_DEFAULT_LAYOUT=")) {
            char *v = g_strdup(lines[i] + strlen("XKB_DEFAULT_LAYOUT="));
            g_strstrip(v);
            *out_layout = v;
        } else if (!*out_opts && g_str_has_prefix(lines[i], "XKB_DEFAULT_OPTIONS=")) {
            char *v = g_strdup(lines[i] + strlen("XKB_DEFAULT_OPTIONS="));
            g_strstrip(v);
            *out_opts = v;
        }
    }
    g_strfreev(lines);
    g_free(content);
}

static void update_wayfire_kbd(const char *layout_csv, const char *opts) {
    const char *rt = getenv("PLUME_RUNTIME");
    if (!rt || !*rt) return;
    char *wf = g_build_filename(rt, "wayfire", "wayfire.ini", NULL);
    char *content = NULL;
    if (!g_file_get_contents(wf, &content, NULL, NULL)) {
        g_free(wf);
        return;
    }
    char **lines = g_strsplit(content, "\n", -1);
    GString *out = g_string_new("");
    for (int i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "xkb_layout")) {
            g_string_append_printf(out, "xkb_layout = %s", layout_csv);
        } else if (g_str_has_prefix(lines[i], "xkb_options")) {
            g_string_append_printf(out, "xkb_options = %s", opts);
        } else {
            g_string_append(out, lines[i]);
        }
        if (lines[i + 1]) g_string_append_c(out, '\n');
    }
    g_file_set_contents(wf, out->str, -1, NULL);
    g_string_free(out, TRUE);
    g_strfreev(lines);
    g_free(content);
    g_free(wf);
}

static void save_and_apply_keyboard(State *st) {
    GString *csv = g_string_new("");
    for (guint i = 0; i < st->layouts->len; i++) {
        if (i > 0) g_string_append_c(csv, ',');
        g_string_append(csv, (const char *)st->layouts->pdata[i]);
    }
    if (csv->len == 0) g_string_append(csv, "us");

    guint t_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->dd_toggle));
    if (t_idx > 3) t_idx = 0;
    const char *toggle = TOGGLE_IDS[t_idx];

    char *cfg = get_config_dir();
    char *kf = g_build_filename(cfg, "keyboard.env", NULL);
    g_free(cfg);

    char *body = g_strdup_printf(
        "XKB_DEFAULT_LAYOUT=%s\nXKB_DEFAULT_OPTIONS=%s\n",
        csv->str, toggle
    );
    g_file_set_contents(kf, body, -1, NULL);
    g_free(body);
    g_free(kf);

    update_wayfire_kbd(csv->str, toggle);
    g_string_free(csv, TRUE);

    gtk_label_set_text(GTK_LABEL(st->kbd_status_lbl), "Раскладки сохранены и применены в Wayfire");
}

static void refresh_keyboard_ui(State *st);

static void on_move_layout_up(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "layout-idx"));
    if (idx == 0 || idx >= st->layouts->len) return;
    gpointer tmp = st->layouts->pdata[idx - 1];
    st->layouts->pdata[idx - 1] = st->layouts->pdata[idx];
    st->layouts->pdata[idx] = tmp;
    save_and_apply_keyboard(st);
    refresh_keyboard_ui(st);
}

static void on_remove_layout(GtkButton *btn, gpointer user_data) {
    State *st = user_data;
    guint idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "layout-idx"));
    if (st->layouts->len <= 1 || idx >= st->layouts->len) return;
    g_ptr_array_remove_index(st->layouts, idx);
    save_and_apply_keyboard(st);
    refresh_keyboard_ui(st);
}

static void on_add_layout(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->dd_add_layout));
    if (sel >= st->addable_codes->len) return;
    const char *code = (const char *)st->addable_codes->pdata[sel];
    if (!has_layout(st, code)) {
        g_ptr_array_add(st->layouts, g_strdup(code));
        save_and_apply_keyboard(st);
        refresh_keyboard_ui(st);
    }
}

static void on_toggle_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)obj; (void)pspec;
    State *st = user_data;
    if (st->loading) return;
    save_and_apply_keyboard(st);
}

static void refresh_keyboard_ui(State *st) {
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(st->kbd_list)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(st->kbd_list), ch);
    }

    for (guint i = 0; i < st->layouts->len; i++) {
        const char *code = (const char *)st->layouts->pdata[i];
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

        char *txt = g_strdup_printf("%u. %s", i + 1, layout_display_name(code));
        GtkWidget *lbl = gtk_label_new(txt);
        g_free(txt);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_box_append(GTK_BOX(row_box), lbl);

        if (i > 0) {
            GtkWidget *up = gtk_button_new_with_label("↑");
            gtk_widget_add_css_class(up, "ps-btn");
            gtk_widget_add_css_class(up, "ps-btn-sm");
            g_object_set_data(G_OBJECT(up), "layout-idx", GUINT_TO_POINTER(i));
            g_signal_connect(up, "clicked", G_CALLBACK(on_move_layout_up), st);
            gtk_box_append(GTK_BOX(row_box), up);
        }

        GtkWidget *del = gtk_button_new_with_label("×");
        gtk_widget_add_css_class(del, "ps-btn");
        gtk_widget_add_css_class(del, "ps-btn-sm");
        gtk_widget_set_sensitive(del, st->layouts->len > 1);
        g_object_set_data(G_OBJECT(del), "layout-idx", GUINT_TO_POINTER(i));
        g_signal_connect(del, "clicked", G_CALLBACK(on_remove_layout), st);
        gtk_box_append(GTK_BOX(row_box), del);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(st->kbd_list), row);
    }

    g_ptr_array_set_size(st->addable_codes, 0);
    GtkStringList *slist = gtk_string_list_new(NULL);
    for (int i = 0; ALL_LAYOUTS[i].code; i++) {
        if (!has_layout(st, ALL_LAYOUTS[i].code)) {
            g_ptr_array_add(st->addable_codes, g_strdup(ALL_LAYOUTS[i].code));
            gtk_string_list_append(slist, ALL_LAYOUTS[i].name);
        }
    }
    gtk_drop_down_set_model(GTK_DROP_DOWN(st->dd_add_layout), G_LIST_MODEL(slist));
    g_object_unref(slist);
    gtk_widget_set_sensitive(st->dd_add_layout, st->addable_codes->len > 0);
    gtk_widget_set_sensitive(st->btn_add_layout, st->addable_codes->len > 0);
}

static void load_keyboard_prefs(State *st) {
    char *raw_layouts = NULL;
    char *raw_opts = NULL;

    char *cfg = get_config_dir();
    char *kf = g_build_filename(cfg, "keyboard.env", NULL);
    g_free(cfg);
    parse_env_file_for_kbd(kf, &raw_layouts, &raw_opts);
    g_free(kf);
    parse_env_file_for_kbd("/etc/environment", &raw_layouts, &raw_opts);

    g_ptr_array_set_size(st->layouts, 0);
    const char *src = (raw_layouts && *raw_layouts) ? raw_layouts : "us,ru";
    char **parts = g_strsplit(src, ",", -1);
    for (int i = 0; parts[i]; i++) {
        g_strstrip(parts[i]);
        if (*parts[i] && !has_layout(st, parts[i]))
            g_ptr_array_add(st->layouts, g_strdup(parts[i]));
    }
    g_strfreev(parts);
    if (st->layouts->len == 0)
        g_ptr_array_add(st->layouts, g_strdup("us"));

    guint t_idx = 0;
    if (raw_opts) {
        for (guint j = 0; TOGGLE_IDS[j]; j++) {
            if (strcmp(raw_opts, TOGGLE_IDS[j]) == 0) { t_idx = j; break; }
        }
    }
    g_free(raw_layouts);
    g_free(raw_opts);

    st->loading = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->dd_toggle), t_idx);
    st->loading = FALSE;
    refresh_keyboard_ui(st);
}

/* ==================== Общие виджеты и страницы ==================== */

static void on_launch_tool(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    const char *cmd = g_object_get_data(G_OBJECT(btn), "tool-cmd");
    if (!cmd) return;
    char *argv[] = { (char *)cmd, NULL };
    GError *err = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err) g_error_free(err);
}

static void on_nav_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    if (!row) return;
    State *st = user_data;
    const char *page = g_object_get_data(G_OBJECT(row), "page-id");
    if (page) gtk_stack_set_visible_child_name(GTK_STACK(st->stack), page);
}

static GtkWidget *make_section_title(const char *text) {
    GtkWidget *lbl = gtk_label_new(text);
    gtk_widget_add_css_class(lbl, "ps-title");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    return lbl;
}

static GtkWidget *make_tool_row(const char *icon_txt, const char *title, const char *desc, const char *cmd) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *ico = gtk_label_new(icon_txt);
    gtk_widget_add_css_class(ico, "ps-tool-icon");
    gtk_box_append(GTK_BOX(card), ico);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(info, TRUE);
    GtkWidget *t = gtk_label_new(title);
    gtk_widget_add_css_class(t, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(t), 0.0f);
    GtkWidget *d = gtk_label_new(desc);
    gtk_widget_add_css_class(d, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(d), 0.0f);
    gtk_box_append(GTK_BOX(info), t);
    gtk_box_append(GTK_BOX(info), d);
    gtk_box_append(GTK_BOX(card), info);

    GtkWidget *btn = gtk_button_new_with_label("Открыть");
    gtk_widget_add_css_class(btn, "ps-btn");
    gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(btn), "tool-cmd", (gpointer)cmd);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(card), btn);

    return card;
}

static GtkWidget *make_info_row(const char *key, const char *val) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *k = gtk_label_new(key);
    gtk_widget_add_css_class(k, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(k), 0.0f);
    gtk_widget_set_size_request(k, 140, -1);

    GtkWidget *v = gtk_label_new(val);
    gtk_label_set_xalign(GTK_LABEL(v), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);

    gtk_box_append(GTK_BOX(row), k);
    gtk_box_append(GTK_BOX(row), v);
    return row;
}

static char *read_os_pretty_name(void) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return g_strdup("Linux");
    char line[256];
    char *res = NULL;
    while (fgets(line, sizeof(line), f)) {
        if (g_str_has_prefix(line, "PRETTY_NAME=")) {
            char *v = line + strlen("PRETTY_NAME=");
            g_strstrip(v);
            size_t n = strlen(v);
            if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
                v[n - 1] = '\0';
                v++;
            }
            res = g_strdup(v);
            break;
        }
    }
    fclose(f);
    return res ? res : g_strdup("Linux");
}

static GtkWidget *build_appearance_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);

    gtk_box_append(GTK_BOX(box), make_section_title("Внешний вид"));

    /* Карточка: Стиль и акцент */
    GtkWidget *style_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(style_card, "ps-card");

    GtkWidget *m_lbl = gtk_label_new("Тема интерфейса");
    gtk_widget_add_css_class(m_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(m_lbl), 0.0f);
    gtk_box_append(GTK_BOX(style_card), m_lbl);

    GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->btn_dark = gtk_button_new_with_label("Тёмная");
    st->btn_light = gtk_button_new_with_label("Светлая");
    g_signal_connect(st->btn_dark, "clicked", G_CALLBACK(on_mode_dark), st);
    g_signal_connect(st->btn_light, "clicked", G_CALLBACK(on_mode_light), st);
    gtk_box_append(GTK_BOX(mode_row), st->btn_dark);
    gtk_box_append(GTK_BOX(mode_row), st->btn_light);
    gtk_box_append(GTK_BOX(style_card), mode_row);

    GtkWidget *a_lbl = gtk_label_new("Акцентный цвет");
    gtk_widget_add_css_class(a_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(a_lbl), 0.0f);
    gtk_box_append(GTK_BOX(style_card), a_lbl);

    GtkWidget *acc_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (int i = 0; i < N_ACCENTS; i++) {
        GtkWidget *sw = gtk_button_new();
        gtk_widget_add_css_class(sw, "ps-swatch");
        char cls[32];
        g_snprintf(cls, sizeof(cls), "ps-swatch-%d", i);
        gtk_widget_add_css_class(sw, cls);
        g_object_set_data(G_OBJECT(sw), "accent-hex", (gpointer)ACCENT_PRESETS[i]);
        g_signal_connect(sw, "clicked", G_CALLBACK(on_swatch_clicked), st);
        st->swatch_btns[i] = sw;
        gtk_box_append(GTK_BOX(acc_row), sw);
    }

    GtkWidget *custom_c = gtk_button_new_with_label("Свой цвет…");
    gtk_widget_add_css_class(custom_c, "ps-btn");
    g_signal_connect(custom_c, "clicked", G_CALLBACK(on_choose_custom_color), st);
    gtk_box_append(GTK_BOX(acc_row), custom_c);

    st->accent_hex_lbl = gtk_label_new(st->accent);
    gtk_widget_add_css_class(st->accent_hex_lbl, "ps-muted");
    gtk_box_append(GTK_BOX(acc_row), st->accent_hex_lbl);
    gtk_box_append(GTK_BOX(style_card), acc_row);

    gtk_box_append(GTK_BOX(box), style_card);

    /* Карточка: Персональные темы */
    GtkWidget *prof_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(prof_card, "ps-card");

    GtkWidget *p_lbl = gtk_label_new("Персональные темы");
    gtk_widget_add_css_class(p_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(p_lbl), 0.0f);
    gtk_box_append(GTK_BOX(prof_card), p_lbl);

    st->profiles_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(st->profiles_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(st->profiles_flow), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(st->profiles_flow), 6);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(st->profiles_flow), 6);
    gtk_box_append(GTK_BOX(prof_card), st->profiles_flow);

    GtkWidget *save_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->profile_entry = gtk_entry_new();
    gtk_widget_add_css_class(st->profile_entry, "ps-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->profile_entry), "Название своей темы…");
    gtk_widget_set_hexpand(st->profile_entry, TRUE);
    GtkWidget *save_btn = gtk_button_new_with_label("Сохранить текущую");
    gtk_widget_add_css_class(save_btn, "ps-btn");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_profile), st);
    gtk_box_append(GTK_BOX(save_row), st->profile_entry);
    gtk_box_append(GTK_BOX(save_row), save_btn);
    gtk_box_append(GTK_BOX(prof_card), save_row);
    gtk_box_append(GTK_BOX(box), prof_card);

    refresh_profiles_ui(st);

    /* Карточка: Обои */
    GtkWidget *wall_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(wall_card, "ps-card");

    GtkWidget *w_lbl = gtk_label_new("Обои рабочего стола");
    gtk_widget_add_css_class(w_lbl, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(w_lbl), 0.0f);
    gtk_box_append(GTK_BOX(wall_card), w_lbl);

    GtkWidget *frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "ps-preview-wrap");
    st->wall_pic = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(st->wall_pic), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_size_request(st->wall_pic, 440, 185);
    gtk_box_append(GTK_BOX(frame), st->wall_pic);
    gtk_box_append(GTK_BOX(wall_card), frame);

    st->wall_path_lbl = gtk_label_new("");
    gtk_widget_add_css_class(st->wall_path_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->wall_path_lbl), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(st->wall_path_lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(wall_card), st->wall_path_lbl);

    char *cur = read_current_wallpaper();
    if (g_file_test(cur, G_FILE_TEST_IS_REGULAR)) {
        g_strlcpy(st->cur_wall, cur, sizeof(st->cur_wall));
        gtk_picture_set_filename(GTK_PICTURE(st->wall_pic), cur);
        gtk_label_set_text(GTK_LABEL(st->wall_path_lbl), cur);
    }
    g_free(cur);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *pick = gtk_button_new_with_label("Выбрать файл…");
    gtk_widget_add_css_class(pick, "ps-btn-primary");
    g_signal_connect(pick, "clicked", G_CALLBACK(on_choose_wall), st);

    GtkWidget *reset = gtk_button_new_with_label("Сбросить на стандартные");
    gtk_widget_add_css_class(reset, "ps-btn");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_wall), st);

    gtk_box_append(GTK_BOX(btns), pick);
    gtk_box_append(GTK_BOX(btns), reset);
    gtk_box_append(GTK_BOX(wall_card), btns);

    gtk_box_append(GTK_BOX(box), wall_card);
    return scroll;
}

static GtkWidget *build_keyboard_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Клавиатура и раскладки"));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *l1 = gtk_label_new("Активные раскладки");
    gtk_widget_add_css_class(l1, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
    gtk_box_append(GTK_BOX(card), l1);

    st->kbd_list = gtk_list_box_new();
    gtk_widget_add_css_class(st->kbd_list, "ps-kbd-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->kbd_list), GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(card), st->kbd_list);

    GtkWidget *add_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->dd_add_layout = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_hexpand(st->dd_add_layout, TRUE);
    st->btn_add_layout = gtk_button_new_with_label("+ Добавить раскладку");
    gtk_widget_add_css_class(st->btn_add_layout, "ps-btn-primary");
    g_signal_connect(st->btn_add_layout, "clicked", G_CALLBACK(on_add_layout), st);
    gtk_box_append(GTK_BOX(add_row), st->dd_add_layout);
    gtk_box_append(GTK_BOX(add_row), st->btn_add_layout);
    gtk_box_append(GTK_BOX(card), add_row);

    gtk_box_append(GTK_BOX(box), card);

    GtkWidget *tcard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(tcard, "ps-card");
    GtkWidget *l2 = gtk_label_new("Комбинация переключения");
    gtk_widget_add_css_class(l2, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
    st->dd_toggle = gtk_drop_down_new_from_strings(TOGGLE_LABELS);
    gtk_box_append(GTK_BOX(tcard), l2);
    gtk_box_append(GTK_BOX(tcard), st->dd_toggle);
    gtk_box_append(GTK_BOX(box), tcard);

    st->kbd_status_lbl = gtk_label_new("Подхватывает системные раскладки; изменения сохраняются в ~/.config/plume/keyboard.env");
    gtk_widget_add_css_class(st->kbd_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->kbd_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), st->kbd_status_lbl);

    load_keyboard_prefs(st);
    g_signal_connect(st->dd_toggle, "notify::selected", G_CALLBACK(on_toggle_changed), st);

    return box;
}

static GtkWidget *build_system_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Система и устройства"));

    gtk_box_append(GTK_BOX(box), make_tool_row(
        "󰕾", "Звук", "Громкость, устройства вывода и микрофон (pavucontrol)", "pavucontrol"
    ));
    gtk_box_append(GTK_BOX(box), make_tool_row(
        "󰈀", "Сеть и Wi-Fi", "Проводные и беспроводные соединения (NetworkManager)", "nm-connection-editor"
    ));
    gtk_box_append(GTK_BOX(box), make_tool_row(
        "󰂯", "Bluetooth", "Поиск, сопряжение и устройства Bluetooth (Blueman)", "blueman-manager"
    ));
    return box;
}

static GtkWidget *build_about_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(hero, "ps-card");

    char *root = get_plume_root();
    char *logo = g_build_filename(root, "assets", "plume-logo-clean.svg", NULL);
    g_free(root);
    GtkWidget *img = g_file_test(logo, G_FILE_TEST_IS_REGULAR)
        ? gtk_image_new_from_file(logo)
        : gtk_image_new_from_icon_name("preferences-system");
    gtk_image_set_pixel_size(GTK_IMAGE(img), 56);
    g_free(logo);
    gtk_box_append(GTK_BOX(hero), img);

    GtkWidget *htxt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(htxt, GTK_ALIGN_CENTER);
    GtkWidget *name = gtk_label_new("Plume");
    gtk_widget_add_css_class(name, "ps-hero-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    GtkWidget *ver = gtk_label_new("Версия 0.1.0-beta1 · Wayland Desktop Environment");
    gtk_widget_add_css_class(ver, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(ver), 0.0f);
    gtk_box_append(GTK_BOX(htxt), name);
    gtk_box_append(GTK_BOX(htxt), ver);
    gtk_box_append(GTK_BOX(hero), htxt);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    char *os_name = read_os_pretty_name();
    struct utsname u;
    const char *kernel = (uname(&u) == 0) ? u.release : "неизвестно";

    gtk_box_append(GTK_BOX(card), make_info_row("Система", os_name));
    gtk_box_append(GTK_BOX(card), make_info_row("Ядро", kernel));
    gtk_box_append(GTK_BOX(card), make_info_row("Композитор", "Wayfire (wlroots · Wayland)"));
    gtk_box_append(GTK_BOX(card), make_info_row("Компоненты", "Waybar · Mako · GTK4"));
    gtk_box_append(GTK_BOX(card), make_info_row("Репозиторий", "github.com/etern4llyfunny/plume"));
    g_free(os_name);

    gtk_box_append(GTK_BOX(box), card);
    return box;
}

static void add_nav_item(GtkWidget *list, const char *icon_txt, const char *label_txt, const char *page_id) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *ico = gtk_label_new(icon_txt);
    gtk_widget_add_css_class(ico, "ps-nav-icon");
    GtkWidget *lbl = gtk_label_new(label_txt);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(hbox), ico);
    gtk_box_append(GTK_BOX(hbox), lbl);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    g_object_set_data(G_OBJECT(row), "page-id", (gpointer)page_id);
    gtk_list_box_append(GTK_LIST_BOX(list), row);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    State *st = user_data;
    if (st->win) {
        gtk_window_present(GTK_WINDOW(st->win));
        return;
    }

    st->layouts = g_ptr_array_new_with_free_func(g_free);
    st->addable_codes = g_ptr_array_new_with_free_func(g_free);
    load_theme_prefs(st);

    st->css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(st->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER
    );

    st->win = gtk_application_window_new(app);
    gtk_widget_add_css_class(st->win, "plume-settings-win");
    gtk_window_set_title(GTK_WINDOW(st->win), "Настройки Plume");
    gtk_window_set_default_size(GTK_WINDOW(st->win), 780, 560);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(st->win), root_box);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(sidebar, "ps-sidebar");
    gtk_widget_set_size_request(sidebar, 195, -1);
    gtk_box_append(GTK_BOX(root_box), sidebar);

    GtkWidget *nav = gtk_list_box_new();
    gtk_widget_add_css_class(nav, "ps-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav), GTK_SELECTION_SINGLE);
    add_nav_item(nav, "󰏘", "Внешний вид", "appearance");
    add_nav_item(nav, "󰌌", "Клавиатура", "keyboard");
    add_nav_item(nav, "󰒓", "Система", "system");
    add_nav_item(nav, "󰋼", "О Plume", "about");
    gtk_box_append(GTK_BOX(sidebar), nav);

    st->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(st->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(st->stack), 140);
    gtk_widget_set_hexpand(st->stack, TRUE);
    gtk_widget_set_vexpand(st->stack, TRUE);

    gtk_stack_add_named(GTK_STACK(st->stack), build_appearance_page(st), "appearance");
    gtk_stack_add_named(GTK_STACK(st->stack), build_keyboard_page(st), "keyboard");
    gtk_stack_add_named(GTK_STACK(st->stack), build_system_page(), "system");
    gtk_stack_add_named(GTK_STACK(st->stack), build_about_page(), "about");
    gtk_box_append(GTK_BOX(root_box), st->stack);

    apply_window_css(st);

    g_signal_connect(nav, "row-selected", G_CALLBACK(on_nav_selected), st);
    gtk_list_box_select_row(GTK_LIST_BOX(nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(nav), 0));

    gtk_window_present(GTK_WINDOW(st->win));
}

int main(int argc, char **argv) {
    State st = { 0 };
    GtkApplication *app = gtk_application_new("org.ouvert.plume.settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (st.layouts) g_ptr_array_unref(st.layouts);
    if (st.addable_codes) g_ptr_array_unref(st.addable_codes);
    if (st.css_provider) g_object_unref(st.css_provider);
    g_object_unref(app);
    return status;
}
