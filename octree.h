#ifndef OCTREE_H
#define OCTREE_H

#include <stdint.h>

#define row_two_size 64 //8*8 nodes
#define row_four_size 4096 //8*8*8*8
#define shift_red 11 //offset to look at red bits
#define shift_green 5 //offset to look at green bits
#define shift_blue 0 //offset to look at blue bits
#define red_offset 256 // 2^8 to look at high 4 bits
#define green_offset 16 // 2^4 to look at green bits
#define blue_offset 1
#define bit_mask 0xF //bit mask to look at 4 bits
#define six_bit_mask 0x3F // looks at the last 6 bits
#define five_bit_mask 0x1F // looks at the last 5 bits
#define specific_colors 128
#define two_bit_mask 0x3

struct octree_node
{
	size_t matches;
	size_t index;
	size_t red_total;
	size_t green_total;
	size_t blue_total;
};

void build_octree(struct octree_node* row_four, int* row_four_palette_indices);

void process_pixel(uint16_t pixel, struct octree_node* row_four);

void make_palette(uint8_t palette[192][3], struct octree_node* row_four, int* row_four_palette_indices);

uint8_t search_palette(uint16_t pixel, int* row_four_palette_indices);

void build_row_two(struct octree_node* row_two, int size);

#endif
