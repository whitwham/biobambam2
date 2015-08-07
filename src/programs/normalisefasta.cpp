/**
    biobambam
    Copyright (C) 2009-2014 German Tischler
    Copyright (C) 2011-2014 Genome Research Limited

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/
#include <biobambam2/BamBamConfig.hpp>
#include <biobambam2/Licensing.hpp>

#include <config.h>

#include <libmaus2/bambam/BamBlockWriterBaseFactory.hpp>
#include <libmaus2/fastx/BgzfFastAIndexEntry.hpp>
#include <libmaus2/fastx/FastABgzfIndex.hpp>
#include <libmaus2/fastx/StreamFastAReader.hpp>
#include <libmaus2/lz/BgzfDeflate.hpp>
#include <libmaus2/util/ArgInfo.hpp>
#include <libmaus2/util/MemUsage.hpp>


static unsigned int getDefaultCols()
{
	return 80;
}

static unsigned int getDefaultBgzf()
{
	return 0;
}

static int getDefaultLevel()
{
	return -1;
}

std::string stripName(std::string const & s)
{
	uint64_t j = 0;
	while ( j < s.size() && isspace(s[j]) )
		++j;
		
	uint64_t i = j;
	while ( i < s.size() && !isspace(s[i]) )
		++i;
		
	return s.substr(j,i-j);
}

void normalisefastaUncompressed(libmaus2::util::ArgInfo const & arginfo)
{
	libmaus2::fastx::StreamFastAReaderWrapper in(std::cin);
	libmaus2::fastx::StreamFastAReaderWrapper::pattern_type pattern;
	unsigned int const cols = arginfo.getValue<unsigned int>("cols",getDefaultCols());
	uint64_t offset = 0;
	
	while ( in.getNextPatternUnlocked(pattern) )
	{
		std::string const name = pattern.getStringId();
		std::string const shortname = stripName(name);
	
		std::cerr <<  shortname << "\t" << pattern.patlen << "\t" 
			<< offset+pattern.getStringId().size()+2 << "\t" << cols << "\t" << cols+1 << std::endl;
			
		pattern.printMultiLine(std::cout,cols,offset);
	}
	
	std::cout << std::flush;
}

void normalisefastaBgzf(libmaus2::util::ArgInfo const & arginfo, std::ostream & out)
{
	libmaus2::fastx::StreamFastAReaderWrapper in(std::cin);
	libmaus2::fastx::StreamFastAReaderWrapper::pattern_type pattern;
	int const level = libmaus2::bambam::BamBlockWriterBaseFactory::checkCompressionLevel(arginfo.getValue("level",getDefaultLevel()));
	std::string const indexfn = arginfo.getUnparsedValue("index","");

	::libmaus2::bambam::BamBlockWriterBaseFactory::checkCompressionLevel(level);

	libmaus2::lz::BgzfDeflate<std::ostream> defl(out,level,false /* full flush */);
	uint64_t const inbufsize = defl.getInputBufferSize();
	uint64_t zoffset = 0;
	uint64_t ioffset = 0;
	std::vector<libmaus2::fastx::BgzfFastAIndexEntry> index;
	std::ostringstream indexstr;
	
	ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,inbufsize);
	uint64_t patid = 0;
	
	while ( in.getNextPatternUnlocked(pattern) )
	{
		std::string const name = pattern.getStringId();
		std::string const shortname = stripName(name);
		std::string const & spat = pattern.spattern;
		char const * cpat = spat.c_str();
		uint64_t const patlen = spat.size();
		uint64_t const numblocks = (patlen + inbufsize - 1)/inbufsize;

		index.push_back(libmaus2::fastx::BgzfFastAIndexEntry(shortname,patid++,ioffset));
		
		ioffset += libmaus2::util::StringSerialisation::serialiseString(indexstr,name);
		ioffset += libmaus2::util::StringSerialisation::serialiseString(indexstr,shortname);
		ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,patlen);
		ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,zoffset);
		ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,numblocks);
		
		std::ostringstream nameostr;
		nameostr << '>' << name << '\n';
		std::string const nameser = nameostr.str();
				
		std::pair<uint64_t,uint64_t> const P0 = defl.writeSyncedCount(nameser.c_str(),nameser.size());
		zoffset += P0.second;
		
		uint64_t o = 0;
		while ( o != patlen )
		{
			assert ( o % inbufsize == 0 );
			uint64_t const towrite = std::min(patlen-o,inbufsize);
			std::pair<uint64_t,uint64_t> const P1 = defl.writeSyncedCount(cpat,towrite);
			
			ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,zoffset);

			zoffset += P1.second;
			o += towrite;
			cpat += towrite;
		}		

		ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,zoffset);

		std::pair<uint64_t,uint64_t> const Pn = defl.writeSyncedCount("\n",1);
		zoffset += Pn.second;
	}

	defl.flush();
	out << std::flush;
	
	uint64_t const imetaoffset = ioffset;

	ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,index.size());
	for ( uint64_t i = 0; i < index.size(); ++i )
		ioffset += libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,index[i].ioffset);
	
	libmaus2::util::NumberSerialisation::serialiseNumber(indexstr,imetaoffset);

	if ( indexfn.size() )
	{
		std::string const & sindex = indexstr.str();
		libmaus2::aio::OutputStreamInstance indexCOS(indexfn);
		indexCOS.write(sindex.c_str(),sindex.size());
		indexCOS.flush();
	}
}

void normalisefasta(libmaus2::util::ArgInfo const & arginfo)
{
	if ( arginfo.getValue<unsigned int>("bgzf",getDefaultBgzf()) )
		normalisefastaBgzf(arginfo,std::cout);
	else
		normalisefastaUncompressed(arginfo);
}

int main(int argc, char * argv[])
{
	try
	{
		libmaus2::timing::RealTimeClock rtc; rtc.start();
		
		::libmaus2::util::ArgInfo const arginfo(argc,argv);
		
		for ( uint64_t i = 0; i < arginfo.restargs.size(); ++i )
			if ( 
				arginfo.restargs[i] == "-v"
				||
				arginfo.restargs[i] == "--version"
			)
			{
				std::cerr << ::biobambam2::Licensing::license();
				return EXIT_SUCCESS;
			}
			else if ( 
				arginfo.restargs[i] == "-h"
				||
				arginfo.restargs[i] == "--help"
			)
			{
				std::cerr << ::biobambam2::Licensing::license() << std::endl;
				std::cerr << "Key=Value pairs:" << std::endl;
				std::cerr << std::endl;
				
				std::vector< std::pair<std::string,std::string> > V;

				V.push_back ( std::pair<std::string,std::string> ( std::string("cols=<[")+libmaus2::util::NumberSerialisation::formatNumber(getDefaultCols(),0)+"]>", "column width" ) );
				V.push_back ( std::pair<std::string,std::string> ( std::string("bgzf=<[")+libmaus2::util::NumberSerialisation::formatNumber(getDefaultBgzf(),0)+"]>", "compress output" ) );
				V.push_back ( std::pair<std::string,std::string> ( std::string("index=<>"), "file name for index if bgzf=1 (no index is created if key is not given)" ) );
				V.push_back ( std::pair<std::string,std::string> ( std::string("level=<[")+::biobambam2::Licensing::formatNumber(getDefaultLevel())+"]>", std::string("compression level if bgzf=1 (") + libmaus2::bambam::BamBlockWriterBaseFactory::getLevelHelpText() + std::string(")") ) );
				
				::biobambam2::Licensing::printMap(std::cerr,V);

				std::cerr << std::endl;
				return EXIT_SUCCESS;
			}
		
		normalisefasta(arginfo);
	}
	catch(std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}

}
