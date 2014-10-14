#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "octree.h"

/*
* build_row_two
* DESCRIPTION: Sets up octree array values used in the first run over
* INPUTS: row_two: array representing the nodes on the second row of the octree
* 					size: the size of the second row of the octree
* OUTPUTS: none
* RETURN VALUE: none
* SIDE EFFECTS: initializes and array of size row_two_size.
*/
void build_row_two(struct octree_node* row_two, int size)
{
	int i;
	for(i = 0; i < row_two_size; i++)
	{
		row_two[i].index = i;
		row_two[i].matches = 0;
		row_two[i].red_total = 0;
		row_two[i].green_total = 0;
		row_two[i].blue_total = 0;
	}
}

/*
* build_octree
* DESCRIPTION: Sets up octree array values used in the first run over
* INPUTS: row_four: array representing the nodes on the fourth level of the octree
* 					row_four_palette_indices: array that mapes row_four pixels to palette
* OUTPUTS: none
* RETURN VALUE: none
* SIDE EFFECTS: row_four and the palette map array are representing
*/

void build_octree(struct octree_node* row_four, int* row_four_palette_indices)
{
	int i;
	for (i = 0; i < row_four_size; i++)
	{
		row_four[i].index = i;
		row_four[i].matches = 0;
		row_four[i].red_total = 0;
		row_four[i].green_total = 0;
		row_four[i].blue_total = 0;
		row_four_palette_indices[i] = -1;
	}
}

/*
 * process_pixel
 * DESCRIPTION: reads in a pixel, find the right node and update the value
 * INPUTS: pixel: pixel to be processed
 		   row_four: the array containing the node in row four
 * OUTPUTS: none
 * RETURN VALUE: none
 * SIDE EFFECTS: an octree_node is changed
 */

void process_pixel(uint16_t pixel, struct octree_node* row_four)
{
	int i = 0; // used to hold the index of the pixel
	i += (red_offset * (pixel >> (shift_red + 1)));
	i += (green_offset * ((pixel >> (shift_green + 2)) & bit_mask));
	i += ((pixel >> (shift_blue + 1)) & bit_mask);

	row_four[i].matches++;
	// pixels correspond to RGB [5:6:5] so red and blue need to be extended
	//multiplied by two to sign extend to 6 bits
	row_four[i].red_total += ((pixel >> shift_red) *2);
	row_four[i].green_total += ((pixel >> shift_green) & six_bit_mask);
	//multiplied by two to sign extend to 6 bits
	row_four[i].blue_total += (((pixel >> shift_blue) & five_bit_mask) *2); 
}


/*
* compare_counts
* DESCRIPTION: comparison function to be used in qsort
* INPUTS: node_one: first node to be used in compare
		  node_two: second node to be used in compare
* OUTPUTS: none
* RETURN VALUE: Positive int if node_two > node_one, and negative int if node_one > node_two
* SIDE EFFECTS: none
*/

static int compare_counts(void const *node_one, void const *node_two)
{
	return ((struct octree_node*)node_two)->matches - ((struct octree_node*)node_one)->matches;
}

/*
* make_palette
* DESCRIPTION: Called after first pass. Creates a palette using all nodes
* INPUTS: palette: palette array
		  row_four: array that is providing the octree_node information
		  row_four_palette_indices: array that maps the palette values to pixel values,
		  							useful for the searching during the second pass.
* OUTPUTS: A palette with 128 wmost popular RGB values with 64 general RGB values
* RETURN VALUE: none
* SIDE EFFECTS: palette is changed with new values
*/

void make_palette(uint8_t palette[192][3], struct octree_node* row_four, int* row_four_palette_indices)
{
	// call quicksort to get the most ocmmon colors in order
	qsort(row_four, row_four_size, sizeof(struct octree_node), &compare_counts);
	int i;

	//specific_colors = 128. stores them into palette (total_value/total_count)
	for(i = 0; i < specific_colors; i++)
	{
		if(row_four[i].matches != 0)
		{
			palette[i][0] = ((row_four[i].red_total) / (row_four[i].matches));
			palette[i][1] = ((row_four[i].green_total) / (row_four[i].matches));
			palette[i][2] = ((row_four[i].blue_total) / (row_four[i].matches));
		}
		row_four_palette_indices[row_four[i].index] = i;
	}

	struct octree_node row_two[row_two_size];
	build_row_two(row_two, row_two_size);
	/* take the row_four nodes that weren't used and add their values into row_two values
	* then take the average to get the row_two values which are the 64 general colors
	*/
	int row_two_index;
	for(i = specific_colors; row_four[i].matches != 0 && i < row_four_size; i++)
	{
		row_two_index = 0;
		row_two_index += (16*((row_four[i].index >> (shift_red - 1)) & two_bit_mask));
		row_two_index += (4*((row_four[i].index >> (shift_green + 1)) & two_bit_mask));
		row_two_index += ((row_four[i].index >> (shift_blue + 2)) & two_bit_mask);
		// now update the totals
		row_two[row_two_index].matches += row_four[i].matches;
		row_two[row_two_index].red_total += row_four[i].red_total;
		row_two[row_two_index].green_total += row_four[i].green_total;
		row_two[row_two_index].blue_total += row_four[i].blue_total;
	}

	//copy the row_two values into the palette
	for(i = 0; i < row_two_size; i++)
	{
		// if there were row_four values corresponding to the row_two values, use the info we have.
		if(row_two[i].matches != 0)
		{
			/* start from 129 because 128 of the colors were already filled */
			palette[i + specific_colors][0] = (row_two[i].red_total / row_two[i].matches);
			palette[i + specific_colors][1] = (row_two[i].green_total / row_two[i].matches);
			palette[i + specific_colors][2] = (row_two[i].blue_total / row_two[i].matches);
		}
		//otherwise there weren't any pixels for the row_two node and just write the 2 MSB's.
		else
		{
			palette[i + specific_colors][0] = (16*((i >> 4) & two_bit_mask));
			palette[i + specific_colors][1] = (16*((i >> 2) & two_bit_mask));
			palette[i + specific_colors][2] = (16*(i & two_bit_mask));
		}
	}
}

/*
* search_palette
* DESCRIPTION: returns an index in the palette that corresponds to a pixel input
* INPUTS: pixel: pixel to find palette index for
*				 row_four_indicies: array that maps the palette values to pixel values,
				 this allows for simpler searching during the second pass
* OUTPUTS: none
* RETURN VALUE: index in palette that corresponds to the pixel input
* SIDE EFFECTS: none
*/

uint8_t search_palette(uint16_t pixel, int* row_four_palette_indices)
{
	//check to see if there's a matching color in the palette
	int index = 0;
	index += (red_offset * (pixel >> (shift_red + 1)));
	index += (green_offset * ((pixel >> (shift_green + 2)) & bit_mask));
	index += ((pixel >> (shift_blue + 1)) & bit_mask);

	// -1 indicates that no index contains the pixel
	if(row_four_palette_indices[index] != -1)
	{
		return row_four_palette_indices[index];
	}
	// otherwise, we'll pick a row_two color, which starts at 128 in the palette
	else
	{
		index = 128;
		index += (16* (pixel >> (shift_red + 3)));
		index += (4* ((pixel >> (shift_green + 4)) & two_bit_mask));
		index += ((pixel >> (shift_blue + 3)) & two_bit_mask);
		return index;
	}
}
