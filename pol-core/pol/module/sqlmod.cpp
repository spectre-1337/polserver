/** @file
 *
 * @par History
 */

#include "pol_global_config.h"

#include "sqlmod.h"
#include <stddef.h>

#include "../../bscript/berror.h"
#include "../../bscript/impstr.h"
#include "../../clib/logfacility.h"
#include "../../clib/refptr.h"
#include "../../clib/weakptr.h"
#include "../globals/network.h"
#include "../polsem.h"
#include "../sqlscrobj.h"
#include "../uoexec.h"

#include <module_defs/sql.h>

namespace Pol
{
namespace Module
{
using namespace Bscript;

SQLExecutorModule::SQLExecutorModule( Bscript::Executor& exec )
    : Bscript::TmplExecutorModule<SQLExecutorModule, Core::PolModule>( exec )
{
}

size_t SQLExecutorModule::sizeEstimate() const
{
  return sizeof( *this );
}

#ifdef HAVE_MYSQL

BObjectImp* SQLExecutorModule::background_connect( weak_ptr<Core::UOExecutor> uoexec,
                                                   const std::string host,
                                                   const std::string username,
                                                   const std::string password )
{
  auto msg = [uoexec, host, username, password]() {
    std::unique_ptr<Core::BSQLConnection> sql;
    {
      Core::PolLock lck;
      sql = std::unique_ptr<Core::BSQLConnection>( new Core::BSQLConnection() );
    }
    if ( sql->getLastErrNo() )
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set(
            new BObject( new BError( "Insufficient memory" ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
    else if ( !sql->connect( host.data(), username.data(), password.data() ) )
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set(
            new BObject( new BError( sql->getLastError() ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
    else
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set( new BObject( sql.release() ) );
        uoexec.get_weakptr()->revive();
      }
    }
  };

  if ( !uoexec->suspend() )
  {
    DEBUGLOG << "Script Error in '" << uoexec->scriptname() << "' PC=" << uoexec->PC << ": \n"
             << "\tThe execution of this script can't be blocked!\n";
    return new Bscript::BError( "Script can't be blocked" );
  }

  Core::networkManager.sql_service->push( std::move( msg ) );
  return new BLong( 0 );
}

Bscript::BObjectImp* SQLExecutorModule::background_select( weak_ptr<Core::UOExecutor> uoexec,
                                                           Core::BSQLConnection* sql,
                                                           const std::string db )
{
  // The BSQLConnection shouldn't be destroyed before the lambda runs
  ref_ptr<Core::BSQLConnection> sqlRef( sql );
  auto msg = [uoexec, sqlRef, db]() {
    if ( sqlRef == nullptr )
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set(
            new BObject( new BError( "Invalid parameters" ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
    else if ( !sqlRef->select_db( db.c_str() ) )
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set(
            new BObject( new BError( sqlRef->getLastError() ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
    else
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set( new BObject( new BLong( 1 ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
  };

  if ( !uoexec->suspend() )
  {
    DEBUGLOG << "Script Error in '" << uoexec->scriptname() << "' PC=" << uoexec->PC << ": \n"
             << "\tThe execution of this script can't be blocked!\n";
    return new Bscript::BError( "Script can't be blocked" );
  }
  Core::networkManager.sql_service->push( std::move( msg ) );
  return new BLong( 0 );
}

Bscript::BObjectImp* SQLExecutorModule::background_query( weak_ptr<Core::UOExecutor> uoexec,
                                                          Core::BSQLConnection* sql,
                                                          const std::string query,
                                                          const Bscript::ObjArray* params )
{
  // Copy and parse params before they will be deleted by this thread (go out of scope)
  Core::QueryParams sharedParams( nullptr );
  if ( params != nullptr )
  {
    sharedParams = std::make_shared<Core::QueryParam>();

    for ( unsigned i = 0; i < params->ref_arr.size(); ++i )
    {
      const BObjectRef& ref = params->ref_arr[i];
      const BObject* obj = ref.get();
      if ( obj != nullptr )
        sharedParams->insert( sharedParams->end(), obj->impptr()->getStringRep() );
    }
  }

  // The BSQLConnection shouldn't be destroyed before the lambda runs
  ref_ptr<Core::BSQLConnection> sqlRef( sql );
  auto msg = [uoexec, sqlRef, query, sharedParams]() {
    if ( sqlRef == nullptr )  // TODO: this doesn't make any sense and should be checked before the
                              // lambda. Same happens in background_select().
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set(
            new BObject( new BError( "Invalid parameters" ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
    else if ( !sqlRef->query( query, sharedParams ) )
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set(
            new BObject( new BError( sqlRef->getLastError() ) ) );
        uoexec.get_weakptr()->revive();
      }
    }
    else
    {
      Core::PolLock lck;
      if ( !uoexec.exists() )
        INFO_PRINT << "Script has been destroyed\n";
      else
      {
        uoexec.get_weakptr()->ValueStack.back().set( new BObject( sqlRef->getResultSet() ) );
        uoexec.get_weakptr()->revive();
      }
    }
  };

  if ( !uoexec->suspend() )
  {
    DEBUGLOG << "Script Error in '" << uoexec->scriptname() << "' PC=" << uoexec->PC << ": \n"
             << "\tThe execution of this script can't be blocked!\n";
    return new Bscript::BError( "Script can't be blocked" );
  }

  Core::networkManager.sql_service->push( std::move( msg ) );
  return new BLong( 0 );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_connect()
{
  const String* host = getStringParam( 0 );
  const String* username = getStringParam( 1 );
  const String* password = getStringParam( 2 );
  if ( !host || !username || !password )
  {
    return new BError( "Invalid parameters" );
  }
  return background_connect( uoexec().weakptr, host->getStringRep(), username->getStringRep(),
                             password->getStringRep() );
}
Bscript::BObjectImp* SQLExecutorModule::mf_mysql_select_db()
{
  Core::BSQLConnection* sql =
      static_cast<Core::BSQLConnection*>( getParamImp( 0, Bscript::BObjectImp::OTSQLConnection ) );
  const String* db = getStringParam( 1 );
  if ( !sql || !db )
  {
    return new BError( "Invalid parameters" );
  }
  return background_select( uoexec().weakptr, sql, db->getStringRep() );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_query()
{
  Core::BSQLConnection* sql =
      static_cast<Core::BSQLConnection*>( getParamImp( 0, Bscript::BObjectImp::OTSQLConnection ) );
  const String* query = getStringParam( 1 );
  ObjArray* params;
  bool use_parameters = getObjArrayParam( 2, params );
  if ( !sql || !query )
  {
    return new BError( "Invalid parameters" );
  }

  return background_query( uoexec().weakptr, sql, query->getStringRep(),
                           use_parameters ? params : nullptr );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_num_fields()
{
  Core::BSQLResultSet* result =
      static_cast<Core::BSQLResultSet*>( getParamImp( 0, Bscript::BObjectImp::OTSQLResultSet ) );
  if ( !result )
  {
    return new BError( "Invalid parameters" );
  }
  return new BLong( result->num_fields() );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_field_name()
{
  Core::BSQLResultSet* result =
      static_cast<Core::BSQLResultSet*>( getParamImp( 0, Bscript::BObjectImp::OTSQLResultSet ) );
  int index;
  if ( !getParam( 1, index ) )
    return new BError( "Invalid parameters" );

  if ( !result || !index )
  {
    return new BError( "Invalid parameters" );
  }
  const char* name = result->field_name( index );
  if ( name == nullptr )
    return new BError( "Column does not exist" );
  return new String( name, String::Tainted::YES );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_affected_rows()
{
  Core::BSQLResultSet* result =
      static_cast<Core::BSQLResultSet*>( getParamImp( 0, Bscript::BObjectImp::OTSQLResultSet ) );
  ;
  if ( !result )
  {
    return new BError( "Invalid parameters" );
  }
  return new BLong( result->affected_rows() );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_num_rows()
{
  Core::BSQLResultSet* result =
      static_cast<Core::BSQLResultSet*>( getParamImp( 0, Bscript::BObjectImp::OTSQLResultSet ) );
  ;
  if ( !result )
  {
    return new BError( "Invalid parameters" );
  }
  return new BLong( result->num_rows() );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_close()
{
  Core::BSQLConnection* sql =
      static_cast<Core::BSQLConnection*>( getParamImp( 0, Bscript::BObjectImp::OTSQLConnection ) );
  if ( !sql )
    return new BError( "Invalid parameters" );
  sql->close();
  return new BLong( 1 );
}

Bscript::BObjectImp* SQLExecutorModule::mf_mysql_fetch_row()
{
  Core::BSQLResultSet* result =
      static_cast<Core::BSQLResultSet*>( getParamImp( 0, Bscript::BObjectImp::OTSQLResultSet ) );
  if ( !result )
  {
    return new BError( "Invalid parameters" );
  }
  else if ( !result->has_result() )
  {
    return new BError( "Query returned no result" );
  }
  return new Core::BSQLRow( result );
}

#else

#define MF_NO_MYSQL( funcName )                                      \
  BObjectImp* SQLExecutorModule::funcName()                          \
  {                                                                  \
    return new BError( "POL was not compiled with MySQL support." ); \
  }
MF_NO_MYSQL( mf_mysql_connect )
MF_NO_MYSQL( mf_mysql_select_db )
MF_NO_MYSQL( mf_mysql_query )
MF_NO_MYSQL( mf_mysql_num_fields )
MF_NO_MYSQL( mf_mysql_field_name )
MF_NO_MYSQL( mf_mysql_affected_rows )
MF_NO_MYSQL( mf_mysql_num_rows )
MF_NO_MYSQL( mf_mysql_close )
MF_NO_MYSQL( mf_mysql_fetch_row )
#endif
}  // namespace Module
}  // namespace Pol
