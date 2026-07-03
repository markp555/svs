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
#include <string>
#include <stdexcept>

namespace svs
{
	struct out_of_data : std::runtime_error {
		out_of_data() : std::runtime_error("Readed more data than expected") {}
	};

	void expect_data(bool expr)
	{
		if (!expr)
		{
			throw out_of_data();
		}
	}

	using filelength_t = std::uint_fast64_t;
	using offset_t = std::int_fast64_t;

	struct stream_reader
	{
		virtual bool on_data(char* buf, size_t buflen) = 0;
		virtual void on_close() {}
		virtual ~stream_reader() = default;
	};

	struct stream_async_reader
	{
		virtual char read_byte() = 0;
		virtual char read_byte_s() = 0;
		virtual void tell_bytes(char* s, int sc) = 0;
		virtual bool eof() = 0;
		virtual ~stream_async_reader() = default;

		void skip_bytes(filelength_t len)
		{
			while (len--)
			{
				read_byte_s();
			}
		}

		inline unsigned long long read_integer(int x)
		{
			unsigned long long y = 0, s = 0;
			while (x--)
			{
				unsigned long long z = read_byte_s();
				y += (z << s);
				s += 8;
			}
			return y;
		}

		int read_int32() { return (int)read_integer(4); }
		offset_t read_offset() { return (offset_t)read_integer(8); }
		filelength_t read_length() { return (filelength_t)read_integer(8); }
	};

	stream_async_reader* create_async_reader(stream_reader** sr);

	struct sfile
	{
		virtual filelength_t read_raw(offset_t from, filelength_t len, char* out) = 0;
		virtual filelength_t append_raw(filelength_t len, const char* dat) = 0;
		virtual filelength_t stream_raw(offset_t from, stream_reader* reader) = 0;
		virtual filelength_t size() = 0;
		virtual ~sfile() = default;

		void append(filelength_t len, const char* dat) {
			filelength_t written = append_raw(len, dat);
			expect_data(written == len);
		}
		void read(offset_t from, filelength_t len, char* out) {
			filelength_t readen = read_raw(from, len, out);
			expect_data(readen == len);
		}
		// opens file for stream reading
		stream_async_reader* stream(offset_t from);
		// opens object for stream reading. ensures no more than object size has been readen
		stream_async_reader* stream_object(offset_t position);
	};
	
	struct webfile : sfile
	{

	};

	struct localfile : sfile
	{

	};
}