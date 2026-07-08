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

	filesave::filesave(HANDLE hdata, db_handle dbh, long long filesize, long long prev, char* psign) : hfile(hdata), filesize(filesize), lck(dbh->lck), parent(prev), dbt(dbh), dbh(dbh)
	{
		db_query config(dbh, "SELECT value FROM config WHERE name = ?;");
		db_query dbq(dbh, "SELECT * FROM files where object = @offset;");
		if (prev != 0)
		{
			dbq.bind("@offset", prev);
			if (!dbq.step())
				throw std::runtime_error("invalid parent file; check repository integrity");
			files_pid = dbq.get<long long>("id");
			// prev = dbq.get<long long>("last_signature");
			// dbq.reset();
		}
		// size of one single signature block
		bsnt = lround(sqrt((double)filesize * 4.0 + 2.0)) + 1;
		raw_file = (bsnt > filesize) || (filesize <= THRESHOLD_DELTA) || (prev == NULL);
		no_signature = (bsnt > filesize) || (filesize <= THRESHOLD_DELTA);
		use_zlib = (filesize > THRESHOLD_ZLIB);
		if (!config.run_one_inline<bool>("zlib"))
			use_zlib = false;
		if (config.run_one_inline<bool>("raw_content"))
			raw_file = true, no_signature = true;
		write_signature = !no_signature && config.run_one_inline<bool>("embed_s");
		if (write_signature && psign != NULL && config.run_one_inline<bool>("local"))
			write_signature = false;
		if (write_signature)
			bsnt = lround(sqrt((double)filesize * 12.0 + 2.0)) + 1;
		if (!raw_file)
		{
			/*dbq.bind("@offset", prev);
			if (!dbq.step())
				throw std::runtime_error("invalid parent file; check repository integrity");*/
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
			raw_file = !prob(rd);
			files_delta_cnt = cnt;
			files_delta_size = sz;
			files_source_size = ssz - sz;
			if (raw_file && config.run_one_inline<bool>("archive"))
				raw_file = false;
			if (!raw_file && psign != NULL)
			{
				int sigsize;
				memcpy(&sigsize, psign, 4);
				memcpy(&blocksize, psign + 4, 4);
				char* signaturestore = psign + 8;
				int k = sigsize / 40;
				for (int i = 0; i < k; i++)
				{
					filehash fh;
					unsigned ph;
					memcpy(&ph, signaturestore + 40 * i, 4);
					memcpy(&fh, signaturestore + 40 * i + 4, 32);
					signature[ph].emplace_back(fh, i);
				}
				for (auto it = signature.begin(); it != signature.end(); it++)
				{
					auto& values = it.value();
					std::sort(values.begin(), values.end());
				}
				dbuf.reserve(DELTA_BUFFER_SIZE);
				dbuffer.init(blocksize);
				dhashes.init(blocksize);
				chash = 0;
				dhashmul = 1;
				for (int i = blocksize; i > 0; i--)
					dhashmul *= K;
			}
			else if (!raw_file)
			{
				// read signature
				SetFilePointer2(hdata, prev);
				char header[24];
				DWORD readen;
				if (!ReadFile(hdata, header, 24, &readen, NULL))
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
					if (!ReadFile(hdata, signaturestore, sigsize, &readen, NULL))
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
					dhashmul = 1;
					for (int i = blocksize; i > 0; i--)
						dhashmul *= K;
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
		DWORD written;
		if (!WriteFile(hdata, header, 16, &written, NULL))
			throw winerror();
		if (written != 16)
			throw ioerror(hdata, "error writing file header; check free space on disk");
		int sbl = (int)(filesize / bsnt) * 40;
		if (!no_signature)
		{
			sigbuf.init(bsnt);
			nsstore = std::make_unique<char[]>(sbl + 8);
			nsstore_lst = 8;
			memcpy(nsstore.get(), &sbl, 4);
			memcpy(nsstore.get() + 4, &bsnt, 4);
		}
		if (write_signature)
		{
			// allocate place for signature since we know what space it takes
			i1.QuadPart = postition + 16 + 8 + sbl;
			if (!SetFilePointerEx(hdata, i1, &data_begin, FILE_BEGIN))
				throw winerror();
			if (!SetEndOfFile(hdata))
				throw winerror();
		}
		else
		{
			i1.QuadPart = 0;
			if (!SetFilePointerEx(hdata, i1, &data_begin, FILE_CURRENT))
				throw winerror();
		}
	}
	void filesave::_process(const char* buf, int bufsz)
	{
		DWORD writen;
		if (use_zlib)
		{
			zs.avail_in = bufsz;
			zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(buf));
			int flush = (bufsz > 0 ? Z_NO_FLUSH : Z_FINISH), ret;
			do
			{
				zs.avail_out = ZSTREAM_BUFFER_SIZE;
				zs.next_out = reinterpret_cast<Bytef*>(zsbuf.get());
				ret = deflate(&zs, flush);
				if (ret == Z_STREAM_ERROR)
					throw std::runtime_error("zlib compression error");
				int have = ZSTREAM_BUFFER_SIZE - zs.avail_out;
				if (have > 0)
				{
					if (!WriteFile(hfile, zsbuf.get(), have, &writen, NULL))
						throw winerror();
					if (writen != have)
						throw ioerror(hfile, "error writing compressed file data; check free space on disk");
				}
			} while (zs.avail_out == 0);
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
		files_delta_size += bufsz;
	}
	inline void filesave::_send_delta_command(int cmd)
	{
		if (use_zlib)
		{
			unsigned ucmd = (unsigned)cmd;
			unsigned xcmd = ucmd - dprevcmd;
			dprevcmd = ucmd;
			_process(reinterpret_cast<char*>(&xcmd), sizeof(xcmd));
		}
		else
		{
			_process(reinterpret_cast<char*>(&cmd), sizeof(cmd));
		}
	}
	void filesave::_flush_delta_buffer()
	{
		if (!dbuf.empty())
		{
			int cmd = -(int)dbuf.size();
			_send_delta_command(cmd);
			_process(dbuf.data(), (int)dbuf.size());
			dbuf.clear();
		}
	}
	void filesave::_push_delta_buffer(char ch)
	{
		if (dbuf.size() == DELTA_BUFFER_SIZE)
			_flush_delta_buffer();
		dbuf.push_back(ch);
	}
	void filesave::_find_delta_hash(unsigned hsh)
	{
		auto it = signature.find(hsh);
		if (it != signature.end())
		{
			blake3_hasher bh;
			blake3_hasher_init(&bh);
			dbuffer.serialize([&bh](const char* s, size_t len)
			{
				blake3_hasher_update(&bh, s, len);
			});
			filehash fh;
			blake3_hasher_finalize(&bh, reinterpret_cast<uint8_t*>(&fh), sizeof(fh));
			const std::vector<keyvalue<filehash, int>>& vars = it.value();
			auto it2 = std::lower_bound(vars.begin(), vars.end(), keyvalue<filehash, int>{fh, 0});
			if (it2 != vars.end() && it2->key == fh)
			{
				_flush_delta_buffer();
				_send_delta_command(it2->value);
				dcurbytes = 0, chash = 0;
			}
		}
	}
	void filesave::process(const char* buf, int bufsz)
	{
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
						char sigentry[40]{ 0 };
						memcpy(sigentry, &shash, 4);
						blake3_hasher sighash;
						blake3_hasher_init(&sighash);
						sigbuf.serialize([&](char* buf, size_t len)
						{
							blake3_hasher_update(&sighash, buf, len);
						});
						blake3_hasher_finalize(&sighash, (uint8_t*)(sigentry + 4), 32);
						memcpy(nsstore.get() + nsstore_lst, sigentry, sizeof(sigentry));
						nsstore_lst += sizeof(sigentry);
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
			for (int i = 0; i < bufsz; i++)
			{
				char ch = buf[i];
				chash *= K;
				chash += (unsigned char)ch;
				if (dcurbytes < blocksize)
				{
					dhashes.push(chash);
					dbuffer.push(ch);
					if (++dcurbytes == blocksize)
					{
						_find_delta_hash(chash);
					}
					continue;
				}
				unsigned phsh = dhashes.push(chash);
				unsigned hsh = chash - phsh * dhashmul;
				_push_delta_buffer(dbuffer.push(ch));
				_find_delta_hash(hsh);
			}
		}
		// check eof
		bytespassed += bufsz;
		if (bytespassed > filesize)
			throw std::runtime_error("Too much bytes recieved than file size");
		if (bytespassed == filesize)
		{
			if (!raw_file)
			{
				dbuffer.serialize([&](const char* x, size_t len)
				{
					int have = std::min(dcurbytes, (int)len);
					dbuf.insert(dbuf.end(), x, x + have);
					dcurbytes -= have;
				});
				_flush_delta_buffer();
			}
			_process(buf, 0);
		}
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
		LARGE_INTEGER i1 = data_begin, i2;
		if (!SetFilePointerEx(hfile, i1, &i2, FILE_BEGIN))
			throw winerror();
		if (!SetEndOfFile(hfile))
			throw winerror();
	}
	long long filesave::commit(const FILETIME* moddate)
	{
		if (bytespassed < filesize)
			throw std::runtime_error("Not enough bytes recieved to file size");
		if (!SetEndOfFile(hfile))
			throw winerror();
		if (use_zlib)
			deflateEnd(&zs);
		LARGE_INTEGER i1, i2;
		i1.QuadPart = 0;
		SetFilePointerEx(hfile, i1, &i2, FILE_CURRENT);
		long long tarsize = i2.QuadPart;
		long long objsize = i2.QuadPart - postition;
		i1.QuadPart = postition;
		SetFilePointerEx(hfile, i1, &i2, FILE_BEGIN);
		int flags = 0;
		if (!raw_file)
			flags |= (int)fileflags::CONTENT_DELTA;
		if (use_zlib)
			flags |= (int)fileflags::CONTENT_ZLIB;
		if (!no_signature)
			flags |= (int)fileflags::CONTAINS_SIGNATURE;
		if (use_zlib && !raw_file)
			flags |= (int)fileflags::CONTENT_DELTA_ENCODED;
		long long header = flags + (objsize << 8);
		DWORD writen;
		if (!WriteFile(hfile, &header, 8, &writen, NULL))
			throw winerror();
		if (write_signature)
		{
			i1.QuadPart = 8;
			if (!SetFilePointerEx(hfile, i1, &i2, FILE_CURRENT))
				throw winerror();
			if (!WriteFile(hfile, nsstore.get(), nsstore_lst, &writen, NULL))
				throw winerror();
			if (writen != nsstore_lst)
				throw ioerror(hfile, "Error writing signature to data file");
		}
		FlushFileBuffers(hfile);
		// main work finished!
		if (raw_file)
		{
			files_delta_cnt = 0;
			files_delta_size = 0;
			files_source_size = filesize;
		}
		filehash filehsh, ohsh;
		blake3_hasher_finalize(&file_hash, reinterpret_cast<uint8_t*>(&filehsh), sizeof(filehsh));
		// reread all our object ((
		i1.QuadPart = postition;
		SetFilePointerEx(hfile, i1, &i2, FILE_BEGIN);
		blake3_hasher_init(&file_hash);
		zsbuf = std::make_unique<char[]>(ZSTREAM_BUFFER_SIZE);
		BOOL ok;
		DWORD readen;
		while (ok = ReadFile(hfile, zsbuf.get(), ZSTREAM_BUFFER_SIZE, &readen, NULL))
		{
			blake3_hasher_update(&file_hash, zsbuf.get(), readen);
			if (readen == 0)
				break;
		}
		if (ok == FALSE)
			throw winerror();
		blake3_hasher_finalize(&file_hash, reinterpret_cast<uint8_t*>(&ohsh), sizeof(ohsh));
		db_query dbq(dbh, "INSERT INTO objects(offset, size, hash, type) VALUES (@offset, @size, @hash, @type);");
		dbq.bind("@offset", postition);
		dbq.bind("@size", objsize);
		dbq.bind("@type", flags);
		dbq.bind("@hash", &ohsh);
		dbq.run();
		long long sid = sqlite3_last_insert_rowid(dbh->db);
		dbq.setup("INSERT INTO files(id, object, file_size, file_hash, modified_date, updated_at, deltas_size, deltas_count, source_size, parent) VALUES (@id, @pos, @fs, @fh, @modd, @updd, @ds, @dc, @ss, @par);");
		dbq.bind("@id", sid);
		dbq.bind("@pos", postition);
		dbq.bind("@fs", filesize);
		dbq.bind("@fh", &filehsh);
		if (moddate == NULL)
		{
			FILETIME ft;
			GetSystemTimeAsFileTime(&ft);
			ULARGE_INTEGER uli;
			uli.LowPart = ft.dwLowDateTime;
			uli.HighPart = ft.dwHighDateTime;
			dbq.bind("@modd", uli.QuadPart);
		}
		else
		{
			ULARGE_INTEGER uli;
			uli.LowPart = moddate->dwLowDateTime;
			uli.HighPart = moddate->dwHighDateTime;
			dbq.bind("@modd", uli.QuadPart);
		}
		dbq.bind("@updd", time(nullptr));
		dbq.bind("@par", files_pid);
		dbq.bind("@ss", files_source_size);
		dbq.bind("@ds", files_delta_size);
		dbq.bind("@dc", files_delta_cnt);
		dbq.run();
		dbq.setup("UPDATE config SET value = @value WHERE name = @name;");
		dbq.bind("@name", "filesize");
		dbq.bind("@value", tarsize);
		dbq.run();
		dbt.commit();
		return postition;
	}
	void SetFilePointer2(HANDLE hFile, LONGLONG position)
	{
		LARGE_INTEGER i1, i2;
		i1.QuadPart = position;
		if (!SetFilePointerEx(hFile, i1, &i2, FILE_BEGIN))
			throw winerror();
	}
}