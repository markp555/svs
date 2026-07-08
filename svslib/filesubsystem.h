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
#pragma once
#include "maindb.h"
#include <tsl/robin_map.h>
#include <vector>
#include <deque>
#include <tuple>
#include <zlib.h>
#include <string>
#include <random>
#include "blake3.h"
#include "framework.h"
// File subsystem
// MAIN.DAT file API
namespace svs
{
	struct winerror : std::runtime_error
	{
		winerror() : std::runtime_error("WinError " + std::to_string(GetLastError())) {}
	};

	struct ioerror : std::runtime_error
	{
		HANDLE hfile;
		ioerror(HANDLE hfile, const char* msg) : std::runtime_error(msg), hfile(hfile) {}
	};

	union filehash
	{
		char raw[32];
		unsigned int val[8];
	};

	inline bool operator<(const filehash& a, const filehash& b)
	{
		return memcmp(a.raw, b.raw, sizeof(a.raw)) < 0;
	}

	inline bool operator==(const filehash& a, const filehash& b)
	{
		return memcmp(a.raw, b.raw, sizeof(a.raw)) == 0;
	}

	// small replacement for std::pair allowing Value be non-sortable
	template <typename Key, typename Value>
	struct keyvalue
	{
		Key key;
		Value value;

		bool operator<(const keyvalue& oth) const
		{
			return key < oth.key;
		}
	};

	// only 3 effective flags now
	enum class fileflags
	{
		CONTENT_ZLIB = (1 << 1),
		SIGNATURE_ZLIB = (1 << 2), // not used, should be 0
		CONTENT_DELTA = (1 << 3),
		CONTAINS_SIGNATURE = (1 << 4),
		CONTENT_DELTA_ENCODED = (1 << 5), // not used, equal to CONTENT_ZLIB & CONTENT_DELTA
		CONTENT_READ_ONLY = (1 << 6) // not used by file subsystem
	};

	template <std::regular T>
	class ring_buffer
	{
	private:
		T* buffer;
		size_t bufsz;
		size_t ptr;
	public:
		ring_buffer(const ring_buffer&) = delete;
		ring_buffer& operator=(const ring_buffer&) = delete;
		ring_buffer() noexcept : buffer(nullptr), bufsz(0), ptr(0) {}
		ring_buffer(size_t sz) : bufsz(sz), ptr(0), buffer(new T[sz]) {}
		~ring_buffer() noexcept { delete buffer; }
		T push(T oth)
		{
			T old = buffer[ptr];
			buffer[ptr] = oth;
			if (++ptr == bufsz)
				ptr = 0;
			return old;
		}
		void init(size_t sz)
		{
			if (bufsz != sz)
			{
				delete buffer;
				buffer = new T[sz];
				bufsz = sz;
			}
			ptr = 0;
		}
		inline bool full() { return ptr == 0; }
		inline T* data() { return buffer; }
		template<std::invocable<T*, size_t> F>
		void serialize(F&& f)
		{
			if (ptr == 0)
			{
				f(buffer, bufsz);
			}
			else
			{
				f(buffer + ptr, bufsz - ptr);
				f(buffer, ptr);
			}
		}
	};

	// file saving streaming algorithm
	struct filesave
	{
		HANDLE hfile;
		db_handle dbh;
		db_transaction dbt;
		std::unique_lock<std::shared_mutex> lck;
		long long parent, postition, filesize;
		int blocksize, bsnt;
		tsl::robin_pg_map<unsigned, std::vector <keyvalue <filehash, int>>> signature;
		// std::deque <keyvalue <char, unsigned>> dq;
		ring_buffer <char> dbuffer, sigbuf;
		ring_buffer <unsigned> dhashes;
		unsigned dhashmul = 0, dprevcmd = 0;
		int dcurbytes = 0;
		unsigned chash = 0;
		unsigned shash = 0;
		long long bytespassed = 0;
		int buffer_lst = 0;
		bool raw_file = true, finished = false, no_signature = true, use_zlib = false;
		z_stream zs;
		int flags = 0;
		long long files_delta_cnt, files_delta_size, files_source_size, files_pid = 0;
		blake3_hasher file_hash;
		bool unwind = false;
		OVERLAPPED olsign;
		std::unique_ptr<char[]> zsbuf;
		std::vector<char> dbuf;

		constexpr static inline unsigned K = 239;
		constexpr static inline unsigned THRESHOLD_ZLIB = 220;
		constexpr static inline unsigned THRESHOLD_DELTA = 90 * 90 / 11;
		constexpr static inline unsigned MAX_DELTAS_ONE_FILE = 500;
		constexpr static inline unsigned ZSTREAM_BUFFER_SIZE = 65524;
		constexpr static inline unsigned DELTA_BUFFER_SIZE = 1024 * 1024;

		/// <summary>
		/// Initialize streaming processor
		/// In parallel calculates delta and signature for new file
		/// And, in addition, compresses file if needed
		/// </summary>
		/// <param name="hdata">handle to MAIN.DAT</param>
		/// <param name="dbh">handle to sqlite3 database MAIN.DB</param>
		/// <param name="filesize">New file size. Required</param>
		/// <param name="prev">Previous file offset. NULL if it is new file</param>
		filesave(HANDLE hdata, db_handle dbh, long long filesize, long long prev);

		// internal function. writes directly to MAIN.DAT
		void _process(const char* buf, int bufsz);

		inline void _send_delta_command(int cmd);

		void _flush_delta_buffer();
		void _push_delta_buffer(char ch);
		void _find_delta_hash(unsigned hsh);

		void process(const char* buf, int bufsz);

		bool need_restart()
		{
			return !raw_file && (files_delta_size >= filesize || files_delta_size + files_source_size >= 3 * filesize);
		}

		void restart();

		long long commit(const FILETIME* moddate);
	};

	// file loading streaming algorithm
	struct fileload
	{
		HANDLE hfile;
		db_handle dbh;
		std::shared_lock<std::shared_mutex> lck;
	};

	// file difference generator algorithm
	struct diffview
	{

	};

	// MAIN.DAT checking algorithm
	struct datacheck
	{
		HANDLE hfile;
		db_query objsel;

		datacheck(HANDLE hdata, db_handle dbh) : hfile(hdata), objsel(dbh, "SELECT * FROM objects;")
		{
			objsel.reset();
		}

		bool run()
		{
			if (!objsel.step())
				return false;

			return true;
		}
	};
}