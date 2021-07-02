#include "fpga_data.h"

unsigned int fpga::read_le16(unsigned char const *p)
{
#pragma HLS inline region

	return ((unsigned int) p[0]) | ((unsigned int) p[1] << 8);
}

/* Build fixed Huffman trees */
void fpga::build_fixed_trees(struct fpga::tinf_tree *lt, struct fpga::tinf_tree *dt)
{
#pragma HLS inline region

	int i;

	// Build fixed literal/length tree
	for (i = 0; i < 16; ++i) lt->counts[i] = 0;

	lt->counts[7] = 24;
	lt->counts[8] = 152;
	lt->counts[9] = 112;

	for (i = 0; i <  24; ++i) lt->symbols[i] = 256 + i;

	for (i = 0; i < 144; ++i) lt->symbols[24 + i] = i;

	for (i = 0; i <   8; ++i) lt->symbols[24 + 144 + i] = 280 + i;

	for (i = 0; i < 112; ++i) lt->symbols[24 + 144 + 8 + i] = 144 + i;

	// Build fixed distance tree
	for (i = 0; i < 16; ++i) dt->counts[i] = 0;

	dt->counts[5] = 32;

	for (i = 0;  i <  32; ++i) dt->symbols[i] = i;
	for (i = 32; i < 288; ++i) dt->symbols[i] = 0;

	lt->max_sym = 285;
	dt->max_sym = 29;
}

/* Given an array of code lengths, build a tree */
int fpga::build_tree(struct fpga::tinf_tree *t, const unsigned char *lengths, unsigned int num)
{
#pragma HLS inline region

	unsigned short offs[16];
	unsigned int i, num_codes, available;

	assert(num <= 288);

	for (i = 0; i < 16; ++i) t->counts[i] = 0;

	t->max_sym = -1;

	/* Count number of codes for each non-zero length */
	build_tree_1: for (i = 0; i < num; ++i)
	{
        //#pragma HLS UNROLL factor=15

		assert(lengths[i] <= 15);

		if (lengths[i])
		{
			t->max_sym = i;
			t->counts[lengths[i]]++;
		}
	}

	/* Compute offset table for distribution sort */
	build_tree_2: for (available = 1, num_codes = 0, i = 0; i < 16; ++i)
	{
        //#pragma HLS UNROLL factor=15

		unsigned int used = t->counts[i];

		/* Check length contains no more codes than available */
		if (used > available) return fpga::TINF_DATA_ERROR;

		available = 2 * (available - used);

		offs[i] = num_codes;
		num_codes += used;
	}

	/*
	 * Check all codes were used, or for the special case of only one
	 * code that it has length 1
	 */
	if ((num_codes > 1 && available > 0) || (num_codes == 1 && t->counts[1] != 1)) return fpga::TINF_DATA_ERROR;

	/* Fill in symbols sorted by code */
	build_tree_3: for (i = 0; i < num; ++i)
	{
        //#pragma HLS UNROLL factor=15
		if (lengths[i]) t->symbols[offs[lengths[i]]++] = i;
	}

	/*
	 * For the special case of only one code (which will be 0) add a
	 * code 1 which results in a symbol that is too large
	 */
	if (num_codes == 1)
	{
		t->counts[1] = 2;
		t->symbols[1] = t->max_sym + 1;
	}

	return fpga::TINF_OK;
}

/* -- Decode functions -- */

int fpga::refill(struct fpga::tinf_data *d, int num)
{
#pragma HLS inline region

	assert(num >= 0 && num <= 32);

	unsigned int help;

	/* Read bytes until at least num bits available */
	//while (d->bitcount < num)
	refill: for(int i = 0; i < 3; ++i)
	{
	#pragma HLS PIPELINE

		if(d->bitcount >= num) break;

		if(d->source != d->source_end)
		////if(d->shift != d->sourceLen-1)
		{
			//d->tag |= (unsigned int) *d->source++ << d->bitcount;
			help = (unsigned int) *d->source << d->bitcount;
			d->tag |= help;
			*d->source++;

			d->src_shift++;
		}
		else d->overflow = 1;

		d->bitcount += 8;
	}

	assert(d->bitcount <= 32);

	return 0;
}

unsigned int fpga::getbits_no_refill(struct fpga::tinf_data *d, int num)
{
#pragma HLS inline region

	unsigned int bits;

	assert(num >= 0 && num <= d->bitcount);

	/* Get bits from tag */
	bits = d->tag & ((1UL << num) - 1);

	/* Remove bits from tag */
	d->tag >>= num;
	d->bitcount -= num;

	return bits;
}

/* Get num bits from source stream */
unsigned int fpga::getbits(struct fpga::tinf_data *d, int num)
{
#pragma HLS inline region

	fpga::refill(d, num);
	return fpga::getbits_no_refill(d, num);
}

/* Read a num bit value from stream and add base */
unsigned int fpga::getbits_base(struct fpga::tinf_data *d, int num, int base)
{
#pragma HLS inline region
	return base + (num ? fpga::getbits(d, num) : 0);
}

/* Given a data stream and a tree, decode a symbol */
int fpga::decode_symbol(struct fpga::tinf_data *d, const struct fpga::tinf_tree *t)
{
	int base = 0, offs = 0;
	int len;

	/*
	 * Get more bits while code index is above number of codes
	 *
	 * Rather than the actual code, we are computing the position of the
	 * code in the sorted order of codes, which is the index of the
	 * corresponding symbol.
	 *
	 * Conceptually, for each code length (level in the tree), there are
	 * counts[len] leaves on the left and internal nodes on the right.
	 * The index we have decoded so far is base + offs, and if that
	 * falls within the leaves we are done. Otherwise we adjust the range
	 * of offs and add one more bit to it.
	 */
	decode_symbol: for (len = 1; len <= 15; ++len)
	{
        #pragma HLS PIPELINE

		offs = 2 * offs + fpga::getbits(d, 1);

		assert(len <= 15);

		if (offs < t->counts[len]) break;

		base += t->counts[len];
		offs -= t->counts[len];
	}

	assert(base + offs >= 0 && base + offs < 288);

	return t->symbols[base + offs];
}

/* Given a data stream, decode dynamic trees from it */
int fpga::decode_trees(struct fpga::tinf_data *d, struct fpga::tinf_tree *lt, struct fpga::tinf_tree *dt)
{
#pragma HLS inline region

	int res;

	unsigned char lengths[288 + 32];

	/* Special ordering of code length codes */
	static const unsigned char clcidx[19] = {
		16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
		11,  4, 12, 3, 13, 2, 14, 1, 15
	};

	/* Get 5 bits HLIT (257-286) */
	unsigned int hlit  = fpga::getbits_base(d, 5, 257);

	/* Get 5 bits HDIST (1-32) */
	unsigned int hdist = fpga::getbits_base(d, 5, 1);

	/* Get 4 bits HCLEN (4-19) */
	unsigned int hclen = fpga::getbits_base(d, 4, 4);

	/*
	 * The RFC limits the range of HLIT to 286, but lists HDIST as range
	 * 1-32, even though distance codes 30 and 31 have no meaning. While
	 * we could allow the full range of HLIT and HDIST to make it possible
	 * to decode the fixed trees with this function, we consider it an
	 * error here.
	 *
	 * See also: https://github.com/madler/zlib/issues/82
	 */
	if(hlit > 286 || hdist > 30) return fpga::TINF_DATA_ERROR;

	for(unsigned int i = 0; i < 19; ++i) lengths[i] = 0;

	/* Read code lengths for code length alphabet */
	decode_trees_1: for(unsigned int i = 0; i < hclen; ++i)
	{
        #pragma HLS PIPELINE
		/* Get 3 bits code length (0-7) */
		unsigned int clen = fpga::getbits(d, 3);
		lengths[clcidx[i]] = clen;
	}

	/* Build code length tree (in literal/length tree to save space) */
	res = fpga::build_tree(lt, lengths, 19);
	if (res != fpga::TINF_OK) return res;

	/* Check code length tree is not empty */
	if (lt->max_sym == -1) return fpga::TINF_DATA_ERROR;

	/* Decode code lengths for the dynamic trees */
	unsigned int num, length;
	decode_trees_2: for(num = 0; num < hlit + hdist; )
	{
        #pragma HLS PIPELINE

		int sym = fpga::decode_symbol(d, lt);

		if (sym > lt->max_sym) return fpga::TINF_DATA_ERROR;

		switch (sym)
		{
		  case 16:
			/* Copy previous code length 3-6 times (read 2 bits) */
			if(num == 0) return fpga::TINF_DATA_ERROR;
			sym = lengths[num - 1];
			length = fpga::getbits_base(d, 2, 3);
			break;
		  case 17:
			/* Repeat code length 0 for 3-10 times (read 3 bits) */
			sym = 0;
			length = fpga::getbits_base(d, 3, 3);
			break;
		  case 18:
			/* Repeat code length 0 for 11-138 times (read 7 bits) */
			sym = 0;
			length = fpga::getbits_base(d, 7, 11);
			break;
		  default:
			/* Values 0-15 represent the actual code lengths */
			length = 1;
			break;
		}

		if (length > hlit + hdist - num) return fpga::TINF_DATA_ERROR;

		//while (length--)
		for(int i = length; i > 0; --i)
		{
		#pragma HLS UNROLL factor=15
		    lengths[num++] = sym;
		}
	}

	/* Check EOB symbol is present */
	if (lengths[256] == 0) return fpga::TINF_DATA_ERROR;

	/* Build dynamic trees */
	res = fpga::build_tree(lt, lengths, hlit);
	if (res != fpga::TINF_OK) return res;

	res = fpga::build_tree(dt, lengths + hlit, hdist);
	if (res != fpga::TINF_OK) return res;

	return fpga::TINF_OK;
}

/* -- Block inflate functions -- */

/* Given a stream and two trees, inflate a block of data */
int fpga::inflate_block_data(struct fpga::tinf_data *d, struct fpga::tinf_tree *lt, struct fpga::tinf_tree *dt)
{
#pragma HLS inline region

	/* Extra bits and base tables for length codes */
	static const unsigned char length_bits[30] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
		1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
		4, 4, 4, 4, 5, 5, 5, 5, 0, 127
	};

	static const unsigned short length_base[30] = {
		 3,  4,  5,   6,   7,   8,   9,  10,  11,  13,
		15, 17, 19,  23,  27,  31,  35,  43,  51,  59,
		67, 83, 99, 115, 131, 163, 195, 227, 258,   0
	};

	/* Extra bits and base tables for distance codes */
	static const unsigned char dist_bits[30] = {
		0, 0,  0,  0,  1,  1,  2,  2,  3,  3,
		4, 4,  5,  5,  6,  6,  7,  7,  8,  8,
		9, 9, 10, 10, 11, 11, 12, 12, 13, 13
	};

	static const unsigned short dist_base[30] = {
		   1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
		  33,   49,   65,   97,  129,  193,  257,   385,   513,   769,
		1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
	};

	inflate_block_data: for(unsigned int j = 0; j < 65535; ++j)
	{
        #pragma HLS PIPELINE

		int sym = fpga::decode_symbol(d, lt);

		// Check for overflow in bit reader
		if (d->overflow) return fpga::TINF_DATA_ERROR;

		if (sym < 256)
		{
			if (d->dest == d->dest_end) return fpga::TINF_BUF_ERROR;

			*d->dest++ = sym;

			d->dst_shift++;
		}
		else
		{
			int length, dist, offs;
			int i;

			// Check for end of block
			if (sym == 256) return fpga::TINF_OK;

			// Check sym is within range and distance tree is not empty
			if (sym > lt->max_sym || sym - 257 > 28 || dt->max_sym == -1) return fpga::TINF_DATA_ERROR;

			sym -= 257;

			// Possibly get more bits from length code
			length = fpga::getbits_base(d, length_bits[sym], length_base[sym]);
			dist = fpga::decode_symbol(d, dt);

			// Check dist is within range
			if (dist > dt->max_sym || dist > 29) return fpga::TINF_DATA_ERROR;

			// Possibly get more bits from distance code
			offs = fpga::getbits_base(d, dist_bits[dist], dist_base[dist]);

			//if (offs > d->dest - d->dest_start) return fpga::TINF_DATA_ERROR;
			//if (d->overflow) return fpga::TINF_DATA_ERROR;
			//if (d->dest_end - d->dest < length) return TINF_BUF_ERROR;

			// Copy match
			for (i = 0; i < length; ++i)
			{
			#pragma HLS UNROLL factor=15
			    d->dest[i] = d->dest[i - offs];
			}

			d->dest += length;
			d->dst_shift += length;
		}
	}

	return fpga::TINF_OK;
}

/* Inflate an uncompressed block of data */
int fpga::inflate_uncompressed_block(struct fpga::tinf_data *d)
{
#pragma HLS inline region

	unsigned int length, invlength;

	if (d->source_end - d->source < 4) return fpga::TINF_DATA_ERROR;

	/* Get length */
	length = fpga::read_le16(d->source);

	/* Get one's complement of length */
	invlength = fpga::read_le16(d->source + 2);

	/* Check length */
	if (length != (~invlength & 0x0000FFFF)) return fpga::TINF_DATA_ERROR;

	d->source += 4;
	d->src_shift += 4;

	if (d->source_end - d->source < length) return fpga::TINF_DATA_ERROR;

	if (d->dest_end - d->dest < length) return fpga::TINF_BUF_ERROR;

	/* Copy block */
	//while (length--)
	for(int i = length; i > length; --i)
	{
	#pragma HLS UNROLL factor=15
	    *d->dest++ = *d->source++;
	    d->src_shift++;
	    d->dst_shift++;
	}

	/* Make sure we start next block on a byte boundary */
	d->tag = 0;
	d->bitcount = 0;

	return fpga::TINF_OK;
}

/* Inflate a block of data compressed with fixed Huffman trees */
int fpga::inflate_fixed_block(struct fpga::tinf_data *d)
{
#pragma HLS inline region

	/* Build fixed Huffman trees */
	fpga::build_fixed_trees(&d->ltree, &d->dtree);

	/* Decode block using fixed trees */
	return fpga::inflate_block_data(d, &d->ltree, &d->dtree);
}

/* Inflate a block of data compressed with dynamic Huffman trees */
int fpga::inflate_dynamic_block(struct tinf_data *d)
{
#pragma HLS inline region

	/* Decode trees from stream */
	int res = fpga::decode_trees(d, &d->ltree, &d->dtree);
	if(res != fpga::TINF_OK) return res;

	/* Decode block using decoded trees */
	return fpga::inflate_block_data(d, &d->ltree, &d->dtree);
}

/* Inflate stream from source to dest */
extern "C" {
void fpga_uncompress(unsigned char *dest,   unsigned int *dLen,
                     unsigned char *source, unsigned int *sourceLen,
                     unsigned int *tag, unsigned int *bitcount, unsigned int *overflow,
		             int *bfinal, int *err)
{
#pragma HLS INTERFACE m_axi port=dest      offset=slave bundle=gmem
#pragma HLS INTERFACE m_axi port=dLen      offset=slave bundle=gmem

#pragma HLS INTERFACE m_axi port=source    offset=slave bundle=gmem
#pragma HLS INTERFACE m_axi port=sourceLen offset=slave bundle=gmem

#pragma HLS INTERFACE m_axi port=tag       offset=slave bundle=gmem
#pragma HLS INTERFACE m_axi port=bitcount  offset=slave bundle=gmem
#pragma HLS INTERFACE m_axi port=overflow  offset=slave bundle=gmem

#pragma HLS INTERFACE m_axi port=bfinal    offset=slave bundle=gmem
#pragma HLS INTERFACE m_axi port=err       offset=slave bundle=gmem

#pragma HLS INTERFACE s_axilite port=dest      bundle=control
#pragma HLS INTERFACE s_axilite port=dLen      bundle=control

#pragma HLS INTERFACE s_axilite port=source    bundle=control
#pragma HLS INTERFACE s_axilite port=sourceLen bundle=control

#pragma HLS INTERFACE s_axilite port=tag       bundle=control
#pragma HLS INTERFACE s_axilite port=bitcount  bundle=control
#pragma HLS INTERFACE s_axilite port=overflow  bundle=control

#pragma HLS INTERFACE s_axilite port=bfinal    bundle=control
#pragma HLS INTERFACE s_axilite port=err       bundle=control

#pragma HLS INTERFACE s_axilite port=return bundle=control
//#pragma HLS INTERFACE ap_ctrl_chain port=return bundle=control

	printf("source: %ld, sourcelen: %d, dest: %ld, destlen: %d, tag: %d, bitcount: %d, overflow: %d\n",
			source, sourceLen[0], dest, dLen[0], tag[0], bitcount[0], overflow[0]);

	err[0] = fpga::TINF_OK;

    // Initialise data
    struct fpga::tinf_data d;
	
	d.source = source;
	d.source_end = source + sourceLen[0];
	d.sourceLen = sourceLen[0];
	d.src_shift = 0;
	d.dst_shift = 0;
	d.tag = tag[0];
	d.bitcount = bitcount[0];
	d.overflow = overflow[0];

	d.dest = dest;
	d.dest_start = d.dest;
	d.dest_end = d.dest + dLen[0];

	// Read final block flag
	bfinal[0] = fpga::getbits(&d, 1);

	// Read block type (2 bits)
	unsigned int btype = fpga::getbits(&d, 2);

	printf("type: %d, final: %d\n", btype, bfinal[0]);

	// Decompress block
	switch(btype)
	{
	  case 0:
		// Decompress uncompressed block
		err[0] = fpga::inflate_uncompressed_block(&d);
		break;
	  case 1:
		// Decompress block with fixed Huffman trees
		err[0] = fpga::inflate_fixed_block(&d);
		break;
	  case 2:
		// Decompress block with dynamic Huffman trees
		err[0] = fpga::inflate_dynamic_block(&d);
		break;
	  default:
		err[0] = fpga::TINF_DATA_ERROR;
		break;
	}

	//source = d.source;
	sourceLen[0] -= d.src_shift;
	tag[0] = d.tag;
	bitcount[0] = d.bitcount;
	overflow[0] = d.overflow;

	//dest = d.dest;
	dLen[0] -= d.dst_shift;

	printf("source: %ld, sourcelen: %d, dest: %ld, destlen: %d, shift: %d %d, tag: %d, bitcount: %d, overflow: %d\n",
			source, sourceLen[0], dest, dLen[0], d.src_shift, d.dst_shift, tag[0], bitcount[0], overflow[0]);
}}
