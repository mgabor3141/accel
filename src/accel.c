#include <pebble.h>

#define SAMPLES_PER_TICK 25
#define BUFFER 3

static Window *window;

static TextLayer *text_layer;
static TextLayer *upper_text_layer;

static GBitmap *bg_image;

static BitmapLayer *bg_layer;

static int recording = 0;

static int16_t appmessage_data[BUFFER*SAMPLES_PER_TICK*3];
static int appmessage_iterator = 0;

static int data_iterator = 0;

static char uid[50];

void sendNextPart(DictionaryIterator *iter) {
    app_message_outbox_begin(&iter);

    int e;
    if ((e = dict_write_int32(iter, 1, data_iterator)) != DICT_OK) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Dictionary write error (1): #%d", e);
        sendNextPart(NULL);
        return;
    }

    if ((e = dict_write_data(iter, 2, (uint8_t*)appmessage_data, BUFFER*SAMPLES_PER_TICK*3*2)) != DICT_OK)
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Dictionary write error (2): #%d", e);

    appmessage_iterator = 0;
    data_iterator++;

    app_message_outbox_send();
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Data: %s", appmessage_string);
}

void sendEOF(DictionaryIterator *iter) {    
    app_message_outbox_begin(&iter);

    if (dict_write_int32(iter, 1, -1) != DICT_OK) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Dictionary write error (EOF)");
        sendEOF(NULL);
        return;
    }

    app_message_outbox_send();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] EOF sent");

    text_layer_set_text(text_layer, "REC");
}

void accel_data_handler(AccelData *data, uint32_t num_samples) {
    if (!recording) return;

    uint32_t i = 0;

    for (i = 0; i < num_samples; i++) {
        appmessage_data[appmessage_iterator] = data[i].x + 5000;
        appmessage_iterator++;
        appmessage_data[appmessage_iterator] = data[i].y + 5000;
        appmessage_iterator++;
        appmessage_data[appmessage_iterator] = data[i].z + 5000;
        appmessage_iterator++;
    }

    if (appmessage_iterator%(BUFFER*SAMPLES_PER_TICK*3) == 0) sendNextPart(NULL);

    static char progress[] = "Record: 12345678";
    snprintf(progress, 18, "%ds", data_iterator*BUFFER/4);
    text_layer_set_text(text_layer, progress);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (recording) {
        recording = 0;
        data_iterator = 0;
        appmessage_iterator = 0;
        sendEOF(NULL);
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recording stopped");
    } else {
        recording = 1;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recording started");
    }
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Send successful");
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
    if (recording) sendNextPart(NULL);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Send failed: #%d", reason);
}

void in_received_handler(DictionaryIterator *received, void *context) {
    Tuple* tuple = dict_find(received, 0);
    if (!tuple) return;
    snprintf(uid, 50, "%s", tuple->value->cstring);
    text_layer_set_text(upper_text_layer, uid);
    text_layer_set_text(text_layer, "REC");
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Receive successful");
}

void in_dropped_handler(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recieve failed");
}

static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    bg_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BG);

    bg_layer = bitmap_layer_create(GRect(0, 0, 144, 168));
    bitmap_layer_set_bitmap(bg_layer, bg_image);
    bitmap_layer_set_alignment(bg_layer, GAlignTopLeft);
    layer_add_child(window_layer, bitmap_layer_get_layer(bg_layer));

    upper_text_layer = text_layer_create((GRect) { .origin = { 0, 4 }, .size = { bounds.size.w, 34 } });
    text_layer_set_text_alignment(upper_text_layer, GTextAlignmentCenter);
    text_layer_set_background_color(upper_text_layer, GColorBlack);
    text_layer_set_text_color(upper_text_layer, GColorWhite);
    text_layer_set_text(upper_text_layer, "Connecting to phone...");
    layer_add_child(window_layer, text_layer_get_layer(upper_text_layer));

    text_layer = text_layer_create((GRect) { .origin = { 53, 78 }, .size = { bounds.size.w - 53 - 18, 36 } });
    text_layer_set_text_alignment(text_layer, GTextAlignmentRight);
    text_layer_set_background_color(text_layer, GColorBlack);
    text_layer_set_text_color(text_layer, GColorWhite);
    text_layer_set_text(text_layer, "");
    layer_add_child(window_layer, text_layer_get_layer(text_layer));

    accel_data_service_subscribe(SAMPLES_PER_TICK, &accel_data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
}

static void window_unload(Window *window) {
    text_layer_destroy(text_layer);
    accel_data_service_unsubscribe();
}

static void init(void) {
  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_fullscreen(window, true);

    app_message_register_inbox_received(in_received_handler);
    app_message_register_inbox_dropped(in_dropped_handler);
    app_message_register_outbox_sent(out_sent_handler);
    app_message_register_outbox_failed(out_failed_handler);

    app_message_open(app_message_outbox_size_maximum(), app_message_outbox_size_maximum()); // TODO

    window_stack_push(window, true);
}

static void deinit(void) {
    window_destroy(window);
}

int main(void) {
    init();

    APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

    app_event_loop();
    deinit();
}
