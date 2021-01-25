#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "argparse.h"
#include "tinf_data.h"

using namespace argparse;

int main(int argc, const char *argv[])
{
    ArgumentParser parser("Argument parser");

	parser.add_argument()
      .names({"-c", "--stdout"})
	  .description("write on standard output, keep original files unchanged")
	  .required(false);
	parser.add_argument()
      .names({"-f", "--force"})
	  .description("force overwrite of output file and compress links")
	  .required(false);
	parser.add_argument()
      .names({"-k", "--keep"})
	  .description("keep (don't delete) input files")
	  .required(false);
	parser.add_argument()
      .names({"-l", "--list"})
	  .description("list compressed file contents")
	  .required(false);
	parser.add_argument()
      .names({"-n", "--no-name"})
	  .description("do not save or restore the original name and time stamp")
	  .required(false);
	parser.add_argument()
      .names({"-N", "--name"})
	  .description("save or restore the original name and time stamp")
	  .required(false);
	parser.add_argument()
      .names({"-q", "--quiet"})
	  .description("suppress all warnings")
	  .required(false);
	parser.add_argument()
      .names({"-r", "--recursive"})
	  .description("operate recursively on directories")
	  .required(false);
	parser.add_argument()
      .names({"-S", "--suffix"})
	  .description("use suffix SUF on compressed files")
	  .required(false);
	parser.add_argument()
      .names({"-t", "--test"})
	  .description("test compressed file integrity")
	  .required(false);
	parser.add_argument()
      .names({"-b", "--binary"})
	  .description("path to the device binary (default: ../binary_container_1.xclbin)")
	  .required(false);
	parser.add_argument()
      .names({"-v", "--verbose"})
	  .description("verbose mode")
	  .required(false);
	parser.add_argument()
      .names({"-V", "--version"})
	  .description("display version information and exit")
	  .required(false);

	parser.enable_help();

	////////////////////////////////////////////////////////////

    int err = parser.parse(argc, argv);
	if(err)
	{
	  std::cerr << "Parser error. Type '" << argv[0] << "' --help for information.\n";
	  return EXIT_FAILURE;
	}

    if(parser.exists("V"))
    {
      std::cout << "X-Gzip 1.0\n This is free software.\n You may redistribute copies of it under the terms of the GNU General Public License <http://www.gnu.org/licenses/gpl.html>. There is NO WARRANTY, to the extent permitted by law.\n\n Written by Thomas Karl.";
      return EXIT_SUCCESS;
    }

	if(parser.exists("help"))
    {
	  std::cout << "Usage: " << argv[0] << " [OPTION]... [FILE(s)]... [-S ... -b ...]\n Uncompress FILEs (by default, in-place).\n\n Mandatory arguments to long options are mandatory for short options too.\n\n";
      parser.print_help();
      std::cout << "\nWith no FILE, or when FILE is -, read standard input.\n\n Report bugs to <Thomas.Karl@physik.uni-regensburg.de>.";
      return EXIT_SUCCESS;
    }

	////////////////////////////////////////////////////////////

	std::vector<std::string> input_list;
	std::string file;
	for(int i = 1; i < argc - 1; ++i)
	{
		file = argv[i];

		if( file.compare("-S") == 0 ||
			file.compare("-b") == 0) break;

		if( file.compare("-c") != 0 &&
			file.compare("-f") != 0 &&
			file.compare("-k") != 0 &&
			file.compare("-l") != 0 &&
			file.compare("-n") != 0 &&
			file.compare("-N") != 0 &&
			file.compare("-q") != 0 &&
			file.compare("-r") != 0 &&
			file.compare("-t") != 0 &&
			file.compare("-V") != 0 &&
			file.compare("-v") != 0) input_list.push_back(file);
	}
	if(input_list.size() == 0 && !parser.exists("q")) std::cerr << "You did not specify any files!\n";
	std::reverse(std::begin(input_list), std::end(input_list));

	if(parser.exists("-t") || parser.exists("-l")) err = inf::check_integrity(input_list, parser);
	else                                           err = inf::gzip_uncompress(input_list, parser);

	return err;
}
