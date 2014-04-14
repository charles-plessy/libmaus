/*
    libmaus
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
*/

#include <libmaus/network/FtpUrl.hpp>
#include <libmaus/network/Socket.hpp>
#include <libmaus/util/stringFunctions.hpp>

namespace libmaus
{
	namespace network
	{
		struct FtpSocket
		{
			typedef ::libmaus::network::ServerSocket server_socket_type;
			typedef server_socket_type::unique_ptr_type server_socket_ptr_type;
			
			libmaus::network::FtpUrl ftpurl;
			libmaus::network::ClientSocket CS;
			std::string statusline;
			libmaus::network::SocketBase::unique_ptr_type recsock;
			bool verbose;

			uint64_t readServerMessage()
			{
				uint64_t stat = 0;
				
				bool readinghelo = true;
				while ( readinghelo )
				{
					std::deque<char> dline;
					
					char c = -1;
					while ( CS.read(&c,1) ==1 )
					{
						dline.push_back(c);
						if ( c == '\n' )
							break;
					}
					
					while ( dline.size() && isspace(dline.front()) )
						dline.pop_front();
					while ( dline.size() && isspace(dline.back()) )
						dline.pop_back();
					
					std::deque<char> firsttoken;
					bool isnum = true;
					for ( uint64_t i = 0; i < dline.size() && !isspace(dline[i]); ++i )
					{
						firsttoken.push_back(dline[i]);
						if ( ! isdigit(firsttoken.back()) )
							isnum = false;
					}
						
					if ( isnum )
					{
						std::istringstream statistr(std::string(firsttoken.begin(),firsttoken.end()));
						statistr >> stat;
					
						if ( statistr )
						{
							readinghelo = false;
							statusline = std::string(dline.begin(),dline.end());
						}
					}
				}	
						
				return stat;
			}

			uint64_t checkedReadServerMessage()
			{
				uint64_t const stat = readServerMessage();
				
				if ( stat >= 400 )
				{
					libmaus::exception::LibMausException lme;
					lme.getStream() << statusline << std::endl;
					lme.finish();
					throw lme;
				}
				
				return stat;
			}
			
			void writeCommand(std::string const & command)
			{
				CS.write(command.c_str(),command.size());
			}

			FtpSocket(std::string const & url, bool const rverbose = false)
			: ftpurl(url), CS(ftpurl.port,ftpurl.host.c_str()), verbose(rverbose)
			{
				// read helo message
				uint64_t const helostat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] helostat=" << helostat << " " << statusline << std::endl;
				
				// send username
				writeCommand("USER anonymous\r\n");
				uint64_t const userstat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] userstat=" << userstat << " " << statusline << std::endl;
					
				// send "password"
				writeCommand("PASS anon@\r\n");
				uint64_t const passstat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] passstat=" << passstat << " " << statusline << std::endl;


				// binary mode
				writeCommand("TYPE I\r\n");
				uint64_t const binstat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] binstat=" << binstat << " " << statusline << std::endl;

				// split path into file and directory
				std::string file = ftpurl.path;
				std::string dir;
				uint64_t lastslash = file.find_last_of('/');
				if ( lastslash != std::string::npos )
				{
					dir = file.substr(0,lastslash+1);
					file = file.substr(lastslash+1);
				}
				
				// change directory
				std::ostringstream cwdostr;
				cwdostr << "CWD " << dir << "\r\n";
				std::string const cwd = cwdostr.str();
				writeCommand(cwd);
				uint64_t const cwdstat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] cwdstat=" << cwdstat << " " << statusline << std::endl;
					
				// see if we can use passive mode
				writeCommand("PASV\r\n");
				uint64_t const pasvstat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] pasvstat=" << pasvstat << " " << statusline << std::endl;
				bool const havepasv = pasvstat < 400;

				server_socket_ptr_type seso;
				std::string passivehost;
				int64_t passiveport = -1;

				// server supports passive mode, parse status for connection information
				if ( havepasv )
				{
					if ( statusline.find_last_of('(') != std::string::npos )
					{
						std::string conline = statusline;
						conline = conline.substr(conline.find_last_of('(')+1);

						if ( conline.find_last_of(')') != std::string::npos )
						{
							conline = conline.substr(0,conline.find_last_of(')'));
							
							std::deque<std::string> stokens =
								libmaus::util::stringFunctions::tokenize<std::string>(conline,std::string(","));
								
							if ( stokens.size() >= 5 )
							{
								std::ostringstream hostostr;
								hostostr << 
									stokens[0] << "." <<
									stokens[1] << "." <<
									stokens[2] << "." <<
									stokens[3];
								std::string const pasvhost = hostostr.str();
								
								uint64_t port = 0;
								for ( uint64_t i = 4; i < stokens.size(); ++i )
								{
									port *= 256;
									port += atoi(stokens[i].c_str());
								}
								
								passivehost = pasvhost;
								passiveport = port;
							}
						}
					}

					if ( passiveport < 0 )
					{
						libmaus::exception::LibMausException lme;
						lme.getStream() << "cannot parse " << statusline << std::endl;
						lme.finish();
						throw lme;
					}
					
					libmaus::network::SocketBase::unique_ptr_type tCS(
						new libmaus::network::ClientSocket(passiveport,passivehost.c_str())
					);
					recsock = UNIQUE_PTR_MOVE(tCS);
				}
				// server does not support passive mode, try active
				else
				{
					// allocate server port
					unsigned short serverport = 1024;
					std::string const & hostname = libmaus::network::GetHostName::getHostName();
					server_socket_ptr_type tseso(
						UNIQUE_PTR_MOVE(server_socket_type::allocateServerSocket(serverport,128,hostname.c_str(),32*1024)));
					seso = UNIQUE_PTR_MOVE(tseso);

					// construct PORT command		
					uint32_t const servadr = ntohl(seso->recadr.sin_addr.s_addr);
					std::ostringstream portostr;
					portostr << "PORT " << 
						((servadr >> 24) & 0xFF) << "," <<
						((servadr >> 16) & 0xFF) << "," <<
						((servadr >>  8) & 0xFF) << "," <<
						((servadr >>  0) & 0xFF);
					
					std::deque<unsigned int> dport;
					uint64_t tserverport = serverport;
					while ( tserverport != 0 )
					{
						dport.push_back(tserverport % 256);
						tserverport /= 256;
					}
					std::reverse(dport.begin(),dport.end());
					for ( uint64_t i = 0; i < dport.size(); ++i )
						portostr << "," << dport[i];
					portostr << "\r\n";
				
					// send port command
					writeCommand(portostr.str());
					uint64_t const portstat = checkedReadServerMessage();
					if ( verbose )
						std::cerr << "[D] portstat=" << portstat << " " << statusline << std::endl;
				}
				
				// send RETR command
				std::ostringstream retrostr;
				retrostr << "RETR " << file << "\r\n";
				writeCommand(retrostr.str());
				uint64_t const retrstat = checkedReadServerMessage();
				if ( verbose )
					std::cerr << "[D] retrstat=" << retrstat << " " << statusline << std::endl;

				if ( ! havepasv )
				{
					// wait for connection from ftp server
					libmaus::network::SocketBase::unique_ptr_type trecsock = seso->accept();
					recsock = UNIQUE_PTR_MOVE(trecsock);		
				}
			}
			
			ssize_t read(char * p, size_t n)
			{
				ssize_t r = recsock->read(p,n);
				
				if ( r <= 0 )
				{
					recsock.reset();		

					uint64_t const postretrstat = checkedReadServerMessage();
					if ( verbose )
						std::cerr << "[D] postretrstat=" << postretrstat << " " << statusline << std::endl;

					writeCommand("QUIT\r\n");
					uint64_t const quitstat = checkedReadServerMessage();
					if ( verbose )
						std::cerr << "[D] quitstat=" << quitstat << " " << statusline << std::endl;
				}
				
				return r;
			}
		};
	}
}

#include <libmaus/util/ArgInfo.hpp>

int main(int argc, char * argv[])
{
	try
	{
		libmaus::util::ArgInfo const arginfo(argc,argv);
		std::string const url = arginfo.getRestArg<std::string>(0);
		libmaus::network::FtpSocket ftpsock(url,true);
		
		char buf[2048];
		ssize_t r = -1;
		while ( (r=ftpsock.read(&buf[0],sizeof(buf))) > 0 )
			std::cout.write(&buf[0],r);
	}
	catch(std::exception const & ex)
	{
		std::cerr << ex.what() << std::endl;
		return EXIT_FAILURE;
	}
}
