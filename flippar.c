#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define FLIPPAR_MAX_PLAYERS 4
#define FLIPPAR_MAX_HOLES 27
#define FLIPPAR_NAME_LEN 32

typedef enum {
    FlipParScreenSetup,
    FlipParScreenGrid,
} FlipParScreen;

typedef enum {
    FlipParFieldHoles,
    FlipParFieldPlayers,
    FlipParFieldPars,
    FlipParFieldNames,
    FlipParFieldStart,
} FlipParSetupField;

typedef enum {
    FlipParViewMain,
    FlipParViewTextInput,
} FlipParViewId;

typedef struct {
    uint8_t dummy;
} FlipParViewModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    TextInput* text_input;

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

    app->screen = FlipParScreenSetup;
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

    snprintf(
        line,
        sizeof(line),
        "%c Holes: %u",
        app->setup_field == FlipParFieldHoles ? '>' : ' ',
        app->holes);
    canvas_draw_str(canvas, 2, 24, line);

    snprintf(
        line,
        sizeof(line),
        "%c Players: %u",
        app->setup_field == FlipParFieldPlayers ? '>' : ' ',
        app->players);
    canvas_draw_str(canvas, 2, 34, line);

    snprintf(
        line,
        sizeof(line),
        "%c Edit Pars in Grid",
        app->setup_field == FlipParFieldPars ? '>' : ' ');
    canvas_draw_str(canvas, 2, 44, line);

    snprintf(
        line,
        sizeof(line),
        "%c Name %u",
        app->setup_field == FlipParFieldNames ? '>' : ' ',
        app->setup_name_index + 1);
    canvas_draw_str(canvas, 2, 54, line);

    snprintf(
        line,
        sizeof(line),
        "%c Start Round",
        app->setup_field == FlipParFieldStart ? '>' : ' ');
    canvas_draw_str(canvas, 2, 64, line);

    if(app->setup_field == FlipParFieldNames) {
        canvas_draw_str(canvas, 64, 54, app->player_names[app->setup_name_index]);
    }
}

static void flippar_draw_grid(Canvas* canvas, FlipParApp* app) {
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "FlipPar");

    char summary[32];
    snprintf(summary, sizeof(summary), "Par %d", flippar_total_par(app));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 84, 10, summary);

    const uint8_t visible_holes = 4;
    if(app->selected_col < app->scroll_hole_offset) {
        app->scroll_hole_offset = app->selected_col;
    }
    if(app->selected_col >= app->scroll_hole_offset + visible_holes) {
        app->scroll_hole_offset = app->selected_col - visible_holes + 1;
    }

    const uint8_t start_x = 2;
    const uint8_t start_y = 16;
    const uint8_t col_w = 24;
    const uint8_t row_h = 10;

    canvas_draw_str(canvas, start_x, start_y + 8, "Row");

    for(uint8_t i = 0; i < visible_holes; i++) {
        uint8_t hole = app->scroll_hole_offset + i;
        if(hole >= app->holes) break;

        char hole_label[8];
        snprintf(hole_label, sizeof(hole_label), "H%u", hole + 1);
        canvas_draw_str(canvas, start_x + 22 + (i * col_w), start_y + 8, hole_label);
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

        for(uint8_t i = 0; i < visible_holes; i++) {
            uint8_t hole = app->scroll_hole_offset + i;
            if(hole >= app->holes) break;

            uint8_t x = start_x + 22 + (i * col_w);

            char value[8];
            uint8_t v = (row == 0) ? app->pars[hole] : app->scores[row - 1][hole];
            snprintf(value, sizeof(value), "%u", v);

            bool selected = (app->selected_row == row) && (app->selected_col == hole);
            if(selected) {
                canvas_draw_frame(canvas, x - 2, y, 16, 10);
            }

            canvas_draw_str(canvas, x + 2, y + 8, value);
        }
    }

    if(app->players > 0) {
        char footer[64];
        int16_t ptotal = flippar_total_for_player(app, 0);
        int16_t par_total = flippar_total_par(app);
        snprintf(
            footer,
            sizeof(footer),
            "%s:%d %+d",
            app->player_names[0],
            ptotal,
            ptotal - par_total);
        canvas_draw_str(canvas, 2, 63, footer);
    }
}

static void flippar_main_view_draw(Canvas* canvas, void* model) {
    UNUSED(model);

    FlipParApp* app = g_flippar_app;
    if(!app) return;

    if(app->screen == FlipParScreenSetup) {
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

    if(app->screen == FlipParScreenSetup) {
        if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

        if(event->key == InputKeyUp) {
            if(app->setup_field > 0) app->setup_field--;
            return true;
        } else if(event->key == InputKeyDown) {
            if(app->setup_field < FlipParFieldStart) app->setup_field++;
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
            } else if(app->setup_field == FlipParFieldPars) {
                app->screen = FlipParScreenGrid;
                app->selected_row = 0;
                app->selected_col = 0;
            } else if(app->setup_field == FlipParFieldStart) {
                app->screen = FlipParScreenGrid;
                app->selected_row = 1;
                app->selected_col = 0;
            }
            return true;
        }
    } else {
        if(
            event->type != InputTypeShort && event->type != InputTypeRepeat &&
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
            } else {
                flippar_adjust_selected_value(app, 1);
            }
            return true;
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
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

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

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipParViewMain);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, FlipParViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, FlipParViewMain);
    view_free(app->main_view);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    g_flippar_app = NULL;
    free(app);

    return 0;
}