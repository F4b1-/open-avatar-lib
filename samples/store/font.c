// The stroke font (font.h): each glyph a list of strokes, four digits each, x0 y0 x1 y1 on the 4 x 6 grid.

#include "font.h"

#include <math.h>
#include <string.h>

#include "mesh.h"

static const char *const letters[26] = {
	"00040426264444400343",                     // A
	"0006063636454544443303333342424141303000", // B
	"460606000040",                             // C
	"000606363645454141303000",                 // D
	"4606060000400333",                         // E
	"460606000333",                             // F
	"46060600004040434323",                     // G
	"000640460343",                             // H
	"202616361030",                             // I
	"464040000002",                             // J
	"000646030340",                             // K
	"06000040",                                 // L
	"0006062323464640",                         // M
	"000606404046",                             // N
	"0006064646404000",                         // O
	"0006064646434303",                         // P
	"000606464641413030002240",                 // Q
	"00060646464343031340",                     // R
	"46060603034343404000",                     // S
	"06462620",                                 // T
	"060000404046",                             // U
	"06202046",                                 // V
	"0610102323303046",                         // W
	"00460640",                                 // X
	"062346232320",                             // Y
	"064646000040",                             // Z
};

static const char zero[] = "1016163636303010";

static const char *strokes_of(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z')
		return letters[c - 'A'];
	return c == '0' ? zero : "";
}

float font_width(const char *text)
{
	const size_t length = strlen(text);
	return length ? (float)(length * FONT_ADVANCE - 2) : 0.0f;
}

static void normalise(float v[3])
{
	const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	for (uint32_t k = 0; k < 3; k++)
		v[k] = length > 0.0f ? v[k] / length : 0.0f;
}

// One stroke from a to b, as a quad thickness wide, its ends carried on by half that so that strokes join.
static void stroke(const float *a, const float *b, const float *normal, float thickness, const float color[4],
	uint32_t material)
{
	float along[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	normalise(along);
	float side[3] = { normal[1] * along[2] - normal[2] * along[1], normal[2] * along[0] - normal[0] * along[2],
		normal[0] * along[1] - normal[1] * along[0] };
	normalise(side);
	const float h = 0.5f * thickness;
	float p[4][3];
	for (uint32_t k = 0; k < 3; k++) {
		p[0][k] = a[k] - along[k] * h - side[k] * h;
		p[1][k] = b[k] + along[k] * h - side[k] * h;
		p[2][k] = b[k] + along[k] * h + side[k] * h;
		p[3][k] = a[k] - along[k] * h + side[k] * h;
	}
	mesh_quad4(p[0], p[1], p[2], p[3], normal, color, material);
}

void font_text(const char *text, const float origin[3], const float right[3], const float up[3], float thickness,
	const float color[4], uint32_t material)
{
	float normal[3] = { right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2],
		right[0] * up[1] - right[1] * up[0] };
	normalise(normal);
	for (size_t i = 0; text[i]; i++) {
		const char *s = strokes_of(text[i]);
		const float x = (float)(i * FONT_ADVANCE);
		for (; s[0] && s[1] && s[2] && s[3]; s += 4) {
			const float g[4] = { (float)(s[0] - '0'), (float)(s[1] - '0'), (float)(s[2] - '0'), (float)(s[3] - '0') };
			float a[3], b[3];
			for (uint32_t k = 0; k < 3; k++) {
				a[k] = origin[k] + right[k] * (x + g[0]) + up[k] * g[1];
				b[k] = origin[k] + right[k] * (x + g[2]) + up[k] * g[3];
			}
			stroke(a, b, normal, thickness, color, material);
		}
	}
}
