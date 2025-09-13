/*
Simplified Sub-GHz Toggle Beacon app for Flipper Zero (compatible with broader SDKs)
*/

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>

#define APP_NAME "Not a Jummer"

static const uint32_t DEFAULT_FREQ_HZ = 433920000UL;
static const uint32_t BURST_MS = 100;
static const uint32_t PERIOD_MS = 1000;

typedef struct {
    FuriMessageQueue* input_queue;
    Gui* gui;
    ViewPort* vp;
    bool running;
    uint32_t freq_hz;
    uint32_t step_hz;   // current frequency step
    FuriThread* tx_thread;
} AppState;

static void draw_callback(Canvas* canvas, void* ctx) {
    AppState* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 16, APP_NAME);

    canvas_set_font(canvas, FontSecondary);
    char line[64];
    snprintf(line, sizeof(line), "Freq: %lu.%03lu MHz", app->freq_hz/1000000UL, (app->freq_hz%1000000UL)/1000UL);
    canvas_draw_str(canvas, 4, 32, line);

    snprintf(line, sizeof(line), "Step: %lu Hz", app->step_hz);
    canvas_draw_str(canvas, 4, 40, line);

    canvas_draw_str(canvas, 4, 48, app->running ? "Status: TRANSMITTING" : "Status: idle");
    canvas_draw_str(canvas, 4, 60, "OK: start/stop  ◄/►: step  UP/DOWN: +/- step  BACK: exit");
}

static int32_t tx_worker(void* ctx);

static void input_callback(InputEvent* event, void* context) {
    AppState* app = (AppState*)context;

    if(event->type != InputTypeShort) return;

    switch(event->key) {
    case InputKeyOk:
    if(!app->running) {
        app->running = true;
        if(!app->tx_thread) {
            app->tx_thread = furi_thread_alloc();
            furi_thread_set_name(app->tx_thread, "SubGhzTX");
            furi_thread_set_stack_size(app->tx_thread, 2048); // можно 1024, если хватает
            furi_thread_set_callback(app->tx_thread, tx_worker); // твой worker 

            furi_thread_set_context(app->tx_thread, app);
        }
        furi_thread_start(app->tx_thread);
    } else {
        app->running = false;
        if(app->tx_thread) {
            furi_thread_join(app->tx_thread);
            furi_thread_free(app->tx_thread);
            app->tx_thread = NULL;
        }
    }
    view_port_update(app->vp);
    break;


    case InputKeyBack: {
        InputEvent quit = {.key = InputKeyBack, .type = InputTypeLong};
        furi_message_queue_put(app->input_queue, &quit, FuriWaitForever);
        break;
    }

    case InputKeyLeft:
        if(app->step_hz == 1000) app->step_hz = 1000000;
        else if(app->step_hz == 10000) app->step_hz = 1000;
        else if(app->step_hz == 100000) app->step_hz = 10000;
        else app->step_hz = 100000;
        view_port_update(app->vp);
        break;

    case InputKeyRight:
        if(app->step_hz == 1000) app->step_hz = 10000;
        else if(app->step_hz == 10000) app->step_hz = 100000;
        else if(app->step_hz == 100000) app->step_hz = 1000000;
        else app->step_hz = 1000;
        view_port_update(app->vp);
        break;

    case InputKeyUp:
        app->freq_hz += app->step_hz;
        view_port_update(app->vp);
        break;

    case InputKeyDown:
        if(app->freq_hz > app->step_hz) app->freq_hz -= app->step_hz;
        view_port_update(app->vp);
        break;

    default:
        break;
    }
}


    

static void subghz_send_burst(void) {
    uint32_t end = furi_get_tick() + furi_ms_to_ticks(BURST_MS);
    while(furi_get_tick() < end) {
        furi_hal_subghz_tx();
        furi_delay_us(200);
        furi_hal_subghz_idle();
        furi_delay_us(200);
    }
}

static int32_t tx_worker(void* ctx) {
    AppState* app = ctx;

    furi_hal_subghz_idle();
    furi_hal_subghz_set_frequency(app->freq_hz);

    NotificationApp* notify = furi_record_open(RECORD_NOTIFICATION);

    while(app->running) {
        // Apply current frequency in case the user changed it
        furi_hal_subghz_set_frequency(app->freq_hz);
        notification_message(notify, &sequence_blink_green_100);
        subghz_send_burst();
        furi_hal_subghz_idle();
        furi_delay_ms(PERIOD_MS);
    }

    furi_hal_subghz_idle();
    furi_record_close(RECORD_NOTIFICATION);
    return 0;
}

int32_t not_a_jummer_app(void* p) {
    UNUSED(p);

    AppState app = {0};
    app.freq_hz = DEFAULT_FREQ_HZ;
    app.step_hz = 100000; // default step = 100 kHz

    app.input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app.vp = view_port_alloc();
    view_port_draw_callback_set(app.vp, draw_callback, &app);
    view_port_input_callback_set(app.vp, input_callback, &app);

    app.gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app.gui, app.vp, GuiLayerFullscreen);

    app.tx_thread = NULL;

    while(1) {
        InputEvent event;
        if(furi_message_queue_get(app.input_queue, &event, 50) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeLong) break;
        }
        furi_delay_ms(10);
    }

    if(app.running && app.tx_thread) {
        app.running = false;
        furi_thread_join(app.tx_thread);
        furi_thread_free(app.tx_thread);
        app.tx_thread = NULL;
    }

    gui_remove_view_port(app.gui, app.vp);
    view_port_free(app.vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app.input_queue);

    return 0;
}
