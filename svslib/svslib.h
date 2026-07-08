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
	class VFS
	{

	};

	class SVS
	{
	private:
		db_handle dbh;
		HANDLE hdata;
		friend class VFS;
	public:
		std::shared_mutex mtx;

		/// <summary>
		/// Main class for manipulations with SVS repositories
		/// Used to create / view / write to SVS repositories
		/// If files on given paths not exist, creates this files
		/// </summary>
		/// <param name="dbfile"></param>
		/// <param name="datafile"></param>
		/// <param name="localfile"></param>
		/// <param name="diskchck"></param>
		SVS(LPCWSTR dbfile, LPCWSTR datafile, LPCWSTR localfile, BOOL diskchck);

		// RAW API
		db_handle acquire_db();
		HANDLE acquire_data();
	};
}
