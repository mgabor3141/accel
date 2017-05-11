#include <pebble.h>

#define SAMPLES_PER_TICK 25
#define BUFFER 3

static Window *window;

static TextLayer *text_layer;
static TextLayer *freq_text_layer;
static TextLayer *upper_text_layer;

static GBitmap *bg_image;

static BitmapLayer *bg_layer;

static enum {
	READY,
	RECORDING,
	STOPPING,
	DISCONNECTED,
	RECONNECTING
} recording = DISCONNECTED;

static int16_t appmessage_data[BUFFER*SAMPLES_PER_TICK*3];
static int16_t appmessage_data_tmp[BUFFER*SAMPLES_PER_TICK*3];
static int appmessage_iterator = 0;

static int data_iterator = 0;

static char uid[50];

static int current_freq = 50;
static int time_passed = -1;

// RESET
void resetRecording(void) {
	recording = READY;
	data_iterator = 0;
	appmessage_iterator = 0;
	time_passed = -1;
	text_layer_set_text(text_layer, "REC");
}

// MESSAGE SENDING
void sendNextPart(DictionaryIterator *iter) {
	if (recording == DISCONNECTED) return;
    int e;
	
    if ((e = app_message_outbox_begin(&iter)) != APP_MSG_OK) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] AppMessage error: #%d", e);
		sendNextPart(NULL);
        return;
	}

	// data (id: 2), contains 2 uint8 for each number, 3 numbers for each entry. 75 entries per packet
    if ((e = dict_write_data(iter, 2, (uint8_t*)appmessage_data_tmp, BUFFER*SAMPLES_PER_TICK*3*2)) != DICT_OK)
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Dictionary write error (2): #%d", e);

	if (recording != STOPPING) {
		// frequency (id: 3), if not the last packet
		if (data_iterator == 0 && (e = dict_write_int32(iter, 3, current_freq)) != DICT_OK)
				APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Dictionary write error (3): #%d", e);
		
  		data_iterator++;
		
    	app_message_outbox_send();
	} else {
		// EOF (id: 3), value = 0 if the LAST packet
		if ((e = dict_write_int32(iter, 3, 0)) != DICT_OK)
			APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Dictionary write error (3, eof): #%d", e);
		
		resetRecording();
		
		app_message_outbox_send();
		text_layer_set_text(text_layer, "REC");
		APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recording stopped");
	}
}

// ACCEL HANDLER
void accel_data_handler(AccelRawData *data, uint32_t num_samples, uint64_t timestamp) {
    if (recording != RECORDING && recording != STOPPING) return;

    uint32_t i = 0;

    for (i = 0; i < num_samples; i++) {
        appmessage_data[appmessage_iterator] = data[i].x + 5000;
        appmessage_iterator++;
        appmessage_data[appmessage_iterator] = data[i].y + 5000;
        appmessage_iterator++;
        appmessage_data[appmessage_iterator] = data[i].z + 5000;
        appmessage_iterator++;
    }

	if (appmessage_iterator%(BUFFER*SAMPLES_PER_TICK*3) == 0) {
		memcpy(appmessage_data_tmp, appmessage_data, BUFFER*SAMPLES_PER_TICK*3*2);
		appmessage_iterator = 0;
		sendNextPart(NULL);
	}
}

// BT EVENT HANDLER
void bt_handler(bool connected) {
	if (!connected) {
		recording = DISCONNECTED;
		vibes_double_pulse();
		text_layer_set_text(text_layer, "Disconnected");
	} else {
		resetRecording();
		recording = RECONNECTING;
		text_layer_set_text(text_layer, "Reconnecting...");
	}
}

// SECONDS CALLBACK
void seconds_handler(struct tm *tick_time, TimeUnits units_changed) {
	time_passed++;
    static char progress[] = "Record: 12345678";
	snprintf(progress, 18, "REC: %ds", time_passed);
    text_layer_set_text(text_layer, progress);
}

// CLICKS
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (recording == RECORDING) {
        recording = STOPPING;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recording stopping after next packet");
		tick_timer_service_unsubscribe();
   		text_layer_set_text(text_layer, "Sending");
    } else if (recording == READY) {
        recording = RECORDING;
        APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recording started");
		tick_timer_service_subscribe(SECOND_UNIT, seconds_handler);
		seconds_handler(NULL, 0);
    }
}

static void set_freq(int freq) {
	current_freq = freq;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Set to %dHz", freq);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
	if (recording != READY) return;

	switch(current_freq) {
		case 10:
			set_freq(25);
   			accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
			text_layer_set_text(freq_text_layer, "25Hz");
			break;
		case 25:
			set_freq(50);
   			accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
			text_layer_set_text(freq_text_layer, "50Hz");
			break;
		case 50:
			set_freq(100);
   			accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
			text_layer_set_text(freq_text_layer, "100Hz");
			break;
		case 100:
			set_freq(10);
   			accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
			text_layer_set_text(freq_text_layer, "10Hz");
			break;
	}
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
}

// MESSAGE HANDLERS
void out_sent_handler(DictionaryIterator *sent, void *context) {
	/*if (recording == STOPPING) {
		recording = READY;
		data_iterator = 0;
		appmessage_iterator = 0;
		time_passed = -1;
		sendEOF(NULL);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recording stopped");
	}*/
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Send successful");
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
    sendNextPart(NULL);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Send failed: #%d", reason);
}

void in_received_handler(DictionaryIterator *received, void *context) {
    Tuple* tuple = dict_find(received, 0);
    if (!tuple) return;
    snprintf(uid, 50, "%s", tuple->value->cstring);
    text_layer_set_text(upper_text_layer, uid);
	//text_layer_set_text(upper_text_layer, "...");
    text_layer_set_text(freq_text_layer, "50Hz");
    text_layer_set_text(text_layer, "REC");
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Receive successful");
	resetRecording();
}

void in_dropped_handler(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "[MG] Recieve failed");
}

// WINDOW LOAD, UNLOAD
static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    bg_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BG);

    bg_layer = bitmap_layer_create(GRect(0, 0, 144, 168));
    bitmap_layer_set_bitmap(bg_layer, bg_image);
    bitmap_layer_set_alignment(bg_layer, GAlignTopLeft);
    layer_add_child(window_layer, bitmap_layer_get_layer(bg_layer));

	// UID
    upper_text_layer = text_layer_create((GRect) { .origin = { 0, 4 }, .size = { bounds.size.w - 10, 34 } });
    text_layer_set_text_alignment(upper_text_layer, GTextAlignmentCenter);
    text_layer_set_background_color(upper_text_layer, GColorClear);
    text_layer_set_text_color(upper_text_layer, GColorWhite);
    text_layer_set_text(upper_text_layer, "Connecting to phone...");
    layer_add_child(window_layer, text_layer_get_layer(upper_text_layer));

	// FREQUENCY SETTING
    freq_text_layer = text_layer_create((GRect) { .origin = { 49, 44 }, .size = { bounds.size.w - 53 - 18, 36 } });
    text_layer_set_text_alignment(freq_text_layer, GTextAlignmentRight);
    text_layer_set_background_color(freq_text_layer, GColorBlack);
    text_layer_set_text_color(freq_text_layer, GColorWhite);
    text_layer_set_text(freq_text_layer, "");
    layer_add_child(window_layer, text_layer_get_layer(freq_text_layer));

    text_layer = text_layer_create((GRect) { .origin = { 49, 77 }, .size = { bounds.size.w - 53 - 18, 36 } });
    text_layer_set_text_alignment(text_layer, GTextAlignmentRight);
    text_layer_set_background_color(text_layer, GColorBlack);
    text_layer_set_text_color(text_layer, GColorWhite);
    text_layer_set_text(text_layer, "");
    layer_add_child(window_layer, text_layer_get_layer(text_layer));

    accel_raw_data_service_subscribe(SAMPLES_PER_TICK, &accel_data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
}

static void window_unload(Window *window) {
    text_layer_destroy(text_layer);
    accel_data_service_unsubscribe();
}

// WINDOW INIT, DEINIT
static void init(void) {
	window = window_create();

	#ifdef PBL_PLATFORM_APLITE
		window_set_fullscreen(window, true);
	#endif

	window_set_click_config_provider(window, click_config_provider);
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload,
	});

    app_message_register_inbox_received(in_received_handler);
    app_message_register_inbox_dropped(in_dropped_handler);
    app_message_register_outbox_sent(out_sent_handler);
    app_message_register_outbox_failed(out_failed_handler);

    app_message_open(app_message_outbox_size_maximum(), app_message_outbox_size_maximum()); // TODO
	bluetooth_connection_service_subscribe(bt_handler);

    window_stack_push(window, true);
}

static void deinit(void) {
    window_destroy(window);
}

// MAIN
int main(void) {
    init();

    APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

    app_event_loop();
    deinit();
}
