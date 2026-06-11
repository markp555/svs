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

fs::path get_home_directory() {
    // 1. Try Windows primary environment variable
#if defined(_WIN32)
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        return fs::path(userProfile);
    }
    // Windows fallback configuration
    if (const char* homeDrive = std::getenv("HOMEDRIVE")) {
        if (const char* homePath = std::getenv("HOMEPATH")) {
            return fs::path(homeDrive) / homePath;
        }
    }
#endif

    // 2. Try macOS / Linux environment variable
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home);
    }

    // Return empty path if lookups fail
    return fs::path();
}

std::string SVSRepository::create(XPATH dir)
{
    std::random_device rd{};
    fs::create_directories(dir);
    std::string name = "MAIN.DB";
    while (!fs::exists(name))
    {
        name.clear();
        name = "REPO-";
        name += std::to_string(rd());
    }
    
    return name;
}

void SVSRepository::apply_migrations()
{
    int cur = rootdb.GetUserVersion();
    int lst = sizeof(migrations) / sizeof(DB_MIGRATION);
    while (cur < lst)
    {
        rootdb.BeginTransaction();
        // C++ is for genius
        // really
        (this->*migrations[cur])();
        rootdb.SetUserVersion(++cur);
        rootdb.CommitTransaction();
    }
}

void SVSRepository::db_migrate_1()
{
    rootdb.Execute("CREATE TABLE ");
}
