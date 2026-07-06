/////////////////////////////////////////////////////////////////////////////
//                                                                         //
//  SVS - Simple Versioning System                                         //
//  Copyright (C) 2026 Mark_Pr                                             //
//                                                                         //
//  This program is free software; you can redistribute it and/or modify   //
//  it under the terms of the GNU General Public License as published by   //
//  the Free Software Foundation; either version 2 of the License, or      //
//  (at your option) any later version.                                    //
//                                                                         //
//  This program is distributed in the hope that it will be useful,        //
//  but WITHOUT ANY WARRANTY; without even the implied warranty of         //
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the           //
//  GNU General Public License for more details.                           //
//                                                                         //
//  You should have received a copy of the GNU General Public License      //
//  along with this program; if not, write to the Free Software            //
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.              //
//                                                                         //
/////////////////////////////////////////////////////////////////////////////
#include "filesubsystem.h"

namespace svs
{
	/// <summary>
	/// Initialize streaming processor
	/// In parallel calculates delta and signature for new file
	/// And, in addition, compresses file if needed
	/// </summary>
	/// <param name="hdata">handle to MAIN.DAT</param>
	/// <param name="dbh">handle to sqlite3 database MAIN.DB</param>
	/// <param name="filesize">New file size. Required</param>
	/// <param name="prev">Previous file offset. NULL if it is new file</param>

	inline filesave::filesave(HANDLE hdata, db_handle dbh, long long filesize, long long prev) : hfile(hdata), filesize(filesize), lck(dbh->lck), parent(prev), dbt(dbh)
	{
		// size of one single signature block
		bsnt = lround(sqrt((double)filesize * 11.0 + 2.0)) + 1;
		raw_file = (bsnt > filesize) || (filesize <= THRESHOLD_DELTA) || (prev == NULL);
		no_signature = (bsnt > filesize) || (filesize <= THRESHOLD_DELTA);
		use_zlib = (filesize > THRESHOLD_ZLIB);
		if (!raw_file)
		{
			db_query dbq(dbh, "SELECT * FROM files where object = @offset;");
			dbq.bind("@offset", prev);
			if (!dbq.step())
				throw std::runtime_error("invalid parent file; check repository integrity");
			// imagine such case: file has 499 deltas and then developers started making fork branches from it
			// in this case all this stuff will just copy files without deltas
			// to improve it there is two ways
			// 1. no Copy-On-Write and each overflow resave #250 file in chain
			// 2. random!
			// of course, as real developers, we select 2nd option
			/*if (dbq.get<long long>("deltas_count") >= MAX_DELTAS_ONE_FILE)
			raw_file = false;*/
			long long cnt = dbq.get<long long>("deltas_count") + 1;
			long long sz = dbq.get<long long>("deltas_size");
			long long ssz = dbq.get<long long>("source_size") + sz;
			std::random_device rd;
			std::normal_distribution<double> limit(MAX_DELTAS_ONE_FILE, 100.0);
			std::normal_distribution<double> opercost(8.0);
			std::normal_distribution<double> cpucost(36.0);
			long long delta_limit = std::max(100ll, llround(limit(rd)));
			// how much data need to be processed to get target file
			// opercost equal to cost of zlib usage
			long long procsize = std::max(0ll, llround(sz * opercost(rd))) + ssz + filesize;
			double efficiency = (double)filesize / (double)procsize;
			// Bigger file -> bigger amount of deltas allowed
			long long cpuwork = std::max(0ll, llround(cnt * cpucost(rd))) + filesize;
			double cpuscore = (double)filesize / (double)cpuwork;
			// if file big (1GB) it is better to have many deltas
			// if file small (3KB) it is better to have few deltas
			// almost same as cpuscore, but attached to delta limit
			double deltascore = pow(std::min(1.0, log(10.0) / log(std::max(1.0, (double)(cnt - delta_limit)))), log((double)filesize) / log(9.0));
			// file x10 from scource -> delta will pure work -> save full
			double highscore = std::min(1.0, pow(0.5, ((double)filesize / (double)ssz - 1) / 5.0));
			std::bernoulli_distribution prob(deltascore * highscore * sqrt(cpuscore) * std::min(1.0, cpuscore * sqrt(3.0 * efficiency)));
			raw_file = prob(rd);
			files_delta_cnt = cnt;
			files_delta_size = sz;
			files_source_size = ssz - sz;
			if (!raw_file)
			{
				// read signature
				OVERLAPPED ol;
				ZeroMemory(&ol, sizeof(ol));
				ol.Offset = static_cast<DWORD>(prev & 0xFFFFFFFF);
				ol.OffsetHigh = static_cast<DWORD>((prev >> 32) & 0xFFFFFFFF);
				char header[24];
				DWORD readen;
				if (!ReadFile(hdata, header, 24, &readen, &ol))
					throw winerror();
				int parflags = header[0];
				if ((parflags & (int)fileflags::CONTAINS_SIGNATURE) == 0)
				{
					// couldn't create delta - no signature
					raw_file = true;
				}
				else
				{
					if (readen != 24)
						throw ioerror(hdata, "error reading file header; check repository integrity");
					memcpy(&blocksize, header + 20, 4);
					int sigsize;
					memcpy(&sigsize, header + 16, 4);
					int k = sigsize / 40;
					char* signaturestore = new char[sigsize];
					if (!ReadFile(hdata, signaturestore, sigsize, &readen, &ol))
						throw winerror();
					if (readen != sigsize)
						throw ioerror(hdata, "error reading file signature; check repository integrity");
					for (int i = 0; i < k; i++)
					{
						filehash fh;
						unsigned ph;
						memcpy(&ph, signaturestore + 40 * i, 4);
						memcpy(&fh, signaturestore + 40 * i + 4, 32);
						signature[ph].emplace_back(fh, i);
					}
					delete[] signaturestore;
					for (auto it = signature.begin(); it != signature.end(); it++)
					{
						auto& values = it.value();
						std::sort(values.begin(), values.end());
					}
					dbuf.reserve(DELTA_BUFFER_SIZE);
					dbuffer.init(blocksize);
					dhashes.init(blocksize);
					chash = 0;
				}
			}
		}
		LARGE_INTEGER i1, i2;
		i1.QuadPart = 0;
		if (!SetFilePointerEx(hdata, i1, &i2, FILE_END))
			throw winerror();
		postition = i2.QuadPart;
		memset(&zs, 0, sizeof(zs));
		if (use_zlib)
		{
			zs.zalloc = Z_NULL;
			zs.zfree = Z_NULL;
			zs.opaque = Z_NULL;
			deflateInit(&zs, Z_BEST_COMPRESSION);
			zsbuf = std::make_unique<char[]>(ZSTREAM_BUFFER_SIZE);
		}
		blake3_hasher_init(&file_hash);
		char header[16];
		// first 8 bytes will be written at commit...
		memcpy(header + 8, &prev, 8);
		olsign.Offset = i2.LowPart;
		olsign.OffsetHigh = i2.HighPart;
		DWORD written;
		if (!WriteFile(hdata, header, 16, &written, &olsign))
			throw winerror();
		if (written != 16)
			throw ioerror(hdata, "error writing file header; check free space on disk");
		if (!no_signature)
		{
			int sbl = (int)(filesize / bsnt) * 40;
			memcpy(header, &sbl, 4);
			memcpy(header + 4, &bsnt, 4);
			sigbuf.init(bsnt);
			if (!WriteFile(hdata, header, 8, &written, &olsign))
				throw winerror();
			if (written != 8)
				throw ioerror(hdata, "error writing signature header; check free space on disk");
			// allocate place for signature since we know what space it takes
			i1.QuadPart = postition + 16 + 8 + sbl;
			if (!SetFilePointerEx(hdata, i1, &i2, FILE_BEGIN))
				throw winerror();
			if (!SetEndOfFile(hdata))
				throw winerror();
		}
	}
	void filesave::_process(const char* buf, int bufsz)
	{
		DWORD readen, writen;
		if (use_zlib)
		{
			zs.avail_in = bufsz;
			zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(buf));
			zs.next_out = reinterpret_cast<Bytef*>(zsbuf.get());
			zs.avail_out = ZSTREAM_BUFFER_SIZE;
		}
		else
		{
			if (bufsz == 0)
				return;
			if (!WriteFile(hfile, buf, bufsz, &writen, NULL))
				throw winerror();
			if (writen != bufsz)
				throw ioerror(hfile, "error writing file data; check free space on disk");
		}
	}
	void filesave::process(const char* buf, int bufsz)
	{
		DWORD readen, writen;
		if (!unwind)
		{
			// first loop through file
			// update hash
			blake3_hasher_update(&file_hash, buf, bufsz * sizeof(char));
			// update signature
			if (!no_signature)
			{
				for (int i = 0; i < bufsz; i++)
				{
					shash *= K;
					shash += (unsigned char)buf[i];
					sigbuf.push(buf[i]);
					if (sigbuf.full())
					{
						char sigentry[40];
						memcpy(sigentry, &shash, 4);
						blake3_hasher sighash;
						blake3_hasher_init(&sighash);
						sigbuf.serialize([&](char* buf, size_t len)
						{
							blake3_hasher_update(&sighash, buf, len);
						});
						blake3_hasher_finalize(&sighash, (uint8_t*)(sigentry + 4), 32);
						if (!WriteFile(hfile, sigentry, 40, &writen, &olsign))
							throw winerror();
						if (writen != 40)
							throw ioerror(hfile, "error writing signature entry; check free space on disk");
						shash = 0;
					}
				}
			}
		}
		// update file
		if (raw_file)
		{
			_process(buf, bufsz);
		}
		else
		{

		}
		// check eof
		bytespassed += bufsz;
		if (bytespassed > filesize)
			throw std::runtime_error("Too much bytes recieved than file size");
		if (bytespassed == filesize)
			_process(buf, 0);
	}
	inline void filesave::restart()
	{
		if (!need_restart())
			throw std::runtime_error("Restart not supported on current configuration");
		raw_file = true;
		unwind = true;
		if (use_zlib)
		{
			deflateEnd(&zs);
			deflateInit(&zs, Z_BEST_COMPRESSION);
		}
		bytespassed = 0;
		LARGE_INTEGER i1, i2;
		i1.LowPart = olsign.Offset;
		i1.HighPart = olsign.OffsetHigh;
		if (!SetFilePointerEx(hfile, i1, &i2, FILE_BEGIN))
			throw winerror();
		if (!SetEndOfFile(hfile))
			throw winerror();
	}
	long long filesave::commit()
	{
		if (bytespassed < filesize)
			throw std::runtime_error("Not enough bytes recieved to file size");
		SetEndOfFile(hfile);
		if (use_zlib)
			deflateEnd(&zs);
		LARGE_INTEGER i1, i2;
		i1.QuadPart = 0;
		SetFilePointerEx(hfile, i1, &i2, FILE_CURRENT);
		i1.QuadPart = postition;
		SetFilePointerEx(hfile, i1, &i2, FILE_BEGIN);
		char header[8];
		DWORD writen;
		if (!WriteFile(hfile, header, 8, &writen, NULL))
			throw winerror();
		FlushFileBuffers(hfile);
		// main work finished!
		filehash filehsh;
		blake3_hasher_finalize(&file_hash, reinterpret_cast<uint8_t*>(&filehsh), sizeof(filehsh));
		db_query dbq1(dbh, "INSERT INTO objects(offset, size, hash, type) VALUES");
		dbt.commit();
		return postition;
	}
}