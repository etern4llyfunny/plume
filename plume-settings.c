#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

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

    /* Сеть */
    GtkWidget *wifi_switch;
    GtkWidget *wifi_status_lbl;
    GtkWidget *wifi_list;
    GtkWidget *network_list;
    gboolean network_loading;

    /* Bluetooth */
    GtkWidget *bluetooth_switch;
    GtkWidget *bluetooth_status_lbl;
    GtkWidget *bluetooth_list;
    gboolean bluetooth_loading;

    /* Дисплеи */
    GtkWidget *display_list;

    /* Звук */
    GtkWidget *sound_output_scale;
    GtkWidget *sound_input_scale;
    GtkWidget *sound_output_enabled;
    GtkWidget *sound_input_enabled;
    GtkWidget *sound_output_label;
    GtkWidget *sound_input_label;
    gboolean sound_loading;

    /* Питание */
    GtkWidget *power_status_label;
    GtkWidget *power_profile_label;
    GtkWidget *power_profile_buttons[3];

    /* Многозадачность */
    GtkWidget *workspace_buttons[5];
    GtkWidget *workspace_status_label;

    /* Приложения */
    GtkWidget *default_app_labels[4];
    GtkWidget *autostart_status_label;
    GtkWidget *autostart_list;

    /* Уведомления */
    GtkWidget *notifications_dnd_switch;
    GtkWidget *notifications_status_label;
    gboolean notifications_loading;

    /* Поиск */
    GtkWidget *search_descriptions_switch;
    GtkWidget *search_commands_switch;
    gboolean search_loading;

    /* Благополучие */
    GtkWidget *wellbeing_switch;
    GtkWidget *wellbeing_interval;
    GtkWidget *wellbeing_status_label;
    gboolean wellbeing_loading;

    /* Мышь и сенсорная панель */
    GtkWidget *mouse_speed_scale;
    GtkWidget *mouse_scroll_scale;
    GtkWidget *touchpad_speed_scale;
    GtkWidget *touchpad_scroll_scale;
    GtkWidget *natural_scroll_switch;
    GtkWidget *tap_to_click_switch;
    GtkWidget *disable_while_typing_switch;
    GtkWidget *mouse_status_label;
    gboolean mouse_loading;

    /* Управление цветом */
    GtkWidget *color_warm_switch;
    GtkWidget *color_temperature_scale;
    GtkWidget *color_status_label;
    gboolean color_loading;

    /* Принтеры */
    GtkWidget *printers_list;
    GtkWidget *printers_status_label;
    GtkWidget *printers_enable_button;
    GtkWidget *printers_add_button;

    /* Специальные возможности */
    GtkWidget *access_large_text_switch;
    GtkWidget *access_reduce_motion_switch;
    GtkWidget *access_cursor_size_scale;
    GtkWidget *access_status_label;
    gboolean access_loading;

    /* Конфиденциальность и безопасность */
    GtkWidget *privacy_lock_switch;
    GtkWidget *privacy_lock_delay;
    GtkWidget *privacy_microphone_switch;
    GtkWidget *privacy_status_label;
    gboolean privacy_loading;

    /* Система */
    GtkWidget *system_updates_label;
    GtkWidget *system_updates_list;
    GtkWidget *system_updates_button;
    GtkWidget *system_install_button;
    GtkWidget *system_cancel_button;
    GtkWidget *system_install_revealer;
    GtkWidget *system_results_revealer;
    GSubprocess *system_process;
    gboolean system_updates_running;
    gboolean system_updates_cancelled;
    gboolean system_installing;
} State;

static char *get_plume_root(void) {
    const char *env = getenv("PLUME_ROOT");
    if (env && *env) return g_strdup(env);
    if (g_file_test("/usr/share/plume/assets", G_FILE_TEST_IS_DIR))
        return g_strdup("/usr/share/plume");
    return g_build_filename(g_get_home_dir(), "plume", NULL);
}

static char *get_default_wallpaper_for_mode(const char *mode) {
    char *root = get_plume_root();
    const char *name = (mode && strcmp(mode, "light") == 0)
        ? "wallpaper-light.png" : "wallpaper.png";
    char *p = g_build_filename(root, "assets", name, NULL);
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
        "window.plume-settings-win, window.plume-settings-win headerbar {"
        "  border-radius: 0;"
        "}"
        "window.plume-settings-win headerbar windowcontrols > button {"
        "  background: transparent; border: 1px solid transparent;"
        "  border-radius: 0; box-shadow: none;"
        "}"
        "window.plume-settings-win headerbar windowcontrols > button:hover {"
        "  background: rgba(127,127,127,0.17);"
        "}"

        "headerbar button.ps-window-btn {"
        "  min-width: 32px; min-height: 32px; padding: 0;"
        "  background: transparent; border: 0; border-radius: 0;"
        "  box-shadow: none; color: inherit;"
        "}"
        "headerbar button.ps-window-btn:hover { background: rgba(127,127,127,0.17); }"
        "headerbar button.ps-window-btn.ps-close:hover { background: #e04f3e; color: white; }"
        "box.ps-sidebar {"
        "  background-color: %s; border-right: 1px solid %s; padding: 10px 7px;"
        "}"
        ".ps-sidebar-title { font-size: 16px; font-weight: 700; padding: 4px 9px 7px 9px; }"
        "entry.ps-nav-search {"
        "  min-height: 32px; margin: 0 3px 5px 3px; padding: 2px 9px;"
        "  background: %s; color: %s; border: 1px solid %s; border-radius: 8px;"
        "}"
        "entry.ps-nav-search:focus-within { border-color: %s; }"
        "list.ps-nav { background: transparent; }"
        "list.ps-nav row { min-height: 36px; padding: 4px 10px; margin: 1px 0; border-radius: 7px; color: %s; }"
        "list.ps-nav row:hover { background-color: %s; color: %s; }"
        "list.ps-nav row:selected { background-color: %s; color: #ffffff; }"
        ".ps-nav-icon { font-size: 15px; min-width: 22px; }"
        ".ps-placeholder-icon { font-size: 34px; color: %s; }"
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
        bg, fg, border_s, st->accent,
        fg_dim, bg_hover, fg, st->accent, st->accent,
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

    g_string_append_printf(css,
        "window.plume-settings-win switch {"
        "  min-width: 40px; min-height: 20px; padding: 1px;"
        "  background-color: %s; background-image: none;"
        "  border: 1px solid %s; border-radius: 5px; box-shadow: none;"
        "}"
        "window.plume-settings-win switch:checked,"
        "window.plume-settings-win switch:checked:hover,"
        "window.plume-settings-win switch:checked:active {"
        "  background-color: %s; background-image: none; border-color: %s;"
        "}"
        "window.plume-settings-win switch slider {"
        "  min-width: 16px; min-height: 16px; margin: 1px;"
        "  background-color: %s; background-image: none;"
        "  border: 0; border-radius: 3px; box-shadow: none;"
        "}"
        "window.plume-settings-win switch:checked slider {"
        "  background-color: #ffffff; background-image: none;"
        "}"
        "window.plume-settings-win switch:disabled { opacity: 0.45; }"
        ".ps-source-fixed { color: %s; font-size: 12px; font-weight: 600; }",
        bg_hover, border_s, st->accent, st->accent, fg, st->accent
    );

    g_string_append_printf(css,
        "window.plume-settings-win scale trough {"
        "  min-height: 4px; min-width: 4px;"
        "  background: %s; background-image: none;"
        "  border: 0; border-radius: 2px; box-shadow: none;"
        "}"
        "window.plume-settings-win scale highlight {"
        "  min-height: 4px; min-width: 4px;"
        "  background: %s; background-image: none;"
        "  border: 0; border-radius: 2px; box-shadow: none;"
        "}"
        "window.plume-settings-win scale slider {"
        "  min-width: 14px; min-height: 14px; margin: -5px;"
        "  background: #ffffff; background-image: none;"
        "  border: 2px solid %s; border-radius: 9px; box-shadow: none;"
        "}"
        "window.plume-settings-win scale slider:hover {"
        "  background: #ffffff; background-image: none;"
        "  border-color: %s; box-shadow: none;"
        "}",
        border_s, st->accent, st->accent, acc_hover
    );
    g_string_append(css,
        "window.plume-settings-win.ps-large-text { font-size: 16px; }");
    g_string_append(css,
        "button.ps-btn-danger { background: #d83e3e; background-image: none;"
        " color: #ffffff; border: 1px solid #d83e3e; border-radius: 6px;"
        " padding: 6px 12px; box-shadow: none; }"
        "button.ps-btn-danger:hover { background: #eb5050; }"
        "button.ps-btn-danger:disabled { background: #454952; border-color: #454952;"
        " color: #a1a4ab; opacity: 0.65; }"
        "button.ps-btn-primary:disabled { opacity: 0.45; }");

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

static gboolean has_custom_wallpaper(void) {
    char *cfg = get_config_dir();
    char *wf = g_build_filename(cfg, "wallpaper", NULL);
    g_free(cfg);
    gboolean exists = g_file_test(wf, G_FILE_TEST_IS_REGULAR);
    g_free(wf);
    return exists;
}

static char *read_current_wallpaper(State *st) {
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
    return get_default_wallpaper_for_mode(st->mode);
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

static void apply_theme_wallpaper_if_default(State *st) {
    if (has_custom_wallpaper()) return;
    char *wall = get_default_wallpaper_for_mode(st->mode);
    apply_wallpaper(st, wall, FALSE);
    g_free(wall);
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
    char *def = get_default_wallpaper_for_mode(st->mode);
    apply_wallpaper(st, def, FALSE);
    g_free(def);
}

static void on_mode_dark(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    g_strlcpy(st->mode, "dark", sizeof(st->mode));
    save_and_apply_theme(st);
    apply_theme_wallpaper_if_default(st);
}

static void on_mode_light(GtkButton *btn, gpointer user_data) {
    (void)btn;
    State *st = user_data;
    g_strlcpy(st->mode, "light", sizeof(st->mode));
    save_and_apply_theme(st);
    apply_theme_wallpaper_if_default(st);
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
    apply_theme_wallpaper_if_default(st);
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
        char *dark = get_default_wallpaper_for_mode("dark");
        char *light = get_default_wallpaper_for_mode("light");
        gboolean is_custom = strcmp(wall, dark) != 0 && strcmp(wall, light) != 0;
        g_free(dark);
        g_free(light);
        apply_wallpaper(st, wall, is_custom);
    } else {
        apply_theme_wallpaper_if_default(st);
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

static void refresh_wifi(State *st);
static void refresh_network(State *st);
static void refresh_bluetooth(State *st);
static void refresh_displays(State *st);
static void refresh_sound(State *st);
static void refresh_power(State *st);
static void refresh_workspaces(State *st);
static void refresh_applications(State *st);
static void refresh_notifications(State *st);
static void refresh_wellbeing_status(State *st);
static void refresh_printers(State *st);
static GtkWidget *make_search_source_row(const char *title_text,
    const char *description, GtkWidget *control);

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
    if (!page) return;
    gtk_stack_set_visible_child_name(GTK_STACK(st->stack), page);
    if (g_strcmp0(page, "wifi") == 0) refresh_wifi(st);
    else if (g_strcmp0(page, "network") == 0) refresh_network(st);
    else if (g_strcmp0(page, "bluetooth") == 0) refresh_bluetooth(st);
    else if (g_strcmp0(page, "displays") == 0) refresh_displays(st);
    else if (g_strcmp0(page, "sound") == 0) refresh_sound(st);
    else if (g_strcmp0(page, "power") == 0) refresh_power(st);
    else if (g_strcmp0(page, "multitasking") == 0) refresh_workspaces(st);
    else if (g_strcmp0(page, "applications") == 0) refresh_applications(st);
    else if (g_strcmp0(page, "notifications") == 0) refresh_notifications(st);
    else if (g_strcmp0(page, "wellbeing") == 0) refresh_wellbeing_status(st);
    else if (g_strcmp0(page, "printers") == 0) refresh_printers(st);
}

static GtkWidget *make_section_title(const char *text) {
    GtkWidget *lbl = gtk_label_new(text);
    gtk_widget_add_css_class(lbl, "ps-title");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    return lbl;
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

static char *run_command_capture(char *argv[]) {
    char *out = NULL;
    char *err = NULL;
    int status = 0;
    GError *error = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &out, &err, &status, &error);
    if (!ok) {
        g_clear_error(&error);
        g_free(out);
        out = g_strdup("");
    }
    g_free(err);
    return out ? out : g_strdup("");
}

static gboolean run_command_success(char *argv[]) {
    char *out = NULL;
    char *err = NULL;
    int status = 0;
    GError *error = NULL;
    gboolean spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &out, &err, &status, &error);
    gboolean success = spawned && g_spawn_check_wait_status(status, &error);
    g_clear_error(&error);
    g_free(out);
    g_free(err);
    return success;
}

static gboolean run_command_background(char *argv[]) {
    GError *error = NULL;
    gboolean started = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, NULL, &error);
    g_clear_error(&error);
    return started;
}

static void clear_box(GtkWidget *box) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(box)) != NULL)
        gtk_box_remove(GTK_BOX(box), child);
}

static GtkWidget *make_network_row(const char *icon_text, const char *title,
    const char *details, gboolean active) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "ps-card");

    GtkWidget *icon = gtk_label_new(icon_text);
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *name = gtk_label_new(title);
    gtk_widget_add_css_class(name, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    GtkWidget *info = gtk_label_new(details);
    gtk_widget_add_css_class(info, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);
    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(text), info);
    gtk_box_append(GTK_BOX(row), text);

    if (active) {
        GtkWidget *badge = gtk_label_new("Подключено");
        gtk_widget_add_css_class(badge, "ps-card-title");
        gtk_box_append(GTK_BOX(row), badge);
    }
    return row;
}

static void refresh_wifi(State *st) {
    if (!st->wifi_list) return;
    clear_box(st->wifi_list);

    char *radio_argv[] = { "nmcli", "radio", "wifi", NULL };
    char *radio = run_command_capture(radio_argv);
    g_strstrip(radio);
    gboolean enabled = (g_strcmp0(radio, "enabled") == 0);
    st->network_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->wifi_switch), enabled);
    st->network_loading = FALSE;
    gtk_label_set_text(GTK_LABEL(st->wifi_status_lbl),
        enabled ? "Wi-Fi включён" : "Wi-Fi выключен");
    g_free(radio);

    if (!enabled) {
        GtkWidget *empty = gtk_label_new("Включи Wi-Fi, чтобы увидеть доступные сети.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->wifi_list), empty);
        return;
    }

    char *list_argv[] = {
        "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
        "device", "wifi", "list", "--rescan", "yes", NULL
    };
    char *output = run_command_capture(list_argv);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i] && count < 30; i++) {
        if (!lines[i][0]) continue;
        char **fields = g_strsplit(lines[i], ":", 4);
        if (!fields[0] || !fields[1]) {
            g_strfreev(fields);
            continue;
        }
        gboolean active = (g_strcmp0(fields[0], "*") == 0);
        const char *ssid = fields[1][0] ? fields[1] : "Скрытая сеть";
        const char *signal = fields[2] ? fields[2] : "?";
        const char *security = (fields[3] && fields[3][0]) ? fields[3] : "Открытая сеть";
        char *details = g_strdup_printf("Сигнал: %s%% · %s", signal, security);
        gtk_box_append(GTK_BOX(st->wifi_list),
            make_network_row(active ? "󰤨" : "󰤯", ssid, details, active));
        g_free(details);
        g_strfreev(fields);
        count++;
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new("Доступные сети не найдены.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->wifi_list), empty);
    }
    g_strfreev(lines);
    g_free(output);
}

static void refresh_network(State *st) {
    if (!st->network_list) return;
    clear_box(st->network_list);
    char *argv[] = {
        "nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
        "device", "status", NULL
    };
    char *output = run_command_capture(argv);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i]; i++) {
        if (!lines[i][0]) continue;
        char **fields = g_strsplit(lines[i], ":", 4);
        if (!fields[0] || !fields[1]) {
            g_strfreev(fields);
            continue;
        }
        const char *state = fields[2] ? fields[2] : "неизвестно";
        const char *connection = (fields[3] && fields[3][0]) ? fields[3] : "Нет подключения";
        gboolean active = g_str_has_prefix(state, "connected") ||
            g_str_has_prefix(state, "подключ");
        const char *icon = "󰒍";
        if (g_strcmp0(fields[1], "ethernet") == 0) icon = "󰈀";
        else if (g_strcmp0(fields[1], "wifi") == 0) icon = "󰤨";
        else if (g_strcmp0(fields[1], "bridge") == 0) icon = "󰘘";
        else if (g_strcmp0(fields[1], "wireguard") == 0 ||
                 g_strcmp0(fields[1], "tun") == 0) icon = "󰖂";
        char *details = g_strdup_printf("%s · %s · %s",
            fields[1], state, connection);
        gtk_box_append(GTK_BOX(st->network_list),
            make_network_row(icon, fields[0], details, active));
        g_free(details);
        g_strfreev(fields);
        count++;
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new("NetworkManager не вернул список устройств.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->network_list), empty);
    }
    g_strfreev(lines);
    g_free(output);
}

static gboolean on_wifi_state_set(GtkSwitch *widget, gboolean state,
    gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->network_loading) return FALSE;
    char *argv[] = { "nmcli", "radio", "wifi", state ? "on" : "off", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_wifi(st);
    return FALSE;
}

static void on_wifi_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_wifi(user_data);
}

static void on_network_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_network(user_data);
}

static void refresh_bluetooth(State *st) {
    if (!st->bluetooth_list) return;
    clear_box(st->bluetooth_list);

    char *show_argv[] = { "bluetoothctl", "show", NULL };
    char *show = run_command_capture(show_argv);
    gboolean available = strstr(show, "Controller ") != NULL;
    gboolean powered = strstr(show, "Powered: yes") != NULL;

    st->bluetooth_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->bluetooth_switch), powered);
    st->bluetooth_loading = FALSE;
    gtk_widget_set_sensitive(st->bluetooth_switch, available);
    gtk_label_set_text(GTK_LABEL(st->bluetooth_status_lbl),
        !available ? "Bluetooth-адаптер не найден" :
        powered ? "Bluetooth включён" : "Bluetooth выключен");
    g_free(show);

    if (!available || !powered) {
        GtkWidget *empty = gtk_label_new(!available
            ? "Подключи Bluetooth-адаптер, чтобы увидеть устройства."
            : "Включи Bluetooth, чтобы увидеть устройства.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->bluetooth_list), empty);
        return;
    }

    char *devices_argv[] = { "bluetoothctl", "devices", NULL };
    char *output = run_command_capture(devices_argv);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i] && count < 30; i++) {
        if (!g_str_has_prefix(lines[i], "Device ")) continue;
        char *record = g_strdup(lines[i] + 7);
        char *space = strchr(record, ' ');
        if (!space) {
            g_free(record);
            continue;
        }
        *space = '\0';
        const char *mac = record;
        const char *name = space + 1;

        char *info_argv[] = { "bluetoothctl", "info", (char *)mac, NULL };
        char *info = run_command_capture(info_argv);
        gboolean connected = strstr(info, "Connected: yes") != NULL;
        gboolean paired = strstr(info, "Paired: yes") != NULL;
        char *details = g_strdup_printf("%s%s", mac,
            paired ? " · Сопряжено" : "");
        gtk_box_append(GTK_BOX(st->bluetooth_list),
            make_network_row("󰂯", name, details, connected));
        g_free(details);
        g_free(info);
        g_free(record);
        count++;
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new("Устройства пока не найдены. Запусти поиск.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->bluetooth_list), empty);
    }
    g_strfreev(lines);
    g_free(output);
}

static gboolean on_bluetooth_state_set(GtkSwitch *widget, gboolean state,
    gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->bluetooth_loading) return FALSE;
    char *argv[] = { "bluetoothctl", "power", state ? "on" : "off", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_bluetooth(st);
    return FALSE;
}

static void on_bluetooth_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_bluetooth(user_data);
}

static gboolean on_bluetooth_scan_finished(gpointer user_data) {
    refresh_bluetooth(user_data);
    return G_SOURCE_REMOVE;
}

static void on_bluetooth_scan(GtkButton *button, gpointer user_data) {
    (void)button;
    char *argv[] = { "bluetoothctl", "--timeout", "6", "scan", "on", NULL };
    GError *error = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, NULL, &error);
    if (error) g_error_free(error);
    g_timeout_add_seconds(7, on_bluetooth_scan_finished, user_data);
}

static void refresh_displays(State *st);

static void on_display_scale(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *output = g_object_get_data(G_OBJECT(button), "output-name");
    const char *scale = g_object_get_data(G_OBJECT(button), "output-scale");
    if (!output || !scale) return;
    char *argv[] = {
        "wlr-randr", "--output", (char *)output, "--scale", (char *)scale, NULL
    };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_displays(st);
}

static GtkWidget *make_display_row(State *st, const char *output,
    const char *details) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon = gtk_label_new("󰍹");
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *name = gtk_label_new(output);
    gtk_widget_add_css_class(name, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    GtkWidget *info = gtk_label_new(details);
    gtk_widget_add_css_class(info, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(info), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);
    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(text), info);
    gtk_box_append(GTK_BOX(head), icon);
    gtk_box_append(GTK_BOX(head), text);
    gtk_box_append(GTK_BOX(card), head);

    GtkWidget *scales = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *label = gtk_label_new("Масштаб:");
    gtk_widget_add_css_class(label, "ps-muted");
    gtk_box_append(GTK_BOX(scales), label);
    const char *scale_values[] = { "1", "1.25", "1.5", "2", NULL };
    const char *scale_labels[] = { "100%", "125%", "150%", "200%", NULL };
    for (guint i = 0; scale_values[i]; i++) {
        GtkWidget *button = gtk_button_new_with_label(scale_labels[i]);
        gtk_widget_add_css_class(button, "ps-btn");
        g_object_set_data_full(G_OBJECT(button), "output-name",
            g_strdup(output), g_free);
        g_object_set_data(G_OBJECT(button), "output-scale",
            (gpointer)scale_values[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(on_display_scale), st);
        gtk_box_append(GTK_BOX(scales), button);
    }
    gtk_box_append(GTK_BOX(card), scales);
    return card;
}

static void append_display_detail(GString *details, const char *value) {
    if (details->len) g_string_append(details, " · ");
    g_string_append(details, value);
}

static void refresh_displays(State *st) {
    if (!st->display_list) return;
    clear_box(st->display_list);
    char *argv[] = { "wlr-randr", NULL };
    char *output = run_command_capture(argv);
    char **lines = g_strsplit(output, "\n", -1);
    char *current_output = NULL;
    GString *details = g_string_new(NULL);
    guint count = 0;

    for (guint i = 0; lines[i]; i++) {
        if (lines[i][0] && !g_ascii_isspace(lines[i][0])) {
            if (current_output) {
                gtk_box_append(GTK_BOX(st->display_list),
                    make_display_row(st, current_output,
                        details->len ? details->str : "Параметры не получены"));
                count++;
                g_free(current_output);
                g_string_truncate(details, 0);
            }
            char *header = g_strdup(lines[i]);
            char *space = strchr(header, ' ');
            if (space) *space = '\0';
            current_output = header;
            continue;
        }
        if (!current_output) continue;
        char *value = g_strdup(lines[i]);
        g_strstrip(value);
        if (g_str_has_prefix(value, "Enabled:") ||
            g_str_has_prefix(value, "Position:") ||
            g_str_has_prefix(value, "Scale:") ||
            g_str_has_prefix(value, "Transform:") ||
            strstr(value, "(current)")) {
            append_display_detail(details, value);
        }
        g_free(value);
    }
    if (current_output) {
        gtk_box_append(GTK_BOX(st->display_list),
            make_display_row(st, current_output,
                details->len ? details->str : "Параметры не получены"));
        count++;
        g_free(current_output);
    }
    if (!count) {
        GtkWidget *empty = gtk_label_new(
            "Не удалось получить список дисплеев. Проверь наличие wlr-randr.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->display_list), empty);
    }
    g_string_free(details, TRUE);
    g_strfreev(lines);
    g_free(output);
}

static void on_display_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_displays(user_data);
}

static char *get_wpctl_description(const char *target) {
    char *argv[] = { "wpctl", "inspect", (char *)target, NULL };
    char *output = run_command_capture(argv);
    char *result = NULL;
    char **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        char *key = strstr(lines[i], "node.description =");
        if (!key) continue;
        char *value = strchr(key, '=');
        if (!value) continue;
        value++;
        g_strstrip(value);
        if (value[0] == '"') value++;
        size_t len = strlen(value);
        if (len && value[len - 1] == '"') value[len - 1] = '\0';
        result = g_strdup(value);
        break;
    }
    g_strfreev(lines);
    g_free(output);
    return result;
}

static gboolean read_wpctl_volume(const char *target, double *volume,
    gboolean *muted) {
    char *argv[] = { "wpctl", "get-volume", (char *)target, NULL };
    char *output = run_command_capture(argv);
    char *start = strstr(output, "Volume:");
    char *end = NULL;
    double value = 0.0;
    if (start) {
        start += strlen("Volume:");
        while (g_ascii_isspace(*start)) start++;
        value = g_ascii_strtod(start, &end);
    }
    gboolean ok = start && end != start;
    if (ok) {
        *volume = value;
        *muted = strstr(output, "[MUTED]") != NULL;
    }
    g_free(output);
    return ok;
}

static void refresh_sound(State *st) {
    double output_volume = 0.0, input_volume = 0.0;
    gboolean output_muted = FALSE, input_muted = FALSE;
    gboolean have_output = read_wpctl_volume(
        "@DEFAULT_AUDIO_SINK@", &output_volume, &output_muted);
    gboolean have_input = read_wpctl_volume(
        "@DEFAULT_AUDIO_SOURCE@", &input_volume, &input_muted);
    char *output_name = get_wpctl_description("@DEFAULT_AUDIO_SINK@");
    char *input_name = get_wpctl_description("@DEFAULT_AUDIO_SOURCE@");

    st->sound_loading = TRUE;
    gtk_widget_set_sensitive(st->sound_output_scale, have_output);
    gtk_widget_set_sensitive(st->sound_output_enabled, have_output);
    gtk_widget_set_sensitive(st->sound_input_scale, have_input);
    gtk_widget_set_sensitive(st->sound_input_enabled, have_input);
    if (have_output) {
        gtk_range_set_value(GTK_RANGE(st->sound_output_scale),
            CLAMP(output_volume * 100.0, 0.0, 150.0));
        gtk_switch_set_active(GTK_SWITCH(st->sound_output_enabled), !output_muted);
    }
    if (have_input) {
        gtk_range_set_value(GTK_RANGE(st->sound_input_scale),
            CLAMP(input_volume * 100.0, 0.0, 150.0));
        gtk_switch_set_active(GTK_SWITCH(st->sound_input_enabled), !input_muted);
    }
    st->sound_loading = FALSE;

    char *output_text = have_output
        ? g_strdup_printf("%s · %.0f%%", output_name ? output_name : "Выход",
            output_volume * 100.0)
        : g_strdup("Устройство вывода не найдено");
    char *input_text = have_input
        ? g_strdup_printf("%s · %.0f%%", input_name ? input_name : "Микрофон",
            input_volume * 100.0)
        : g_strdup("Микрофон не найден");
    gtk_label_set_text(GTK_LABEL(st->sound_output_label), output_text);
    gtk_label_set_text(GTK_LABEL(st->sound_input_label), input_text);
    g_free(output_text);
    g_free(input_text);
    g_free(output_name);
    g_free(input_name);
}

static void on_sound_volume_changed(GtkRange *range, gpointer user_data) {
    State *st = user_data;
    if (st->sound_loading) return;
    const char *target = g_object_get_data(G_OBJECT(range), "audio-target");
    if (!target) return;
    char value[24];
    g_snprintf(value, sizeof(value), "%.0f%%", gtk_range_get_value(range));
    char *argv[] = { "wpctl", "set-volume", (char *)target, value, NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);

    GtkWidget *label = g_strcmp0(target, "@DEFAULT_AUDIO_SINK@") == 0
        ? st->sound_output_label : st->sound_input_label;
    const char *old_text = gtk_label_get_text(GTK_LABEL(label));
    const char *separator = g_strrstr(old_text, " · ");
    char *new_text = separator
        ? g_strdup_printf("%.*s · %s", (int)(separator - old_text), old_text, value)
        : g_strdup_printf("%s · %s", old_text, value);
    gtk_label_set_text(GTK_LABEL(label), new_text);
    g_free(new_text);
}

static gboolean on_sound_enabled_changed(GtkSwitch *widget, gboolean state,
    gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->sound_loading) return FALSE;
    const char *target = g_object_get_data(G_OBJECT(widget), "audio-target");
    if (!target) return FALSE;
    char *argv[] = {
        "wpctl", "set-mute", (char *)target, state ? "0" : "1", NULL
    };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    return FALSE;
}

static void on_sound_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_sound(user_data);
}

static char *read_battery_status(void) {
    GDir *dir = g_dir_open("/sys/class/power_supply", 0, NULL);
    if (!dir) return g_strdup("Данные батареи недоступны");
    const char *entry;
    char *result = NULL;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        char *base = g_build_filename("/sys/class/power_supply", entry, NULL);
        char *type_path = g_build_filename(base, "type", NULL);
        char *type = NULL;
        g_file_get_contents(type_path, &type, NULL, NULL);
        g_free(type_path);
        if (!type || !g_str_has_prefix(type, "Battery")) {
            g_free(type);
            g_free(base);
            continue;
        }
        g_free(type);
        char *capacity_path = g_build_filename(base, "capacity", NULL);
        char *status_path = g_build_filename(base, "status", NULL);
        char *capacity = NULL, *status = NULL;
        g_file_get_contents(capacity_path, &capacity, NULL, NULL);
        g_file_get_contents(status_path, &status, NULL, NULL);
        if (capacity) g_strstrip(capacity);
        if (status) g_strstrip(status);
        const char *translated = "Состояние неизвестно";
        if (g_strcmp0(status, "Charging") == 0) translated = "Заряжается";
        else if (g_strcmp0(status, "Discharging") == 0) translated = "Разряжается";
        else if (g_strcmp0(status, "Full") == 0) translated = "Полностью заряжена";
        else if (g_strcmp0(status, "Not charging") == 0) translated = "Не заряжается";
        result = g_strdup_printf("%s%% · %s", capacity ? capacity : "?", translated);
        g_free(capacity);
        g_free(status);
        g_free(capacity_path);
        g_free(status_path);
        g_free(base);
        break;
    }
    g_dir_close(dir);
    return result ? result : g_strdup("Батарея не обнаружена");
}

static void refresh_power(State *st) {
    static const char *profiles[] = { "power-saver", "balanced", "performance" };
    char *get_argv[] = { "powerprofilesctl", "get", NULL };
    char *current = run_command_capture(get_argv);
    g_strstrip(current);
    char *list_argv[] = { "powerprofilesctl", "list", NULL };
    char *available = run_command_capture(list_argv);

    const char *current_label = "Служба профилей питания недоступна";
    if (g_strcmp0(current, "power-saver") == 0) current_label = "Экономия энергии";
    else if (g_strcmp0(current, "balanced") == 0) current_label = "Сбалансированный";
    else if (g_strcmp0(current, "performance") == 0) current_label = "Производительность";
    gtk_label_set_text(GTK_LABEL(st->power_profile_label), current_label);

    for (guint i = 0; i < 3; i++) {
        gtk_widget_remove_css_class(st->power_profile_buttons[i], "ps-btn-primary");
        gtk_widget_add_css_class(st->power_profile_buttons[i], "ps-btn");
        gtk_widget_set_sensitive(st->power_profile_buttons[i],
            strstr(available, profiles[i]) != NULL);
        if (g_strcmp0(current, profiles[i]) == 0) {
            gtk_widget_remove_css_class(st->power_profile_buttons[i], "ps-btn");
            gtk_widget_add_css_class(st->power_profile_buttons[i], "ps-btn-primary");
        }
    }
    char *battery = read_battery_status();
    gtk_label_set_text(GTK_LABEL(st->power_status_label), battery);
    g_free(battery);
    g_free(current);
    g_free(available);
}

static void on_power_profile(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *profile = g_object_get_data(G_OBJECT(button), "power-profile");
    if (!profile) return;
    char *argv[] = { "powerprofilesctl", "set", (char *)profile, NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_power(st);
}

static void on_power_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_power(user_data);
}

static void refresh_workspaces(State *st) {
    guint active = 0;
    for (guint i = 0; i < 5; i++) {
        char index[8];
        g_snprintf(index, sizeof(index), "%u", i + 1);
        char *argv[] = { "plume-ws", "get", index, NULL };
        char *output = run_command_capture(argv);
        gboolean is_active = strstr(output, "\"active\"") != NULL;
        gtk_widget_remove_css_class(st->workspace_buttons[i], "ps-btn");
        gtk_widget_remove_css_class(st->workspace_buttons[i], "ps-btn-primary");
        gtk_widget_add_css_class(st->workspace_buttons[i],
            is_active ? "ps-btn-primary" : "ps-btn");
        if (is_active) active = i + 1;
        g_free(output);
    }
    char *status = active
        ? g_strdup_printf("Сейчас открыт рабочий стол %u", active)
        : g_strdup("Не удалось определить активный рабочий стол");
    gtk_label_set_text(GTK_LABEL(st->workspace_status_label), status);
    g_free(status);
}

static void on_workspace_selected(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    guint index = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "workspace-index"));
    if (index < 1 || index > 5) return;
    char value[8];
    g_snprintf(value, sizeof(value), "%u", index);
    char *argv[] = { "plume-ws", "set", value, NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_workspaces(st);
}

static void on_workspace_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_workspaces(user_data);
}

static void on_settings_active_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)pspec;
    State *st = user_data;
    if (!gtk_window_is_active(GTK_WINDOW(object))) return;
    const char *page = gtk_stack_get_visible_child_name(GTK_STACK(st->stack));
    if (g_strcmp0(page, "multitasking") == 0)
        refresh_workspaces(st);
}

static char *get_user_autostart_dir(void) {
    char *dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    g_mkdir_with_parents(dir, 0755);
    return dir;
}

static gboolean load_autostart_info(const char *path, char **name,
    char **command, gboolean *disabled) {
    GKeyFile *key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(key);
        return FALSE;
    }
    *name = g_key_file_get_locale_string(key, "Desktop Entry", "Name", NULL, NULL);
    *command = g_key_file_get_string(key, "Desktop Entry", "Exec", NULL);
    GError *error = NULL;
    gboolean hidden = g_key_file_get_boolean(
        key, "Desktop Entry", "Hidden", &error);
    g_clear_error(&error);
    gboolean enabled = g_key_file_get_boolean(
        key, "Desktop Entry", "X-GNOME-Autostart-enabled", &error);
    if (error) {
        g_clear_error(&error);
        enabled = TRUE;
    }
    *disabled = hidden || !enabled;
    g_key_file_unref(key);
    return TRUE;
}

static gboolean write_autostart_entry(const char *filename, const char *name,
    const char *command, gboolean terminal) {
    char *dir = get_user_autostart_dir();
    char *path = g_build_filename(dir, filename, NULL);
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key, "Desktop Entry", "Name", name);
    g_key_file_set_string(key, "Desktop Entry", "Exec", command);
    g_key_file_set_boolean(key, "Desktop Entry", "Terminal", terminal);
    g_key_file_set_boolean(key, "Desktop Entry", "X-GNOME-Autostart-enabled", TRUE);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    gboolean ok = g_file_set_contents(path, data, length, NULL);
    g_free(data);
    g_key_file_unref(key);
    g_free(path);
    g_free(dir);
    return ok;
}

static char *safe_desktop_filename(const char *prefix, const char *value) {
    char *safe = g_strdup(value && *value ? value : "application");
    for (char *p = safe; *p; p++)
        if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_' && *p != '.')
            *p = '-';
    char *result = g_strdup_printf("%s-%s.desktop", prefix, safe);
    g_free(safe);
    return result;
}

static void refresh_autostart(State *st);

static void write_system_autostart_override(const char *filename) {
    char *dir = get_user_autostart_dir();
    char *path = g_build_filename(dir, filename, NULL);
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_boolean(key, "Desktop Entry", "Hidden", TRUE);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    g_file_set_contents(path, data, length, NULL);
    g_free(data);
    g_key_file_unref(key);
    g_free(path);
    g_free(dir);
}

static void on_autostart_confirm_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        const char *filename = g_object_get_data(G_OBJECT(dialog), "filename");
        const char *action = g_object_get_data(G_OBJECT(dialog), "action");
        if (filename && g_strcmp0(action, "disable-system") == 0) {
            write_system_autostart_override(filename);
        } else if (filename && g_strcmp0(action, "delete-user") == 0) {
            char *dir = get_user_autostart_dir();
            char *path = g_build_filename(dir, filename, NULL);
            g_unlink(path);
            g_free(path);
            g_free(dir);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_autostart(st);
}

static void on_autostart_action(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *filename = g_object_get_data(G_OBJECT(button), "filename");
    const char *action = g_object_get_data(G_OBJECT(button), "action");
    if (!filename || !action) return;

    if (g_strcmp0(action, "enable-system") == 0) {
        char *dir = get_user_autostart_dir();
        char *path = g_build_filename(dir, filename, NULL);
        g_unlink(path);
        g_free(path);
        g_free(dir);
        refresh_autostart(st);
        return;
    }

    const char *message = g_strcmp0(action, "disable-system") == 0
        ? "Отключить системное приложение для этого пользователя? Системный файл не будет удалён, действие можно отменить позже."
        : "Удалить это приложение из пользовательского автозапуска?";
    const char *confirm = g_strcmp0(action, "disable-system") == 0
        ? "Отключить" : "Удалить";
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Подтверждение", GTK_WINDOW(st->win), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, confirm, GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 14);
    gtk_widget_set_margin_end(label, 14);
    gtk_box_append(GTK_BOX(content), label);
    g_object_set_data_full(G_OBJECT(dialog), "filename",
        g_strdup(filename), g_free);
    g_object_set_data_full(G_OBJECT(dialog), "action",
        g_strdup(action), g_free);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_autostart_confirm_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static GtkWidget *make_autostart_row(State *st, const char *filename,
    const char *source_path, gboolean system, gboolean disabled) {
    char *name = NULL, *command = NULL;
    gboolean file_disabled = FALSE;
    load_autostart_info(source_path, &name, &command, &file_disabled);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "ps-card");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(name ? name : filename);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *exec = gtk_label_new(command && *command
        ? command : "Команда не указана");
    gtk_widget_add_css_class(exec, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(exec), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(exec), PANGO_ELLIPSIZE_END);
    GtkWidget *source = gtk_label_new(system
        ? (disabled ? "Системное · отключено" : "Системное")
        : "Пользовательское");
    gtk_widget_add_css_class(source, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(source), 0.0f);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), exec);
    gtk_box_append(GTK_BOX(text), source);

    const char *action = system
        ? (disabled ? "enable-system" : "disable-system") : "delete-user";
    GtkWidget *button = gtk_button_new_with_label(system
        ? (disabled ? "Включить" : "Отключить") : "Удалить");
    gtk_widget_add_css_class(button, "ps-btn");
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(button), "filename",
        g_strdup(filename), g_free);
    g_object_set_data(G_OBJECT(button), "action", (gpointer)action);
    g_signal_connect(button, "clicked", G_CALLBACK(on_autostart_action), st);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(row), button);
    g_free(name);
    g_free(command);
    return row;
}

static void refresh_autostart(State *st) {
    if (!st->autostart_list) return;
    clear_box(st->autostart_list);
    char *user_dir = get_user_autostart_dir();
    guint enabled_count = 0, disabled_count = 0;

    GDir *system_dir = g_dir_open("/etc/xdg/autostart", 0, NULL);
    const char *filename;
    if (system_dir) {
        while ((filename = g_dir_read_name(system_dir)) != NULL) {
            if (!g_str_has_suffix(filename, ".desktop")) continue;
            char *system_path = g_build_filename(
                "/etc/xdg/autostart", filename, NULL);
            char *user_path = g_build_filename(user_dir, filename, NULL);
            if (g_file_test(user_path, G_FILE_TEST_IS_REGULAR)) {
                char *n = NULL, *c = NULL;
                gboolean hidden = FALSE;
                load_autostart_info(user_path, &n, &c, &hidden);
                g_free(n);
                g_free(c);
                if (!hidden) {
                    g_free(system_path);
                    g_free(user_path);
                    continue;
                }
                disabled_count++;
                gtk_box_append(GTK_BOX(st->autostart_list),
                    make_autostart_row(st, filename, system_path, TRUE, TRUE));
            } else {
                gboolean disabled = FALSE;
                char *n = NULL, *c = NULL;
                load_autostart_info(system_path, &n, &c, &disabled);
                g_free(n);
                g_free(c);
                if (disabled) disabled_count++; else enabled_count++;
                gtk_box_append(GTK_BOX(st->autostart_list),
                    make_autostart_row(st, filename, system_path, TRUE, disabled));
            }
            g_free(system_path);
            g_free(user_path);
        }
        g_dir_close(system_dir);
    }

    GDir *user_entries = g_dir_open(user_dir, 0, NULL);
    if (user_entries) {
        while ((filename = g_dir_read_name(user_entries)) != NULL) {
            if (!g_str_has_suffix(filename, ".desktop")) continue;
            char *path = g_build_filename(user_dir, filename, NULL);
            char *system_path = g_build_filename(
                "/etc/xdg/autostart", filename, NULL);
            char *n = NULL, *c = NULL;
            gboolean disabled = FALSE;
            load_autostart_info(path, &n, &c, &disabled);
            g_free(n);
            g_free(c);
            if (!disabled) {
                enabled_count++;
                gtk_box_append(GTK_BOX(st->autostart_list),
                    make_autostart_row(st, filename, path, FALSE, FALSE));
            } else if (!g_file_test(system_path, G_FILE_TEST_IS_REGULAR)) {
                disabled_count++;
            }
            g_free(system_path);
            g_free(path);
        }
        g_dir_close(user_entries);
    }

    if (!enabled_count && !disabled_count) {
        GtkWidget *empty = gtk_label_new("В автозапуске пока нет приложений.");
        gtk_widget_add_css_class(empty, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0f);
        gtk_box_append(GTK_BOX(st->autostart_list), empty);
    }
    char *status = g_strdup_printf("Включено: %u · Отключено: %u",
        enabled_count, disabled_count);
    gtk_label_set_text(GTK_LABEL(st->autostart_status_label), status);
    g_free(status);
    g_free(user_dir);
}

static void refresh_applications(State *st) {
    static const char *content_types[] = {
        "text/html", "inode/directory", "image/png", "application/pdf"
    };
    for (guint i = 0; i < 4; i++) {
        GAppInfo *app = g_app_info_get_default_for_type(content_types[i], FALSE);
        gtk_label_set_text(GTK_LABEL(st->default_app_labels[i]),
            app ? g_app_info_get_display_name(app) : "Не выбрано");
        if (app) g_object_unref(app);
    }

    refresh_autostart(st);
}

static void on_app_chooser_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        const char *content_type = g_object_get_data(
            G_OBJECT(dialog), "content-type");
        GAppInfo *app = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
        if (app && content_type) {
            GError *error = NULL;
            g_app_info_set_as_default_for_type(app, content_type, &error);
            if (error) g_error_free(error);
            g_object_unref(app);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_applications(st);
}

static void on_choose_default_app(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *content_type = g_object_get_data(
        G_OBJECT(button), "content-type");
    if (!content_type) return;
    GtkWidget *dialog = gtk_app_chooser_dialog_new_for_content_type(
        GTK_WINDOW(st->win), GTK_DIALOG_MODAL, content_type);
    gtk_window_set_title(GTK_WINDOW(dialog), "Выбор приложения");
    g_object_set_data_full(G_OBJECT(dialog), "content-type",
        g_strdup(content_type), g_free);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_app_chooser_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_add_installed_app_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        GtkDropDown *dropdown = g_object_get_data(
            G_OBJECT(dialog), "app-dropdown");
        GPtrArray *apps = g_object_get_data(G_OBJECT(dialog), "app-list");
        guint selected = gtk_drop_down_get_selected(dropdown);
        if (apps && selected < apps->len) {
            GAppInfo *app = g_ptr_array_index(apps, selected);
            const char *name = g_app_info_get_display_name(app);
            const char *command = g_app_info_get_commandline(app);
            const char *id = g_app_info_get_id(app);
            if (name && command) {
                char *filename = safe_desktop_filename("plume", id ? id : name);
                write_autostart_entry(filename, name, command, FALSE);
                g_free(filename);
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_autostart(st);
}

static void on_add_installed_app(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Добавить приложение", GTK_WINDOW(st->win), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, "Добавить", GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkStringList *names = gtk_string_list_new(NULL);
    GPtrArray *apps = g_ptr_array_new_with_free_func(g_object_unref);
    GList *all = g_app_info_get_all();
    for (GList *item = all; item; item = item->next) {
        GAppInfo *app = item->data;
        if (!g_app_info_should_show(app) || !g_app_info_get_commandline(app)) {
            g_object_unref(app);
            continue;
        }
        gtk_string_list_append(names, g_app_info_get_display_name(app));
        g_ptr_array_add(apps, app);
    }
    g_list_free(all);
    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(names), NULL);
    gtk_widget_set_hexpand(dropdown, TRUE);
    gtk_widget_set_margin_top(dropdown, 14);
    gtk_widget_set_margin_bottom(dropdown, 14);
    gtk_widget_set_margin_start(dropdown, 14);
    gtk_widget_set_margin_end(dropdown, 14);
    gtk_box_append(GTK_BOX(content), dropdown);
    g_object_set_data(G_OBJECT(dialog), "app-dropdown", dropdown);
    g_object_set_data_full(G_OBJECT(dialog), "app-list", apps,
        (GDestroyNotify)g_ptr_array_unref);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_add_installed_app_response), st);
    g_object_unref(names);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_add_command_response(GtkDialog *dialog, int response,
    gpointer user_data) {
    State *st = user_data;
    if (response == GTK_RESPONSE_OK) {
        GtkEditable *name_entry = g_object_get_data(
            G_OBJECT(dialog), "name-entry");
        GtkEditable *command_entry = g_object_get_data(
            G_OBJECT(dialog), "command-entry");
        GtkCheckButton *terminal = g_object_get_data(
            G_OBJECT(dialog), "terminal-check");
        const char *name = gtk_editable_get_text(name_entry);
        const char *command = gtk_editable_get_text(command_entry);
        if (name && *name && command && *command) {
            char stamp[32];
            g_snprintf(stamp, sizeof(stamp), "command-%" G_GINT64_FORMAT,
                g_get_real_time());
            char *filename = safe_desktop_filename("plume", stamp);
            write_autostart_entry(filename, name, command,
                gtk_check_button_get_active(terminal));
            g_free(filename);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
    refresh_autostart(st);
}

static void on_add_command(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Добавить команду", GTK_WINDOW(st->win), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, "Добавить", GTK_RESPONSE_OK, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(form, 14);
    gtk_widget_set_margin_bottom(form, 14);
    gtk_widget_set_margin_start(form, 14);
    gtk_widget_set_margin_end(form, 14);
    GtkWidget *name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Название");
    GtkWidget *command = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(command), "Команда запуска");
    GtkWidget *terminal = gtk_check_button_new_with_label(
        "Запускать в терминале");
    gtk_box_append(GTK_BOX(form), name);
    gtk_box_append(GTK_BOX(form), command);
    gtk_box_append(GTK_BOX(form), terminal);
    gtk_box_append(GTK_BOX(content), form);
    g_object_set_data(G_OBJECT(dialog), "name-entry", name);
    g_object_set_data(G_OBJECT(dialog), "command-entry", command);
    g_object_set_data(G_OBJECT(dialog), "terminal-check", terminal);
    g_signal_connect(dialog, "response",
        G_CALLBACK(on_add_command_response), st);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void ensure_dnd_mode_config(void) {
    const char *runtime = getenv("PLUME_RUNTIME");
    if (!runtime || !*runtime) return;
    char *path = g_build_filename(runtime, "mako", "config", NULL);
    char *content = NULL;
    if (g_file_get_contents(path, &content, NULL, NULL) &&
        !strstr(content, "[mode=do-not-disturb]")) {
        char *updated = g_strconcat(content,
            "\n[mode=do-not-disturb]\ninvisible=1\n", NULL);
        g_file_set_contents(path, updated, -1, NULL);
        g_free(updated);
    }
    g_free(content);
    g_free(path);
    char *argv[] = { "makoctl", "reload", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
}

static void refresh_notifications(State *st) {
    char *argv[] = { "makoctl", "mode", NULL };
    char *output = run_command_capture(argv);
    gboolean dnd = strstr(output, "do-not-disturb") != NULL;
    st->notifications_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->notifications_dnd_switch), dnd);
    st->notifications_loading = FALSE;
    gtk_label_set_text(GTK_LABEL(st->notifications_status_label),
        dnd ? "Уведомления временно скрыты" : "Уведомления показываются");
    g_free(output);
}

static gboolean on_notifications_dnd_changed(GtkSwitch *widget,
    gboolean state, gpointer user_data) {
    (void)widget;
    State *st = user_data;
    if (st->notifications_loading) return FALSE;
    ensure_dnd_mode_config();
    char *argv[] = {
        "makoctl", "mode", state ? "-a" : "-r", "do-not-disturb", NULL
    };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
    refresh_notifications(st);
    return FALSE;
}

static void on_notifications_test(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    char *argv[] = {
        "notify-send", "Plume", "Тестовое уведомление работает", NULL
    };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
}

static void on_notifications_dismiss(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    char *argv[] = { "makoctl", "dismiss", "-a", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
}

static void on_notifications_restore(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    char *argv[] = { "makoctl", "restore", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
}

static char *get_search_prefs_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "search.ini", NULL);
    g_free(dir);
    return path;
}

static void load_search_prefs(gboolean *descriptions, gboolean *commands) {
    *descriptions = TRUE;
    *commands = FALSE;
    char *path = get_search_prefs_path();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gboolean value = g_key_file_get_boolean(
            key, "Search", "descriptions", &error);
        if (!error) *descriptions = value;
        g_clear_error(&error);
        value = g_key_file_get_boolean(key, "Search", "commands", &error);
        if (!error) *commands = value;
        g_clear_error(&error);
    }
    g_key_file_unref(key);
    g_free(path);
}

static void save_search_prefs(State *st) {
    GKeyFile *key = g_key_file_new();
    g_key_file_set_boolean(key, "Search", "descriptions",
        gtk_switch_get_active(GTK_SWITCH(st->search_descriptions_switch)));
    g_key_file_set_boolean(key, "Search", "commands",
        gtk_switch_get_active(GTK_SWITCH(st->search_commands_switch)));
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    char *path = get_search_prefs_path();
    g_file_set_contents(path, data, length, NULL);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);
}

static void on_search_source_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (st->search_loading) return;
    save_search_prefs(st);
}

static void on_search_reset(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    st->search_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->search_descriptions_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(st->search_commands_switch), FALSE);
    st->search_loading = FALSE;
    save_search_prefs(st);
}

/* ==================== Благополучие ==================== */

static const guint WELLBEING_INTERVALS[] = { 20, 30, 45, 60 };

static char *get_wellbeing_prefs_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "wellbeing.ini", NULL);
    g_free(dir);
    return path;
}

static void load_wellbeing_prefs(gboolean *enabled, guint *interval) {
    *enabled = FALSE;
    *interval = 30;
    char *path = get_wellbeing_prefs_path();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gboolean stored_enabled = g_key_file_get_boolean(
            key, "Wellbeing", "enabled", &error);
        if (!error) *enabled = stored_enabled;
        g_clear_error(&error);
        gint stored_interval = g_key_file_get_integer(
            key, "Wellbeing", "interval_minutes", &error);
        if (!error && stored_interval >= 5 && stored_interval <= 240)
            *interval = (guint)stored_interval;
        g_clear_error(&error);
    }
    g_key_file_unref(key);
    g_free(path);
}

static void write_break_timer(guint interval) {
    char *user_cfg = g_build_filename(g_get_user_config_dir(),
        "systemd", "user", NULL);
    g_mkdir_with_parents(user_cfg, 0755);
    char *service_path = g_build_filename(user_cfg,
        "plume-break-reminder.service", NULL);
    char *timer_path = g_build_filename(user_cfg,
        "plume-break-reminder.timer", NULL);
    const char *service =
        "[Unit]\nDescription=Plume break reminder\n\n"
        "[Service]\nType=oneshot\n"
        "ExecStart=/usr/bin/env notify-send -a Plume "
        "'Пора сделать перерыв' 'Отведи взгляд от экрана, встань и немного разомнись.'\n";
    char *timer = g_strdup_printf(
        "[Unit]\nDescription=Plume periodic break reminder\n\n"
        "[Timer]\nOnActiveSec=%umin\nOnUnitActiveSec=%umin\n"
        "AccuracySec=30s\n\n[Install]\nWantedBy=timers.target\n",
        interval, interval);
    g_file_set_contents(service_path, service, -1, NULL);
    g_file_set_contents(timer_path, timer, -1, NULL);
    g_free(timer);
    g_free(timer_path);
    g_free(service_path);
    g_free(user_cfg);
}

static void refresh_wellbeing_status(State *st) {
    char *argv[] = { "systemctl", "--user", "is-active",
        "plume-break-reminder.timer", NULL };
    char *output = run_command_capture(argv);
    gboolean active = g_str_has_prefix(output, "active");
    gtk_label_set_text(GTK_LABEL(st->wellbeing_status_label), active
        ? "Напоминания запущены"
        : "Напоминания выключены");
    g_free(output);
}

static void save_and_apply_wellbeing(State *st) {
    gboolean enabled = gtk_switch_get_active(
        GTK_SWITCH(st->wellbeing_switch));
    guint selected = gtk_drop_down_get_selected(
        GTK_DROP_DOWN(st->wellbeing_interval));
    if (selected >= G_N_ELEMENTS(WELLBEING_INTERVALS)) selected = 1;
    guint interval = WELLBEING_INTERVALS[selected];

    GKeyFile *key = g_key_file_new();
    g_key_file_set_boolean(key, "Wellbeing", "enabled", enabled);
    g_key_file_set_integer(key, "Wellbeing", "interval_minutes", interval);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    char *path = get_wellbeing_prefs_path();
    g_file_set_contents(path, data, length, NULL);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);

    write_break_timer(interval);
    char *reload_argv[] = { "systemctl", "--user", "daemon-reload", NULL };
    char *ignored = run_command_capture(reload_argv);
    g_free(ignored);
    if (enabled) {
        char *enable_argv[] = { "systemctl", "--user", "enable", "--now",
            "plume-break-reminder.timer", NULL };
        ignored = run_command_capture(enable_argv);
        g_free(ignored);
        char *restart_argv[] = { "systemctl", "--user", "restart",
            "plume-break-reminder.timer", NULL };
        ignored = run_command_capture(restart_argv);
        g_free(ignored);
    } else {
        char *disable_argv[] = { "systemctl", "--user", "disable", "--now",
            "plume-break-reminder.timer", NULL };
        ignored = run_command_capture(disable_argv);
        g_free(ignored);
    }
    refresh_wellbeing_status(st);
}

static void on_wellbeing_enabled(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (st->wellbeing_loading) return;
    save_and_apply_wellbeing(st);
}

static void on_wellbeing_interval_changed(GObject *object,
    GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (!st->wellbeing_loading) save_and_apply_wellbeing(st);
}

static void on_wellbeing_test(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;
    char *argv[] = { "notify-send", "-a", "Plume",
        "Пора сделать перерыв",
        "Отведи взгляд от экрана, встань и немного разомнись.", NULL };
    char *ignored = run_command_capture(argv);
    g_free(ignored);
}

/* ==================== Мышь и сенсорная панель ==================== */

static char *get_mouse_prefs_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "mouse.ini", NULL);
    g_free(dir);
    return path;
}

static void load_mouse_prefs(double *mouse_speed, double *mouse_scroll_speed,
    double *touchpad_speed, double *touchpad_scroll_speed,
    gboolean *natural_scroll, gboolean *tap_to_click,
    gboolean *disable_while_typing) {
    *mouse_speed = 0.0;
    *mouse_scroll_speed = 1.0;
    *touchpad_speed = 0.0;
    *touchpad_scroll_speed = 1.0;
    *natural_scroll = FALSE;
    *tap_to_click = TRUE;
    *disable_while_typing = TRUE;
    char *path = get_mouse_prefs_path();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
#define READ_DOUBLE(name, target, low, high) do { \
        double value = g_key_file_get_double(key, "Input", name, &error); \
        if (!error) *(target) = CLAMP(value, low, high); \
        g_clear_error(&error); \
    } while (0)
#define READ_BOOL(name, target) do { \
        gboolean value = g_key_file_get_boolean(key, "Input", name, &error); \
        if (!error) *(target) = value; \
        g_clear_error(&error); \
    } while (0)
        READ_DOUBLE("mouse_cursor_speed", mouse_speed, -1.0, 1.0);
        READ_DOUBLE("mouse_scroll_speed", mouse_scroll_speed, 0.1, 3.0);
        READ_DOUBLE("touchpad_cursor_speed", touchpad_speed, -1.0, 1.0);
        READ_DOUBLE("touchpad_scroll_speed", touchpad_scroll_speed, 0.1, 3.0);
        READ_BOOL("natural_scroll", natural_scroll);
        READ_BOOL("tap_to_click", tap_to_click);
        READ_BOOL("disable_touchpad_while_typing", disable_while_typing);
#undef READ_DOUBLE
#undef READ_BOOL
    }
    g_key_file_unref(key);
    g_free(path);
}

static void apply_mouse_to_runtime(double mouse_speed, double mouse_scroll_speed,
    double touchpad_speed, double touchpad_scroll_speed,
    gboolean natural_scroll, gboolean tap_to_click,
    gboolean disable_while_typing) {
    const char *runtime = getenv("PLUME_RUNTIME");
    if (!runtime || !*runtime) return;
    char *path = g_build_filename(runtime, "wayfire", "wayfire.ini", NULL);
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path,
        G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
        g_key_file_set_double(key, "input", "mouse_cursor_speed", mouse_speed);
        g_key_file_set_double(key, "input", "mouse_scroll_speed",
            mouse_scroll_speed);
        g_key_file_set_double(key, "input", "touchpad_cursor_speed", touchpad_speed);
        g_key_file_set_double(key, "input", "touchpad_scroll_speed",
            touchpad_scroll_speed);
        g_key_file_set_boolean(key, "input", "natural_scroll", natural_scroll);
        g_key_file_set_boolean(key, "input", "tap_to_click", tap_to_click);
        g_key_file_set_boolean(key, "input", "disable_touchpad_while_typing",
            disable_while_typing);
        gsize length = 0;
        char *data = g_key_file_to_data(key, &length, NULL);
        g_file_set_contents(path, data, length, NULL);
        g_free(data);
    }
    g_key_file_unref(key);
    g_free(path);
}

static void save_and_apply_mouse(State *st) {
    double mouse_speed = gtk_range_get_value(GTK_RANGE(st->mouse_speed_scale));
    double mouse_scroll_speed = gtk_range_get_value(
        GTK_RANGE(st->mouse_scroll_scale));
    double touchpad_speed = gtk_range_get_value(
        GTK_RANGE(st->touchpad_speed_scale));
    double touchpad_scroll_speed = gtk_range_get_value(
        GTK_RANGE(st->touchpad_scroll_scale));
    gboolean natural_scroll = gtk_switch_get_active(
        GTK_SWITCH(st->natural_scroll_switch));
    gboolean tap_to_click = gtk_switch_get_active(
        GTK_SWITCH(st->tap_to_click_switch));
    gboolean disable_while_typing = gtk_switch_get_active(
        GTK_SWITCH(st->disable_while_typing_switch));

    GKeyFile *key = g_key_file_new();
    g_key_file_set_double(key, "Input", "mouse_cursor_speed", mouse_speed);
    g_key_file_set_double(key, "Input", "mouse_scroll_speed",
        mouse_scroll_speed);
    g_key_file_set_double(key, "Input", "touchpad_cursor_speed", touchpad_speed);
    g_key_file_set_double(key, "Input", "touchpad_scroll_speed",
        touchpad_scroll_speed);
    g_key_file_set_boolean(key, "Input", "natural_scroll", natural_scroll);
    g_key_file_set_boolean(key, "Input", "tap_to_click", tap_to_click);
    g_key_file_set_boolean(key, "Input", "disable_touchpad_while_typing",
        disable_while_typing);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    char *path = get_mouse_prefs_path();
    g_file_set_contents(path, data, length, NULL);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);

    apply_mouse_to_runtime(mouse_speed, mouse_scroll_speed,
        touchpad_speed, touchpad_scroll_speed, natural_scroll,
        tap_to_click, disable_while_typing);
    gtk_label_set_text(GTK_LABEL(st->mouse_status_label),
        getenv("PLUME_RUNTIME")
            ? "Параметры сохранены и применены в Wayfire"
            : "Параметры сохранены; применятся при следующем входе в Plume");
}

static void on_mouse_scale_changed(GtkRange *range, gpointer user_data) {
    (void)range;
    State *st = user_data;
    if (!st->mouse_loading) save_and_apply_mouse(st);
}

static void on_mouse_switch_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (st->mouse_loading) return;
    save_and_apply_mouse(st);
}

/* ==================== Управление цветом ==================== */

static char *get_color_prefs_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "color.ini", NULL);
    g_free(dir);
    return path;
}

static void load_color_prefs(gboolean *enabled, guint *temperature) {
    *enabled = FALSE;
    *temperature = 4500;
    char *path = get_color_prefs_path();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gboolean value = g_key_file_get_boolean(key, "Color", "warm", &error);
        if (!error) *enabled = value;
        g_clear_error(&error);
        gint temp = g_key_file_get_integer(key, "Color", "temperature", &error);
        if (!error && temp >= 2500 && temp <= 6500) *temperature = (guint)temp;
        g_clear_error(&error);
    }
    g_key_file_unref(key);
    g_free(path);
}

static void save_and_apply_color(State *st) {
    gboolean enabled = gtk_switch_get_active(GTK_SWITCH(st->color_warm_switch));
    guint temperature = (guint)gtk_range_get_value(
        GTK_RANGE(st->color_temperature_scale));
    GKeyFile *key = g_key_file_new();
    g_key_file_set_boolean(key, "Color", "warm", enabled);
    g_key_file_set_integer(key, "Color", "temperature", temperature);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    char *path = get_color_prefs_path();
    g_file_set_contents(path, data, length, NULL);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);

    char *program = g_find_program_in_path("gammastep");
    if (!program) {
        gtk_label_set_text(GTK_LABEL(st->color_status_label),
            "Для применения требуется пакет gammastep");
        return;
    }
    char temp[16];
    g_snprintf(temp, sizeof(temp), "%u", temperature);
    char *apply_argv[] = { program, "-P", "-O", temp, NULL };
    char *reset_argv[] = { program, "-x", NULL };
    gboolean started = run_command_background(enabled ? apply_argv : reset_argv);
    gtk_label_set_text(GTK_LABEL(st->color_status_label), started
        ? (enabled ? "Тёплые цвета применены"
                   : "Стандартная цветовая температура восстановлена")
        : "Не удалось применить цветовую температуру");
    g_free(program);
}

static void on_color_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (!st->color_loading) save_and_apply_color(st);
}

static void on_color_temperature_changed(GtkRange *range, gpointer user_data) {
    (void)range;
    State *st = user_data;
    if (!st->color_loading) save_and_apply_color(st);
}

static GtkWidget *build_color_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Управление цветом"));

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card, "ps-card");
    st->color_warm_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(card), make_search_source_row(
        "Тёплые цвета", "Уменьшает количество холодного синего света.",
        st->color_warm_switch));
    GtkWidget *title = gtk_label_new("Цветовая температура");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *hint = gtk_label_new(
        "Более низкая температура делает изображение теплее.");
    gtk_widget_add_css_class(hint, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    st->color_temperature_scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 2500, 6500, 100);
    gtk_scale_set_draw_value(GTK_SCALE(st->color_temperature_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(st->color_temperature_scale), GTK_POS_RIGHT);
    gtk_scale_add_mark(GTK_SCALE(st->color_temperature_scale), 2500,
        GTK_POS_BOTTOM, "Теплее");
    gtk_scale_add_mark(GTK_SCALE(st->color_temperature_scale), 4500,
        GTK_POS_BOTTOM, "Обычно");
    gtk_scale_add_mark(GTK_SCALE(st->color_temperature_scale), 6500,
        GTK_POS_BOTTOM, "Холоднее");
    gtk_box_append(GTK_BOX(card), title);
    gtk_box_append(GTK_BOX(card), hint);
    gtk_box_append(GTK_BOX(card), st->color_temperature_scale);
    gtk_box_append(GTK_BOX(box), card);
    st->color_status_label = gtk_label_new("Стандартная цветовая температура");
    gtk_widget_add_css_class(st->color_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->color_status_label), 0.0f);
    gtk_box_append(GTK_BOX(box), st->color_status_label);

    gboolean enabled;
    guint temperature;
    load_color_prefs(&enabled, &temperature);
    st->color_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->color_warm_switch), enabled);
    gtk_range_set_value(GTK_RANGE(st->color_temperature_scale), temperature);
    st->color_loading = FALSE;
    gtk_label_set_text(GTK_LABEL(st->color_status_label), enabled
        ? "Тёплые цвета включены" : "Стандартная цветовая температура");
    g_signal_connect(st->color_warm_switch, "notify::active",
        G_CALLBACK(on_color_changed), st);
    g_signal_connect(st->color_temperature_scale, "value-changed",
        G_CALLBACK(on_color_temperature_changed), st);
    return box;
}

/* ==================== Принтеры ==================== */

static void refresh_printers(State *st);

static void on_printer_default(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *name = g_object_get_data(G_OBJECT(button), "printer-name");
    if (!name) return;
    char *argv[] = { "lpoptions", "-d", (char *)name, NULL };
    gboolean success = run_command_success(argv);
    refresh_printers(st);
    if (!success)
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Не удалось изменить принтер по умолчанию");
}

static void on_printer_remove(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    const char *name = g_object_get_data(G_OBJECT(button), "printer-name");
    if (!name) return;
    char *argv[] = { "pkexec", "lpadmin", "-x", (char *)name, NULL };
    gboolean success = run_command_success(argv);
    refresh_printers(st);
    if (!success)
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Не удалось удалить принтер");
}

static GtkWidget *make_printer_row(State *st, const char *name,
    gboolean is_default) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "ps-card");
    GtkWidget *icon = gtk_label_new("󰐪");
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(name);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *status = gtk_label_new(is_default
        ? "Принтер по умолчанию" : "Готов к печати");
    gtk_widget_add_css_class(status, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), status);
    gtk_box_append(GTK_BOX(row), icon);
    gtk_box_append(GTK_BOX(row), text);
    if (!is_default) {
        GtkWidget *set_default = gtk_button_new_with_label("По умолчанию");
        gtk_widget_add_css_class(set_default, "ps-btn");
        g_object_set_data_full(G_OBJECT(set_default), "printer-name",
            g_strdup(name), g_free);
        g_signal_connect(set_default, "clicked",
            G_CALLBACK(on_printer_default), st);
        gtk_box_append(GTK_BOX(row), set_default);
    }
    GtkWidget *remove = gtk_button_new_with_label("Удалить");
    gtk_widget_add_css_class(remove, "ps-btn");
    g_object_set_data_full(G_OBJECT(remove), "printer-name",
        g_strdup(name), g_free);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_printer_remove), st);
    gtk_box_append(GTK_BOX(row), remove);
    return row;
}

static void refresh_printers(State *st) {
    if (!st->printers_list) return;
    clear_box(st->printers_list);
    char *lpstat_program = g_find_program_in_path("lpstat");
    if (!lpstat_program) {
        gtk_widget_set_visible(st->printers_enable_button, FALSE);
        gtk_widget_set_sensitive(st->printers_add_button, FALSE);
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Компоненты печати CUPS не установлены");
        return;
    }
    g_free(lpstat_program);
    char *service_argv[] = { "systemctl", "is-active", "cups.service", NULL };
    char *service = run_command_capture(service_argv);
    g_strstrip(service);
    gboolean service_active = g_strcmp0(service, "active") == 0;
    g_free(service);
    if (st->printers_enable_button)
        gtk_widget_set_visible(st->printers_enable_button, !service_active);
    gtk_widget_set_sensitive(st->printers_add_button, service_active);
    if (!service_active) {
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Служба печати CUPS выключена");
        GtkWidget *hint = gtk_label_new(
            "Включи службу печати, чтобы добавлять и использовать принтеры.");
        gtk_widget_add_css_class(hint, "ps-muted");
        gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
        gtk_box_append(GTK_BOX(st->printers_list), hint);
        return;
    }
    char *list_argv[] = { "lpstat", "-e", NULL };
    char *default_argv[] = { "lpstat", "-d", NULL };
    char *output = run_command_capture(list_argv);
    char *default_output = run_command_capture(default_argv);
    char *colon = strchr(default_output, ':');
    char *default_name = colon ? g_strdup(colon + 1) : g_strdup("");
    g_strstrip(default_name);
    char **lines = g_strsplit(output, "\n", -1);
    guint count = 0;
    for (guint i = 0; lines[i]; i++) {
        g_strstrip(lines[i]);
        if (!lines[i][0]) continue;
        gtk_box_append(GTK_BOX(st->printers_list), make_printer_row(st,
            lines[i], g_strcmp0(lines[i], default_name) == 0));
        count++;
    }
    gtk_label_set_text(GTK_LABEL(st->printers_status_label), count
        ? "Принтеры CUPS готовы к работе"
        : "Принтеры пока не добавлены");
    g_strfreev(lines);
    g_free(default_name);
    g_free(default_output);
    g_free(output);
}

static void on_printers_refresh(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_printers(user_data);
}

static void on_enable_printing(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    char *argv[] = { "pkexec", "systemctl", "enable", "--now",
        "cups.service", NULL };
    gboolean success = run_command_success(argv);
    refresh_printers(st);
    if (!success)
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Не удалось включить службу печати");
}

static void on_add_printer_cancel(GtkButton *button, gpointer user_data) {
    (void)button;
    gtk_window_destroy(GTK_WINDOW(user_data));
}

static void on_add_printer_confirm(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *window = user_data;
    State *st = g_object_get_data(G_OBJECT(window), "state");
    GtkWidget *name_entry = g_object_get_data(G_OBJECT(window), "name-entry");
    GtkWidget *uri_entry = g_object_get_data(G_OBJECT(window), "uri-entry");
    const char *name = gtk_editable_get_text(GTK_EDITABLE(name_entry));
    const char *uri = gtk_editable_get_text(GTK_EDITABLE(uri_entry));
    if (!name || !*name || !uri || !*uri) {
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Укажи название и IPP-адрес принтера");
        return;
    }
    char *argv[] = { "pkexec", "lpadmin", "-p", (char *)name,
        "-E", "-v", (char *)uri, "-m", "everywhere", NULL };
    gboolean success = run_command_success(argv);
    gtk_window_destroy(GTK_WINDOW(window));
    refresh_printers(st);
    if (!success)
        gtk_label_set_text(GTK_LABEL(st->printers_status_label),
            "Не удалось добавить принтер: проверь название и адрес");
}

static void on_add_printer(GtkButton *button, gpointer user_data) {
    (void)button;
    State *st = user_data;
    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Добавить принтер");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(st->win));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, -1);
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(form, 18);
    gtk_widget_set_margin_bottom(form, 18);
    gtk_widget_set_margin_start(form, 18);
    gtk_widget_set_margin_end(form, 18);
    GtkWidget *title = gtk_label_new("Добавить IPP-принтер");
    gtk_widget_add_css_class(title, "ps-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Название принтера");
    GtkWidget *uri = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(uri),
        "Адрес IPP, например ipp://192.168.1.50/ipp/print");
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Отмена");
    gtk_widget_add_css_class(cancel, "ps-btn");
    GtkWidget *confirm = gtk_button_new_with_label("Добавить");
    gtk_widget_add_css_class(confirm, "ps-btn-primary");
    gtk_box_append(GTK_BOX(actions), cancel);
    gtk_box_append(GTK_BOX(actions), confirm);
    gtk_box_append(GTK_BOX(form), title);
    gtk_box_append(GTK_BOX(form), name);
    gtk_box_append(GTK_BOX(form), uri);
    gtk_box_append(GTK_BOX(form), actions);
    gtk_window_set_child(GTK_WINDOW(window), form);
    g_object_set_data(G_OBJECT(window), "state", st);
    g_object_set_data(G_OBJECT(window), "name-entry", name);
    g_object_set_data(G_OBJECT(window), "uri-entry", uri);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_add_printer_cancel), window);
    g_signal_connect(confirm, "clicked", G_CALLBACK(on_add_printer_confirm), window);
    gtk_window_present(GTK_WINDOW(window));
}

static GtkWidget *build_printers_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Принтеры"));
    st->printers_status_label = gtk_label_new("Проверка CUPS…");
    gtk_widget_add_css_class(st->printers_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->printers_status_label), 0.0f);
    gtk_box_append(GTK_BOX(box), st->printers_status_label);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->printers_enable_button = gtk_button_new_with_label("Включить печать");
    gtk_widget_add_css_class(st->printers_enable_button, "ps-btn-primary");
    g_signal_connect(st->printers_enable_button, "clicked",
        G_CALLBACK(on_enable_printing), st);
    st->printers_add_button = gtk_button_new_with_label("Добавить IPP-принтер");
    gtk_widget_add_css_class(st->printers_add_button, "ps-btn-primary");
    g_signal_connect(st->printers_add_button, "clicked",
        G_CALLBACK(on_add_printer), st);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_printers_refresh), st);
    gtk_box_append(GTK_BOX(actions), st->printers_enable_button);
    gtk_box_append(GTK_BOX(actions), st->printers_add_button);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(box), actions);
    st->printers_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->printers_list);
    return scroll;
}

/* ==================== Специальные возможности ==================== */

static char *get_accessibility_prefs_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "accessibility.ini", NULL);
    g_free(dir);
    return path;
}

static void load_accessibility_prefs(gboolean *large_text,
    gboolean *reduce_motion, guint *cursor_size) {
    *large_text = FALSE;
    *reduce_motion = FALSE;
    *cursor_size = 24;
    char *path = get_accessibility_prefs_path();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gboolean value = g_key_file_get_boolean(key, "Accessibility",
            "large_text", &error);
        if (!error) *large_text = value;
        g_clear_error(&error);
        value = g_key_file_get_boolean(key, "Accessibility",
            "reduce_motion", &error);
        if (!error) *reduce_motion = value;
        g_clear_error(&error);
        gint size = g_key_file_get_integer(key, "Accessibility",
            "cursor_size", &error);
        if (!error && size >= 16 && size <= 64) *cursor_size = (guint)size;
        g_clear_error(&error);
    }
    g_key_file_unref(key);
    g_free(path);
}

static void save_and_apply_accessibility(State *st) {
    gboolean large_text = gtk_switch_get_active(
        GTK_SWITCH(st->access_large_text_switch));
    gboolean reduce_motion = gtk_switch_get_active(
        GTK_SWITCH(st->access_reduce_motion_switch));
    guint cursor_size = (guint)gtk_range_get_value(
        GTK_RANGE(st->access_cursor_size_scale));
    GKeyFile *key = g_key_file_new();
    g_key_file_set_boolean(key, "Accessibility", "large_text", large_text);
    g_key_file_set_boolean(key, "Accessibility", "reduce_motion", reduce_motion);
    g_key_file_set_integer(key, "Accessibility", "cursor_size", cursor_size);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    char *path = get_accessibility_prefs_path();
    g_file_set_contents(path, data, length, NULL);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);

    if (large_text) gtk_widget_add_css_class(st->win, "ps-large-text");
    else gtk_widget_remove_css_class(st->win, "ps-large-text");
    GtkSettings *gtk_settings = gtk_settings_get_default();
    if (gtk_settings)
        g_object_set(gtk_settings, "gtk-enable-animations", !reduce_motion, NULL);

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source,
        "org.gnome.desktop.interface", TRUE) : NULL;
    if (schema) {
        GSettings *settings = g_settings_new_full(schema, NULL, NULL);
        g_settings_set_double(settings, "text-scaling-factor",
            large_text ? 1.25 : 1.0);
        g_settings_set_boolean(settings, "enable-animations", !reduce_motion);
        g_object_unref(settings);
        g_settings_schema_unref(schema);
    }

    const char *runtime = getenv("PLUME_RUNTIME");
    if (runtime && *runtime) {
        char *wf = g_build_filename(runtime, "wayfire", "wayfire.ini", NULL);
        GKeyFile *wf_key = g_key_file_new();
        if (g_key_file_load_from_file(wf_key, wf,
            G_KEY_FILE_KEEP_COMMENTS, NULL)) {
            g_key_file_set_integer(wf_key, "input", "cursor_size", cursor_size);
            g_key_file_set_string(wf_key, "animate", "duration",
                reduce_motion ? "0ms linear" : "220ms circle");
            g_key_file_set_string(wf_key, "animate", "zoom_duration",
                reduce_motion ? "0ms linear" : "220ms circle");
            g_key_file_set_string(wf_key, "animate", "fade_duration",
                reduce_motion ? "0ms linear" : "180ms linear");
            g_key_file_set_string(wf_key, "animate", "squeezimize_duration",
                reduce_motion ? "0ms linear" : "200ms circle");
            gsize wf_length = 0;
            char *wf_data = g_key_file_to_data(wf_key, &wf_length, NULL);
            g_file_set_contents(wf, wf_data, wf_length, NULL);
            g_free(wf_data);
        }
        g_key_file_unref(wf_key);
        g_free(wf);
    }
    gtk_label_set_text(GTK_LABEL(st->access_status_label),
        "Параметры специальных возможностей применены");
}

static void on_accessibility_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (!st->access_loading) save_and_apply_accessibility(st);
}

static void on_cursor_size_changed(GtkRange *range, gpointer user_data) {
    (void)range;
    State *st = user_data;
    if (!st->access_loading) save_and_apply_accessibility(st);
}

static GtkWidget *build_accessibility_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Специальные возможности"));
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card, "ps-card");
    st->access_large_text_switch = gtk_switch_new();
    st->access_reduce_motion_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(card), make_search_source_row("Крупный текст",
        "Увеличивает текст интерфейса до 125%.",
        st->access_large_text_switch));
    gtk_box_append(GTK_BOX(card), make_search_source_row("Уменьшить анимации",
        "Отключает лишние переходы GTK и Wayfire.",
        st->access_reduce_motion_switch));
    GtkWidget *cursor_title = gtk_label_new("Размер указателя");
    gtk_widget_add_css_class(cursor_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(cursor_title), 0.0f);
    st->access_cursor_size_scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 16, 64, 4);
    gtk_scale_set_draw_value(GTK_SCALE(st->access_cursor_size_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(st->access_cursor_size_scale), GTK_POS_RIGHT);
    gtk_scale_add_mark(GTK_SCALE(st->access_cursor_size_scale), 24,
        GTK_POS_BOTTOM, "Обычный");
    gtk_scale_add_mark(GTK_SCALE(st->access_cursor_size_scale), 48,
        GTK_POS_BOTTOM, "Крупный");
    gtk_box_append(GTK_BOX(card), cursor_title);
    gtk_box_append(GTK_BOX(card), st->access_cursor_size_scale);
    gtk_box_append(GTK_BOX(box), card);
    st->access_status_label = gtk_label_new("Параметры применяются сразу");
    gtk_widget_add_css_class(st->access_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->access_status_label), 0.0f);
    gtk_box_append(GTK_BOX(box), st->access_status_label);

    gboolean large_text, reduce_motion;
    guint cursor_size;
    load_accessibility_prefs(&large_text, &reduce_motion, &cursor_size);
    st->access_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->access_large_text_switch), large_text);
    gtk_switch_set_active(GTK_SWITCH(st->access_reduce_motion_switch), reduce_motion);
    gtk_range_set_value(GTK_RANGE(st->access_cursor_size_scale), cursor_size);
    st->access_loading = FALSE;
    if (large_text) gtk_widget_add_css_class(st->win, "ps-large-text");
    g_signal_connect(st->access_large_text_switch, "notify::active",
        G_CALLBACK(on_accessibility_changed), st);
    g_signal_connect(st->access_reduce_motion_switch, "notify::active",
        G_CALLBACK(on_accessibility_changed), st);
    g_signal_connect(st->access_cursor_size_scale, "value-changed",
        G_CALLBACK(on_cursor_size_changed), st);
    return box;
}

static char *read_os_pretty_name(void) {
    char *name = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
    if (!name || !*name) {
        g_free(name);
        return g_strdup("Linux");
    }
    return name;
}

static GtkWidget *make_distro_logo(void) {
    char *id = g_get_os_info(G_OS_INFO_KEY_ID);
    char *logo = g_get_os_info("LOGO");
    GtkWidget *image = NULL;
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    /* LOGO is an icon-theme name, never a shell command or remote URL. */
    if (logo && *logo && !strchr(logo, '/') && gtk_icon_theme_has_icon(theme, logo))
        image = gtk_image_new_from_icon_name(logo);

    if (!image && g_strcmp0(id, "ouvert") == 0) {
        const char *paths[] = {
            "/usr/share/pixmaps/ouvert-logo.png",
            "/usr/share/pixmaps/ouvert-logo.svg",
            "/usr/share/pixmaps/ouvert.png",
            "/usr/share/ouvert/ouvert-logo.png",
            "/usr/share/icons/hicolor/512x512/apps/ouvert-logo.png",
            "/usr/share/icons/hicolor/scalable/apps/ouvert-logo.svg",
            NULL
        };
        for (guint i = 0; paths[i] && !image; i++) {
            if (g_file_test(paths[i], G_FILE_TEST_IS_REGULAR))
                image = gtk_image_new_from_file(paths[i]);
        }
    }

    if (!image && id && *id) {
        char *candidate = g_strdup_printf("distributor-logo-%s", id);
        if (gtk_icon_theme_has_icon(theme, candidate))
            image = gtk_image_new_from_icon_name(candidate);
        g_free(candidate);
        if (!image) {
            candidate = g_strdup_printf("%s-logo", id);
            if (gtk_icon_theme_has_icon(theme, candidate))
                image = gtk_image_new_from_icon_name(candidate);
            g_free(candidate);
        }
    }

    static const struct {
        const char *id;
        const char *icon;
        const char *glyph;
        const char *color;
    } distros[] = {
        { "arch", "archlinux-logo", "\uf303", "#1793d1" },
        { "debian", "debian-logo", "\uf306", "#d70a53" },
        { "fedora", "fedora-logo", "\uf30a", "#51a2da" },
        { "ubuntu", "ubuntu-logo", "\uf31b", "#e95420" },
        { "linuxmint", "linuxmint-logo", "\uf30e", "#69a942" },
        { "manjaro", "manjaro-logo", "\uf312", "#35bf5c" },
        { "opensuse-leap", "opensuse-logo", "\uf314", "#73ba25" },
        { "opensuse-tumbleweed", "opensuse-logo", "\uf314", "#73ba25" },
        { "gentoo", "gentoo-logo", "\uf30d", "#a698cf" },
        { "alpine", "alpine-logo", "\uf300", "#0d8cbb" },
        { "nixos", "nix-snowflake", "\uf313", "#7ebae4" },
        { "ouvert", "ouvert-logo", NULL, NULL }
    };
    const char *glyph = "\uf17c"; /* Tux, Font Awesome Linux. */
    const char *color = NULL; /* Inherit foreground for light/dark themes. */
    for (guint i = 0; !image && i < G_N_ELEMENTS(distros); i++) {
        if (g_strcmp0(id, distros[i].id) != 0) continue;
        if (gtk_icon_theme_has_icon(theme, distros[i].icon))
            image = gtk_image_new_from_icon_name(distros[i].icon);
        if (distros[i].glyph) {
            glyph = distros[i].glyph;
            color = distros[i].color;
        }
        break;
    }
    if (image) {
        gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
    } else {
        image = gtk_label_new(NULL);
        char *markup = color
            ? g_markup_printf_escaped("<span font_family=\"Symbols Nerd Font\" size=\"36000\" foreground=\"%s\">%s</span>", color, glyph)
            : g_markup_printf_escaped("<span font_family=\"Symbols Nerd Font\" size=\"36000\">%s</span>", glyph);
        gtk_label_set_markup(GTK_LABEL(image), markup);
        g_free(markup);
    }
    gtk_widget_set_size_request(image, 56, 56);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(image, id && *id ? id : "Linux");
    g_free(id);
    g_free(logo);
    return image;
}

static GtkWidget *make_distro_row(const char *name) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_append(GTK_BOX(row), make_distro_logo());
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    GtkWidget *title = gtk_label_new(name);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(title), TRUE);
    GtkWidget *subtitle = gtk_label_new("Дистрибутив");
    gtk_widget_add_css_class(subtitle, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), subtitle);
    gtk_box_append(GTK_BOX(row), text);
    return row;
}

static GtkWidget *build_wifi_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Wi-Fi"));

    GtkWidget *radio_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(radio_card, "ps-card");
    GtkWidget *radio_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(radio_text, TRUE);
    GtkWidget *radio_title = gtk_label_new("Беспроводная сеть");
    gtk_widget_add_css_class(radio_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(radio_title), 0.0f);
    st->wifi_status_lbl = gtk_label_new("Проверка состояния…");
    gtk_widget_add_css_class(st->wifi_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->wifi_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(radio_text), radio_title);
    gtk_box_append(GTK_BOX(radio_text), st->wifi_status_lbl);
    st->wifi_switch = gtk_switch_new();
    gtk_widget_set_valign(st->wifi_switch, GTK_ALIGN_CENTER);
    g_signal_connect(st->wifi_switch, "state-set",
        G_CALLBACK(on_wifi_state_set), st);
    gtk_box_append(GTK_BOX(radio_card), radio_text);
    gtk_box_append(GTK_BOX(radio_card), st->wifi_switch);
    gtk_box_append(GTK_BOX(box), radio_card);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить список");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_wifi_refresh), st);
    GtkWidget *editor = gtk_button_new_with_label("Дополнительные параметры");
    gtk_widget_add_css_class(editor, "ps-btn");
    g_object_set_data(G_OBJECT(editor), "tool-cmd", "nm-connection-editor");
    g_signal_connect(editor, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), editor);
    gtk_box_append(GTK_BOX(box), actions);

    st->wifi_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->wifi_list);
    return scroll;
}

static GtkWidget *build_network_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Сеть"));

    GtkWidget *description = gtk_label_new(
        "Сетевые устройства и активные подключения NetworkManager.");
    gtk_widget_add_css_class(description, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
    gtk_box_append(GTK_BOX(box), description);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_network_refresh), st);
    GtkWidget *editor = gtk_button_new_with_label("Изменить подключения");
    gtk_widget_add_css_class(editor, "ps-btn");
    g_object_set_data(G_OBJECT(editor), "tool-cmd", "nm-connection-editor");
    g_signal_connect(editor, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), editor);
    gtk_box_append(GTK_BOX(box), actions);

    st->network_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->network_list);
    return scroll;
}

static GtkWidget *build_bluetooth_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Bluetooth"));

    GtkWidget *power_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(power_card, "ps-card");
    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(info, TRUE);
    GtkWidget *title = gtk_label_new("Bluetooth");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    st->bluetooth_status_lbl = gtk_label_new("Проверка адаптера…");
    gtk_widget_add_css_class(st->bluetooth_status_lbl, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->bluetooth_status_lbl), 0.0f);
    gtk_box_append(GTK_BOX(info), title);
    gtk_box_append(GTK_BOX(info), st->bluetooth_status_lbl);
    st->bluetooth_switch = gtk_switch_new();
    gtk_widget_set_valign(st->bluetooth_switch, GTK_ALIGN_CENTER);
    g_signal_connect(st->bluetooth_switch, "state-set",
        G_CALLBACK(on_bluetooth_state_set), st);
    gtk_box_append(GTK_BOX(power_card), info);
    gtk_box_append(GTK_BOX(power_card), st->bluetooth_switch);
    gtk_box_append(GTK_BOX(box), power_card);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *scan = gtk_button_new_with_label("Искать устройства");
    gtk_widget_add_css_class(scan, "ps-btn-primary");
    g_signal_connect(scan, "clicked", G_CALLBACK(on_bluetooth_scan), st);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_bluetooth_refresh), st);
    GtkWidget *manager = gtk_button_new_with_label("Управление устройствами");
    gtk_widget_add_css_class(manager, "ps-btn");
    g_object_set_data(G_OBJECT(manager), "tool-cmd", "blueman-manager");
    g_signal_connect(manager, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), scan);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), manager);
    gtk_box_append(GTK_BOX(box), actions);

    st->bluetooth_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->bluetooth_list);
    return scroll;
}

static GtkWidget *build_displays_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Дисплеи"));

    GtkWidget *description = gtk_label_new(
        "Подключённые дисплеи, текущий режим и быстрый выбор масштаба.");
    gtk_widget_add_css_class(description, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
    gtk_box_append(GTK_BOX(box), description);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_display_refresh), st);
    GtkWidget *layout = gtk_button_new_with_label("Расположение дисплеев");
    gtk_widget_add_css_class(layout, "ps-btn");
    g_object_set_data(G_OBJECT(layout), "tool-cmd", "wdisplays");
    g_signal_connect(layout, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), layout);
    gtk_box_append(GTK_BOX(box), actions);

    st->display_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->display_list);
    return scroll;
}

static GtkWidget *make_sound_card(State *st, const char *icon_text,
    const char *title_text, const char *target, GtkWidget **scale_out,
    GtkWidget **switch_out, GtkWidget **label_out) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *icon = gtk_label_new(icon_text);
    gtk_widget_add_css_class(icon, "ps-tool-icon");
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *status = gtk_label_new("Проверка устройства…");
    gtk_widget_add_css_class(status, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
    GtkWidget *enabled = gtk_switch_new();
    gtk_widget_set_valign(enabled, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(enabled), "audio-target", (gpointer)target);
    g_signal_connect(enabled, "state-set",
        G_CALLBACK(on_sound_enabled_changed), st);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), status);
    gtk_box_append(GTK_BOX(head), icon);
    gtk_box_append(GTK_BOX(head), text);
    gtk_box_append(GTK_BOX(head), enabled);
    gtk_box_append(GTK_BOX(card), head);

    GtkWidget *scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0.0, 150.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
    gtk_widget_set_hexpand(scale, TRUE);
    g_object_set_data(G_OBJECT(scale), "audio-target", (gpointer)target);
    g_signal_connect(scale, "value-changed",
        G_CALLBACK(on_sound_volume_changed), st);
    gtk_box_append(GTK_BOX(card), scale);
    *scale_out = scale;
    *switch_out = enabled;
    *label_out = status;
    return card;
}

static GtkWidget *build_sound_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Звук"));

    gtk_box_append(GTK_BOX(box), make_sound_card(st, "󰕾", "Вывод звука",
        "@DEFAULT_AUDIO_SINK@", &st->sound_output_scale,
        &st->sound_output_enabled, &st->sound_output_label));
    gtk_box_append(GTK_BOX(box), make_sound_card(st, "󰍬", "Микрофон",
        "@DEFAULT_AUDIO_SOURCE@", &st->sound_input_scale,
        &st->sound_input_enabled, &st->sound_input_label));

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_sound_refresh), st);
    GtkWidget *mixer = gtk_button_new_with_label("Расширенный микшер");
    gtk_widget_add_css_class(mixer, "ps-btn");
    g_object_set_data(G_OBJECT(mixer), "tool-cmd", "pavucontrol");
    g_signal_connect(mixer, "clicked", G_CALLBACK(on_launch_tool), NULL);
    gtk_box_append(GTK_BOX(actions), refresh);
    gtk_box_append(GTK_BOX(actions), mixer);
    gtk_box_append(GTK_BOX(box), actions);
    return scroll;
}

static GtkWidget *build_power_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Питание"));

    GtkWidget *battery_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(battery_card, "ps-card");
    GtkWidget *battery_icon = gtk_label_new("󰁹");
    gtk_widget_add_css_class(battery_icon, "ps-tool-icon");
    GtkWidget *battery_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *battery_title = gtk_label_new("Батарея");
    gtk_widget_add_css_class(battery_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(battery_title), 0.0f);
    st->power_status_label = gtk_label_new("Проверка состояния…");
    gtk_widget_add_css_class(st->power_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->power_status_label), 0.0f);
    gtk_box_append(GTK_BOX(battery_text), battery_title);
    gtk_box_append(GTK_BOX(battery_text), st->power_status_label);
    gtk_box_append(GTK_BOX(battery_card), battery_icon);
    gtk_box_append(GTK_BOX(battery_card), battery_text);
    gtk_box_append(GTK_BOX(box), battery_card);

    GtkWidget *profile_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(profile_card, "ps-card");
    GtkWidget *profile_title = gtk_label_new("Профиль энергопотребления");
    gtk_widget_add_css_class(profile_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(profile_title), 0.0f);
    st->power_profile_label = gtk_label_new("Проверка профилей…");
    gtk_widget_add_css_class(st->power_profile_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->power_profile_label), 0.0f);
    gtk_box_append(GTK_BOX(profile_card), profile_title);
    gtk_box_append(GTK_BOX(profile_card), st->power_profile_label);
    GtkWidget *profiles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *profile_ids[] = { "power-saver", "balanced", "performance" };
    const char *profile_labels[] = {
        "Экономия энергии", "Сбалансированный", "Производительность"
    };
    for (guint i = 0; i < 3; i++) {
        GtkWidget *button = gtk_button_new_with_label(profile_labels[i]);
        g_object_set_data(G_OBJECT(button), "power-profile",
            (gpointer)profile_ids[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(on_power_profile), st);
        st->power_profile_buttons[i] = button;
        gtk_box_append(GTK_BOX(profiles), button);
    }
    gtk_box_append(GTK_BOX(profile_card), profiles);
    gtk_box_append(GTK_BOX(box), profile_card);

    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn-primary");
    gtk_widget_set_halign(refresh, GTK_ALIGN_START);
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_power_refresh), st);
    gtk_box_append(GTK_BOX(box), refresh);
    return scroll;
}

static GtkWidget *build_multitasking_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Многозадачность"));

    GtkWidget *workspace_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(workspace_card, "ps-card");
    GtkWidget *title = gtk_label_new("Рабочие столы");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    st->workspace_status_label = gtk_label_new("Проверка рабочего стола…");
    gtk_widget_add_css_class(st->workspace_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->workspace_status_label), 0.0f);
    gtk_box_append(GTK_BOX(workspace_card), title);
    gtk_box_append(GTK_BOX(workspace_card), st->workspace_status_label);
    GtkWidget *workspaces = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (guint i = 0; i < 5; i++) {
        char label[8];
        g_snprintf(label, sizeof(label), "%u", i + 1);
        GtkWidget *button = gtk_button_new_with_label(label);
        g_object_set_data(G_OBJECT(button), "workspace-index",
            GUINT_TO_POINTER(i + 1));
        g_signal_connect(button, "clicked",
            G_CALLBACK(on_workspace_selected), st);
        st->workspace_buttons[i] = button;
        gtk_box_append(GTK_BOX(workspaces), button);
    }
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(refresh, "ps-btn");
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_workspace_refresh), st);
    gtk_box_append(GTK_BOX(workspaces), refresh);
    gtk_box_append(GTK_BOX(workspace_card), workspaces);
    gtk_box_append(GTK_BOX(box), workspace_card);

    GtkWidget *shortcuts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(shortcuts, "ps-card");
    GtkWidget *shortcuts_title = gtk_label_new("Управление окнами");
    gtk_widget_add_css_class(shortcuts_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(shortcuts_title), 0.0f);
    gtk_box_append(GTK_BOX(shortcuts), shortcuts_title);
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Рабочие столы", "Ctrl + Super + ← / →"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Привязка окна", "Super + ← / →"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Развернуть", "Super + ↑"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Восстановить", "Super + ↓"));
    gtk_box_append(GTK_BOX(shortcuts), make_info_row(
        "Переключение окон", "Alt + Tab"));
    gtk_box_append(GTK_BOX(box), shortcuts);
    return scroll;
}

static GtkWidget *make_default_app_row(State *st, guint index,
    const char *title_text, const char *content_type) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *current = gtk_label_new("Проверка…");
    gtk_widget_add_css_class(current, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(current), 0.0f);
    st->default_app_labels[index] = current;
    GtkWidget *choose = gtk_button_new_with_label("Изменить");
    gtk_widget_add_css_class(choose, "ps-btn");
    g_object_set_data(G_OBJECT(choose), "content-type",
        (gpointer)content_type);
    g_signal_connect(choose, "clicked",
        G_CALLBACK(on_choose_default_app), st);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), current);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(row), choose);
    return row;
}

static GtkWidget *build_applications_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Приложения"));

    GtkWidget *defaults = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(defaults, "ps-card");
    GtkWidget *defaults_title = gtk_label_new("Приложения по умолчанию");
    gtk_widget_add_css_class(defaults_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(defaults_title), 0.0f);
    gtk_box_append(GTK_BOX(defaults), defaults_title);
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 0, "Веб-браузер", "text/html"));
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 1, "Файловый менеджер", "inode/directory"));
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 2, "Просмотр изображений", "image/png"));
    gtk_box_append(GTK_BOX(defaults), make_default_app_row(
        st, 3, "Документы PDF", "application/pdf"));
    gtk_box_append(GTK_BOX(box), defaults);

    GtkWidget *autostart = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(autostart, "ps-card");
    GtkWidget *autostart_title = gtk_label_new("Автозапуск");
    gtk_widget_add_css_class(autostart_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(autostart_title), 0.0f);
    st->autostart_status_label = gtk_label_new("Проверка…");
    gtk_widget_add_css_class(st->autostart_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->autostart_status_label), 0.0f);
    GtkWidget *autostart_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *add_app = gtk_button_new_with_label("Добавить приложение");
    gtk_widget_add_css_class(add_app, "ps-btn-primary");
    g_signal_connect(add_app, "clicked", G_CALLBACK(on_add_installed_app), st);
    GtkWidget *add_command = gtk_button_new_with_label("Добавить команду");
    gtk_widget_add_css_class(add_command, "ps-btn");
    g_signal_connect(add_command, "clicked", G_CALLBACK(on_add_command), st);
    gtk_box_append(GTK_BOX(autostart_actions), add_app);
    gtk_box_append(GTK_BOX(autostart_actions), add_command);
    gtk_box_append(GTK_BOX(autostart), autostart_title);
    gtk_box_append(GTK_BOX(autostart), st->autostart_status_label);
    gtk_box_append(GTK_BOX(autostart), autostart_actions);
    gtk_box_append(GTK_BOX(box), autostart);

    st->autostart_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), st->autostart_list);
    return scroll;
}

static GtkWidget *build_notifications_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Уведомления"));

    GtkWidget *dnd_card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(dnd_card, "ps-card");
    GtkWidget *dnd_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(dnd_text, TRUE);
    GtkWidget *dnd_title = gtk_label_new("Не беспокоить");
    gtk_widget_add_css_class(dnd_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(dnd_title), 0.0f);
    st->notifications_status_label = gtk_label_new("Проверка состояния…");
    gtk_widget_add_css_class(st->notifications_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->notifications_status_label), 0.0f);
    st->notifications_dnd_switch = gtk_switch_new();
    gtk_widget_set_valign(st->notifications_dnd_switch, GTK_ALIGN_CENTER);
    g_signal_connect(st->notifications_dnd_switch, "state-set",
        G_CALLBACK(on_notifications_dnd_changed), st);
    gtk_box_append(GTK_BOX(dnd_text), dnd_title);
    gtk_box_append(GTK_BOX(dnd_text), st->notifications_status_label);
    gtk_box_append(GTK_BOX(dnd_card), dnd_text);
    gtk_box_append(GTK_BOX(dnd_card), st->notifications_dnd_switch);
    gtk_box_append(GTK_BOX(box), dnd_card);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(actions, "ps-card");
    GtkWidget *actions_title = gtk_label_new("Действия");
    gtk_widget_add_css_class(actions_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(actions_title), 0.0f);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *test = gtk_button_new_with_label("Тестовое уведомление");
    gtk_widget_add_css_class(test, "ps-btn-primary");
    g_signal_connect(test, "clicked", G_CALLBACK(on_notifications_test), st);
    GtkWidget *dismiss = gtk_button_new_with_label("Закрыть все");
    gtk_widget_add_css_class(dismiss, "ps-btn");
    g_signal_connect(dismiss, "clicked",
        G_CALLBACK(on_notifications_dismiss), st);
    GtkWidget *restore = gtk_button_new_with_label("Вернуть последнее");
    gtk_widget_add_css_class(restore, "ps-btn");
    g_signal_connect(restore, "clicked",
        G_CALLBACK(on_notifications_restore), st);
    gtk_box_append(GTK_BOX(buttons), test);
    gtk_box_append(GTK_BOX(buttons), dismiss);
    gtk_box_append(GTK_BOX(buttons), restore);
    gtk_box_append(GTK_BOX(actions), actions_title);
    gtk_box_append(GTK_BOX(actions), buttons);
    gtk_box_append(GTK_BOX(box), actions);
    return scroll;
}

static GtkWidget *make_search_source_row(const char *title_text,
    const char *description, GtkWidget *toggle) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *details = gtk_label_new(description);
    gtk_widget_add_css_class(details, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(details), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(details), TRUE);
    gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(text), title);
    gtk_box_append(GTK_BOX(text), details);
    gtk_box_append(GTK_BOX(row), text);
    gtk_box_append(GTK_BOX(row), toggle);
    return row;
}

static GtkWidget *build_search_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Поиск"));

    GtkWidget *sources = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(sources, "ps-card");
    GtkWidget *sources_title = gtk_label_new("Источники поиска Plume");
    gtk_widget_add_css_class(sources_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(sources_title), 0.0f);
    gtk_box_append(GTK_BOX(sources), sources_title);

    GtkWidget *apps_toggle = gtk_label_new("Всегда включено");
    gtk_widget_add_css_class(apps_toggle, "ps-source-fixed");
    gtk_box_append(GTK_BOX(sources), make_search_source_row(
        "Названия приложений", "Основной источник результатов поиска.",
        apps_toggle));

    st->search_descriptions_switch = gtk_switch_new();
    st->search_commands_switch = gtk_switch_new();
    gboolean descriptions = TRUE, commands = FALSE;
    load_search_prefs(&descriptions, &commands);
    st->search_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->search_descriptions_switch), descriptions);
    gtk_switch_set_active(GTK_SWITCH(st->search_commands_switch), commands);
    st->search_loading = FALSE;
    g_signal_connect(st->search_descriptions_switch, "notify::active",
        G_CALLBACK(on_search_source_changed), st);
    g_signal_connect(st->search_commands_switch, "notify::active",
        G_CALLBACK(on_search_source_changed), st);
    gtk_box_append(GTK_BOX(sources), make_search_source_row(
        "Описания приложений",
        "Находить приложения по их назначению и описанию.",
        st->search_descriptions_switch));
    gtk_box_append(GTK_BOX(sources), make_search_source_row(
        "Названия команд",
        "Находить приложения по исполняемому файлу и команде запуска.",
        st->search_commands_switch));
    gtk_box_append(GTK_BOX(box), sources);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *try_search = gtk_button_new_with_label("Проверить поиск");
    gtk_widget_add_css_class(try_search, "ps-btn-primary");
    g_object_set_data(G_OBJECT(try_search), "tool-cmd", "plume-launcher");
    g_signal_connect(try_search, "clicked", G_CALLBACK(on_launch_tool), NULL);
    GtkWidget *reset = gtk_button_new_with_label("Сбросить настройки");
    gtk_widget_add_css_class(reset, "ps-btn");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_search_reset), st);
    gtk_box_append(GTK_BOX(actions), try_search);
    gtk_box_append(GTK_BOX(actions), reset);
    gtk_box_append(GTK_BOX(box), actions);
    return scroll;
}

static GtkWidget *build_wellbeing_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box), make_section_title("Благополучие"));

    gboolean enabled = FALSE;
    guint interval = 30;
    load_wellbeing_prefs(&enabled, &interval);

    GtkWidget *reminder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(reminder, "ps-card");
    GtkWidget *title = gtk_label_new("Перерывы от экрана");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(reminder), title);

    st->wellbeing_switch = gtk_switch_new();
    st->wellbeing_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->wellbeing_switch), enabled);
    st->wellbeing_loading = FALSE;
    gtk_box_append(GTK_BOX(reminder), make_search_source_row(
        "Напоминать о перерыве",
        "Уведомление работает через пользовательский таймер systemd.",
        st->wellbeing_switch));

    GtkWidget *interval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *interval_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(interval_text, TRUE);
    GtkWidget *interval_title = gtk_label_new("Интервал");
    gtk_widget_add_css_class(interval_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(interval_title), 0.0f);
    GtkWidget *interval_hint = gtk_label_new(
        "Как часто Plume будет предлагать сделать короткий перерыв.");
    gtk_widget_add_css_class(interval_hint, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(interval_hint), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(interval_hint), TRUE);
    gtk_box_append(GTK_BOX(interval_text), interval_title);
    gtk_box_append(GTK_BOX(interval_text), interval_hint);
    const char *interval_labels[] = {
        "20 минут", "30 минут", "45 минут", "60 минут", NULL
    };
    st->wellbeing_interval = gtk_drop_down_new_from_strings(interval_labels);
    guint interval_index = 1;
    for (guint i = 0; i < G_N_ELEMENTS(WELLBEING_INTERVALS); i++) {
        if (WELLBEING_INTERVALS[i] == interval) interval_index = i;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->wellbeing_interval),
        interval_index);
    gtk_widget_set_valign(st->wellbeing_interval, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(interval_row), interval_text);
    gtk_box_append(GTK_BOX(interval_row), st->wellbeing_interval);
    gtk_box_append(GTK_BOX(reminder), interval_row);

    st->wellbeing_status_label = gtk_label_new("Проверка состояния…");
    gtk_widget_add_css_class(st->wellbeing_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->wellbeing_status_label), 0.0f);
    gtk_box_append(GTK_BOX(reminder), st->wellbeing_status_label);
    gtk_box_append(GTK_BOX(box), reminder);

    GtkWidget *test = gtk_button_new_with_label("Проверить напоминание");
    gtk_widget_add_css_class(test, "ps-btn-primary");
    gtk_widget_set_halign(test, GTK_ALIGN_START);
    g_signal_connect(test, "clicked", G_CALLBACK(on_wellbeing_test), st);
    gtk_box_append(GTK_BOX(box), test);

    g_signal_connect(st->wellbeing_switch, "notify::active",
        G_CALLBACK(on_wellbeing_enabled), st);
    g_signal_connect(st->wellbeing_interval, "notify::selected",
        G_CALLBACK(on_wellbeing_interval_changed), st);
    return scroll;
}

static GtkWidget *make_pointer_speed_card(const char *title_text,
    const char *description, GtkWidget **scale_out, State *st) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "ps-card");
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *details = gtk_label_new(description);
    gtk_widget_add_css_class(details, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(details), 0.0f);
    GtkWidget *scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, -1.0, 1.0, 0.05);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_scale_add_mark(GTK_SCALE(scale), -1.0, GTK_POS_BOTTOM, "Медленно");
    gtk_scale_add_mark(GTK_SCALE(scale), 0.0, GTK_POS_BOTTOM, "Обычно");
    gtk_scale_add_mark(GTK_SCALE(scale), 1.0, GTK_POS_BOTTOM, "Быстро");
    gtk_box_append(GTK_BOX(card), title);
    gtk_box_append(GTK_BOX(card), details);
    gtk_box_append(GTK_BOX(card), scale);
    g_signal_connect(scale, "value-changed",
        G_CALLBACK(on_mouse_scale_changed), st);
    *scale_out = scale;
    return card;
}

static GtkWidget *make_scroll_speed_control(const char *title_text,
    const char *description, GtkWidget **scale_out, State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *title = gtk_label_new(title_text);
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *details = gtk_label_new(description);
    gtk_widget_add_css_class(details, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(details), 0.0f);
    GtkWidget *scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0.1, 3.0, 0.05);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_scale_add_mark(GTK_SCALE(scale), 0.1, GTK_POS_BOTTOM, "Медленно");
    gtk_scale_add_mark(GTK_SCALE(scale), 1.0, GTK_POS_BOTTOM, "Обычно");
    gtk_scale_add_mark(GTK_SCALE(scale), 3.0, GTK_POS_BOTTOM, "Быстро");
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), details);
    gtk_box_append(GTK_BOX(box), scale);
    g_signal_connect(scale, "value-changed",
        G_CALLBACK(on_mouse_scale_changed), st);
    *scale_out = scale;
    return box;
}

static GtkWidget *build_mouse_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_box_append(GTK_BOX(box),
        make_section_title("Мышь и сенсорная панель"));

    double mouse_speed = 0.0, mouse_scroll_speed = 1.0;
    double touchpad_speed = 0.0, touchpad_scroll_speed = 1.0;
    gboolean natural_scroll = FALSE, tap_to_click = TRUE;
    gboolean disable_while_typing = TRUE;
    load_mouse_prefs(&mouse_speed, &mouse_scroll_speed,
        &touchpad_speed, &touchpad_scroll_speed,
        &natural_scroll, &tap_to_click, &disable_while_typing);

    st->mouse_loading = TRUE;
    GtkWidget *mouse_card = make_pointer_speed_card(
        "Скорость мыши", "Скорость перемещения указателя мышью.",
        &st->mouse_speed_scale, st);
    gtk_range_set_value(GTK_RANGE(st->mouse_speed_scale), mouse_speed);
    gtk_box_append(GTK_BOX(mouse_card), make_scroll_speed_control(
        "Скорость прокрутки",
        "Скорость прокрутки колёсиком мыши.",
        &st->mouse_scroll_scale, st));
    gtk_range_set_value(GTK_RANGE(st->mouse_scroll_scale), mouse_scroll_speed);
    gtk_box_append(GTK_BOX(box), mouse_card);

    GtkWidget *touchpad_card = make_pointer_speed_card(
        "Скорость сенсорной панели",
        "Скорость перемещения указателя по сенсорной панели.",
        &st->touchpad_speed_scale, st);
    gtk_range_set_value(GTK_RANGE(st->touchpad_speed_scale), touchpad_speed);
    gtk_box_append(GTK_BOX(touchpad_card), make_scroll_speed_control(
        "Скорость прокрутки",
        "Скорость прокрутки жестом на сенсорной панели.",
        &st->touchpad_scroll_scale, st));
    gtk_range_set_value(GTK_RANGE(st->touchpad_scroll_scale),
        touchpad_scroll_speed);

    st->natural_scroll_switch = gtk_switch_new();
    st->tap_to_click_switch = gtk_switch_new();
    st->disable_while_typing_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(st->natural_scroll_switch), natural_scroll);
    gtk_switch_set_active(GTK_SWITCH(st->tap_to_click_switch), tap_to_click);
    gtk_switch_set_active(GTK_SWITCH(st->disable_while_typing_switch),
        disable_while_typing);
    st->mouse_loading = FALSE;

    gtk_box_append(GTK_BOX(touchpad_card), make_search_source_row(
        "Естественная прокрутка",
        "Содержимое движется в направлении пальцев.",
        st->natural_scroll_switch));
    gtk_box_append(GTK_BOX(touchpad_card), make_search_source_row(
        "Касание для щелчка",
        "Короткое касание сенсорной панели выполняет щелчок.",
        st->tap_to_click_switch));
    gtk_box_append(GTK_BOX(touchpad_card), make_search_source_row(
        "Отключать при наборе",
        "Предотвращает случайные движения указателя во время печати.",
        st->disable_while_typing_switch));
    gtk_box_append(GTK_BOX(box), touchpad_card);

    st->mouse_status_label = gtk_label_new(
        "Изменения применяются к Wayfire сразу");
    gtk_widget_add_css_class(st->mouse_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->mouse_status_label), 0.0f);
    gtk_box_append(GTK_BOX(box), st->mouse_status_label);

    g_signal_connect(st->natural_scroll_switch, "notify::active",
        G_CALLBACK(on_mouse_switch_changed), st);
    g_signal_connect(st->tap_to_click_switch, "notify::active",
        G_CALLBACK(on_mouse_switch_changed), st);
    g_signal_connect(st->disable_while_typing_switch, "notify::active",
        G_CALLBACK(on_mouse_switch_changed), st);
    return scroll;
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

    char *cur = read_current_wallpaper(st);
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

/* ==================== Конфиденциальность и безопасность ==================== */

static char *get_privacy_prefs_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "privacy.ini", NULL);
    g_free(dir);
    return path;
}

static void load_privacy_prefs(gboolean *auto_lock, guint *delay,
    gboolean *microphone_blocked) {
    *auto_lock = FALSE;
    *delay = 300;
    *microphone_blocked = FALSE;
    char *path = get_privacy_prefs_path();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gboolean value = g_key_file_get_boolean(key, "Privacy", "auto_lock", &error);
        if (!error) *auto_lock = value;
        g_clear_error(&error);
        gint seconds = g_key_file_get_integer(key, "Privacy", "lock_delay", &error);
        if (!error && seconds >= 60 && seconds <= 3600) *delay = (guint)seconds;
        g_clear_error(&error);
        value = g_key_file_get_boolean(key, "Privacy", "microphone_blocked", &error);
        if (!error) *microphone_blocked = value;
        g_clear_error(&error);
    }
    g_key_file_unref(key);
    g_free(path);
}

static void start_idle_lock(gboolean enabled, guint seconds) {
    char *stop_argv[] = { "pkill", "-x", "swayidle", NULL };
    if (!enabled) {
        run_command_background(stop_argv);
        return;
    }
    char *swayidle = g_find_program_in_path("swayidle");
    if (!swayidle) {
        g_free(swayidle);
        return;
    }
    g_free(swayidle);
    char *command = g_strdup_printf(
        "pkill -x swayidle 2>/dev/null; exec swayidle -w timeout %u "
        "'plume-lock' before-sleep 'plume-lock'", seconds);
    char *start_argv[] = { "sh", "-c", command, NULL };
    run_command_background(start_argv);
    g_free(command);
}

static void save_and_apply_privacy(State *st) {
    gboolean auto_lock = gtk_switch_get_active(GTK_SWITCH(st->privacy_lock_switch));
    gboolean microphone_blocked = gtk_switch_get_active(
        GTK_SWITCH(st->privacy_microphone_switch));
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->privacy_lock_delay));
    static const guint delays[] = { 60, 300, 600, 900, 1800 };
    guint delay = selected < G_N_ELEMENTS(delays) ? delays[selected] : 300;
    GKeyFile *key = g_key_file_new();
    g_key_file_set_boolean(key, "Privacy", "auto_lock", auto_lock);
    g_key_file_set_integer(key, "Privacy", "lock_delay", delay);
    g_key_file_set_boolean(key, "Privacy", "microphone_blocked", microphone_blocked);
    gsize length = 0;
    char *data = g_key_file_to_data(key, &length, NULL);
    char *path = get_privacy_prefs_path();
    g_file_set_contents(path, data, length, NULL);
    g_free(path);
    g_free(data);
    g_key_file_unref(key);

    start_idle_lock(auto_lock, delay);
    if (g_find_program_in_path("wpctl")) {
        char *mute_argv[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@",
            microphone_blocked ? "1" : "0", NULL };
        run_command_background(mute_argv);
    }
    gtk_label_set_text(GTK_LABEL(st->privacy_status_label), microphone_blocked
        ? "Автоблокировка применена, микрофон выключен"
        : "Параметры конфиденциальности применены");
}

static void on_privacy_changed(GObject *object, GParamSpec *pspec,
    gpointer user_data) {
    (void)object;
    (void)pspec;
    State *st = user_data;
    if (!st->privacy_loading) save_and_apply_privacy(st);
}

static GtkWidget *build_privacy_page(State *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("Конфиденциальность и безопасность"));
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(card, "ps-card");
    st->privacy_lock_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(card), make_search_source_row("Автоматическая блокировка",
        "Блокирует экран при бездействии и перед переходом в сон.",
        st->privacy_lock_switch));
    GtkWidget *delay_title = gtk_label_new("Блокировать через");
    gtk_widget_add_css_class(delay_title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(delay_title), 0.0f);
    st->privacy_lock_delay = gtk_drop_down_new_from_strings((const char *const[]) {
        "1 минуту", "5 минут", "10 минут", "15 минут", "30 минут", NULL });
    gtk_box_append(GTK_BOX(card), delay_title);
    gtk_box_append(GTK_BOX(card), st->privacy_lock_delay);
    st->privacy_microphone_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(card), make_search_source_row("Выключить микрофон",
        "Отключает выбранный микрофон через PipeWire.",
        st->privacy_microphone_switch));
    gtk_box_append(GTK_BOX(box), card);
    st->privacy_status_label = gtk_label_new("Параметры применяются сразу");
    gtk_widget_add_css_class(st->privacy_status_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->privacy_status_label), 0.0f);
    gtk_box_append(GTK_BOX(box), st->privacy_status_label);

    gboolean auto_lock, microphone_blocked;
    guint delay;
    load_privacy_prefs(&auto_lock, &delay, &microphone_blocked);
    const guint values[] = { 60, 300, 600, 900, 1800 };
    guint selected = 1;
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) if (values[i] == delay) selected = i;
    st->privacy_loading = TRUE;
    gtk_switch_set_active(GTK_SWITCH(st->privacy_lock_switch), auto_lock);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->privacy_lock_delay), selected);
    gtk_switch_set_active(GTK_SWITCH(st->privacy_microphone_switch), microphone_blocked);
    st->privacy_loading = FALSE;
    g_signal_connect(st->privacy_lock_switch, "notify::active",
        G_CALLBACK(on_privacy_changed), st);
    g_signal_connect(st->privacy_lock_delay, "notify::selected",
        G_CALLBACK(on_privacy_changed), st);
    g_signal_connect(st->privacy_microphone_switch, "notify::active",
        G_CALLBACK(on_privacy_changed), st);
    return box;
}

/* ==================== Система ==================== */

static GtkWidget *build_about_page(void);

static void system_check_child_setup(gpointer data) {
    (void)data;
    setsid();
}

static void stop_system_check(State *st) {
    if (!st->system_process || st->system_installing) return;
    const char *identifier = g_subprocess_get_identifier(st->system_process);
    if (identifier) {
        pid_t pid = (pid_t)g_ascii_strtoll(identifier, NULL, 10);
        if (pid > 1 && kill(-pid, SIGTERM) != 0)
            g_subprocess_send_signal(st->system_process, SIGTERM);
    }
}

static void on_system_cancel(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    if (st->system_installing) return;
    if (!st->system_updates_running) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_install_revealer), FALSE);
        gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_results_revealer), FALSE);
        gtk_widget_set_sensitive(st->system_install_button, FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        gtk_label_set_text(GTK_LABEL(st->system_updates_label),
            "Проверка выполняется только по кнопке");
        return;
    }
    st->system_updates_cancelled = TRUE;
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_label_set_text(GTK_LABEL(st->system_updates_label), "Отменяем проверку…");
    stop_system_check(st);
}

static gboolean on_settings_close(GtkWindow *window, gpointer user_data) {
    (void)window;
    State *st = user_data;
    stop_system_check(st);
    g_clear_object(&st->system_process);
    return FALSE;
}

static void on_system_updates_finished(GObject *source, GAsyncResult *result,
    gpointer user_data) {
    State *st = user_data;
    char *output = NULL;
    char *error_output = NULL;
    GError *error = NULL;
    gboolean received = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &output, &error_output, &error);
    if (st->system_process != G_SUBPROCESS(source)) {
        g_clear_error(&error);
        g_free(error_output);
        g_free(output);
        return;
    }
    g_clear_object(&st->system_process);
    st->system_updates_running = FALSE;
    gtk_widget_set_sensitive(st->system_updates_button, TRUE);
    gtk_widget_set_sensitive(st->system_cancel_button, FALSE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_install_revealer), FALSE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_results_revealer), FALSE);
    clear_box(st->system_updates_list);
    int exit_status = received && g_subprocess_get_if_exited(G_SUBPROCESS(source))
        ? g_subprocess_get_exit_status(G_SUBPROCESS(source)) : -1;
    if (st->system_updates_cancelled) {
        gtk_label_set_text(GTK_LABEL(st->system_updates_label), "Проверка отменена");
    } else if (exit_status == 2) {
        gtk_label_set_text(GTK_LABEL(st->system_updates_label),
            "Обновлений нет");
    } else if (!received || exit_status != 0) {
        gtk_label_set_text(GTK_LABEL(st->system_updates_label),
            error_output && *error_output ? error_output : "Не удалось проверить обновления");
    } else if (!output || !*output) {
        gtk_label_set_text(GTK_LABEL(st->system_updates_label),
            "Система обновлена");
    } else {
        char **lines = g_strsplit(output, "\n", -1);
        guint count = 0;
        for (guint i = 0; lines[i]; i++) {
            g_strstrip(lines[i]);
            if (!lines[i][0]) continue;
            GtkWidget *line = gtk_label_new(lines[i]);
            gtk_label_set_xalign(GTK_LABEL(line), 0.0f);
            gtk_label_set_selectable(GTK_LABEL(line), TRUE);
            gtk_label_set_ellipsize(GTK_LABEL(line), PANGO_ELLIPSIZE_END);
            gtk_widget_set_tooltip_text(line, lines[i]);
            gtk_box_append(GTK_BOX(st->system_updates_list), line);
            count++;
        }
        char *status = g_strdup_printf("Доступно обновлений: %u", count);
        gtk_label_set_text(GTK_LABEL(st->system_updates_label), status);
        gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_install_revealer), count > 0);
        gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_results_revealer), count > 0);
        gtk_widget_set_sensitive(st->system_cancel_button, count > 0);
        g_free(status);
        g_strfreev(lines);
    }
    g_clear_error(&error);
    g_free(error_output);
    g_free(output);
}

static void on_system_check_updates(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    if (st->system_updates_running) return;
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_install_revealer), FALSE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_results_revealer), FALSE);
    gtk_widget_set_sensitive(st->system_cancel_button, FALSE);
    gtk_label_set_text(GTK_LABEL(st->system_updates_label), "Проверяем обновления…");
    GError *error = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_child_setup(launcher, system_check_child_setup, NULL, NULL);
    GSubprocess *process = g_subprocess_launcher_spawn(launcher, &error, "checkupdates", NULL);
    g_object_unref(launcher);
    if (!process) {
        gtk_label_set_text(GTK_LABEL(st->system_updates_label),
            "Установи пакет pacman-contrib для проверки обновлений");
        g_clear_error(&error);
        return;
    }
    st->system_updates_running = TRUE;
    st->system_updates_cancelled = FALSE;
    gtk_widget_set_sensitive(st->system_install_button, TRUE);
    st->system_process = g_object_ref(process);
    st->system_updates_button = GTK_WIDGET(button);
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_set_sensitive(st->system_cancel_button, TRUE);
    g_subprocess_communicate_utf8_async(process, NULL, NULL,
        on_system_updates_finished, st);
    g_object_unref(process);
}

static void on_system_install_finished(GObject *source, GAsyncResult *result,
    gpointer user_data) {
    State *st = user_data;
    GError *error = NULL;
    g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
    g_clear_error(&error);
    if (st->system_process != G_SUBPROCESS(source)) return;
    g_clear_object(&st->system_process);
    st->system_installing = FALSE;
    st->system_updates_running = FALSE;
    gtk_widget_set_sensitive(st->system_updates_button, TRUE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_install_revealer), FALSE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->system_results_revealer), FALSE);
    gtk_label_set_text(GTK_LABEL(st->system_updates_label),
        "Окно обновления закрыто. Проверь обновления ещё раз.");
}

static void on_system_install(GtkButton *button, gpointer user_data) {
    State *st = user_data;
    if (st->system_updates_running) return;
    GError *error = NULL;
    GSubprocess *process = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error,
        "kitty", "--title", "Обновление системы — Plume", "sh", "-c",
        "sudo pacman -Syu; result=$?; printf '\\nКод завершения: %s\\nНажми Enter, чтобы закрыть окно.\\n' \"$result\"; "
        "read -r answer; exit \"$result\"", NULL);
    if (!process) {
        gtk_label_set_text(GTK_LABEL(st->system_updates_label),
            "Не удалось открыть терминал Kitty для обновления");
        g_clear_error(&error);
        return;
    }
    st->system_process = g_object_ref(process);
    st->system_installing = TRUE;
    st->system_updates_running = TRUE;
    gtk_widget_set_sensitive(st->system_updates_button, FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_set_sensitive(st->system_cancel_button, FALSE);
    gtk_label_set_text(GTK_LABEL(st->system_updates_label),
        "Подтверди обновление в терминале. Отмена проверки доступна до начала установки.");
    g_subprocess_wait_async(process, NULL, on_system_install_finished, st);
    g_object_unref(process);
}

static GtkWidget *build_system_page(State *st) {
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    gtk_widget_add_css_class(box, "ps-page");
    gtk_box_append(GTK_BOX(box), make_section_title("О системе"));
    gtk_box_append(GTK_BOX(box), build_about_page());
    GtkWidget *updates = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(updates, "ps-card");
    GtkWidget *title = gtk_label_new("Обновления");
    gtk_widget_add_css_class(title, "ps-card-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    st->system_updates_label = gtk_label_new("Проверка выполняется только по кнопке");
    gtk_widget_add_css_class(st->system_updates_label, "ps-muted");
    gtk_label_set_xalign(GTK_LABEL(st->system_updates_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(st->system_updates_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(st->system_updates_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(st->system_updates_label), 60);
    GtkWidget *check = gtk_button_new_with_label("Проверить обновления");
    st->system_updates_button = check;
    gtk_widget_add_css_class(check, "ps-btn-primary");
    gtk_widget_set_halign(check, GTK_ALIGN_START);
    g_signal_connect(check, "clicked", G_CALLBACK(on_system_check_updates), st);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    st->system_install_button = gtk_button_new_with_label("Обновить");
    gtk_widget_add_css_class(st->system_install_button, "ps-btn-primary");
    st->system_install_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(st->system_install_revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(st->system_install_revealer), 240);
    gtk_revealer_set_child(GTK_REVEALER(st->system_install_revealer), st->system_install_button);
    g_signal_connect(st->system_install_button, "clicked", G_CALLBACK(on_system_install), st);
    st->system_cancel_button = gtk_button_new_with_label("Отмена");
    gtk_widget_add_css_class(st->system_cancel_button, "ps-btn-danger");
    gtk_widget_set_sensitive(st->system_cancel_button, FALSE);
    g_signal_connect(st->system_cancel_button, "clicked", G_CALLBACK(on_system_cancel), st);
    gtk_box_append(GTK_BOX(actions), check);
    gtk_box_append(GTK_BOX(actions), st->system_install_revealer);
    gtk_box_append(GTK_BOX(actions), st->system_cancel_button);
    st->system_updates_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *list_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(list_scroll), 240);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(list_scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scroll), st->system_updates_list);
    st->system_results_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(st->system_results_revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(st->system_results_revealer), 300);
    gtk_revealer_set_child(GTK_REVEALER(st->system_results_revealer), list_scroll);
    gtk_box_append(GTK_BOX(updates), title);
    gtk_box_append(GTK_BOX(updates), st->system_updates_label);
    gtk_box_append(GTK_BOX(updates), actions);
    gtk_box_append(GTK_BOX(updates), st->system_results_revealer);
    gtk_box_append(GTK_BOX(box), updates);
    return scroll;
}

static GtkWidget *build_about_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_box_append(GTK_BOX(box), make_section_title("О Plume"));

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
    gtk_label_set_wrap(GTK_LABEL(ver), TRUE);
    gtk_box_append(GTK_BOX(htxt), name);
    gtk_box_append(GTK_BOX(htxt), ver);
    gtk_box_append(GTK_BOX(hero), htxt);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "ps-card");

    char *os_name = read_os_pretty_name();
    struct utsname u;
    const char *kernel = (uname(&u) == 0) ? u.release : "неизвестно";

    gtk_box_append(GTK_BOX(card), make_distro_row(os_name));
    gtk_box_append(GTK_BOX(card), make_info_row("Ядро", kernel));
    gtk_box_append(GTK_BOX(card), make_info_row("Композитор", "Wayfire (wlroots · Wayland)"));
    gtk_box_append(GTK_BOX(card), make_info_row("Компоненты", "Waybar · Mako · GTK4"));
    gtk_box_append(GTK_BOX(card), make_info_row("Репозиторий", "github.com/etern4llyfunny/plume"));
    g_free(os_name);

    gtk_box_append(GTK_BOX(box), card);
    return box;
}

static void on_nav_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    GtkWidget *nav = GTK_WIDGET(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    char *needle = g_utf8_casefold(text ? text : "", -1);

    for (GtkWidget *row = gtk_widget_get_first_child(nav); row;
         row = gtk_widget_get_next_sibling(row)) {
        const char *label = g_object_get_data(G_OBJECT(row), "search-label");
        gboolean visible = !needle[0] || (label && g_strstr_len(label, -1, needle));
        gtk_widget_set_visible(row, visible);
    }
    g_free(needle);
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
    g_object_set_data_full(G_OBJECT(row), "search-label",
        g_utf8_casefold(label_txt, -1), g_free);
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
    gtk_window_set_default_size(GTK_WINDOW(st->win), 1040, 700);

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(st->win), root_box);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(sidebar, "ps-sidebar");
    gtk_widget_set_size_request(sidebar, 270, -1);
    gtk_box_append(GTK_BOX(root_box), sidebar);

    GtkWidget *sidebar_title = gtk_label_new("Настройки");
    gtk_widget_add_css_class(sidebar_title, "ps-sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(sidebar_title), 0.0f);
    gtk_box_append(GTK_BOX(sidebar), sidebar_title);

    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_add_css_class(search, "ps-nav-search");
    g_object_set(search, "placeholder-text", "Поиск настроек", NULL);
    gtk_box_append(GTK_BOX(sidebar), search);

    GtkWidget *nav = gtk_list_box_new();
    gtk_widget_add_css_class(nav, "ps-nav");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(nav), GTK_SELECTION_SINGLE);
    add_nav_item(nav, "󰤨", "Wi-Fi", "wifi");
    add_nav_item(nav, "󰈀", "Сеть", "network");
    add_nav_item(nav, "󰂯", "Bluetooth", "bluetooth");
    add_nav_item(nav, "󰍹", "Дисплеи", "displays");
    add_nav_item(nav, "󰕾", "Звук", "sound");
    add_nav_item(nav, "󰁹", "Питание", "power");
    add_nav_item(nav, "󰕮", "Многозадачность", "multitasking");
    add_nav_item(nav, "󰏘", "Внешний вид", "appearance");
    add_nav_item(nav, "󰀻", "Приложения", "applications");
    add_nav_item(nav, "󰂚", "Уведомления", "notifications");
    add_nav_item(nav, "󰍉", "Поиск", "search");
    add_nav_item(nav, "󰒡", "Благополучие", "wellbeing");
    add_nav_item(nav, "󰍽", "Мышь и сенсорная панель", "mouse");
    add_nav_item(nav, "󰌌", "Клавиатура", "keyboard");
    add_nav_item(nav, "󰏘", "Управление цветом", "color");
    add_nav_item(nav, "󰐪", "Принтеры", "printers");
    add_nav_item(nav, "󰖚", "Специальные возможности", "accessibility");
    add_nav_item(nav, "󰌾", "Конфиденциальность и безопасность", "privacy");
    add_nav_item(nav, "󰒓", "О системе", "system");

    GtkWidget *nav_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nav_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(nav_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(nav_scroll), nav);
    gtk_box_append(GTK_BOX(sidebar), nav_scroll);

    st->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(st->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(st->stack), 140);
    gtk_widget_set_hexpand(st->stack, TRUE);
    gtk_widget_set_vexpand(st->stack, TRUE);

    gtk_stack_add_named(GTK_STACK(st->stack), build_wifi_page(st), "wifi");
    gtk_stack_add_named(GTK_STACK(st->stack), build_network_page(st), "network");
    gtk_stack_add_named(GTK_STACK(st->stack), build_bluetooth_page(st), "bluetooth");
    gtk_stack_add_named(GTK_STACK(st->stack), build_displays_page(st), "displays");
    gtk_stack_add_named(GTK_STACK(st->stack), build_sound_page(st), "sound");
    gtk_stack_add_named(GTK_STACK(st->stack), build_power_page(st), "power");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_multitasking_page(st), "multitasking");
    gtk_stack_add_named(GTK_STACK(st->stack), build_appearance_page(st), "appearance");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_applications_page(st), "applications");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_notifications_page(st), "notifications");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_search_page(st), "search");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_wellbeing_page(st), "wellbeing");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_mouse_page(st), "mouse");
    gtk_stack_add_named(GTK_STACK(st->stack), build_keyboard_page(st), "keyboard");
    gtk_stack_add_named(GTK_STACK(st->stack), build_color_page(st), "color");
    gtk_stack_add_named(GTK_STACK(st->stack), build_printers_page(st), "printers");
    gtk_stack_add_named(GTK_STACK(st->stack),
        build_accessibility_page(st), "accessibility");
    gtk_stack_add_named(GTK_STACK(st->stack), build_privacy_page(st), "privacy");
    gtk_stack_add_named(GTK_STACK(st->stack), build_system_page(st), "system");
    gtk_box_append(GTK_BOX(root_box), st->stack);

    apply_window_css(st);

    g_signal_connect(nav, "row-selected", G_CALLBACK(on_nav_selected), st);
    g_signal_connect(search, "search-changed", G_CALLBACK(on_nav_search_changed), nav);
    g_signal_connect(st->win, "notify::is-active",
        G_CALLBACK(on_settings_active_changed), st);
    g_signal_connect(st->win, "close-request", G_CALLBACK(on_settings_close), st);
    gtk_list_box_select_row(GTK_LIST_BOX(nav), gtk_list_box_get_row_at_index(GTK_LIST_BOX(nav), 7));

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
