#define VERSION "v1.2"

typedef struct Meteor {
	float x;
	float y;
	float speed_x;
	float speed_y;
	int size;
	struct Meteor* next;
} Meteor;

void show_game();
void reset_game();