#ifndef FPGA_H_INCLUDED
#define FPGA_H_INCLUDED

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#if defined(UINT_MAX) && (UINT_MAX) < 0xFFFFFFFFUL
#  error "tinf requires unsigned int to be at least 32-bit"
#endif

namespace fpga {
  
static const unsigned int tinf_crc32tab[16] = {
	0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190,
	0x6B6B51F4, 0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344,
	0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278,
	0xBDBDF21C
};

/***************************************************************//**
* Enum type that maps error codes                                  
********************************************************************/
typedef enum {
	TINF_OK          =  0, /**< Success */
	TINF_DATA_ERROR  = -3, /**< Input error */
	TINF_BUF_ERROR   = -5, /**< Not enough room for output */
	TINF_FILE_ERROR  = -7  /**< Not enoug diskspace or wrong file permissions */
} tinf_error_code;

/***************************************************************//**
* Enum type as a mask for the flag byte (exactly the 4th)          
* specified in rfc1952.                                            
********************************************************************/
typedef enum {
    FTEXT    =  1, /**< the file is probably ASCII text */
    FHCRC    =  2, /**< CRC16 for the gzip header is present */
    FEXTRA   =  4, /**< optional extra fields are present */
    FNAME    =  8, /**< an original file name is present */
    FCOMMENT = 16  /**< a zero-terminated file comment is present */
} tinf_gzip_flag;

/***************************************************************//**
* Data structure that contains a Huffman tree                      
********************************************************************/
struct tinf_tree {
	unsigned short counts[16];   /* Number of codes with a given length */
	unsigned short symbols[288]; /* Symbols sorted by code */
	int max_sym;
};

/***************************************************************//**
* Data structure that contains information on compressed and       
* uncompressed files                                               
********************************************************************/
struct tinf_data {
    unsigned char *source;     /**< pointer to current position in input array */
    unsigned char *source_end; /**< pointer to end of input array */
    unsigned int sourceLen;    /**< length of th the data located at *source */
    unsigned int src_shift;    /**< number of source pointer shifts */
    unsigned int dst_shift;    /**< number of output pointer shifts */
    unsigned int tag;
    int bitcount;
    int overflow;

    unsigned char *dest_start; /**< pointer to begin of output array */
    unsigned char *dest;       /**< pointer to current position in output array */
    unsigned char *dest_end;   /**< pointer to end of output array */

    struct tinf_tree ltree;    /**< Literal/length tree */
    struct tinf_tree dtree;    /**< Distance tree */
};
 
/***************************************************************//**
* \brief Reads 16 bit and converts to unsigned integer             
*                                                                  
* The function reads exactly 2 bytes from    
* right to left and returns it as an unsigned integer. The function 
* may return a segmentation fault if the array is to short.        
* 
* @param *p pointer to data
********************************************************************/
unsigned int read_le16(const unsigned char *p);
  
/***************************************************************//**
* \brief Builds a fixed Huffman literal tree and a dynamic 
* distance tree.  
*
* @param *lt literal tree  
* @param *dt distance tree             
********************************************************************/
void build_fixed_trees(struct tinf_tree *lt, struct tinf_tree *dt);

/***************************************************************//**
* \brief Get a number of bits of an array containing lengths.
*
* @param *t Huffman tree
* @param *lengths pointer to array containing lengths
* @param num number of bits
********************************************************************/
int build_tree(struct tinf_tree *t, const unsigned char *lengths, unsigned int num);

int refill(struct tinf_data *d, int num);

unsigned int getbits_no_refill(struct tinf_data *d, int num);

/***************************************************************//**
* \brief Get a number of bits abd returns   ???
*
* @param num number of bits
********************************************************************/
unsigned int getbits(struct tinf_data *d, int num);

/***************************************************************//**
* \brief Reads a bit value from stream, adds base and returns it.          
*
* @param num number bit
* @param base base to be added                                                   
********************************************************************/
unsigned int getbits_base(struct tinf_data *d, int num, int base);

/***************************************************************//**
* \brief Decodes a symbol. Returns a tinf_error_code.
********************************************************************/
int decode_symbol(struct tinf_data *d, const struct tinf_tree *t);

/***************************************************************//**
* \brief Decodes a dynamic literal tree and a dynamic distance
* tree. Returns a tinf_error_code.
*
* @param *lt literal tree  
* @param *dt distance tree
********************************************************************/
int decode_trees(struct tinf_data *d, struct tinf_tree *lt, struct tinf_tree *dt);

/***************************************************************//**
* \brief Inflates a block of data given a literal tree and a 
* distance tree. Returns a tinf_error_code.  
*
* @param *lt literal tree  
* @param *dt distance tree                                     
********************************************************************/
int inflate_block_data(struct tinf_data *d, struct tinf_tree *lt, struct tinf_tree *dt);

/***************************************************************//**
* \brief Inflate an uncompressed block of data.                    
* Returns a tinf_error_code.                                       
********************************************************************/
int inflate_uncompressed_block(struct tinf_data *d);

/***************************************************************//**
* \brief Inflate a block of data compressed with fixed             
* Huffman trees. Returns a tinf_error_code.                        
********************************************************************/
int inflate_fixed_block(struct tinf_data *d);

/***************************************************************//**
* \brief Inflate a block of data compressed with dynamic           
* Huffman trees. Returns a tinf_error_code.                        
********************************************************************/
int inflate_dynamic_block(struct tinf_data *d);

} //namepsace fpga

/***************************************************************//**
* \brief FPGA top-level function: Decompresses a block of a gzip
* compressed file
*
* The function takes a buffer of 64kB, decompresses data and stores
* it in a buffer of 100MB. Additionally, the parameters from the data
* structure tinf_data have to be transported to the device. The
* user has to copy tag, bitcount and overflow as one-component
* arrays to global device memory in  order to ensure, that the data
* is consistent throughout different kernel runs.
*
* @param *dest pointer to begin of output buffer
* @param *dLen first component gets overridden with actual length of
* output after successful run
* @param *source pointer to begin of input buffer
* @param *sourceLen first component gets overridden with actual
* length of compressed block after successful run
* @param *tag
* @param *bitcount
* @param *overflow
* @param *bfinal after successful run the first component is 1 if
* the block is the last one, 0 else
* @param *err first element holds TINF_BUF_ERROR if there is not enough
* room for output, TINF_DATA_ERROR if the data is corrupted or
* TINF_OK else
********************************************************************/
extern "C" {
void fpga_uncompress(unsigned char *dest,   unsigned int *dLen,
                     unsigned char *source, unsigned int *sourceLen,
			         unsigned int *tag, unsigned int *bitcount, unsigned int *overflow,
			         int *bfinal, int *err);
}

#endif /* FPGA_H_INCLUDED */
