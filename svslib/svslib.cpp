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
// svslib.cpp : Defines the functions for the static library.
//

#include "framework.h"
#include "svslib.h"

namespace fs = std::filesystem;

namespace svs
{
	bool CheckFileExistence(LPCWSTR file)
	{
		if (file == NULL)
			return false;
		DWORD dwAttrib = GetFileAttributesW(file);
		return dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	SVS::SVS(LPCWSTR dbfile, LPCWSTR datafile, LPCWSTR localfile, bool diskchck, bool readonly) : readonly(readonly), hdata(INVALID_HANDLE_VALUE)
	{
		if (dbfile == nullptr)
			throw std::runtime_error("Database is required for SVS");
		bool dbexist = CheckFileExistence(dbfile);
		dbh = std::make_shared<database>(dbfile);
		bool datexist = CheckFileExistence(datafile);
		if (dbexist != datexist && datafile != NULL)
		{
			throw std::runtime_error("Error creating / opening repository. Both database and data file must exist or be created");
		}
		if (!dbexist)
		{
			db_transaction dbt(dbh);
			dbh->exec("CREATE TABLE config(name TEXT PRIMARY KEY, value ANY);");
			dbh->exec("CREATE TABLE objects(id INTEGER PRIMARY KEY, offset INT64, size INT64, hash BLOB, type INT8);");
			dbh->exec("CREATE TABLE files(id INTEGER PRIMARY KEY, object INT64, file_size INT64, file_hash BLOB, modified_date WINDATETIME, updated_at DATETIME, deltas_size INT32, deltas_count INT32, source_size INT32, parent INTEGER);");
			dbh->exec("CREATE TABLE views(id INTEGER PRIMARY KEY, object INT64, created_date DATETIME, data_hash BLOB, depth INT32, up BLOB, parent INTEGER);");
			dbh->exec("CREATE TABLE branches(id INTEGER PRIMARY KEY, name TEXT, view INTEGER, last_commit INTEGER);");
			dbh->exec("CREATE TABLE commits(id INTEGER PRIMARY KEY, branch INTEGER, new_view INTEGER, old_view INTEGER, hash BLOB, timestamp DATETIME, author TEXT, message TEXT, parent INTEGER, signature BLOB, depth INT32, up BLOB, xview INTEGER);");
			dbh->exec("CREATE TABLE comments(id INTEGER PRIMARY KEY, topic INTEGER, author TEXT, message TEXT, timestamp INTEGER);");
			dbh->exec("CREATE TABLE ignores(id INTEGER PRIMARY KEY, path TEXT, prefix TINYINT);");
			dbh->exec("CREATE TABLE revisions(id INTEGER PRIMARY KEY, topic INTEGER, author TEXT, message TEXT, result INTEGER, signature BLOB);");
			dbh->exec("CREATE UNIQUE INDEX idx_object_offset ON objects(offset);");
			dbh->exec("CREATE UNIQUE INDEX idx_file_offset ON files(object);");
			dbh->exec("CREATE UNIQUE INDEX idx_view_offset ON views(object);");
			// dbh->exec("CREATE INDEX idx_file_update_history ON files(updated_at DESC);");
			// dbh->exec("CREATE INDEX idx_file_modify_history ON files(modified_date DESC);");
			// dbh->exec("CREATE INDEX idx_file_tree ON files(parent);");
			dbh->exec("CREATE INDEX idx_view_tree ON views(parent, created_date ASC);");
			dbh->exec("CREATE INDEX idx_view_history ON views(created_date);");
			dbh->exec("CREATE UNIQUE INDEX idx_branch_name ON branches(name);");
			dbh->exec("CREATE INDEX idx_commits_branch ON commits(branch, timestamp);");
			dbh->exec("CREATE INDEX idx_commits_author ON commits(author, timestamp);");
			dbh->exec("CREATE INDEX idx_commits_view ON commits(new_view);");
			dbh->exec("CREATE INDEX idx_comments_topic ON comments(topic, timestamp);");
			dbh->exec("CREATE INDEX idx_comments_author ON comments(author, timestamp);");
			dbh->exec("CREATE INDEX idx_revisions_by_commit ON revisions(topic, result);");
			dbt.commit();
			db_query dbq(dbh, "INSERT INTO config(name, value) VALUES (@name, @value);");
			if (datafile != nullptr)
			{
				hdata = CreateFileW(datafile, readonly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
				char header[4] = "SVS";
				DWORD writen;
				if (!WriteFile(hdata, header, 4, &writen, NULL))
					throw winerror();
				if (writen != 4)
					throw ioerror(hdata, "Error creating MAIN.DAT; check free space on disk");
				// dbh->exec("INSERT INTO config(name, value) VALUES ('filesize', 4);");
				dbq.bind("@name", "filesize");
				dbq.bind("@value", 4);
				dbq.run();
			}
			else
			{
				dbh->exec("INSERT INTO config(name, value) VALUES ('filesize', 0);");
			}
			// !!! file names codepage
			dbq.bind("@name", "codepage");
			dbq.bind("@value", GetACP());
			dbq.run();
			dbq.bind("@name", "author");
			dbq.bind("@value", "svs");
			dbq.run();
			dbq.bind("@name", "time");
			dbq.bind("@value", time(NULL));
			dbq.run();
			dbq.bind("@name", "zlib");
			dbq.bind("@value", true);
			dbq.run();
			// enables emedding signature in files
			dbq.bind("@name", "embed_s");
			dbq.bind("@value", true);
			dbq.run();
			// coefficient to random algorithm
			dbq.bind("@name", "delta_prob");
			dbq.bind("@value", 1.0);
			dbq.run();
			dbq.bind("@name", "delta_coef");
			dbq.bind("@value", 1.0);
			dbq.run();
			// maximum compression mode with archive&local&local_inline
			// makes always use delta when signature avaiable
			// makes always use treep instead of B+ tree
			dbq.bind("@name", "archive");
			dbq.bind("@value", false);
			dbq.run();
			// disables signature saving if previous presented by outer world
			// not efficient with uploading to remote server unless local_inline is set
			dbq.bind("@name", "local");
			dbq.bind("@value", true);
			dbq.run();
			// if no signature provided, calculates it on the fly
			dbq.bind("@name", "local_inline");
			dbq.bind("@value", false);
			dbq.run();
			// disables delta & signature algorithm
			// stores raw files or zlib-ed
			dbq.bind("@name", "raw_content");
			dbq.bind("@value", false);
			dbq.run();
			// disclaimer
			dbq.bind("@name", "warning");
			dbq.bind("@value", "EXPERIEMENTAL FEATURES AHEAD!");
			dbq.run();
		}
		else if (datafile != NULL)
		{
			hdata = CreateFileW(datafile, readonly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, NULL);
			if (diskchck)
			{
				db_query dq(dbh, "SELECT COUNT(*) AS c FROM objects;");
				long long odc = dq.run_one<long long>();
				datacheck dch(hdata, dbh);
				long long oc = 0;
				clock_t clck = 0;
				while (dch.run())
				{
					if (clck != clock())
					{
						printf("\rChecking repository integrity... (%lld/%lld)", oc, odc);
						clck = clock();
					}
					oc++;
				}
				printf("\r\nChecking completed\n");
				dq.setup("SELECT value FROM config WHERE name = @name;");
				dq.bind("@name", "filesize");
				long long oldsize = dq.run_one<long long>();
				LARGE_INTEGER i1, i2;
				i1.QuadPart = 0;
				SetFilePointerEx(hdata, i1, &i2, FILE_END);
				long long cursize = i2.QuadPart;
				printf("MAIN.DAT size in database: %lld\n", oldsize);
				printf("MAIN.DAT real size: %lld\n", cursize);
				if (cursize != oldsize)
				{
					printf("[ERROR] Size mismatch\n");
					if (!readonly && oldsize < cursize)
					{
						i1.QuadPart = oldsize;
						SetFilePointerEx(hdata, i1, &i2, FILE_BEGIN);
						SetEndOfFile(hdata);
						printf("[INFO] Truncated file\n");
					}
				}
			}
			else
			{
				db_query dq(dbh, "SELECT value FROM config WHERE name = @name;");
				dq.bind("@name", "filesize");
				long long oldsize = dq.run_one<long long>();
				LARGE_INTEGER i1, i2;
				i1.QuadPart = 0;
				SetFilePointerEx(hdata, i1, &i2, FILE_END);
				long long cursize = i2.QuadPart;
				if (cursize != oldsize)
				{
					if (readonly)
					{
						printf("[ERROR] MAIN.DAT file size mismatch (was %lld, found %lld)\n", oldsize, cursize);
					}
					else
					{
						printf("[FATAL] MAIN.DAT file size mismatch (was %lld, found %lld)\n", oldsize, cursize);
						throw ioerror(hdata, "file size mismatch");
					}
				}
				SetFilePointer2(hdata, 0);
			}
		}
		if (localfile != NULL)
		{
			bool ok = CheckFileExistence(localfile);
			db_query dbq(dbh, "ATTACH DATABASE @db AS local;");
			dbq.bind("@db", localfile);
			dbq.run();
			if (!ok)
			{
				throw std::runtime_error("NOT IMPLEMENTED");
			}
		}
	}
	SVS::~SVS()
	{
		CloseHandle(hdata);
	}
	db_handle SVS::acquire_db()
	{
		std::unique_lock lck(mtx);
		return dbh;
	}
	HANDLE SVS::acquire_data()
	{
		std::unique_lock lck(mtx);
		return hdata;
	}
}
