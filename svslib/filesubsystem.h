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
#include "framework.h"
// File subsystem
// MAIN.DAT file API
namespace svs
{
	union filehash
	{
		char raw[32];
		unsigned int val[8];
	};

	

	filehash calc_hash_blake3(LPCWSTR filepath);
	LONGLONG update_file(LPCWSTR filepath, HANDLE data, LONGLONG old, int flags);
	void extract_file(HANDLE outfile, HANDLE data, LONGLONG position, int flags);
	LPWSTR make_human_diff(HANDLE data, LONGLONG file1, LONGLONG file2, int flags);
}