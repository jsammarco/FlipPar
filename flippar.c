#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <storage/storage.h>
#include <furi_hal_rtc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "flippar_icons.h"

#define FLIPPAR_MAX_PLAYERS 4
#define FLIPPAR_MAX_HOLES 27
#define FLIPPAR_NAME_LEN 32
#define FLIPPAR_SAVE_DIR "/ext/apps_data/flippar"
#define FLIPPAR_SAVE_BASENAME "FlipPar"

typedef enum {
    FlipParScreenSplash,
    FlipParScreenSetup,
    FlipParScreenGrid,
} FlipParScreen;

typedef enum {
    FlipParFieldHoles,
    FlipParFieldPlayers,
    FlipParFieldNames,
    FlipParFieldStart,
    FlipParFieldSave,
} FlipParSetupField;

typedef enum {
    FlipParViewMain,
    FlipParViewTextInput,
} FlipParViewId;

typedef enum {
    FlipParCustomEventSplashDone = 1,
} FlipParCustomEvent;

typedef struct {
    uint8_t dummy;
} FlipParViewModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    TextInput* text_input;
    FuriTimer* splash_timer;

    uint8_t holes;
    uint8_t players;
    char player_names[FLIPPAR_MAX_PLAYERS][FLIPPAR_NAME_LEN];
    uint8_t pars[FLIPPAR_MAX_HOLES];
    uint8_t scores[FLIPPAR_MAX_PLAYERS][FLIPPAR_MAX_HOLES];

    FlipParScreen screen;
    FlipParSetupField setup_field;
    uint8_t setup_name_index;

    uint8_t selected_row;
    uint8_t selected_col;
    uint8_t scroll_hole_offset;

    char text_input_buffer[FLIPPAR_NAME_LEN];
    uint8_t editing_player_index;

    bool running;
} FlipParApp;

static FlipParApp* g_flippar_app = NULL;

static void flippar_finish_splash(FlipParApp* app) {
    if(!app || app->screen != FlipParScreenSplash) return;

    if(app->splash_timer) {
        furi_timer_stop(app->splash_timer);
    }

    app->screen = FlipParScreenSetup;
    app->setup_field = FlipParFieldHoles;
}

static void flippar_splash_timer_callback(void* context) {
    FlipParApp* app = context;

    if(app && app->view_dispatcher) {
        view_dispatcher_send_custom_event(app->view_dispatcher, FlipParCustomEventSplashDone);
    }
}

static bool flippar_custom_event_callback(void* context, uint32_t event) {
    FlipParApp* app = context;

    if(event == FlipParCustomEventSplashDone) {
        flippar_finish_splash(app);
        return true;
    }

    return false;
}

static void flippar_init_data(FlipParApp* app) {
    app->holes = 18;
    app->players = 2;

    memset(app->player_names, 0, sizeof(app->player_names));
    snprintf(app->player_names[0], FLIPPAR_NAME_LEN, "P1");
    snprintf(app->player_names[1], FLIPPAR_NAME_LEN, "P2");
    snprintf(app->player_names[2], FLIPPAR_NAME_LEN, "P3");
    snprintf(app->player_names[3], FLIPPAR_NAME_LEN, "P4");

    for(uint8_t h = 0; h < FLIPPAR_MAX_HOLES; h++) {
        app->pars[h] = 3;
        for(uint8_t p = 0; p < FLIPPAR_MAX_PLAYERS; p++) {
            app->scores[p][h] = 0;
        }
    }

    app->screen = FlipParScreenSplash;
    app->setup_field = FlipParFieldHoles;
    app->setup_name_index = 0;
    app->selected_row = 1;
    app->selected_col = 0;
    app->scroll_hole_offset = 0;
    app->editing_player_index = 0;
    memset(app->text_input_buffer, 0, sizeof(app->text_input_buffer));
    app->running = true;
}

static int16_t flippar_total_for_player(FlipParApp* app, uint8_t player) {
    int16_t total = 0;
    for(uint8_t h = 0; h < app->holes; h++) {
        total += app->scores[player][h];
    }
    return total;
}

static int16_t flippar_total_par(FlipParApp* app) {
    int16_t total = 0;
    for(uint8_t h = 0; h < app->holes; h++) {
        total += app->pars[h];
    }
    return total;
}

static void flippar_format_relative_score(int16_t score, int16_t par_total, char* out, size_t out_size) {
    int16_t delta = score - par_total;

    if(delta == 0) {
        snprintf(out, out_size, "E");
    } else {
        snprintf(out, out_size, "%+d", delta);
    }
}

static uint8_t flippar_find_winner(FlipParApp* app) {
    uint8_t winner = 0;
    int16_t best_score = flippar_total_for_player(app, 0);

    for(uint8_t p = 1; p < app->players; p++) {
        int16_t score = flippar_total_for_player(app, p);
        if(score < best_score) {
            best_score = score;
            winner = p;
        }
    }

    return winner;
}

static bool flippar_write_text(File* file, const char* text) {
    size_t len = strlen(text);
    return storage_file_write(file, text, len) == len;
}

static bool flippar_build_save_path(Storage* storage, char* path, size_t path_size) {
    DateTime datetime = {0};
    furi_hal_rtc_get_datetime(&datetime);

    char base_name[64];
    if(datetime.year > 0) {
        snprintf(
            base_name,
            sizeof(base_name),
            "%s_%u-%u-%u",
            FLIPPAR_SAVE_BASENAME,
            datetime.year,
            datetime.month,
            datetime.day);
    } else {
        snprintf(base_name, sizeof(base_name), "%s_unknown-date", FLIPPAR_SAVE_BASENAME);
    }

    snprintf(path, path_size, "%s/%s.txt", FLIPPAR_SAVE_DIR, base_name);
    if(!storage_file_exists(storage, path)) {
        return true;
    }

    for(uint8_t index = 2; index < 100; index++) {
        snprintf(path, path_size, "%s/%s_%02u.txt", FLIPPAR_SAVE_DIR, base_name, index);
        if(!storage_file_exists(storage, path)) {
            return true;
        }
    }

    path[0] = '\0';
    return false;
}

static bool flippar_save_score_sheet(FlipParApp* app) {
    bool success = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(!storage_simply_mkdir(storage, FLIPPAR_SAVE_DIR)) {
        goto cleanup;
    }

    char path[128];
    if(!flippar_build_save_path(storage, path, sizeof(path))) {
        goto cleanup;
    }

    if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        goto cleanup;
    }

    char line[128];
    int16_t par_total = flippar_total_par(app);

    snprintf(line, sizeof(line), "FlipPar by ConsultingJoe\r\n");
    if(!flippar_write_text(file, line)) goto close_file;

    snprintf(line, sizeof(line), "Players:\t%u\r\nHoles:\t%u\r\n", app->players, app->holes);
    if(!flippar_write_text(file, line)) goto close_file;

    snprintf(line, sizeof(line), "Par Total:\t%d\r\n\r\n", par_total);
    if(!flippar_write_text(file, line)) goto close_file;

    if(!flippar_write_text(file, "Hole\tPar")) goto close_file;
    for(uint8_t p = 0; p < app->players; p++) {
        snprintf(line, sizeof(line), "\t%s", app->player_names[p]);
        if(!flippar_write_text(file, line)) goto close_file;
    }
    if(!flippar_write_text(file, "\r\n")) goto close_file;

    for(uint8_t h = 0; h < app->holes; h++) {
        snprintf(line, sizeof(line), "%u\t%u", h + 1, app->pars[h]);
        if(!flippar_write_text(file, line)) goto close_file;

        for(uint8_t p = 0; p < app->players; p++) {
            snprintf(line, sizeof(line), "\t%u", app->scores[p][h]);
            if(!flippar_write_text(file, line)) goto close_file;
        }
        if(!flippar_write_text(file, "\r\n")) goto close_file;
    }

    if(!flippar_write_text(file, "Total\t")) goto close_file;
    snprintf(line, sizeof(line), "%d", par_total);
    if(!flippar_write_text(file, line)) goto close_file;
    for(uint8_t p = 0; p < app->players; p++) {
        snprintf(line, sizeof(line), "\t%d", flippar_total_for_player(app, p));
        if(!flippar_write_text(file, line)) goto close_file;
    }
    if(!flippar_write_text(file, "\r\n")) goto close_file;

    if(!storage_file_sync(file)) goto close_file;
    success = true;

close_file:
    storage_file_close(file);

cleanup:
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

static void flippar_adjust_selected_value(FlipParApp* app, int8_t delta) {
    uint8_t* target = NULL;

    if(app->selected_col >= app->holes) return;
    if(app->selected_row > app->players) return;

    if(app->selected_row == 0) {
        target = &app->pars[app->selected_col];
    } else {
        target = &app->scores[app->selected_row - 1][app->selected_col];
    }

    int16_t value = *target;
    value += delta;

    if(value < 0) value = 0;
    if(value > 15) value = 15;

    *target = (uint8_t)value;
}

static void flippar_draw_setup(Canvas* canvas, FlipParApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "FlipPar Setup");

    canvas_set_font(canvas, FontSecondary);

    char line[64];

    const uint8_t visible_items = 5;
    uint8_t scroll_offset = 0;
    if(app->setup_field >= visible_items) {
        scroll_offset = app->setup_field - visible_items + 1;
    }

    for(uint8_t i = 0; i < visible_items; i++) {
        uint8_t field = scroll_offset + i;
        if(field > FlipParFieldSave) break;

        uint8_t y = 18 + (i * 8);
        if(field == FlipParFieldHoles) {
            snprintf(
                line,
                sizeof(line),
                "%c Holes: %u",
                app->setup_field == field ? '>' : ' ',
                app->holes);
        } else if(field == FlipParFieldPlayers) {
            snprintf(
                line,
                sizeof(line),
                "%c Players: %u",
                app->setup_field == field ? '>' : ' ',
                app->players);
        } else if(field == FlipParFieldNames) {
            snprintf(
                line,
                sizeof(line),
                "%c Name %u: %s",
                app->setup_field == field ? '>' : ' ',
                app->setup_name_index + 1,
                app->player_names[app->setup_name_index]);
        } else if(field == FlipParFieldStart) {
            snprintf(
                line,
                sizeof(line),
                "%c Start Round",
                app->setup_field == field ? '>' : ' ');
        } else {
            snprintf(
                line,
                sizeof(line),
                "%c Save Score Sheet",
                app->setup_field == field ? '>' : ' ');
        }

        canvas_draw_str(canvas, 2, y, line);
    }

    canvas_draw_line(canvas, 2, 54, 126, 54);
    canvas_draw_str(canvas, 2, 62, "By ConsultingJoe.com");
}

static void flippar_draw_splash(Canvas* canvas) {
    canvas_clear(canvas);
    canvas_draw_icon(canvas, 0, 0, &I_flippar_splash_128x64);
}

static void flippar_draw_grid(Canvas* canvas, FlipParApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "FlipPar");

    const int16_t par_total = flippar_total_par(app);
    canvas_set_font(canvas, FontSecondary);
    if(app->players > 0) {
        uint8_t winner = flippar_find_winner(app);
        int16_t winner_score = flippar_total_for_player(app, winner);
        char relative_score[8];
        char summary[32];
        char short_name[7];

        memset(short_name, 0, sizeof(short_name));
        strncpy(short_name, app->player_names[winner], sizeof(short_name) - 1);
        flippar_format_relative_score(winner_score, par_total, relative_score, sizeof(relative_score));
        snprintf(summary, sizeof(summary), "%s %s", short_name, relative_score);
        canvas_draw_str(canvas, 78, 10, summary);
    }

    const uint8_t visible_cols = 4;
    const uint8_t total_col = app->holes;
    const uint8_t total_columns = app->holes + 1;
    uint8_t max_scroll_offset = 0;
    if(total_columns > visible_cols) {
        max_scroll_offset = total_columns - visible_cols;
    }

    if(app->selected_col < app->scroll_hole_offset) {
        app->scroll_hole_offset = app->selected_col;
    }
    if(app->selected_col >= app->scroll_hole_offset + visible_cols) {
        app->scroll_hole_offset = app->selected_col - visible_cols + 1;
    }
    if(app->selected_col == (app->holes - 1) && app->scroll_hole_offset < max_scroll_offset) {
        app->scroll_hole_offset = max_scroll_offset;
    }
    if(app->scroll_hole_offset > max_scroll_offset) {
        app->scroll_hole_offset = max_scroll_offset;
    }

    const uint8_t start_x = 2;
    const uint8_t start_y = 16;
    const uint8_t col_w = 24;
    const uint8_t row_h = 10;

    char par_total_label[8];
    snprintf(par_total_label, sizeof(par_total_label), "%d", par_total);
    canvas_draw_str(canvas, start_x, start_y + 8, par_total_label);

    for(uint8_t i = 0; i < visible_cols; i++) {
        uint8_t col = app->scroll_hole_offset + i;
        if(col >= total_columns) break;

        char header_label[8];
        if(col == total_col) {
            snprintf(header_label, sizeof(header_label), "TOT");
        } else {
            snprintf(header_label, sizeof(header_label), "H%u", col + 1);
        }
        canvas_draw_str(canvas, start_x + 22 + (i * col_w), start_y + 8, header_label);
    }

    for(uint8_t row = 0; row < app->players + 1; row++) {
        uint8_t y = start_y + 12 + (row * row_h);

        if(row == 0) {
            canvas_draw_str(canvas, start_x, y + 8, "Par");
        } else {
            char short_name[7];
            memset(short_name, 0, sizeof(short_name));
            strncpy(short_name, app->player_names[row - 1], sizeof(short_name) - 1);
            canvas_draw_str(canvas, start_x, y + 8, short_name);
        }

        for(uint8_t i = 0; i < visible_cols; i++) {
            uint8_t col = app->scroll_hole_offset + i;
            if(col >= total_columns) break;

            uint8_t x = start_x + 22 + (i * col_w);

            char value[8];
            if(col == total_col) {
                int16_t total = (row == 0) ? par_total : flippar_total_for_player(app, row - 1);
                snprintf(value, sizeof(value), "%d", total);
            } else {
                uint8_t v = (row == 0) ? app->pars[col] : app->scores[row - 1][col];
                snprintf(value, sizeof(value), "%u", v);
            }

            bool selected = (col < app->holes) && (app->selected_row == row) &&
                            (app->selected_col == col);
            if(selected) {
                canvas_draw_frame(canvas, x - 2, y, 16, 10);
            }

            canvas_draw_str(canvas, x + 2, y + 8, value);
        }
    }
}

static void flippar_main_view_draw(Canvas* canvas, void* model) {
    UNUSED(model);

    FlipParApp* app = g_flippar_app;
    if(!app) return;

    if(app->screen == FlipParScreenSplash) {
        flippar_draw_splash(canvas);
    } else if(app->screen == FlipParScreenSetup) {
        flippar_draw_setup(canvas, app);
    } else {
        flippar_draw_grid(canvas, app);
    }
}

static uint32_t flippar_main_view_previous(void* context) {
    FlipParApp* app = context;

    if(app->screen == FlipParScreenGrid) {
        app->screen = FlipParScreenSetup;
        return FlipParViewMain;
    }

    app->running = false;
    view_dispatcher_stop(app->view_dispatcher);
    return VIEW_NONE;
}

static void flippar_name_input_done(void* context) {
    FlipParApp* app = context;

    strncpy(
        app->player_names[app->editing_player_index],
        app->text_input_buffer,
        FLIPPAR_NAME_LEN - 1);
    app->player_names[app->editing_player_index][FLIPPAR_NAME_LEN - 1] = '\0';

    if(app->player_names[app->editing_player_index][0] == '\0') {
        snprintf(
            app->player_names[app->editing_player_index],
            FLIPPAR_NAME_LEN,
            "P%u",
            app->editing_player_index + 1);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipParViewMain);
}

static void flippar_open_name_editor(FlipParApp* app, uint8_t player_index) {
    app->editing_player_index = player_index;

    memset(app->text_input_buffer, 0, sizeof(app->text_input_buffer));
    strncpy(
        app->text_input_buffer,
        app->player_names[player_index],
        FLIPPAR_NAME_LEN - 1);

    text_input_set_header_text(app->text_input, "Player Name");
    text_input_set_result_callback(
        app->text_input,
        flippar_name_input_done,
        app,
        app->text_input_buffer,
        FLIPPAR_NAME_LEN,
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipParViewTextInput);
}

static bool flippar_main_view_input(InputEvent* event, void* context) {
    FlipParApp* app = context;

    if(!app) return false;

    if(app->screen == FlipParScreenSplash) {
        if(event->type == InputTypeShort || event->type == InputTypeRepeat ||
           event->type == InputTypeLong) {
            flippar_finish_splash(app);
            return true;
        }
        return false;
    }

    if(app->screen == FlipParScreenSetup) {
        if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

        if(event->key == InputKeyUp) {
            if(app->setup_field > 0) app->setup_field--;
            return true;
        } else if(event->key == InputKeyDown) {
            if(app->setup_field < FlipParFieldSave) app->setup_field++;
            return true;
        } else if(event->key == InputKeyLeft) {
            if(app->setup_field == FlipParFieldHoles && app->holes > 1) {
                app->holes--;
            } else if(app->setup_field == FlipParFieldPlayers && app->players > 1) {
                app->players--;
                if(app->setup_name_index >= app->players) {
                    app->setup_name_index = app->players - 1;
                }
            } else if(app->setup_field == FlipParFieldNames && app->setup_name_index > 0) {
                app->setup_name_index--;
            }
            return true;
        } else if(event->key == InputKeyRight) {
            if(app->setup_field == FlipParFieldHoles && app->holes < FLIPPAR_MAX_HOLES) {
                app->holes++;
            } else if(app->setup_field == FlipParFieldPlayers && app->players < FLIPPAR_MAX_PLAYERS) {
                app->players++;
            } else if(
                app->setup_field == FlipParFieldNames &&
                (app->setup_name_index + 1) < app->players) {
                app->setup_name_index++;
            }
            return true;
        } else if(event->key == InputKeyOk) {
            if(app->setup_field == FlipParFieldNames) {
                flippar_open_name_editor(app, app->setup_name_index);
            } else if(app->setup_field == FlipParFieldStart) {
                app->screen = FlipParScreenGrid;
                app->selected_row = 1;
                app->selected_col = 0;
            } else if(app->setup_field == FlipParFieldSave) {
                flippar_save_score_sheet(app);
            }
            return true;
        }
    } else {
        if(event->type != InputTypeShort && event->type != InputTypeRepeat &&
           event->type != InputTypeLong) {
            return false;
        }

        if(event->key == InputKeyLeft) {
            if(app->selected_col > 0) app->selected_col--;
            return true;
        } else if(event->key == InputKeyRight) {
            if(app->selected_col + 1 < app->holes) app->selected_col++;
            return true;
        } else if(event->key == InputKeyUp) {
            if(app->selected_row > 0) app->selected_row--;
            return true;
        } else if(event->key == InputKeyDown) {
            if(app->selected_row < app->players) app->selected_row++;
            return true;
        } else if(event->key == InputKeyOk) {
            if(event->type == InputTypeLong) {
                flippar_adjust_selected_value(app, -1);
                return true;
            } else if(event->type == InputTypeShort) {
                flippar_adjust_selected_value(app, 1);
                return true;
            }
            return false;
        }
    }

    return false;
}

int32_t flippar_app(void* p) {
    UNUSED(p);

    FlipParApp* app = malloc(sizeof(FlipParApp));
    if(!app) return -1;
    memset(app, 0, sizeof(FlipParApp));

    g_flippar_app = app;
    flippar_init_data(app);

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, flippar_custom_event_callback);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    app->main_view = view_alloc();
    view_allocate_model(app->main_view, ViewModelTypeLockFree, sizeof(FlipParViewModel));
    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, flippar_main_view_draw);
    view_set_input_callback(app->main_view, flippar_main_view_input);
    view_set_previous_callback(app->main_view, flippar_main_view_previous);
    view_dispatcher_add_view(app->view_dispatcher, FlipParViewMain, app->main_view);

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        FlipParViewTextInput,
        text_input_get_view(app->text_input));

    app->splash_timer = furi_timer_alloc(flippar_splash_timer_callback, FuriTimerTypeOnce, app);
    if(app->splash_timer) {
        furi_timer_start(app->splash_timer, furi_ms_to_ticks(1200));
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipParViewMain);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, FlipParViewTextInput);
    text_input_free(app->text_input);

    if(app->splash_timer) {
        furi_timer_stop(app->splash_timer);
        furi_timer_free(app->splash_timer);
    }

    view_dispatcher_remove_view(app->view_dispatcher, FlipParViewMain);
    view_free(app->main_view);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    g_flippar_app = NULL;
    free(app);

    return 0;
}
