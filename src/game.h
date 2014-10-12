#ifndef _GAME_H_
#define _GAME_H_

#define VERSION "v1.5"
#define ACC_DEATH_ZONE 0.5
#define SHIP_WIDTH 10
#define SHIP_HEIGHT 15
#define SCREEN_WIDTH 144
#define SCREEN_HEIGHT 168
#define UPDATE_INTERVAL 25
#define ASTEROID_START_INTERVAL 40
#define MIN_TUTORIAL_SCORE 200

typedef struct Asteroid {
	float x;
	float y;
	float speed_x;
	float speed_y;
	int size;
	GPathInfo path;
	GPath *draw_path;
	int rot;
	int rot_speed;
	struct Asteroid* next;
} Asteroid;

void show_game();
void reset_game();

#endif
