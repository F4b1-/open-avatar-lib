#ifndef STORE_FOLLOW_CAMERA_H
#define STORE_FOLLOW_CAMERA_H

// A camera that follows an avatar: orbiting it at a distance, turned by the right stick, kept inside the room; and the
// left stick's direction on the ground as the camera sees it, for walking.

typedef struct {
	float yaw;      // radians about y; 0 looks along -z
	float pitch;    // radians above the level
	float distance; // metres from the point it looks at
	float height;   // metres above the ground it looks at
	// The room it stays inside: x within +-room[0], below room[1], z within +-room[2] (metres).
	float room[3];
} FollowCamera;

// Turns it by a stick's x (round) and y (up and down), each -1 to 1, as far as one frame goes.
void follow_camera_turn(FollowCamera *c, float x, float y);
// A stick's direction (x right, y up) on the ground, as the camera faces: out[0] along x, out[1] along z.
void follow_camera_direction(const FollowCamera *c, float x, float y, float out[2]);
// The view-projection looking at the avatar standing at (x, z), for a screen of aspect width over height; and its
// right, up and forward axes, for lighting the title's own shapes.
void follow_camera_view(const FollowCamera *c, float x, float z, float aspect, float vp[16], float axes[3][3]);

#endif
