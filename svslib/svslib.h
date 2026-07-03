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
#include <curl/curl.h>
#include <string>
#include <filesystem>
#include "filesubsystem.h"
#include "maindb.h"

namespace svs
{
	class SVS
	{
	public:
		std::shared_mutex mtx;


	};

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

	using OFFSET = long long;
	typedef size_t (*STREAM_CALLBACK)(char* ptr, size_t size, size_t nmemb, void* userdata);

	struct file_stream
	{
		virtual OFFSET read_raw(OFFSET from, OFFSET len, char* out) = 0;
		virtual void read_stream(OFFSET from, OFFSET len, STREAM_CALLBACK sc, void* user) = 0;
		virtual void append_raw(OFFSET len, const char* dat) = 0;
		virtual void write_raw(OFFSET from, OFFSET len, const char* dat) = 0;
		virtual bool readable() = 0;
		virtual bool writeable() = 0;
		virtual long long filesize() = 0;
		virtual ~file_stream() = default;
	};

	// file system block size
	const int K = 1024 * 4;

	struct stream_reader
	{
		file_stream* fs;
		char buff[K];
		int buff_sz = 0, buff_lst = 0;
		OFFSET cur, lim = INT64_MAX;

		stream_reader(OFFSET pos, file_stream* fs) : fs(fs), cur(pos) {}

		char getchar()
		{
			expect_data(lim-- > 0);
			if (buff_lst == buff_sz)
			{
				expect_data(buff_sz == K);
				buff_lst = 0;
				buff_sz = (int)fs->read_raw(cur, K, buff);
				cur += buff_sz;
			}
			expect_data(buff_lst < buff_sz);
			return buff[buff_lst++];
		}

		void read(int len, char* opt)
		{
			for (size_t i = 0; i < len; i++)
			{
				opt[i] = getchar();
			}
		}
	};

	struct input_object_stream
	{
		stream_reader sr;
		OFFSET limit;
		int flags;
		OFFSET objlen;

		char read_byte()
		{
			return sr.getchar();
		}

		inline unsigned long long read_integer(int x)
		{
			unsigned long long y = 0, s = 0;
			while (x--)
			{
				unsigned long long z = read_byte();
				y += (z << s);
				s += 8;
			}
			return y;
		}

		int read_int32() { return (int)read_integer(4); }
		OFFSET read_position() { return (OFFSET)read_integer(8); }

		input_object_stream(OFFSET pos, file_stream* fs) : sr(pos, fs)
		{
			expect_data(fs->readable());
			expect_data(pos < fs->filesize());
			limit = fs->filesize() - pos;
			flags = read_byte();
			objlen = read_integer(7);
			expect_data(objlen <= limit);
			limit = objlen;
			sr.lim = limit;
		}

		int get_flags()
		{
			return flags;
		}

		OFFSET get_size()
		{
			return objlen;
		}

		OFFSET get_size_left()
		{
			return sr.lim;
		}

		std::string read_string()
		{
			std::string res;
			while (char x = read_byte())
				res.push_back(x);
			return res;
		}

		unsigned long long read_vln()
		{
			unsigned s = 0;
			unsigned long long x = 0;
			while (char y = read_byte())
			{
				x += ((static_cast<unsigned long long>(y & 127)) << s);
				s += 7;
				if (y < 128)
					break;
			}
			return x;
		}
	};

	struct output_object_stream
	{
		file_stream* fs;
		OFFSET len = 0, begin = 0;
		output_object_stream(int flags, file_stream* str) : fs(str)
		{
			expect_data(fs->writeable());
			begin = fs->filesize();
			fs->append_raw(1, reinterpret_cast<const char*>(&flags));
		}

		void append(OFFSET sz, const char* dat)
		{
			fs->append_raw(sz, dat);
			len += sz;
		}

		void append(OFFSET sz, const void* dat)
		{
			append(sz, reinterpret_cast<const char*>(dat));
		}

		void write_byte(char x)
		{
			append(sizeof(x), &x);
		}

#ifdef _MSC_VER // Little endian windows
		void write_int32(int x)
		{
			append(sizeof(x), &x);
		}

		void write_position(OFFSET x)
		{
			append(sizeof(x), &x);
		}
#else
		void write_int32(int x)
		{
			char y[4];
			for (int i = 0; i < 4; i++)
				y[i] = (x >> (8 * i)) & 255;
			append(sizeof(y), y);
		}

		void write_position(OFFSET x)
		{
			char y[8];
			for (int i = 0; i < 8; i++)
				y[i] = (x >> (8 * i)) & 255;
			append(sizeof(y), &y);
		}
#endif

		void write_string(const char* s)
		{
			append(strlen(s) + 1, s);
		}

		void write_string(const std::string& s)
		{
			expect_data(std::count(s.begin(), s.end(), '\0') == 0);
			append(s.size() + 1, s.c_str());
		}

		void write_vln(unsigned long long raw)
		{
			while (raw)
			{
				unsigned char ch = (raw & 127);
				raw >>= 7;
				if (raw > 0)
					ch += 128;
				write_byte(ch);
			}
		}

		~output_object_stream()
		{
			// writing object length
			fs->write_raw(begin + 1, 7, reinterpret_cast<const char*>(&len));
		}
	};

	struct buffered_file_stream : file_stream
	{
		file_stream* fs;
		buffered_file_stream(file_stream* fs) : fs(fs) {}
	};
}
