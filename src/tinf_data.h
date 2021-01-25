#ifndef INF_H_INCLUDED
#define INF_H_INCLUDED

#define CL_HPP_CL_1_2_DEFAULT_BUILD
#define CL_HPP_TARGET_OPENCL_VERSION 120
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#define CL_HPP_ENABLE_PROGRAM_CONSTRUCTION_FROM_ARRAY_COMPATIBILITY 1
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include <assert.h>
#include <limits.h>
#include <omp.h>
#include <CL/cl2.hpp>
#include <iostream>
#include <fstream>
#include <CL/cl_ext_xilinx.h>
#include <sys/stat.h>
#include <unistd.h>
#include "./argparse.h"
using namespace argparse;

/***************************************************************//**
* Executes an OpenCL command and prints the error if not succeeded.
* Does not work if call has templatized function call.
* Unset OCL_CHECK to turn off debugging.
*
* @param call  command to be executed
* @param error returned OpenCL error code
********************************************************************/
#define OCL_CHECK(error,call)                                       \
    call;                                                           \
    if (error != CL_SUCCESS) {                                      \
      printf("%s:%d Error calling " #call ", error code is: %d\n",  \
              __FILE__,__LINE__, error);                            \
    }                                       

namespace inf {
  
/***************************************************************//**
* Custom allocator to ensure that vectors are page-aligned
********************************************************************/
template <typename T>
struct aligned_allocator
{
  using value_type = T;
  T* allocate(std::size_t num)
  {
    void* ptr = nullptr;
    if (posix_memalign(&ptr,4096,num*sizeof(T)))
      throw std::bad_alloc();
    return reinterpret_cast<T*>(ptr);
  }
  void deallocate(T* p, std::size_t num)
  {
    free(p);
  }
};

class Stream
{
  public:
    static decltype(&clCreateStream) createStream;
    static decltype(&clReleaseStream) releaseStream;
    static decltype(&clReadStream) readStream;
    static decltype(&clWriteStream) writeStream;
    static decltype(&clPollStreams) pollStreams;
    static void init(const cl_platform_id& platform) {
        void *bar = clGetExtensionFunctionAddressForPlatform(platform, "clCreateStream");
        createStream = (decltype(&clCreateStream))bar;
        bar = clGetExtensionFunctionAddressForPlatform(platform, "clReleaseStream");
        releaseStream = (decltype(&clReleaseStream))bar;
        bar = clGetExtensionFunctionAddressForPlatform(platform, "clReadStream");
        readStream = (decltype(&clReadStream))bar;
        bar = clGetExtensionFunctionAddressForPlatform(platform, "clWriteStream");
        writeStream = (decltype(&clWriteStream))bar;
        bar = clGetExtensionFunctionAddressForPlatform(platform, "clPollStreams");
        pollStreams = (decltype(&clPollStreams))bar;
    }
};

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
* \brief Reads 32 bit and converts to unsigned integer             
*                                                                  
* The function reads exactly 4 bytes from    
* right to left and returns it as an unsigned integer. The function 
* may return a segmentation fault if the array is to short.    
* 
* @param *p pointer to data    
********************************************************************/
unsigned int read_le32(const unsigned char *p);

/***************************************************************//**
* \brief Returns the smaller of value of both inputs     
********************************************************************/
unsigned int min(unsigned int first, unsigned int second);

/***************************************************************//**
* \brief Uncompresses a number of gzip files                       
*                                                                  
* Depending on specific options the function decompresses gzip 
* files possibly in parallel. Output files are created 
* automatically. The output names also depend on options.
* 
* @param input_list contains paths to gzip files (absolute or relative)
* @param parser the argument parser that contains specific options                            
********************************************************************/
int gzip_uncompress(std::vector<std::string> input_list, ArgumentParser &parser);

/***************************************************************//**
* \brief Performs an integrity check on a number of gzip files     
*                                                                  
* Depending on specific options the function performs an integrity 
* check on the files possibly in parallel. The function returns an 
* error if at least one thread encounters an error, else TIN_OK. 
* The function returns TINF_DATA_ERROR if at least one file is not 
* a valid gzip file, else TINF_OK. The function may encounter an 
* out-of-memory exception if the headers do not fit into system  memory.   
* 
* @param input_list contains paths to gzip files (absolute or relative)
* @param parser the argument parser that contains specific options        
********************************************************************/
int check_integrity(std::vector<std::string> input_list, ArgumentParser &parser);

/***************************************************************//**
* \brief Performs an integrity check on a gzip file                
*                                                                  
* The function reads a number of bytes from a specific file loaded 
* into system memory and performs an integrity check on the file.         
* The function returns TINF_DATA_ERROR if file is not a valid gzip 
* file, else TINF_OK. The function may encounter an out-of-memory 
* exception if the header does not fit into system  memory. The 
* function may encounter a segmentation fault if the number of 
* bytes is larger than the array that contains the data. 
*
* @param *src pointer to data to be checked
* @param sourceLen number of bytes to be read
* @param time gets overridden with original timestamp if present
* @param dist gets overridden with actual length of header
* @param filename gets overridden with original filename if present                 
********************************************************************/
int check_gzip_header(unsigned char *src, unsigned int sourceLen, unsigned int &time, unsigned int &dist, std::string &filename);

/***************************************************************//**
* Returns a cyclic redundancy checksum of a number of bytes of   
* given input data as specified in ISO 3309 standard. The function 
* may encounter a segmentation fault if the number is larger than 
* the array that contains the data. 
*
* @param *data pointer to data
* @param length number of bytes that should be accounted                   
********************************************************************/
unsigned int crc32(const void *data, unsigned int length);
 
/***************************************************************//**
* \brief Finds all valid Xilinx devices and stores it in a 
* standard vector
********************************************************************/
std::vector<cl::Device> get_devices();
 
/***************************************************************//**
* \brief Reads a valid .xclbin binary file and returns a pointer 
* to buffer
*
* @param xclbin_file_name path to file (relative or absolute)
* @param nb gets overridden with length of file buffer in bytes
********************************************************************/
char* read_binary_file(const std::string &xclbin_file_name, unsigned &nb);
  
/***************************************************************//**
* \brief Returns true if the execution mode is hardware 
* or software emulation
********************************************************************/
bool is_emulation();

/***************************************************************//**
* \brief Returns true if the execution mode is hardware emulation
********************************************************************/
bool is_hw_emulation();

} //namespace inf
  
#endif /* TINF_H_INCLUDED */
