#include "pebble.h"

static Window *window;

static GRect window_frame;

static Layer *bars_layer;

static DataLoggingSessionRef log_session;

static int32_t ACCEL_MAX = 1000;

static int32_t x_average = 0;
static int32_t y_average = 0;
static int32_t z_average = 0;

static bool is_drawing = false;
static bool xmit = false;

static uint counter = 0;

static DataLoggingResult status = DATA_LOGGING_CLOSED;
static time_t state_changed = 0;

int CHANNEL = 1234;

/**
 * \brief    Fast Square root algorithm
 *
 * Fractional parts of the answer are discarded. That is:
 *      - SquareRoot(3) --> 1
 *      - SquareRoot(4) --> 2
 *      - SquareRoot(5) --> 2
 *      - SquareRoot(8) --> 2
 *      - SquareRoot(9) --> 3
 *
 * \param[in] a_nInput - unsigned integer for which to find the square root
 *
 * \return Integer square root of the input value.
 */
uint32_t square_root(uint32_t a_nInput)
{
    uint32_t op  = a_nInput;
    uint32_t res = 0;
    uint32_t one = 1uL << 30; // The second-to-top bit is set: use 1u << 14 for uint16_t type; use 1uL<<30 for uint32_t type

    // "one" starts at the highest power of four <= than the argument.
    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0)
    {
        if (op >= res + one)
        {
            op = op - (res + one);
            res = res +  2 * one;
        }
        res >>= 1;
        one >>= 2;
    }
    return res;
}

static char * create_label(int32_t average)
{
	char * label = malloc(32 * sizeof(char));
	
	double normalized = ((double) average) / 1000;
	
	int whole_normalized = (int) (normalized * 100);
	
	int whole = whole_normalized / 100;
	int remainder = whole_normalized % 100;
	
	if (remainder < 0)
		remainder = 0 - remainder;
		
	if (whole < 0)
		whole = 0 - whole;
	
	if (remainder < 10)
	{
		if (average < 0)
			snprintf(label, 32, "-%d.0%d", whole, remainder);
		else
			snprintf(label, 32, "%d.0%d", whole, remainder);
	}
	else
	{
		if (average < 0)
			snprintf(label, 32, "-%d.%d", whole, remainder);
		else
			snprintf(label, 32, "%d.%d", whole, remainder);
	}

	return label;
}

static void bars_layer_update_callback(Layer *me, GContext *ctx) 
{
	if (is_drawing)
		return;
		
	is_drawing = true;

	double x_center = window_frame.size.w / 2;
	double y_center = window_frame.size.h / 2;

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, window_frame, 0, GCornerNone);

	char * time_label = malloc(sizeof(char) * 32);
	
	clock_copy_time_string(time_label, 32);
	
	if (clock_is_24h_style() == false)
	{
		for (int32_t i = 0; i < 32; i++)
		{
			if (time_label[i] == 32)
			{
				time_label[i] = 0;
				break;
			}
		}
	}
	
	GFont time_font = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);

	GRect time_box;
	time_box.size.h = y_center / 2;
	time_box.size.w = window_frame.size.w;
	time_box.origin.y = 0;
	time_box.origin.x = 0;

	graphics_draw_text(ctx, time_label, time_font, time_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

	time_t seconds = time(NULL);
	
	struct tm * now = localtime(&seconds);
	
	strftime(time_label, 32, "%a, %b %d", now);

	time_font = fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21);

	time_box.size.h = y_center / 2;
	time_box.size.w = window_frame.size.w;
	time_box.origin.y = (y_center / 2 + 10);
	time_box.origin.x = 0;

	graphics_draw_text(ctx, time_label, time_font, time_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

	free(time_label);
	
	double bar_height = y_center / 4;
	double max_bar_width = x_center - 16;
	
	GFont labelFont = fonts_get_system_font(FONT_KEY_GOTHIC_14);

	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_text_color(ctx, GColorWhite);

	double top = y_center;
	
	if (x_average != 0)
	{
		GRect bar;
		bar.size.h = bar_height;
		bar.origin.y = top;
		
		if (x_average > 0)
		{
			bar.size.w = (max_bar_width * x_average) / ACCEL_MAX;
			bar.origin.x = max_bar_width + 32;
		}
		else
		{
			bar.size.w = (max_bar_width * (0 - x_average)) / ACCEL_MAX;
			bar.origin.x = max_bar_width - bar.size.w;
		}
		
		graphics_fill_rect(ctx, bar, 0, GCornerNone);	
		
		GRect text_box;
		text_box.size.w = 26;
		text_box.size.h = bar_height;
		text_box.origin.x = x_center - 14;
		text_box.origin.y = top + 1;

		char * label = create_label(x_average);
		
		graphics_draw_text(ctx, label, labelFont, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
		
		free(label);
	}
	
	top += bar_height;

	if (y_average != 0)
	{
		GRect bar;
		bar.size.h = bar_height;
		bar.origin.y = top;
		
		if (y_average > 0)
		{
			bar.size.w = (max_bar_width * y_average) / ACCEL_MAX;
			bar.origin.x = max_bar_width + 32;
		}
		else
		{
			bar.size.w = (max_bar_width * (0 - y_average)) / ACCEL_MAX;
			bar.origin.x = max_bar_width - bar.size.w;
		}
	
		graphics_fill_rect(ctx, bar, 0, GCornerNone);	
		
		GRect text_box;
		text_box.size.w = 26;
		text_box.size.h = bar_height;
		text_box.origin.x = x_center - 14;
		text_box.origin.y = top + 1;

		char * label = create_label(y_average);
		
		graphics_draw_text(ctx, label, labelFont, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
		
		free(label);
	}

	top += bar_height;

	if (z_average != 0)
	{
		GRect bar;
		bar.size.h = bar_height;
		bar.origin.y = top;
		
		if (z_average > 0)
		{
			bar.size.w = (max_bar_width * z_average) / ACCEL_MAX;
			bar.origin.x = max_bar_width + 32;
		}
		else
		{
			bar.size.w = (max_bar_width * (0 - z_average)) / ACCEL_MAX;
			bar.origin.x = max_bar_width - bar.size.w;
		}
	
		graphics_fill_rect(ctx, bar, 0, GCornerNone);
		
		GRect text_box;
		text_box.size.w = 26;
		text_box.size.h = bar_height;
		text_box.origin.x = x_center - 14;
		text_box.origin.y = top + 1;

		char * label = create_label(z_average);
		
		graphics_draw_text(ctx, label, labelFont, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
		
		free(label);
	}
	
	top += bar_height;
	
/*	
Commented out to make space for error report...

	int32_t remainder = (int32_t) square_root((x_average*x_average) + (y_average*y_average) + (z_average*z_average));
	remainder = remainder - 1000;
	
	GRect bar;
	bar.size.h = bar_height;
	bar.origin.y = top;
		
	if (remainder < 0)
		remainder = 0 - remainder;
	bar.size.w = (max_bar_width * remainder) / ACCEL_MAX;
	bar.origin.x = max_bar_width + 32;

	graphics_fill_rect(ctx, bar, 0, GCornerNone);
		
	GRect text_box;
	text_box.size.w = 26;
	text_box.size.h = bar_height;
	text_box.origin.x = x_center - 14;
	text_box.origin.y = top + 1;

	char * label = create_label(remainder);
		
	graphics_draw_text(ctx, label, labelFont, text_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
*/	

// Start error report

	GRect status_box;
	status_box.size.w = max_bar_width + 26;
	status_box.size.h = bar_height;
	status_box.origin.x = x_center - 14;
	status_box.origin.y = top + 1;

	char * state_label = malloc(sizeof(char) * 32);

	struct tm * change = localtime(&state_changed);

	if (status == DATA_LOGGING_SUCCESS)
		strftime(state_label, 32, "%m/%d %R  :)", change);
	else if (status == DATA_LOGGING_BUSY)
		strftime(state_label, 32, "%m/%d %R  :|", change);
	else
		strftime(state_label, 32, "%m/%d %R  :(", change);
		
	graphics_draw_text(ctx, state_label, labelFont, status_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
	
	free(state_label);

// End error report
	
	char * battery_label = malloc(sizeof(char) * 32);
	
	GRect battery_box;
	battery_box.size.w = max_bar_width;
	battery_box.size.h = bar_height;
	battery_box.origin.x = 0;
	battery_box.origin.y = top + 1;
	
	BatteryChargeState charge = battery_state_service_peek();
	
	if (charge.is_charging && charge.is_plugged)
		snprintf(battery_label, 32, "%d%% (C/P)", charge.charge_percent);
	else if (charge.is_plugged)
		snprintf(battery_label, 32, "%d%% (P)", charge.charge_percent);
	else if (charge.is_charging)
		snprintf(battery_label, 32, "%d%% (C)", charge.charge_percent);
	else
		snprintf(battery_label, 32, "%d%%", charge.charge_percent);

	graphics_draw_text(ctx, battery_label, labelFont, battery_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
	
	free(battery_label);
	
	is_drawing = false;
}

static void window_load(Window *window) 
{
	Layer *window_layer = window_get_root_layer(window);
	
	GRect frame = window_frame = layer_get_frame(window_layer);

	bars_layer = layer_create(frame);

	layer_set_update_proc(bars_layer, bars_layer_update_callback);
	layer_add_child(window_layer, bars_layer);
}

static void window_unload(Window *window) 
{
	layer_destroy(bars_layer);
}

void accel_data_handler(AccelData *data, uint32_t num_samples) 
{
	if (data == NULL)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "NULL data");

	APP_LOG(APP_LOG_LEVEL_DEBUG, "+ %d", (int) num_samples);

	counter = counter + 1;

	DataLoggingResult new_status = data_logging_log(log_session, data, num_samples);
	
	if (new_status != status)
	{
		if (status == DATA_LOGGING_SUCCESS && new_status == DATA_LOGGING_BUSY)
		{
			// Do nothing...
		}
		else if (status == DATA_LOGGING_BUSY && new_status == DATA_LOGGING_SUCCESS)
		{
			// Do nothing...
		}
		else
		{
			state_changed = time(NULL);
			status = new_status;
		}
	}

	if (status == DATA_LOGGING_SUCCESS)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "DATA_LOGGING_SUCCESS");
	else if (status == DATA_LOGGING_BUSY)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "DATA_LOGGING_BUSY");
	else if (status == DATA_LOGGING_FULL)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "DATA_LOGGING_FULL");
	else if (status == DATA_LOGGING_NOT_FOUND)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "DATA_LOGGING_NOT_FOUND");
	else if (status == DATA_LOGGING_CLOSED)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "DATA_LOGGING_CLOSED");
	else if (status == DATA_LOGGING_INVALID_PARAMS)
		APP_LOG(APP_LOG_LEVEL_DEBUG, "DATA_LOGGING_INVALID_PARAMS");

	if (counter % 5 == 0)
	{
		data_logging_finish(log_session);

		APP_LOG(APP_LOG_LEVEL_DEBUG, "logged");

		log_session = data_logging_create(CHANNEL, DATA_LOGGING_BYTE_ARRAY, sizeof(AccelData), true);
	}

	x_average = 0;
	y_average = 0;
	z_average = 0;

	for (uint32_t i = 0; i < num_samples; i++)
	{
		x_average += data[i].x;
		y_average += data[i].y;
		z_average += data[i].z;
	}
	
	APP_LOG(APP_LOG_LEVEL_DEBUG, "observed %d %d %d (%d)", (int) x_average, (int) y_average, (int) z_average, (int) (counter % 100));

	x_average = x_average / (int32_t) num_samples;
	y_average = y_average / (int32_t) num_samples;
	z_average = z_average / (int32_t) num_samples;

	if (counter % 4 == 0)
		layer_mark_dirty(bars_layer);
}

static void init(void) 
{
	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload
	});
		
	window_stack_push(window, true /* Animated */);
	window_set_background_color(window, GColorBlack);

	log_session = data_logging_create(CHANNEL, DATA_LOGGING_BYTE_ARRAY, sizeof(AccelData), true);

	accel_data_service_subscribe(10, &accel_data_handler);
	accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
}

static void deinit(void) 
{
	accel_data_service_unsubscribe();

	data_logging_finish(log_session);

	window_destroy(window);
}

int main(void) 
{
	init();
	app_event_loop();
	deinit();
}
