#include "tinf_data.h"

unsigned int inf::crc32(const void *data, unsigned int length)
{
	const unsigned char *buf = (const unsigned char *) data;
	unsigned int crc = 0xFFFFFFFF;
	unsigned int i;

	if (length == 0) return 0;

	for (i = 0; i < length; ++i)
	{
		crc ^= buf[i];
		crc = inf::tinf_crc32tab[crc & 0x0F] ^ (crc >> 4);
		crc = inf::tinf_crc32tab[crc & 0x0F] ^ (crc >> 4);
	}

	return crc ^ 0xFFFFFFFF;
}

int inf::check_integrity(std::vector<std::string> input_list, ArgumentParser &parser)
{
	cl_int ret = inf::TINF_OK;

	if(parser.exists("l")) std::cout << "compressed\t uncompressed\t ratio\t uncompressed_name\n";

#pragma omp parallel num_threads(input_list.size())
{
	cl_int err = inf::TINF_OK;

    std::string  input_file;
    std::string output_file;

    input_file = input_list[omp_get_thread_num()];

	FILE *fin  = NULL;

	//Open files
	if((fin   = fopen(input_file.c_str(), "rb")) == NULL)
	{
		std::cerr << "unable to open input file '" << input_file.c_str() << "'\n";
		err = inf::TINF_FILE_ERROR;
	}

	//Check header
	fseek(fin, 0, SEEK_END);
	unsigned int srclen = ftell(fin);
	fseek(fin, 0, SEEK_SET);
	if(srclen < 18)
	{
		std::cerr << "input too small to be gzip\n";
		if(err == inf::TINF_OK) err = inf::TINF_DATA_ERROR;
	}

	//Read header
	std::vector<unsigned char,aligned_allocator<unsigned char>> header(50);
	if(fread(header.data(), 1, 50, fin) != 50 && err == inf::TINF_OK) err = inf::TINF_FILE_ERROR;

	//Read footer
	fseek(fin, -8, SEEK_END);
	std::vector<unsigned char,aligned_allocator<unsigned char>> footer(8);
	if(fread(footer.data(), 1,  8, fin) !=  8 && err == inf::TINF_OK) err = inf::TINF_FILE_ERROR;

	// Check Header
    unsigned int dist, time;
	std::string filename;
	unsigned int buf = err;
	err = inf::check_gzip_header(header.data(), srclen, time, dist, filename);
	if(err != inf::TINF_OK && buf == inf::TINF_OK) err = inf::TINF_DATA_ERROR;

	if(!parser.exists("q") && err != inf::TINF_OK)
	{
		std::cerr << "process #" << omp_get_thread_num() << " exited with error code " << err << "\n";
		ret = err;
	}

	if(parser.exists("l") && err == inf::TINF_OK)
	{
		//Read output length
		double olen   = inf::read_le32(&footer.data()[4]);
		double ratio;
		if(olen > srclen) ratio = 1-srclen/olen;
		else ratio = -olen/double(srclen);

		std::cout << srclen << "\t" << olen << "\t" << ratio*100 << "%\t" << filename << "\n";
	}
}
    return ret;
}

int inf::gzip_uncompress(std::vector<std::string> input_list, ArgumentParser &parser)
{
	cl_int ret = inf::TINF_OK;

	std::string binaryFile;
	if(parser.exists("b")) binaryFile = parser.get<std::string>("b");
	else                   binaryFile = "../binary_container_1.xclbin";
    std::vector<cl::Device> devices = inf::get_devices();
    cl::Device device = devices[0];
    unsigned fileBufSize;
    char* fileBuf = inf::read_binary_file(binaryFile, fileBufSize);
    cl::Program::Binaries bins{{fileBuf, fileBufSize}};
    devices.resize(1);

    OCL_CHECK(ret, cl::Context context(device, NULL, NULL, NULL, &ret));
    OCL_CHECK(ret, cl::Program program(context, devices, bins, NULL, &ret));
    free(fileBuf);

	if(parser.exists("l")) std::cout << "compressed\t uncompressed\t ratio\t uncompressed_name\n";

#pragma omp parallel
{
	std::string kernel_name = "fpga_uncompress:{fpga_uncompress_" + std::to_string(omp_get_thread_num()+1) + "}";
	std::cout << "!!" << kernel_name << "!!\n";
	OCL_CHECK(ret, cl::Kernel kernel_inflate(program, kernel_name.c_str(), &ret));

	cl_int err = inf::TINF_OK;

    std::string  input_file;
    std::string output_file;

    std::string suff = ".gz";
    if(parser.exists("S")) suff = parser.get<std::string>("S");

    #pragma omp critical
    {
        input_file = input_list[input_list.size()-1];
        input_list.pop_back();
    }
    
    if(input_file.compare(input_file.size()-suff.size(), suff.size(), suff) != 0)
    {
    	std::cerr << "'" << input_file.c_str() << "' has wrong suffix\n";
    	err = inf::TINF_FILE_ERROR;
    }

	FILE *fin  = NULL;
	FILE *fout = NULL;

	//Open input file
	if((fin   = fopen(input_file.c_str(), "rb")) == NULL)
	{
		std::cerr << "unable to open input file '" << input_file.c_str() << "'\n";
		if(err == inf::TINF_OK) err = inf::TINF_FILE_ERROR;
	}

	//Check header
	fseek(fin, 0, SEEK_END);
	unsigned int srclen = ftell(fin);
	fseek(fin, 0, SEEK_SET);
	if(srclen < 18)
	{
		std::cerr << "input too small to be gzip\n";
		if(err == inf::TINF_OK) err = inf::TINF_DATA_ERROR;
	}

	//Read header
	std::vector<unsigned char,aligned_allocator<unsigned char>> header(50);
	unsigned int actual_size = fread(header.data(), 1, 50, fin);
	if(actual_size != 50 && err == inf::TINF_OK)
	{
		fseek(fin, 0, SEEK_SET);
		header.resize(actual_size);
		if(fread(header.data(), 1, actual_size, fin) != actual_size && err == inf::TINF_OK) err = inf::TINF_FILE_ERROR;;
	}

	//Read footer
	fseek(fin, -8, SEEK_END);
	std::vector<unsigned char,aligned_allocator<unsigned char>> footer(8);
	if(fread(footer.data(), 1,  8, fin) !=  8 && err == inf::TINF_OK) err = inf::TINF_FILE_ERROR;

	//Read output length
	unsigned int olen   = inf::read_le32(&footer.data()[4]);
	//Read CRC checksum
	unsigned int crc32v = inf::read_le32(&footer.data()[0]);

	// Check Header
    unsigned int dist, time;
	std::string filename;
	unsigned int buf = err;
	err = inf::check_gzip_header(header.data(), srclen, time, dist, filename);
	if(err != inf::TINF_OK && buf == inf::TINF_OK) err = inf::TINF_DATA_ERROR;
	std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::from_time_t(time);
	srclen -= dist; // Subtract header length from input length
	output_file = filename;
	if(parser.exists("n") || output_file == "")
	{
		output_file = input_file;
		for(unsigned int i = 0; i < suff.size(); ++i) output_file.pop_back();
	}

	//Open output file
	if((fout  = fopen(output_file.c_str(), "rb")) == NULL)
	{
		if((fout = fopen(output_file.c_str(), "wb")) == NULL)
		{
			std::cerr << "unable to create output file '" << output_file.c_str() << "'\n";
			err = inf::TINF_FILE_ERROR;
		}
	}
	else
	{
		if(!parser.exists("c"))
		{
			if(parser.exists("f"))
			{
				if((fout = fopen(output_file.c_str(), "wb")) == NULL)
				{
					std::cerr << "unable to create output file '" << output_file.c_str() << "'\n";
					err = inf::TINF_FILE_ERROR;
				}
			}
			else
			{
				std::cerr << "output file already exists\n";
				err = inf::TINF_FILE_ERROR;
			}
		}
	}

	// -- Decompress data --
	////////////////////////////////////////////////////////////////////////////////////////////////
    OCL_CHECK(err, cl::CommandQueue q(context, device, CL_QUEUE_PROFILING_ENABLE, &err));


    std::vector<unsigned char,aligned_allocator<unsigned char>> dest(100000000);
    OCL_CHECK(err,
        cl::Buffer buffer_output(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, cl::size_type(100000000),     dest.data(),   &err)
    );
    std::vector<unsigned int,aligned_allocator<unsigned int>> outlen(1); outlen[0] = olen;
    OCL_CHECK(err,
        cl::Buffer buffer_outlen(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, cl::size_type(4),             outlen.data(), &err)
    );
    std::vector<unsigned char,aligned_allocator<unsigned char>> source(100000);
    OCL_CHECK(err,
        cl::Buffer buffer_input( context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY,  cl::size_type(100000),        source.data(), &err)
    );
    std::vector<unsigned int,aligned_allocator<unsigned int>> len(1); len[0] = srclen;
    OCL_CHECK(err,
        cl::Buffer buffer_len(   context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, cl::size_type(4),             len.data(),    &err)
    );

    std::vector<unsigned int,aligned_allocator<unsigned int>> tag(1); tag[0] = 0;
    OCL_CHECK(err,
        cl::Buffer buffer_tag      (context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, cl::size_type(4), tag.data(),      &err)
    );
    std::vector<unsigned int,aligned_allocator<unsigned int>> bitcount(1); bitcount[0] = 0;
    OCL_CHECK(err,
        cl::Buffer buffer_bitcount (context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, cl::size_type(4), bitcount.data(), &err)
    );
    std::vector<unsigned int,aligned_allocator<unsigned int>> overflow(1); overflow[0] = 0;
    OCL_CHECK(err,
        cl::Buffer buffer_overflow (context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, cl::size_type(4), overflow.data(), &err)
    );
    std::vector<int,aligned_allocator<int>> bfinal(1);
    OCL_CHECK(err,
        cl::Buffer buffer_bfinal (  context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE, cl::size_type(4), bfinal.data(),   &err)
    );
    std::vector<int,aligned_allocator<int>> kernel_error(1);
    OCL_CHECK(err,
        cl::Buffer buffer_error (   context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, cl::size_type(4), kernel_error.data(), &err)
    );

    size_t narg = 0;
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_output  ));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_outlen  ));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_input   ));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_len     ));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_tag     ));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_bitcount));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_overflow));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_bfinal  ));
    OCL_CHECK(err, err = kernel_inflate.setArg(narg++, buffer_error   ));

    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_tag},      0));
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_bitcount}, 0));
    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_overflow}, 0));

    unsigned int crcsum = 0xFFFFFFFF;
    size_t  input_offset = 0;
    size_t output_offset = 0;
    size_t  input_length = 0;
    size_t output_length = 0;
    
    do
    {
    	if(len[0] < 8 && err == inf::TINF_OK) err = inf::TINF_DATA_ERROR; //Error if rest length smaller than 8 bytes of footer

    	std::cout << "buffer input offset: "  <<  input_offset << "\n";
    	std::cout << "buffer output offset: " << output_offset << "\n";

    	//Copy to device
    	input_length = min(100000, len[0] - 8); //Calculate length of input: rest or 100 MB
    	fseek(fin, dist + input_offset, SEEK_SET);
    	fread(source.data(), 1, input_length, fin);
    	fseek(fout, output_offset, SEEK_SET);
    	_cl_buffer_region sub_buffer_input_region{0, input_length};
	    OCL_CHECK(err,
	        cl::Buffer sub_buffer_input = buffer_input.createSubBuffer(CL_MEM_READ_ONLY, CL_BUFFER_CREATE_TYPE_REGION, &sub_buffer_input_region, &err)
        );
	    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({sub_buffer_input}, 0));
    	//OCL_CHECK(err, err = q.enqueueWriteBuffer(buffer_input, CL_FALSE, 0, input_length, source.data()));
	
	    OCL_CHECK(err, q.finish());

    	OCL_CHECK(err, err = q.enqueueTask(kernel_inflate)); //Execute kernel

	    OCL_CHECK(err, q.finish());

	    cl::Event copy_outlen_event;
    	OCL_CHECK(err, err = q.enqueueMigrateMemObjects(
    			{buffer_error,   //Get error on kernel_error[0]
    			buffer_len,     //Get rest length of  input in "len[0]"
				buffer_outlen,  //Get rest length of output in "outlen[0]"
				buffer_bfinal}, //In last block bfinal[0] is 1
				CL_MIGRATE_MEM_OBJECT_HOST, NULL, &copy_outlen_event));
    	OCL_CHECK(err, copy_outlen_event.wait());
    	OCL_CHECK(err, q.finish());

    	std::cout << "test: " << outlen[0] << "\n";

    	//Copy to host
    	output_length = (olen - outlen[0]) - output_offset;
    	std::cout << "buffer output size: " << output_length << "\n";

    	_cl_buffer_region sub_buffer_output_region{0, output_length};
	    OCL_CHECK(err,
	        cl::Buffer sub_buffer_output = buffer_output.createSubBuffer(CL_MEM_WRITE_ONLY, CL_BUFFER_CREATE_TYPE_REGION, &sub_buffer_output_region, &err)
        );
	    cl::Event copy_dest_event;
	    OCL_CHECK(err, err = q.enqueueMigrateMemObjects({sub_buffer_output}, CL_MIGRATE_MEM_OBJECT_HOST, NULL, &copy_dest_event));
	    OCL_CHECK(err, copy_dest_event.wait());
	    OCL_CHECK(err, q.finish());
    	//OCL_CHECK(err, err = q.enqueueReadBuffer(buffer_output, CL_FALSE, 0, output_length, dest.data()));  OCL_CHECK(err, q.finish());
	
    	if(parser.exists("c"))
    	{
    		for(unsigned int s = 0; s < output_length; ++s) std::cout << dest.data()[s];
    	}
    	else
    	{
    		fwrite(dest.data(), 1, output_length, fout);
    	}

    	//Get offsets
    	output_offset = olen - outlen[0];
    	 input_offset = srclen -  len[0];

    	//Check kernel errors
    	std::cout << kernel_error[0] << " " << bfinal[0] << "\n\n";
    	if(kernel_error[0] != inf::TINF_OK && err == inf::TINF_OK)
    	{
    		std::cerr << "decompression failed\n";
    		err = kernel_error[0];
    		break;
    	}

    	//Do CRC partially
    	for(size_t i = 0; i < output_offset; ++i)
    	{
    		crcsum ^= dest.data()[i];
    		crcsum = tinf_crc32tab[crcsum & 0x0F] ^ (crcsum >> 4);
    		crcsum = tinf_crc32tab[crcsum & 0x0F] ^ (crcsum >> 4);
    	}

    }while(!bfinal[0]);

    fclose(fin);
    fclose(fout);
    if(!parser.exists("k")) remove(input_list[0].c_str());

    crcsum ^= 0xFFFFFFFF;

	////////////////////////////////////////////////////////////////////////////////////////////////

	if(crc32v    != crcsum && err == inf::TINF_OK) err = inf::TINF_DATA_ERROR; //Check CRC
	if(outlen[0] != 0      && err == inf::TINF_OK) err = inf::TINF_DATA_ERROR; //Rest length must be zero

	if(!parser.exists("q") && err == inf::TINF_OK)
	{
		std::cout << "decompressed " << olen << " bytes from file '" << input_file << "' (#" << omp_get_thread_num() << ") to " << output_file << "\n";
	}
	if(!parser.exists("q") && err != inf::TINF_OK)
	{
		std::cerr << "process #" << omp_get_thread_num() << " exited with error code " << err << "\n";
		ret = err;
	}

	if(parser.exists("N")) std::filesystem::last_write_time(output_file, timestamp);
}
	return ret;
}

unsigned int inf::read_le16(const unsigned char *p)
{
	return ((unsigned int) p[0])
	     | ((unsigned int) p[1] << 8);
}

unsigned int inf::read_le32(const unsigned char *p)
{
	return ((unsigned int) p[0])
	     | ((unsigned int) p[1] << 8)
	     | ((unsigned int) p[2] << 16)
	     | ((unsigned int) p[3] << 24);
}

unsigned int inf::min(unsigned int first, unsigned int second)
{
	if(first > second) return second;
	else return first;
}

int inf::check_gzip_header(unsigned char *src, unsigned int sourceLen, unsigned int &time, unsigned int &dist, std::string &filename)
{
	unsigned char flg;

	/* -- Check header -- */

	/* Check room for at least 10 byte header and 8 byte trailer */
	if (sourceLen < 18) return inf::TINF_DATA_ERROR;

	/* Check id bytes */
	if (src[0] != 0x1F || src[1] != 0x8B) return inf::TINF_DATA_ERROR;

	/* Check method is deflate */
	if (src[2] != 8) return inf::TINF_DATA_ERROR;

	/* Get flag byte */
	flg = src[3];

	/* Check that reserved bits are zero */
	if (flg & 0xE0) return inf::TINF_DATA_ERROR;

	//get timestamp of original file
	time = read_le32(src+4);

	/* -- Find start of compressed data -- */

	/* Skip base header of 10 bytes */
	unsigned char *start = src + 10;

	/* Skip extra data if present */
	unsigned int xlen = 0;
	if (flg & inf::FEXTRA)
	{
		xlen = read_le16(start);

		if (xlen > sourceLen - 12)
		{
			return inf::TINF_DATA_ERROR;
		}

		start += xlen + 2;
	}

	/* Skip file name if present */
	if (flg & inf::FNAME)
	{
		unsigned int s = 0;
		do
		{
			if (start - src >= sourceLen)
			{
				return inf::TINF_DATA_ERROR;
			}
			if (flg & inf::FEXTRA) filename.push_back(src[12 + xlen + s++]);
			else              filename.push_back(src[10 + xlen + s++]);
		} while (*start++);
	}

	/* Skip file comment if present */
	if (flg & inf::FCOMMENT)
	{
		do
		{
			if (start - src >= sourceLen)
			{
				return inf::TINF_DATA_ERROR;
			}
		} while (*start++);
	}

	/* Check header crc if present */
	if (flg & inf::FHCRC)
	{
		unsigned int hcrc;

		if (start - src > sourceLen - 2) return inf::TINF_DATA_ERROR;

		hcrc = read_le16(start);

		if (hcrc != (crc32(src, start - src) & 0x0000FFFF)) return inf::TINF_DATA_ERROR;

		start += 2;
	}

	if((src + sourceLen) - start < 8) return inf::TINF_DATA_ERROR;

	dist = (unsigned long int)(start) - (unsigned long int)(src);

	return inf::TINF_OK;
}

std::vector<cl::Device> inf::get_devices()
{
    cl_int err;
  
    std::vector<cl::Platform> platforms;
    OCL_CHECK(err, err = cl::Platform::get(&platforms));
    cl::Platform platform;
    
    size_t i;
    for(i = 0; i < platforms.size(); i++)
    {
        platform = platforms[i];
        OCL_CHECK(err,  std::string platformName = platform.getInfo<CL_PLATFORM_NAME>(&err));
        if (platformName == "Xilinx")
	    {
            //std::cout << "Found Platform" << std::endl;
            //std::cout << "Platform Name: " << platformName.c_str() << std::endl;
            break;
        }
    }
    if(i == platforms.size()) std::cerr << "Error: Failed to find Xilinx platform" << std::endl;
    
    //Getting ACCELERATOR Devices and selecting 1st such device
    std::vector<cl::Device> devices;
    OCL_CHECK(err, err = platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &devices));
    
    return devices;
}

char* inf::read_binary_file(const std::string &xclbin_file_name, unsigned &nb)
{
    std::cout << "INFO: Reading " << xclbin_file_name << std::endl;
    
    //Loading XCL Bin into char buffer
    std::cout << "Loading: '" << xclbin_file_name.c_str() << "'\n";
    std::ifstream bin_file(xclbin_file_name.c_str(), std::ifstream::binary);
    bin_file.seekg(0, bin_file.end);
    nb = bin_file.tellg();
    bin_file.seekg(0, bin_file.beg);
    char *buf = new char[nb];
    bin_file.read(buf, nb);
    
    return buf;
}

bool inf::is_emulation()
{
    bool ret = false;
    char *xcl_mode = getenv("XCL_EMULATION_MODE");
    if (xcl_mode != NULL) ret = true;

    return ret;
}

bool inf::is_hw_emulation()
{
    bool ret = false;
    char *xcl_mode = getenv("XCL_EMULATION_MODE");
    if ((xcl_mode != NULL) && !strcmp(xcl_mode, "hw_emu")) ret = true;

    return ret;
}
