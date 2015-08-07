/**
    bambam
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
#include "config.h"

#include <iostream>
#include <queue>

#include <libmaus2/bambam/BamAlignment.hpp>
#include <libmaus2/bambam/BamBlockWriterBaseFactory.hpp>
#include <libmaus2/bambam/BamDecoder.hpp>
#include <libmaus2/bambam/BamWriter.hpp>
#include <libmaus2/bambam/ProgramHeaderLineSet.hpp>

#include <libmaus2/util/ArgInfo.hpp>

#include <biobambam2/Licensing.hpp>
#include <biobambam2/ResetAlignment.hpp>

static int getDefaultLevel() { return Z_DEFAULT_COMPRESSION; }
static int getDefaultVerbose() { return 1; }
static int getDefaultResetSortOrder() { return 1; }

#include <libmaus2/lz/BgzfDeflateOutputCallbackMD5.hpp>
#include <libmaus2/bambam/BgzfDeflateOutputCallbackBamIndex.hpp>
static int getDefaultMD5() { return 0; }
static int getDefaultIndex() { return 0; }
static std::string getDefaultExcludeFlags() { return "SECONDARY,SUPPLEMENTARY"; }
static int getDefaultResetAux() { return 1; }


int bamclipextract(::libmaus2::util::ArgInfo const & arginfo)
{
	if ( isatty(STDIN_FILENO) )
	{
		::libmaus2::exception::LibMausException se;
		se.getStream() << "Refusing to read binary data from terminal, please redirect standard input to pipe or file." << std::endl;
		se.finish();
		throw se;
	}

	if ( isatty(STDOUT_FILENO) )
	{
		::libmaus2::exception::LibMausException se;
		se.getStream() << "Refusing write binary data to terminal, please redirect standard output to pipe or file." << std::endl;
		se.finish();
		throw se;
	}
	
	int const level = libmaus2::bambam::BamBlockWriterBaseFactory::checkCompressionLevel(arginfo.getValue<int>("level",getDefaultLevel()));
	int const verbose = arginfo.getValue<int>("verbose",getDefaultVerbose());
	int const resetsortorder = arginfo.getValue<int>("resetsortorder",getDefaultResetSortOrder());
	
	::libmaus2::bambam::BamDecoder dec(std::cin,false);
	::libmaus2::bambam::BamHeader const & header = dec.getHeader();

	std::string headertext = header.text;

	// no replacement header file given
	if ( ! arginfo.hasArg("resetheadertext") )
	{
		// remove SQ lines
		std::vector<libmaus2::bambam::HeaderLine> allheaderlines = libmaus2::bambam::HeaderLine::extractLines(headertext);

		std::ostringstream upheadstr;
		for ( uint64_t i = 0; i < allheaderlines.size(); ++i )
			if ( allheaderlines[i].type != "SQ" )
				upheadstr << allheaderlines[i].line << std::endl;

		headertext = upheadstr.str();
	}
	// replace header given in file
	else
	{
		std::string const headerfilename = arginfo.getUnparsedValue("resetheadertext","");
		uint64_t const headerlen = libmaus2::util::GetFileSize::getFileSize(headerfilename);
		libmaus2::aio::InputStreamInstance CIS(headerfilename);
		libmaus2::autoarray::AutoArray<char> ctext(headerlen,false);
		CIS.read(ctext.begin(),headerlen);
		headertext = std::string(ctext.begin(),ctext.end());		
	}

	// add PG line to header
	headertext = libmaus2::bambam::ProgramHeaderLineSet::addProgramLine(
		headertext,
		"bamclipextract", // ID
		"bamclipextract", // PN
		arginfo.commandline, // CL
		::libmaus2::bambam::ProgramHeaderLineSet(headertext).getLastIdInChain(), // PP
		std::string(PACKAGE_VERSION) // VN			
	);
	
	// construct new header
	libmaus2::bambam::BamHeader uphead(headertext);
	if ( resetsortorder )
		uphead.changeSortOrder("unknown");

	/*
	 * start index/md5 callbacks
	 */
	std::string const tmpfilenamebase = arginfo.getValue<std::string>("tmpfile",arginfo.getDefaultTmpFileName());
	std::string const tmpfileindex = tmpfilenamebase + "_index";
	::libmaus2::util::TempFileRemovalContainer::addTempFile(tmpfileindex);

	std::string md5filename;
	std::string indexfilename;

	std::vector< ::libmaus2::lz::BgzfDeflateOutputCallback * > cbs;
	::libmaus2::lz::BgzfDeflateOutputCallbackMD5::unique_ptr_type Pmd5cb;
	if ( arginfo.getValue<unsigned int>("md5",getDefaultMD5()) )
	{
		if ( arginfo.hasArg("md5filename") &&  arginfo.getUnparsedValue("md5filename","") != "" )
			md5filename = arginfo.getUnparsedValue("md5filename","");
		else
			std::cerr << "[V] no filename for md5 given, not creating hash" << std::endl;

		if ( md5filename.size() )
		{
			::libmaus2::lz::BgzfDeflateOutputCallbackMD5::unique_ptr_type Tmd5cb(new ::libmaus2::lz::BgzfDeflateOutputCallbackMD5);
			Pmd5cb = UNIQUE_PTR_MOVE(Tmd5cb);
			cbs.push_back(Pmd5cb.get());
		}
	}
	libmaus2::bambam::BgzfDeflateOutputCallbackBamIndex::unique_ptr_type Pindex;
	if ( arginfo.getValue<unsigned int>("index",getDefaultIndex()) )
	{
		if ( arginfo.hasArg("indexfilename") &&  arginfo.getUnparsedValue("indexfilename","") != "" )
			indexfilename = arginfo.getUnparsedValue("indexfilename","");
		else
			std::cerr << "[V] no filename for index given, not creating index" << std::endl;

		if ( indexfilename.size() )
		{
			libmaus2::bambam::BgzfDeflateOutputCallbackBamIndex::unique_ptr_type Tindex(new libmaus2::bambam::BgzfDeflateOutputCallbackBamIndex(tmpfileindex));
			Pindex = UNIQUE_PTR_MOVE(Tindex);
			cbs.push_back(Pindex.get());
		}
	}
	std::vector< ::libmaus2::lz::BgzfDeflateOutputCallback * > * Pcbs = 0;
	if ( cbs.size() )
		Pcbs = &cbs;
	/*
	 * end md5/index callbacks
	 */

	::libmaus2::bambam::BamWriter::unique_ptr_type writer(new ::libmaus2::bambam::BamWriter(std::cout,uphead,level,Pcbs));
 	libmaus2::timing::RealTimeClock rtc; rtc.start();
 	
	libmaus2::bambam::BamAlignment & algn = dec.getAlignment();
	uint64_t c = 0;

	libmaus2::bambam::BamAlignment front, back;
	::libmaus2::autoarray::AutoArray<char> readdata;
	::libmaus2::autoarray::AutoArray<char> qualdata;
	libmaus2::autoarray::AutoArray<uint8_t,libmaus2::bambam::BamAlignment::D_array_alloc_type> T;
	uint64_t const minclip = arginfo.getValueUnsignedNumeric<uint64_t>("minclip",20);
	libmaus2::bambam::BamSeqEncodeTable const seqenc;           
	
	char const * cfront = "_front";
	uint64_t const cfrontlen = strlen(cfront);
	char const * cback = "_back";
	uint64_t const cbacklen = strlen(cback);
	
	::libmaus2::autoarray::AutoArray<char> namedata;
	libmaus2::autoarray::AutoArray<libmaus2::bambam::cigar_operation> cigop;

	while ( dec.readAlignment() )
	{
		if ( algn.isMapped() )
		{
			uint32_t const numcig = algn.getCigarOperations(cigop);

			char const * name = algn.getName();
			uint64_t const lname = strlen(name);

			uint64_t const lseq = algn.decodeRead(readdata);
			algn.decodeQual(qualdata);
				
			if ( lname + std::max(cfrontlen,cbacklen) + 1 > namedata.size() )
				namedata = ::libmaus2::autoarray::AutoArray<char>(lname+std::max(cfrontlen,cbacklen)+1,false);
			
			if ( 
				numcig && 
				cigop[0].first == libmaus2::bambam::BamFlagBase::LIBMAUS2_BAMBAM_CSOFT_CLIP &&
				cigop[0].second >= static_cast<int64_t>(minclip)
			)
			{
				assert ( cigop[0].second <= static_cast<int64_t>(lseq) );
		
				front.copyFrom(algn);
				std::copy(name,name+lname,namedata.begin());
				std::copy(cfront,cfront+cfrontlen,namedata.begin()+lname);
				namedata[lname+cfrontlen] = 0;

				front.replaceSequence(seqenc,readdata.begin(),qualdata.begin(),cigop[0].second,T);
				front.replaceName(namedata.begin(),lname+cfrontlen);
				front.putFlags(front.getFlags() | libmaus2::bambam::BamFlagBase::LIBMAUS2_BAMBAM_FUNMAP);
				front.putRefId(-1);
				front.eraseCigarString();
				front.serialise(writer->getStream());
				
				std::cerr << front.getName() << std::endl;
			}
			if ( 
				numcig > 1 && 
				cigop[numcig-1].first == libmaus2::bambam::BamFlagBase::LIBMAUS2_BAMBAM_CSOFT_CLIP &&
				cigop[numcig-1].second >= static_cast<int64_t>(minclip)
				)
			{
				assert ( cigop[numcig-1].second <= static_cast<int64_t>(lseq) );
		
				back.copyFrom(algn);

				std::copy(name,name+lname,namedata.begin());
				std::copy(cback,cback+cbacklen,namedata.begin()+lname);
				namedata[lname+cbacklen] = 0;

				back.replaceSequence(
					seqenc,
					readdata.begin()+lseq-cigop[numcig-1].second,
					qualdata.begin()+lseq-cigop[numcig-1].second,
					cigop[numcig-1].second,T
				);
				back.replaceName(namedata.begin(),lname+cbacklen);
				back.putFlags(back.getFlags() | libmaus2::bambam::BamFlagBase::LIBMAUS2_BAMBAM_FUNMAP);
				back.putRefId(-1);
				back.eraseCigarString();
				back.serialise(writer->getStream());			

				std::cerr << back.getName() << std::endl;
			}
		}
		else
		{
			algn.serialise(writer->getStream());
		}

		if ( verbose && (++c & (1024*1024-1)) == 0 )
 			std::cerr << "[V] " << c/(1024*1024) << " " << (c / rtc.getElapsedSeconds()) << std::endl;
	}
	
	writer.reset();

	if ( Pmd5cb )
	{
		Pmd5cb->saveDigestAsFile(md5filename);
	}
	if ( Pindex )
	{
		Pindex->flush(std::string(indexfilename));
	}

	return EXIT_SUCCESS;
}

int main(int argc, char * argv[])
{
	try
	{
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
				std::cerr << ::biobambam2::Licensing::license();
				std::cerr << std::endl;
				std::cerr << "Key=Value pairs:" << std::endl;
				std::cerr << std::endl;
				
				std::vector< std::pair<std::string,std::string> > V;
			
				V.push_back ( std::pair<std::string,std::string> ( "level=<["+::biobambam2::Licensing::formatNumber(getDefaultLevel())+"]>", libmaus2::bambam::BamBlockWriterBaseFactory::getBamOutputLevelHelpText() ) );
				V.push_back ( std::pair<std::string,std::string> ( "verbose=<["+::biobambam2::Licensing::formatNumber(getDefaultVerbose())+"]>", "print progress information" ) );
				V.push_back ( std::pair<std::string,std::string> ( "md5=<["+::biobambam2::Licensing::formatNumber(getDefaultMD5())+"]>", "create md5 check sum (default: 0)" ) );
				V.push_back ( std::pair<std::string,std::string> ( "md5filename=<filename>", "file name for md5 check sum" ) );
				V.push_back ( std::pair<std::string,std::string> ( "resetheadertext=[<>]", "replacement SAM header text file (default: filter header in source BAM file)" ) );
				V.push_back ( std::pair<std::string,std::string> ( "exclude=["+getDefaultExcludeFlags()+"]", "drop alignments having any of the given flags set" ) );
				V.push_back ( std::pair<std::string,std::string> ( std::string("resetaux=<[")+::biobambam2::Licensing::formatNumber(getDefaultResetAux())+"]>", "reset auxiliary fields" ) );
				V.push_back ( std::pair<std::string,std::string> ( "auxfilter=[<>]", "comma separated list of aux tags to keep if resetaux=0 (default: keep all)" ) );
				V.push_back ( std::pair<std::string,std::string> ( "resetsortorder=<["+::biobambam2::Licensing::formatNumber(getDefaultResetSortOrder())+"]>", "set sort order to unknown (default: 1)" ) );

				::biobambam2::Licensing::printMap(std::cerr,V);

				std::cerr << std::endl;

				std::cerr << "Alignment flags: PAIRED,PROPER_PAIR,UNMAP,MUNMAP,REVERSE,MREVERSE,READ1,READ2,SECONDARY,QCFAIL,DUP,SUPPLEMENTARY" << std::endl;
				
				return EXIT_SUCCESS;
			}
			
		return bamclipextract(arginfo);
	}
	catch(std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}
}

