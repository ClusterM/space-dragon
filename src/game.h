#define VERSION "v1.3"

typedef struct Meteor {
	float x;
	float y;
	float speed_x;
	float speed_y;
	int size;
	GPathInfo path;
	GPath *draw_path;
	int rot;
	int rot_speed;
	struct Meteor* next;
} Meteor;

void show_game();
void reset_game();