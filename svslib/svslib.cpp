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

	SVS::SVS(LPCWSTR dbfile, LPCWSTR datafile, LPCWSTR localfile, BOOL diskchck)
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
			dbh->exec("CREATE TABLE files(id INTEGER PRIMARY KEY, object INT64, file_size INT64, file_hash BLOB, modified_date DATETIME, updated_at DATETIME, deltas_size INT32, deltas_count INT32, source_size INT32, parent INTEGER);");
			dbh->exec("CREATE TABLE views(id INTEGER PRIMARY KEY, object INT64, created_date DATETIME, data_hash BLOB, depth INT32, up BLOB, parent INTEGER);");
			dbh->exec("CREATE TABLE branches(id INTEGER PRIMARY KEY, name TEXT, view INTEGER, last_commit INTEGER);");
			dbh->exec("CREATE TABLE commits(id INTEGER PRIMARY KEY, branch INTEGER, new_view INTEGER, old_view INTEGER, hash BLOB, timestamp DATETIME, author TEXT, message TEXT, parent INTEGER, signature BLOB, depth INT32, up BLOB, xview INTEGER);");
			dbh->exec("CREATE TABLE comments(id INTEGER PRIMARY KEY, topic INTEGER, author TEXT, message TEXT, timestamp INTEGER);");
			dbh->exec("CREATE TABLE ignores(id INTEGER PRIMARY KEY, path TEXT, prefix TINYINT);");
			dbh->exec("CREATE TABLE revisions(id INTEGER PRIMARY KEY, commit INTEGER, author TEXT, message TEXT, result INTEGER, signature BLOB);");
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
			dbh->exec("CREATE INDEX idx_revisions_by_commit ON revisions(commit, result);");
			dbt.commit();
		}
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
