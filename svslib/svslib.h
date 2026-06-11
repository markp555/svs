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
#include "SqliteDB.h"
#define CPPHTTPLIB_ZLIB_SUPPORT
#define CPPHTTPLIB_MBEDTLS_SUPPORT
#include "httplib.h"
#include "mbedtls/pk.h"
#include <string>
#include <filesystem>

struct InteractiveCredential
{
	virtual std::string get_login() = 0;
	virtual std::string get_password() = 0;
	virtual std::string OTP(std::string server_message) = 0;
};

struct RemoteRepository : public InteractiveCredential
{
	SqliteDatabase origin;
	InteractiveCredential* con;
	virtual std::string get_login() override;
	virtual std::string get_password() override;
	virtual std::string OTP(std::string server_message) override;
	void open_certificate(mbedtls_pk_context* tar);
};

using XID = unsigned long long;
using XPATHS = std::filesystem::path::string_type;
using XPATH = std::filesystem::path;

struct VFS
{
	virtual ~VFS() {}
	virtual void open_file(XPATH path, XPATH local);
	virtual void open_directory(XPATH path, XPATH local);
	virtual std::vector<XPATHS> list_directory(XPATH path);
	virtual XID get_current_id();
	virtual XID get_remote_id(RemoteRepository* repo);
};

struct SVSRepository
{
	SqliteDatabase rootdb;
	void open(XPATH dir, std::string repository);
	std::string create(XPATH dir);
	void close();
	void sync_remote(RemoteRepository* repo);
	XID find_view(std::string name);
	std::string get_view_name(XID id);
	VFS* open_view(XID id);
	void change_current_view(XID nid, XPATH local);
	void update_current_view(XPATH local);
	XID get_current_view();
	void commit_current_view(std::string name, std::string message, mbedtls_pk_context* cert);
	void file_update(XPATH local, XPATH target, XID viewid);
};