#include <pebble.h>
#include "game.h"

static Window *s_window;
static Layer *gfx_layer;
static AppTimer *timer;
static GBitmap *ship_bitmap;

const int SHIP_WIDTH = 10;
const int SHIP_HEIGHT = 15;
const int SCREEN_WIDTH = 144;
const int SCREEN_HEIGHT = 168;
const int UPDATE_INTERVAL = 25;
const int METEOR_START_INTERVAL = 40;

static long int time_ticks = 0;
static float ship_x;
static float ship_y;
static float speed_x = 0;
static float speed_y = 0;
static int acc_x = 0;
static int acc_y = 0;
static int meteor_interval;
static int score;
static int hi_score = 0;
static Meteor* first_meteor = NULL;
static bool started = false;
static bool game_over = true;
static long int game_over_stop_time = 0;
static bool paused = false;
static bool show_debug = false;
static bool use_shapes = false;

static const GPathInfo METEOR1_PATH_INFO = {
  .num_points = 10,
  .points = (GPoint []) {{-4, -10}, {5, -9}, {9, -8}, {7, 0}, {9, 5}, {4, 9}, {0, 7}, {-10, 9}, {-8, 0}, {-10, -5}}
};
static const GPathInfo METEOR2_PATH_INFO = {
  .num_points = 12,
  .points = (GPoint []) {{-1, -10}, {8, -8}, {7, -4}, {9, 0}, {8, 7}, {0, 9}, {-5, 9}, {-9, 7}, {-8, 3}, {-10, 0}, {-8, -4}, {-8, -8}}
};
static const GPathInfo METEOR3_PATH_INFO = {
  .num_points = 12,
  .points = (GPoint []) {{-10, -10}, {-5, -8}, {1, -10}, {8, -10}, {9, -5}, {6, -1}, {9, 9}, {0, 9}, {-5, 7}, {-10, 9}, {-8, 3}, {-10, -2}}
};

// More meteors!
static void add_meteor()
{
	Meteor* new_meteor = malloc(sizeof(Meteor));
	new_meteor->size = 5 + (rand() % 15);
	
	// left-right or up-down?
	if (rand() % 2)
	{
		new_meteor->x = rand() % SCREEN_WIDTH;
		new_meteor->speed_x = 0.2 * (1 + (rand() % 5));
		if ((new_meteor->x > SCREEN_WIDTH / 2) && (new_meteor->speed_x > 0))
			new_meteor->speed_x *= -1;
	
		new_meteor->speed_y = 0.5 * (1 + (rand() % 5));
	
		// Top or bottom side?
		if (rand() % 2)
		{
			new_meteor->y = SCREEN_HEIGHT + new_meteor->size;
			new_meteor->speed_y *= -1;
		}
		else
			new_meteor->y = -new_meteor->size;
	} else {
		new_meteor->y = rand() % SCREEN_HEIGHT;
		new_meteor->speed_y = 0.2 * (1 + (rand() % 5));
		if ((new_meteor->y > SCREEN_HEIGHT / 2) && (new_meteor->speed_y > 0))
			new_meteor->speed_y *= -1;

		new_meteor->speed_x = 0.5 * (1 + (rand() % 5));
	
		// Left or right side?
		if (rand() % 2)
		{
			new_meteor->x = SCREEN_WIDTH + new_meteor->size;
			new_meteor->speed_x *= -1;
		}
		else
			new_meteor->x = -new_meteor->size;
	}
	
	// Selecting random path-shape
	GPathInfo* original_path = NULL;
	switch (rand() % 2)
	{
		case 0:
			original_path = (GPathInfo*)&METEOR1_PATH_INFO;
			break;
		case 1:
			original_path = (GPathInfo*)&METEOR2_PATH_INFO;
			break;
		case 2:
			original_path = (GPathInfo*)&METEOR3_PATH_INFO;
			break;
	}
	new_meteor->path.num_points = original_path->num_points;
	new_meteor->path.points = malloc(sizeof(GPoint) * new_meteor->path.num_points);

	// Reducing size...
	unsigned int p;
	for (p = 0; p < new_meteor->path.num_points; p++)
	{
		new_meteor->path.points[p].x = original_path->points[p].x * (new_meteor->size + 4) / 20;
		new_meteor->path.points[p].y = original_path->points[p].y * (new_meteor->size + 4) / 20;
	}
	new_meteor->draw_path = gpath_create(&new_meteor->path);
	new_meteor->rot = 0;
	new_meteor->rot_speed = 1;
	if (rand() % 2) new_meteor->rot_speed *= -1;

	new_meteor->next = NULL;
	
	if (first_meteor == NULL)
		first_meteor = new_meteor;
	else {
		Meteor* meteor = first_meteor;
		while (meteor->next)
		{		
			meteor = meteor->next;
		}
		meteor->next = new_meteor;
	}
}

// Is point part of meteor?
static bool is_part_of_meteor(int x, int y, int max_r, Meteor* meteor)
{
	int r = meteor->size/2 + max_r;
	int dist_x = x - meteor->x;
	int dist_y = y - meteor->y;
	return dist_x * dist_x + dist_y * dist_y < r * r;
}

// Checks ship and meteor interception
static bool is_boom(Meteor* meteor)
{
#ifdef GOD_MODE
	return false;
#endif	
	return is_part_of_meteor(ship_x, ship_y, 7, meteor);
}

// Update meteors data
static void update_meteors()
{
	// Moving meteors	
	Meteor* meteor = first_meteor;
	while (meteor)
	{
		meteor->x += meteor->speed_x;
		meteor->y += meteor->speed_y;
		// Game over?
		if (!game_over && is_boom(meteor))
		{
			vibes_short_pulse();
			game_over_stop_time = time_ticks + (500 / UPDATE_INTERVAL); // Some more time to rumble...
			//reset_game();
		}
		meteor = meteor->next;
	}
	
	// Removing unused meteors
	Meteor* prev = NULL;
	meteor = first_meteor;
	while (meteor)
	{
		Meteor* next = meteor->next;
		if ((meteor->x < -meteor->size) || (meteor->y < -meteor->size) ||
			(meteor->x > SCREEN_WIDTH + meteor->size) || (meteor->y > SCREEN_HEIGHT + meteor->size))
		{
			if (!game_over && !game_over_stop_time)
			{
				// Increasing score
				score += 10;
				if (score > hi_score) hi_score = score;
				// More score - more meteors!
				if (meteor_interval > 30)
					meteor_interval -= 2;
				else if ((score % 100 == 0) && (meteor_interval > 20)) 
					meteor_interval -= 1;
				else if ((score % 300 == 0) && (meteor_interval > 15)) 
					meteor_interval -= 1;
				else if ((score % 400 == 0) && (meteor_interval > 10)) 
					meteor_interval -= 1;
				else if ((score % 500 == 0) && (meteor_interval > 5)) 
					meteor_interval -= 1;
			}
			gpath_destroy(meteor->draw_path);
			free(meteor->path.points);
			free(meteor);
			if (prev != NULL)
				prev->next = next;
			else first_meteor = next;
		}
		else prev = meteor;
		meteor = next;
	}
}

// Updating game data
void update_timer(void* data)
{
	// Reschedule timer
	timer = app_timer_register(UPDATE_INTERVAL, (AppTimerCallback) update_timer, NULL);
	
	// Schedule redraw
	layer_mark_dirty(gfx_layer);
	
	// Game over?
	if (game_over_stop_time && (time_ticks >= game_over_stop_time))
	{
		// Saving hi-score
		if (!game_over)
			 persist_write_int(0, hi_score);
		game_over = true;
	}

	// Paused? Nah.
	if (paused)
		return;

	time_ticks++;
	
	// More meteors!
	if (((time_ticks >= 5000 / UPDATE_INTERVAL) || (hi_score >= 200) // Newbie? Time for some tutorial
		|| !started)  // Title screen? Lets go!
		&& (time_ticks % meteor_interval == 0)) // Or just time is come
		add_meteor();

	// Using accelerometer data to control ship
	AccelData acc;
  accel_service_peek(&acc);
	acc_x = acc.x;
	acc_y = acc.y;
	
	// Moving ship
	speed_x = acc_x / 32;
	speed_y = -acc_y / 32;
	ship_x += speed_x;
	ship_y += speed_y;
	
	// Some limits
	if (ship_x < 0) ship_x = 0;
	if (ship_x > SCREEN_WIDTH) ship_x = SCREEN_WIDTH;
	if (ship_y < 0) ship_y = 0;
	if (ship_y > SCREEN_HEIGHT) ship_y = SCREEN_HEIGHT;
	
	// Moving meteors, updating, etc.
	update_meteors();
}

static void game_draw(Layer *layer, GContext *ctx)
{ 
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_context_set_stroke_color(ctx, GColorBlack);
	graphics_context_set_fill_color(ctx, GColorBlack); 
	
	// Draw ship
	if (started && !game_over)
	{
		//graphics_context_set_fill_color(ctx, GColorBlack); 
		//graphics_fill_rect(ctx, GRect(ship_x - SHIP_WIDTH / 2, ship_y - SHIP_HEIGHT / 2, SHIP_WIDTH, SHIP_HEIGHT), 3, GCornersAll);
		graphics_draw_bitmap_in_rect(ctx, ship_bitmap, GRect(ship_x - SHIP_WIDTH / 2, ship_y - SHIP_HEIGHT / 2, SHIP_WIDTH, SHIP_HEIGHT));
	}
	
	// Draw all meteors	
	Meteor* meteor = first_meteor;
	int meteor_count = 0;
	while (meteor)
	{
		if (meteor->size < 8 || !use_shapes)
		{
			graphics_fill_circle(ctx, (GPoint) { .x = meteor->x, .y = meteor->y }, meteor->size / 2);
		} else {		
			gpath_move_to(meteor->draw_path, GPoint(meteor->x, meteor->y));
			gpath_rotate_to(meteor->draw_path, TRIG_MAX_ANGLE / 360 * (meteor->rot += meteor->rot_speed));
			gpath_draw_filled(ctx, meteor->draw_path);
		//gpath_draw_outline(ctx, meteor->draw_path);
		}
		meteor_count++;
		meteor = meteor->next;
	}
	
	// Score
	if (started)
	{
		char score_text[20];
		snprintf(score_text, sizeof(score_text), "SCORE: %.5d", score);
		GFont *score_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
		graphics_draw_text(ctx, score_text, score_font, (GRect) { .origin = {0, 0}, .size = {140, 30} }, GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
	}

	// Some tutorial
	if (started)
	{
		GFont *tutorial_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
		if (time_ticks < 3000 / UPDATE_INTERVAL && hi_score < 200)
			graphics_draw_text(ctx, "Tilt your watches to control the spaceship", tutorial_font, (GRect) { .origin = {0, 130}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		else if (time_ticks < 6000 / UPDATE_INTERVAL && hi_score < 200)
			graphics_draw_text(ctx, "Avoid asteroids!",  tutorial_font, (GRect) { .origin = {0, 130}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		else if (time_ticks < 9000 / UPDATE_INTERVAL && hi_score < 200)
			graphics_draw_text(ctx, "Good luck!",  tutorial_font, (GRect) { .origin = {0, 130}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
	}
	
	if (!started) // Title screen
	{
		graphics_context_set_stroke_color(ctx, GColorBlack); 
		graphics_draw_rect(ctx, GRect(20, 36, 103, 102));
		graphics_context_set_fill_color(ctx, GColorWhite); 
		graphics_fill_rect(ctx, GRect(21, 37, 101, 100), 0, GCornerNone);
		
		GFont *hi_score_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
		char hi_score_text[30];
		snprintf(hi_score_text, sizeof(hi_score_text), "HI SCORE: %.5d", hi_score);
		graphics_draw_text(ctx, hi_score_text, hi_score_font, (GRect) { .origin = {25, 38}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

		GFont *title_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
		graphics_draw_text(ctx, "SPACE DRAGON", title_font, (GRect) { .origin = {25, 60}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

		GFont *version_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
		graphics_draw_text(ctx, VERSION, version_font, (GRect) { .origin = {25, 103}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
		
		GFont *copyright_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
		graphics_draw_text(ctx, "(c) Cluster, 2014", copyright_font, (GRect) { .origin = {25, 118}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
	}
	else if (game_over) // "GAME OVER" box
	{
		graphics_context_set_stroke_color(ctx, GColorBlack); 
		graphics_draw_rect(ctx, GRect(20, 46, 103, 82));
		graphics_context_set_fill_color(ctx, GColorWhite); 
		graphics_fill_rect(ctx, GRect(21, 47, 101, 80), 0, GCornerNone);
		
		// Flash hi-score if beaten
		if (!hi_score || (score < hi_score) || ((time_ticks / 10) % 2))
		{
			GFont *hi_score_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
			char hi_score_text[30];
			snprintf(hi_score_text, sizeof(hi_score_text), "HI SCORE: %.5d", hi_score);
			graphics_draw_text(ctx, hi_score_text, hi_score_font, (GRect) { .origin = {25, 48}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		}

		GFont *game_over_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
		graphics_draw_text(ctx, "GAME OVER", game_over_font, (GRect) { .origin = {25, 65}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		
		GFont *game_over_comment_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
		graphics_draw_text(ctx, "PRESS ANY BUTTON", game_over_comment_font, (GRect) { .origin = {25, 92}, .size = {94, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		
	}	
	else if (paused) // "PAUSED" box
	{
#ifndef GOD_MODE	
		graphics_context_set_stroke_color(ctx, GColorBlack); 
		graphics_draw_rect(ctx, GRect(44, 76, 55, 16));
		graphics_context_set_fill_color(ctx, GColorWhite); 
		graphics_fill_rect(ctx, GRect(45, 77, 53, 14), 0, GCornerNone);
		GFont *paused_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
		graphics_draw_text(ctx, "PAUSED", paused_font, (GRect) { .origin = {0, 75}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
#endif		
	}
	
	// Some debug info
	if (show_debug)
	{
		GFont *debug_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
		char debug_text[200];
		snprintf(debug_text, sizeof(debug_text), "AccX: %.5d\r\nAccY: %.5d\r\nSpeedX: %d\r\nSpeedY: %d\r\nShipX: %d\r\nShipY: %d\r\nMeteors: %d\r\nInterval: %d\r\nTime: %d", acc_x, acc_y, (int)speed_x, (int)speed_y, (int)ship_x, (int)ship_y, meteor_count, meteor_interval, (int)time_ticks);
		graphics_draw_text(ctx, debug_text, debug_font, (GRect) { .origin = {0, 0}, .size = {144, 168} }, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
	}
}

static void destroy_ui(void) {
	window_destroy(s_window);
	layer_destroy(gfx_layer);
	gbitmap_destroy(ship_bitmap);
}

static void free_meteors()
{
	Meteor* meteor = first_meteor;
	while (meteor)
	{
		Meteor* next = meteor->next;
		gpath_destroy(meteor->draw_path);
		free(meteor->path.points);
		free(meteor);
		meteor = next;
	}
	first_meteor = NULL;
}

static void handle_window_unload(Window* window) {
	app_timer_cancel(timer);	// Cancel timer
	accel_data_service_unsubscribe();	// Disabling accelerometer
	app_focus_service_unsubscribe(); // Unsubscribing from focus
	free_meteors();									// Free memory from meteors
  destroy_ui();									// Free memory from UI
	persist_write_int(0, hi_score); // Saving hi score
}

void app_in_focus_callback(bool in_focus)
{
	if (!in_focus && started && !game_over) paused = true;
}


static void click_handler(ClickRecognizerRef recognizer, void *context) 
{
	if (!started || game_over) reset_game();
	else paused = !paused;
}

static void click_down_handler(ClickRecognizerRef recognizer, void *context) 
{
	use_shapes = !use_shapes;
}


static void config_provider(void *context)
{
  window_single_click_subscribe(BUTTON_ID_UP, click_handler);
	window_single_click_subscribe(BUTTON_ID_SELECT, click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, click_down_handler);
}

void reset_game()
{
	srand(time(NULL));
	free_meteors();
	time_ticks = 0;
	ship_x = SCREEN_WIDTH / 2;
	ship_y = SCREEN_HEIGHT / 2;	
	meteor_interval = METEOR_START_INTERVAL;
	score = 0;
	game_over_stop_time = 0;
	game_over =  false;
	paused = false;
	started = true;
}

void show_game() {
	reset_game();
	game_over = true;
	started = false;
	meteor_interval = 5;
	
	// Reading hi-score from memory
	if (persist_exists(0))	
		hi_score = persist_read_int(0);
	else hi_score = 0;
	
	// Loading ship bitmap
	ship_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIP);

	// Crating window
	s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_fullscreen(s_window, true);
	
	window_set_window_handlers(s_window, (WindowHandlers) {
		.unload = handle_window_unload,
  });

	// The layer
  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_frame(window_layer);
	gfx_layer = layer_create(bounds);
	layer_set_update_proc(gfx_layer, game_draw);
	layer_add_child(window_layer, gfx_layer);
	
	window_set_click_config_provider(s_window, config_provider);
	
  window_stack_push(s_window, true);
	
	timer = app_timer_register(UPDATE_INTERVAL, (AppTimerCallback) update_timer, NULL);
	accel_data_service_subscribe(0, NULL);
	accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
	app_focus_service_subscribe(app_in_focus_callback);
}
