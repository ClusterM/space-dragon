#include <pebble.h>
#include "game.h"

static Window *s_window;
static Layer *gfx_layer;
static AppTimer *timer;
static GBitmap *ship_bitmap;

static long int time_ticks = 0;
static float ship_x;
static float ship_y;
static float speed_x = 0;
static float speed_y = 0;
static int acc_x = 0;
static int acc_y = 0;
static int asteroid_interval;
static int score;
static int hi_score = 0;
static Asteroid* first_asteroid = NULL;
static bool started = false;
static bool game_over = true;
static long int game_over_stop_time = 0;
static bool paused = false;
static bool show_debug = false;
static bool use_shapes = false;
static bool god_mode = false;

// Some path-shapes for asteroids
static const GPathInfo ASTEROID1_PATH_INFO = {
  .num_points = 10,
  .points = (GPoint []) {{-4, -10}, {5, -9}, {9, -8}, {7, 0}, {9, 5}, {4, 9}, {0, 7}, {-10, 9}, {-8, 0}, {-10, -5}}
};
static const GPathInfo ASTEROID2_PATH_INFO = {
  .num_points = 12,
  .points = (GPoint []) {{-1, -10}, {8, -8}, {7, -4}, {9, 0}, {8, 7}, {0, 9}, {-5, 9}, {-9, 7}, {-8, 3}, {-10, 0}, {-8, -4}, {-8, -8}}
};
static const GPathInfo ASTEROID3_PATH_INFO = {
  .num_points = 8,
  .points = (GPoint []) {{-3, -10}, {4, -9}, {9, -2}, {9, 4}, {3, 9}, {-4, 9}, {-10, 3}, {-9, -5}}
};
static const GPathInfo ASTEROID4_PATH_INFO = {
  .num_points = 10,
  .points = (GPoint []) {{-3, -10}, {7, -8}, {9, -2}, {8, -3}, {3, 9}, {-4, 9}, {-10, 7}, {-9, 3}, {-10, -3}, {-8, -8}}
};

// More asteroids!
static void add_asteroid()
{
	Asteroid* new_asteroid = malloc(sizeof(Asteroid));
	new_asteroid->size = 5 + (rand() % 15);
	
	// left-right or up-down?
	if (rand() % 2)
	{
		new_asteroid->x = rand() % SCREEN_WIDTH;
		new_asteroid->speed_x = 0.2 * (1 + (rand() % 5));
		if ((new_asteroid->x > SCREEN_WIDTH / 2) && (new_asteroid->speed_x > 0))
			new_asteroid->speed_x *= -1;
	
		new_asteroid->speed_y = 0.5 * (1 + (rand() % 5));
	
		// Top or bottom side?
		if (rand() % 2)
		{
			new_asteroid->y = SCREEN_HEIGHT + new_asteroid->size;
			new_asteroid->speed_y *= -1;
		}
		else
			new_asteroid->y = -new_asteroid->size;
	} else {
		new_asteroid->y = rand() % SCREEN_HEIGHT;
		new_asteroid->speed_y = 0.2 * (1 + (rand() % 5));
		if ((new_asteroid->y > SCREEN_HEIGHT / 2) && (new_asteroid->speed_y > 0))
			new_asteroid->speed_y *= -1;

		new_asteroid->speed_x = 0.5 * (1 + (rand() % 5));
	
		// Left or right side?
		if (rand() % 2)
		{
			new_asteroid->x = SCREEN_WIDTH + new_asteroid->size;
			new_asteroid->speed_x *= -1;
		}
		else
			new_asteroid->x = -new_asteroid->size;
	}
	
	// Selecting random path-shape
	GPathInfo* original_path = NULL;
	switch (rand() % 4)
	{
		case 0:
			original_path = (GPathInfo*)&ASTEROID1_PATH_INFO;
			break;
		case 1:
			original_path = (GPathInfo*)&ASTEROID2_PATH_INFO;
			break;
		case 2:
			original_path = (GPathInfo*)&ASTEROID3_PATH_INFO;
			break;
		case 3:
			original_path = (GPathInfo*)&ASTEROID4_PATH_INFO;
			break;
	}
	new_asteroid->path.num_points = original_path->num_points;
	new_asteroid->path.points = malloc(sizeof(GPoint) * new_asteroid->path.num_points);

	// Reducing size...
	unsigned int p;
	for (p = 0; p < new_asteroid->path.num_points; p++)
	{
		new_asteroid->path.points[p].x = original_path->points[p].x * (new_asteroid->size + 4) / 20;
		new_asteroid->path.points[p].y = original_path->points[p].y * (new_asteroid->size + 4) / 20;
	}
	new_asteroid->draw_path = gpath_create(&new_asteroid->path);
	new_asteroid->rot = 0;
	new_asteroid->rot_speed = 1;
	if (rand() % 2) new_asteroid->rot_speed *= -1;

	new_asteroid->next = NULL;
	
	if (first_asteroid == NULL)
		first_asteroid = new_asteroid;
	else {
		Asteroid* asteroid = first_asteroid;
		while (asteroid->next)
		{		
			asteroid = asteroid->next;
		}
		asteroid->next = new_asteroid;
	}
}

// Part of asteroid?
static bool is_part_of_asteroid(int x, int y, int max_r, Asteroid* asteroid)
{
	int r = asteroid->size/2 + max_r;
	int dist_x = x - asteroid->x;
	int dist_y = y - asteroid->y;
	return dist_x * dist_x + dist_y * dist_y < r * r;
}

// Checks ship and asteroid interception
static bool is_boom(Asteroid* asteroid)
{
	return is_part_of_asteroid(ship_x, ship_y, 7, asteroid);
}

// Update asteroids data
static void update_asteroids()
{
	// Moving asteroids	
	Asteroid* asteroid = first_asteroid;
	while (asteroid)
	{
		asteroid->x += asteroid->speed_x;
		asteroid->y += asteroid->speed_y;
		// Game over?
		if (!god_mode && is_boom(asteroid) && !game_over_stop_time)
		{
			vibes_short_pulse();
			game_over_stop_time = time_ticks + (500 / UPDATE_INTERVAL); // Some more time to rumble...
		}
		asteroid = asteroid->next;
	}
	
	// Removing unused asteroids
	Asteroid* prev = NULL;
	asteroid = first_asteroid;
	while (asteroid)
	{
		Asteroid* next = asteroid->next;
		if ((asteroid->x < -asteroid->size) || (asteroid->y < -asteroid->size) ||
			(asteroid->x > SCREEN_WIDTH + asteroid->size) || (asteroid->y > SCREEN_HEIGHT + asteroid->size))
		{
			if (!game_over && !game_over_stop_time)
			{
				// Increasing score
				score += 10;
				if (score > hi_score && !god_mode) hi_score = score;
				// More score - more asteroids!
				if (asteroid_interval > 30)
					asteroid_interval -= 2;
				else if ((score % 100 == 0) && (asteroid_interval > 20)) 
					asteroid_interval -= 1;
				else if ((score % 300 == 0) && (asteroid_interval > 15)) 
					asteroid_interval -= 1;
				else if ((score % 400 == 0) && (asteroid_interval > 10)) 
					asteroid_interval -= 1;
				else if ((score % 500 == 0) && (asteroid_interval > 5)) 
					asteroid_interval -= 1;
			}
			gpath_destroy(asteroid->draw_path);
			free(asteroid->path.points);
			free(asteroid);
			if (prev != NULL)
				prev->next = next;
			else first_asteroid = next;
		}
		else prev = asteroid;
		asteroid = next;
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
	
	// More asteroids!
	if (((time_ticks >= 5000 / UPDATE_INTERVAL) || (hi_score >= MIN_TUTORIAL_SCORE) // Newbie? Time for some tutorial
		|| !started)  // Title screen? Lets go!
		&& (time_ticks % asteroid_interval == 0)) // Or just time is come
		add_asteroid();

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
	
	// Moving asteroids, updating, etc.
	update_asteroids();
}

// Drawing all stuff
static void game_draw(Layer *layer, GContext *ctx)
{ 
	graphics_context_set_text_color(ctx, GColorBlack);
	graphics_context_set_stroke_color(ctx, GColorBlack);
	graphics_context_set_fill_color(ctx, GColorBlack); 
	
	// Draw ship
	if (!game_over)
	{
		//graphics_fill_rect(ctx, GRect(ship_x - SHIP_WIDTH / 2, ship_y - SHIP_HEIGHT / 2, SHIP_WIDTH, SHIP_HEIGHT), 3, GCornersAll);
		graphics_draw_bitmap_in_rect(ctx, ship_bitmap, GRect(ship_x - SHIP_WIDTH / 2, ship_y - SHIP_HEIGHT / 2, SHIP_WIDTH, SHIP_HEIGHT));
	}
	
	// Draw all asteroids	
	Asteroid* asteroid = first_asteroid;
	int asteroid_count = 0;
	while (asteroid)
	{
		if (asteroid->size < 8 || !use_shapes)
		{
			graphics_fill_circle(ctx, (GPoint) { .x = asteroid->x, .y = asteroid->y }, asteroid->size / 2);
		} else {		
			gpath_move_to(asteroid->draw_path, GPoint(asteroid->x, asteroid->y));
			gpath_rotate_to(asteroid->draw_path, TRIG_MAX_ANGLE / 360 * (asteroid->rot += asteroid->rot_speed));
			gpath_draw_filled(ctx, asteroid->draw_path);
		//gpath_draw_outline(ctx, asteroid->draw_path);
		}
		asteroid_count++;
		asteroid = asteroid->next;
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
	if (started && (hi_score < MIN_TUTORIAL_SCORE))
	{
		GFont *tutorial_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
		if (time_ticks < 3000 / UPDATE_INTERVAL)
			graphics_draw_text(ctx, "Tilt your watch to control the spaceship", tutorial_font, (GRect) { .origin = {0, 130}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		else if (time_ticks < 6000 / UPDATE_INTERVAL)
			graphics_draw_text(ctx, "Avoid asteroids!",  tutorial_font, (GRect) { .origin = {0, 130}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
		else if (time_ticks < 9000 / UPDATE_INTERVAL)
			graphics_draw_text(ctx, "Good luck!",  tutorial_font, (GRect) { .origin = {0, 130}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
	}
	
	if (!started) // Title screen
	{
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
	else if (paused && !god_mode) // "PAUSED" box
	{
		graphics_draw_rect(ctx, GRect(44, 76, 55, 16));
		graphics_context_set_fill_color(ctx, GColorWhite); 
		graphics_fill_rect(ctx, GRect(45, 77, 53, 14), 0, GCornerNone);
		GFont *paused_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
		graphics_draw_text(ctx, "PAUSED", paused_font, (GRect) { .origin = {0, 75}, .size = {144, 30} }, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
	}
	
	// Some debug info
	if (show_debug)
	{
		GFont *debug_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
		char debug_text[200];
		snprintf(debug_text, sizeof(debug_text), "SpeedX: %d\r\nSpeedY: %d\r\nShipX: %d\r\nShipY: %d\r\nAsteroids: %d\r\nInterval: %d\r\nTime: %d", (int)speed_x, (int)speed_y, (int)ship_x, (int)ship_y, asteroid_count, asteroid_interval, (int)time_ticks);
		graphics_draw_text(ctx, debug_text, debug_font, (GRect) { .origin = {0, 0}, .size = {144, 168} }, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
	}
}

static void destroy_ui(void) {
	window_destroy(s_window);
	layer_destroy(gfx_layer);
	gbitmap_destroy(ship_bitmap);
}

static void free_asteroids()
{
	Asteroid* asteroid = first_asteroid;
	while (asteroid)
	{
		Asteroid* next = asteroid->next;
		gpath_destroy(asteroid->draw_path);
		free(asteroid->path.points);
		free(asteroid);
		asteroid = next;
	}
	first_asteroid = NULL;
}

static void handle_window_unload(Window* window) {
	app_timer_cancel(timer);	// Cancel timer
	accel_data_service_unsubscribe();	// Disabling accelerometer
	app_focus_service_unsubscribe(); // Unsubscribing from focus
	free_asteroids();									// Free memory from asteroids
  destroy_ui();									// Free memory from UI
	persist_write_int(0, hi_score); // Saving hi score
}

// Automatic pause on call/notification
void app_in_focus_callback(bool in_focus)
{
	if (!in_focus && started && !game_over) paused = true;
}

// Any button
static void click_handler(ClickRecognizerRef recognizer, void *context) 
{
	if (!started || game_over) reset_game();
	else paused = !paused;
}

static void long_up_handler(ClickRecognizerRef recognizer, void *context) 
{
	// God mode! Just for testing and screenshots.
	god_mode = !god_mode;
	vibes_short_pulse();
}

static void long_select_handler(ClickRecognizerRef recognizer, void *context) 
{
	// Some debug info...
	show_debug = !show_debug;
}

static void long_down_handler(ClickRecognizerRef recognizer, void *context) 
{
	// Experimental "shape" mode
	use_shapes = !use_shapes;
}


static void config_provider(void *context)
{
  window_single_click_subscribe(BUTTON_ID_UP, click_handler);
	window_single_click_subscribe(BUTTON_ID_SELECT, click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, click_handler);
	window_long_click_subscribe(BUTTON_ID_UP, 10000, long_up_handler, NULL);
	window_long_click_subscribe(BUTTON_ID_SELECT, 10000, long_select_handler, NULL);
	window_long_click_subscribe(BUTTON_ID_DOWN, 3000, long_down_handler, NULL);
}

void reset_game()
{
	srand(time(NULL));
	free_asteroids();
	time_ticks = 0;
	ship_x = SCREEN_WIDTH / 2;
	ship_y = SCREEN_HEIGHT / 2;	
	asteroid_interval = ASTEROID_START_INTERVAL;
	score = 0;
	game_over_stop_time = 0;
	game_over =  false;
	paused = false;
	use_shapes = true;
	started = true;
}

void show_game() {
	reset_game();	
	
	// For title screen...
	game_over = true;
	started = false;
	asteroid_interval = 5;
	use_shapes = false;
	
	// Reading hi-score from memory
	if (persist_exists(0))	
		hi_score = persist_read_int(0);
	else hi_score = 0;
	
	// Loading ship bitmap
	ship_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SHIP);

	// Creating window
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
	
	// Game timer
	timer = app_timer_register(UPDATE_INTERVAL, (AppTimerCallback) update_timer, NULL);
	// Acceleromter
	accel_data_service_subscribe(0, NULL);
	accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
	// Automatic pause
	app_focus_service_subscribe(app_in_focus_callback);
}
