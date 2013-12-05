/*
    libmaus
    Copyright (C) 2009-2013 German Tischler
    Copyright (C) 2011-2013 Genome Research Limited

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
*/
#if ! defined(LIBMAUS_GAMMA_SPARSEGAMMAGAPCONCATDECODER_HPP)
#define LIBMAUS_GAMMA_SPARSEGAMMAGAPCONCATDECODER_HPP

#include <libmaus/gamma/SparseGammaGapFileIndexMultiDecoder.hpp>

namespace libmaus
{
	namespace gamma
	{
		struct SparseGammaGapConcatDecoder
		{
			typedef SparseGammaGapConcatDecoder this_type;
			
			typedef libmaus::util::unique_ptr<this_type>::type unique_ptr_type;
			typedef libmaus::util::shared_ptr<this_type>::type shared_ptr_type;
			
			typedef libmaus::aio::SynchronousGenericInput<uint64_t> stream_type;
			
			std::vector<std::string> const filenames;
			uint64_t fileptr;
			libmaus::aio::CheckedInputStream::unique_ptr_type CIS;
			libmaus::aio::SynchronousGenericInput<uint64_t>::unique_ptr_type SGI;
			libmaus::gamma::GammaDecoder<stream_type>::unique_ptr_type gdec;
			std::pair<uint64_t,uint64_t> p;
			
			struct iterator
			{
				SparseGammaGapConcatDecoder * owner;
				uint64_t v;
				
				iterator()
				: owner(0), v(0)
				{
				
				}
				iterator(SparseGammaGapConcatDecoder * rowner)
				: owner(rowner), v(owner->decode())
				{
				
				}
				
				uint64_t operator*() const
				{
					return v;
				}
				
				iterator operator++(int)
				{
					iterator copy = *this;
					v = owner->decode();
					return copy;
				}
				
				iterator operator++()
				{
					v = owner->decode();
					return *this;
				}
			};
		
			void openNextFile()
			{			
				if ( fileptr < filenames.size() )
				{
					// std::cerr << "opening file " << fileptr << std::endl;
				
					gdec.reset();
					SGI.reset();
					CIS.reset();
					
					libmaus::aio::CheckedInputStream::unique_ptr_type tCIS(new libmaus::aio::CheckedInputStream(filenames[fileptr++]));
					CIS = UNIQUE_PTR_MOVE(tCIS);
					
					libmaus::aio::SynchronousGenericInput<uint64_t>::unique_ptr_type tSGI(new libmaus::aio::SynchronousGenericInput<uint64_t>(*CIS,8*1024));
					SGI = UNIQUE_PTR_MOVE(tSGI);
					
					libmaus::gamma::GammaDecoder<stream_type>::unique_ptr_type tgdec(new libmaus::gamma::GammaDecoder<stream_type>(*SGI));
					gdec = UNIQUE_PTR_MOVE(tgdec);

					p.first = gdec->decode();
					p.second = gdec->decode();				
				}
				else
				{
					p.first = 0;
					p.second = 0;
				}
			}
		
			SparseGammaGapConcatDecoder(std::vector<std::string> const & rfilenames)
			: filenames(rfilenames), fileptr(0), p(0,0)
			{
				openNextFile();
			}
			
			void seek(uint64_t const ikey)
			{
				p.first = 0;
				p.second = 0;
				
				std::pair<uint64_t,uint64_t> const P = SparseGammaGapFileIndexMultiDecoder(filenames).getBlockIndex(ikey);
				fileptr = P.first;
				
				// std::cerr << "fileptr=" << fileptr << " blockptr=" << P.second << std::endl;
				
				if ( fileptr < filenames.size() )
				{
					std::string const fn = filenames[fileptr++];

					libmaus::aio::CheckedInputStream::unique_ptr_type tCIS(new libmaus::aio::CheckedInputStream(fn));
					CIS = UNIQUE_PTR_MOVE(tCIS);
					
					SparseGammaGapFileIndexDecoder indexdec(*CIS);
					uint64_t const minkey = indexdec.getMinKey();
					
					// std::cerr << "minkey=" << minkey << std::endl;
					
					if ( ikey < minkey )
					{
						// this should only happen for the first file
						assert ( fileptr == 1 ); // value has been incremented above
						assert ( indexdec.getBlockIndex(ikey) == 0 );
						assert ( indexdec.get(indexdec.getBlockIndex(ikey)).ibitoff == 0 );
						
						// seek to front of file
						CIS->clear();
						CIS->seekg(0);

						libmaus::aio::SynchronousGenericInput<uint64_t>::unique_ptr_type tSGI(new libmaus::aio::SynchronousGenericInput<uint64_t>(*CIS,8*1024));
						SGI = UNIQUE_PTR_MOVE(tSGI);
						
						libmaus::gamma::GammaDecoder<stream_type>::unique_ptr_type tgdec(new libmaus::gamma::GammaDecoder<stream_type>(*SGI));
						gdec = UNIQUE_PTR_MOVE(tgdec);

						p.first = gdec->decode() - ikey;
						p.second = gdec->decode();						
					}
					else
					{
						uint64_t const block = indexdec.getBlockIndex(ikey);
						uint64_t const ibitoff = indexdec.get(block).ibitoff;
						uint64_t offset = ikey-indexdec.get(block).ikey;
						uint64_t const word = ibitoff / 64;
						uint64_t const wbitoff = ibitoff - word*64;
						
						// seek to word where we start
						CIS->clear();
						CIS->seekg(word * 8);

						// set up word reader
						libmaus::aio::SynchronousGenericInput<uint64_t>::unique_ptr_type tSGI(new libmaus::aio::SynchronousGenericInput<uint64_t>(*CIS,8*1024));
						SGI = UNIQUE_PTR_MOVE(tSGI);

						// set up gamma decoder
						libmaus::gamma::GammaDecoder<stream_type>::unique_ptr_type tgdec(new libmaus::gamma::GammaDecoder<stream_type>(*SGI));
						gdec = UNIQUE_PTR_MOVE(tgdec);
						// discard wbitoff bits
						if ( wbitoff )
							gdec->decodeWord(wbitoff);

						// read pair and discard difference part
						p.first = gdec->decode();
						p.first = 0;
						p.second = gdec->decode();
						
						// we seeked to a block start, this should not be zero
						assert ( p.second );
						
						// skip value if we do not stay on the start of the block
						if ( offset )
						{
							// skip one value
							offset -= 1;
						
							// read next pair
							p.first = gdec->decode();
							p.second = gdec->decode();
						}

						while ( offset >= p.first+1 )
						{
							if ( p.second == 0 )
							{
								if ( fileptr == filenames.size() )
									break;
								else
									openNextFile();
							}
							else
							{
								offset -= p.first+1;

								p.first = gdec->decode();
								p.second = gdec->decode();
							}
						}
						
						assert ( p.second == 0 || offset <= p.first );

						while ( offset-- )
							decode();
					}
				}			
			
			}

			SparseGammaGapConcatDecoder(std::vector<std::string> const & rfilenames, uint64_t const ikey)
			: filenames(rfilenames)
			{
				seek(ikey);
			}
			
			uint64_t decode()
			{
				// no more non zero values
				while ( (!p.second) && fileptr < filenames.size() )
					openNextFile();

				if ( !p.second )
				{
					return 0;
				}
				
				// zero value
				if ( p.first )
				{
					p.first -= 1;
					return 0;
				}
				// non zero value
				else
				{
					uint64_t const retval = p.second;

					// get information about next non zero value
					p.first = gdec->decode();
					p.second = gdec->decode();
					
					return retval;
				}
			}			
			
			std::pair<uint64_t,uint64_t> nextPair()
			{
				p.first = gdec->decode();
				p.second = gdec->decode();
				
				// no more non zero values
				while ( (!p.second) && fileptr < filenames.size() )
					openNextFile();

				return p;
			}
			
			iterator begin()
			{
				return iterator(this);
			}
		};	
	}
}
#endif
