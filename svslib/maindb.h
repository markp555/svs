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
// maindb - ORM for MAIN.DB and LOCAL.DB
#pragma once
#include <sqlite3.h>
#include <stdexcept>
#include <concepts>
#include <map>
#include <memory>
#include <shared_mutex>
#include <bit>
#include <vector>

namespace svs
{
	struct sql_error : std::runtime_error
	{
		sql_error(sqlite3* x) : std::runtime_error(sqlite3_errmsg(x)) {}
		sql_error(char* query_err) : std::runtime_error(query_err) { sqlite3_free(query_err); }
		sql_error(sqlite3*, int x) : std::runtime_error(sqlite3_errstr(x)) {}
	};

	struct database
	{
		sqlite3* db = nullptr;
		std::shared_mutex lck;
		database(const wchar_t* path)
		{
			if (sqlite3_open16(path, &db) != SQLITE_OK)
				throw sql_error(db);
		}
		database(const database&) = delete;
		database& operator=(database&&) = delete;
		~database() noexcept { sqlite3_close(db); }

		void exec(const char* s)
		{
			char* err = nullptr;
			int ok = sqlite3_exec(db, s, nullptr, nullptr, &err);
			if (err != nullptr)
				throw sql_error(err);
			if (ok != SQLITE_OK)
				throw sql_error(db);
		}
	};

	using db_handle = std::shared_ptr<database>;

	template <typename T>
	concept sql_type = std::integral<T> || std::floating_point<T> || std::is_same_v<T, std::string> || std::is_same_v<T, std::wstring>;
	template <typename T>
	concept trivial_type = std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T> && std::is_trivially_copy_assignable_v<T> && std::is_trivially_default_constructible_v<T>;
	template <typename T>
	concept string_pointer = std::is_same_v<std::remove_const_t<T>, char*> || std::is_same_v<std::remove_const_t<T>, wchar_t*>;
	template <typename T>
	concept sql_blob_type = (trivial_type<T> && !string_pointer<T>);

	struct db_query
	{
		// [unused] just reference so db dont get destroyed
		db_handle dbh;
		sqlite3* db;
		sqlite3_stmt* stmt;
		std::map <std::string, int> colnames;

		db_query(const db_query&) = delete;
		db_query& operator=(const db_query&) = delete;
		db_query(db_handle dbh) noexcept :dbh(dbh), db(dbh->db), stmt(nullptr) {}
		~db_query() noexcept { if (stmt) sqlite3_finalize(stmt); }
		db_query(db_handle dbh, const char* sql) : db_query(dbh)
		{
			setup(sql);
		}

		void setup(const char* sql)
		{
			if (stmt != nullptr)
				sqlite3_finalize(stmt);
			if (int x = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr))
				throw sql_error(db, x);
			colnames.clear();
		}

		void reset()
		{
			if (int x = sqlite3_reset(stmt))
				throw sql_error(db, x);
			colnames.clear();
		}

		bool step()
		{
			int x = sqlite3_step(stmt);
			if (x == SQLITE_DONE)
				return false;
			if (x != SQLITE_ROW)
				throw sql_error(db, x);
			return true;
		}

		void run()
		{
			if (step())
				throw std::runtime_error("Commit should be used only in insert / update queries");
			reset();
		}

		template <std::integral T>
		void bind(const char* param, T x)
		{
			int id = sqlite3_bind_parameter_index(stmt, param);
			if constexpr (sizeof(x) <= sizeof(int))
			{
				if (int ok = sqlite3_bind_int(stmt, id, static_cast<int>(x)))
					throw sql_error(db, ok);
			}
			else
			{
				if (int ok = sqlite3_bind_int64(stmt, id, x))
					throw sql_error(db, ok);
			}
		}

		void bind(const char* param, const void* dat, int n)
		{
			int id = sqlite3_bind_parameter_index(stmt, param);
			if (int ok = sqlite3_bind_blob(stmt, id, dat, n, SQLITE_TRANSIENT))
				throw sql_error(db, ok);
		}

		template <sql_blob_type T>
		void bind(const char* param, T* val)
		{
			bind(param, (const void*)val, (int)sizeof(T));
		}

		template <std::floating_point T>
		void bind(const char* param, T val)
		{
			int id = sqlite3_bind_parameter_index(stmt, param);
			if (int ok = sqlite3_bind_double(stmt, id, (double)val))
				throw sql_error(db, ok);
		}

		void bind(const char* param, std::string_view sw)
		{
			int id = sqlite3_bind_parameter_index(stmt, param);
			if (int ok = sqlite3_bind_text(stmt, id, sw.data(), static_cast<int>(sw.size()), SQLITE_TRANSIENT))
				throw sql_error(db, ok);
		}

		void bind(const char* param, std::wstring_view sw)
		{
			int id = sqlite3_bind_parameter_index(stmt, param);
			if (int ok = sqlite3_bind_text16(stmt, id, sw.data(), static_cast<int>(sw.size()), SQLITE_TRANSIENT))
				throw sql_error(db, ok);
		}

		void bind(const char* param, nullptr_t)
		{
			int id = sqlite3_bind_parameter_index(stmt, param);
			if (int ok = sqlite3_bind_null(stmt, id))
				throw sql_error(db, ok);
		}

		void prepare()
		{
			if (!colnames.empty())
				return;
			int N = sqlite3_column_count(stmt);
			for (int i = 0; i < N; i++)
			{
				colnames.insert_or_assign(sqlite3_column_name(stmt, i), i);
			}
		}

		int column_id(const char* name)
		{
			auto it = colnames.find(name);
			if (it == colnames.end())
				throw std::runtime_error("[SQL] Invalid column name!");
			return it->second;
		}

		template <sql_type T>
		T get(const char* name);

		template <std::integral T>
		T get(const char* name)
		{
			prepare();
			return static_cast<T>(sqlite3_column_int64(stmt, column_id(name)));
		}

		template <>
		std::string get<std::string>(const char* name)
		{
			prepare();
			return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, column_id(name))));
		}

		template <>
		std::wstring get<std::wstring>(const char* name)
		{
			prepare();
			return std::wstring(reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, column_id(name))));
		}

		template <std::floating_point T>
		T get(const char* name)
		{
			prepare();
			return static_cast<T>(sqlite3_column_double(stmt, column_id(name)));
		}

		std::unique_ptr<char[]> get_blob(const char* name, int* blob_size = NULL)
		{
			prepare();
			int col = column_id(name);
			int sz = sqlite3_column_bytes(stmt, col);
			if (blob_size != NULL)
				*blob_size = sz;
			std::unique_ptr<char[]> ret = std::make_unique<char[]>(sz);
			const void* dat = sqlite3_column_blob(stmt, col);
			if (dat == nullptr)
				return std::unique_ptr<char[]>{};
			memcpy(ret.get(), dat, sz);
			return ret;
		}

		template <sql_blob_type T>
		void get(const char* name, T* out)
		{
			prepare();
			int col = column_id(name);
			if (sqlite3_column_bytes(stmt, col) != sizeof(T))
				throw std::runtime_error("[SQL] blob size mismatch");
			memcpy(static_cast<void*>(out), sqlite3_column_blob(stmt, col), sizeof(T));
		}

		template <sql_blob_type T>
		std::vector<T> get_array(const char* name)
		{
			prepare();
			int col = column_id(name);
			int sz = sqlite3_column_bytes(stmt, col);
			if (sz % sizeof(T) != 0)
				throw std::runtime_error("[SQL] blob size mismatch");
			std::vector<T> ret(sz / sizeof(T));
			memcpy(static_cast<void*>(ret.data()), sqlite3_column_blob(stmt, col), ret.size() * sizeof(T));
			return ret;
		}

		template <sql_type T>
		T run_one()
		{
			if (!step())
				throw std::runtime_error("[SQL] no data returned");
			colnames.insert_or_assign("#", 0);
			T retval = get<T>("#");
			if (step())
				throw std::runtime_error("[SQL] too many data returned");
			reset();
			return retval;
		}

		template <sql_type T>
		T run_one_inline(const char* name)
		{
			sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
			return run_one<T>();
		}
	};

	struct db_transaction
	{
		db_handle dbh;
		bool ok;
		db_transaction(const db_transaction&) = delete;
		db_transaction& operator=(const db_transaction&) = delete;
		db_transaction(db_handle dbh) :dbh(dbh), ok(false)
		{
			dbh->exec("BEGIN TRANSACTION;");
		}
		~db_transaction() noexcept
		{
			if (!ok)
			{
				dbh->exec("ROLLBACK;");
			}
		}
		void commit()
		{
			if (ok)
				throw std::runtime_error("[SQL] commiting transaction more than once!");
			dbh->exec("COMMIT;");
			ok = true;
		}
	};
}
