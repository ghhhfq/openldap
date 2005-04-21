/* config.c - bdb backend configuration file routine */
/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 2000-2005 The OpenLDAP Foundation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in the file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */

#include "portable.h"

#include <stdio.h>
#include <ac/string.h>

#include "back-bdb.h"

#include "config.h"

#include "lutil.h"
#include "ldap_rq.h"

#ifdef DB_DIRTY_READ
#	define	SLAP_BDB_ALLOW_DIRTY_READ
#endif

static ObjectClass *bdb_oc;

static ConfigDriver bdb_cf_oc, bdb_cf_gen;

enum {
	BDB_CHKPT = 1,
	BDB_CONFIG,
	BDB_DIRECTORY,
	BDB_NOSYNC,
	BDB_DIRTYR,
	BDB_INDEX,
	BDB_LOCKD,
	BDB_SSTACK
};

static ConfigTable bdbcfg[] = {
	{ "", "", 0, 0, 0, ARG_MAGIC,
		bdb_cf_oc, NULL, NULL, NULL },
	{ "directory", "dir", 2, 2, 0, ARG_STRING|ARG_MAGIC|BDB_DIRECTORY,
		bdb_cf_gen, "( OLcfgDbAt:0.1 NAME 'olcDbDirectory' "
			"DESC 'Directory for database content' "
			"EQUALITY caseIgnoreMatch "
			"SYNTAX OMsDirectoryString SINGLE-VALUE )", NULL, NULL },
	{ "cachesize", "size", 2, 2, 0, ARG_INT|ARG_OFFSET,
		(void *)offsetof(struct bdb_info, bi_cache.c_maxsize),
		"( OLcfgDbAt:1.1 NAME 'olcDbCacheSize' "
			"DESC 'Entry cache size in entries' "
			"SYNTAX OMsInteger SINGLE-VALUE )", NULL, NULL },
	{ "checkpoint", "kbyte> <min", 3, 3, 0, ARG_MAGIC|BDB_CHKPT,
		bdb_cf_gen, "( OLcfgDbAt:1.2 NAME 'olcDbCheckpoint' "
			"DESC 'Database checkpoint interval in kbytes and minutes' "
			"SYNTAX OMsDirectoryString SINGLE-VALUE )",NULL, NULL },
	{ "dbconfig", "DB_CONFIG setting", 3, 0, 0, ARG_MAGIC|BDB_CONFIG,
		bdb_cf_gen, "( OLcfgDbAt:1.3 NAME 'olcDbConfig' "
			"DESC 'BerkeleyDB DB_CONFIG configuration directives' "
			"SYNTAX OMsDirectoryString )",NULL, NULL },
	{ "dbnosync", NULL, 1, 2, 0, ARG_ON_OFF|ARG_MAGIC|BDB_NOSYNC,
		bdb_cf_gen, "( OLcfgDbAt:1.4 NAME 'olcDbNoSync' "
			"DESC 'Disable synchronous database writes' "
			"SYNTAX OMsBoolean SINGLE-VALUE )", NULL, NULL },
	{ "dirtyread", NULL, 1, 2, 0,
#ifdef SLAP_BDB_ALLOW_DIRTY_READ
		ARG_ON_OFF|ARG_MAGIC|BDB_DIRTYR, bdb_cf_gen,
#else
		ARG_IGNORED, NULL,
#endif
		"( OLcfgDbAt:1.5 NAME 'olcDbDirtyRead' "
		"DESC 'Allow reads of uncommitted data' "
		"SYNTAX OMsBoolean SINGLE-VALUE )", NULL, NULL },
	{ "idlcachesize", "size", 2, 2, 0, ARG_INT|ARG_OFFSET,
		(void *)offsetof(struct bdb_info,bi_idl_cache_max_size),
		"( OLcfgDbAt:1.6 NAME 'olcDbIDLcacheSize' "
		"DESC 'IDL cache size in IDLs' "
		"SYNTAX OMsInteger SINGLE-VALUE )", NULL, NULL },
	{ "index", "attr> <[pres,eq,approx,sub]", 2, 3, 0, ARG_MAGIC|BDB_INDEX,
		bdb_cf_gen, "( OLcfgDbAt:0.2 NAME 'olcDbIndex' "
		"DESC 'Attribute index parameters' "
		"EQUALITY caseIgnoreMatch "
		"SYNTAX OMsDirectoryString )", NULL, NULL },
	{ "linearindex", NULL, 1, 2, 0, ARG_ON_OFF|ARG_OFFSET,
		(void *)offsetof(struct bdb_info, bi_linear_index), 
		"( OLcfgDbAt:1.7 NAME 'olcDbLinearIndex' "
		"DESC 'Index attributes one at a time' "
		"SYNTAX OMsBoolean SINGLE-VALUE )", NULL, NULL },
	{ "lockdetect", "policy", 2, 2, 0, ARG_MAGIC|BDB_LOCKD,
		bdb_cf_gen, "( OLcfgDbAt:1.8 NAME 'olcDbLockDetect' "
		"DESC 'Deadlock detection algorithm' "
		"SYNTAX OMsDirectoryString SINGLE-VALUE )", NULL, NULL },
	{ "mode", "mode", 2, 2, 0, ARG_INT|ARG_OFFSET,
		(void *)offsetof(struct bdb_info, bi_dbenv_mode),
		"( OLcfgDbAt:0.3 NAME 'olcDbMode' "
		"DESC 'Unix permissions of database files' "
		"SYNTAX OMsInteger SINGLE-VALUE )", NULL, NULL },
	{ "searchstack", "depth", 2, 2, 0, ARG_INT|ARG_MAGIC|BDB_SSTACK,
		bdb_cf_gen, "( OLcfgDbAt:1.9 NAME 'olcDbSearchStack' "
		"DESC 'Depth of search stack in IDLs' "
		"SYNTAX OMsInteger SINGLE-VALUE )", NULL, NULL },
	{ "shm_key", "key", 2, 2, 0, ARG_INT|ARG_OFFSET,
		(void *)offsetof(struct bdb_info, bi_shm_key), 
		"( OLcfgDbAt:1.10 NAME 'olcDbShmKey' "
		"DESC 'Key for shared memory region' "
		"SYNTAX OMsInteger SINGLE-VALUE )", NULL, NULL },
	{ NULL, NULL, 0, 0, 0, ARG_IGNORED,
		NULL, NULL, NULL, NULL }
};

static ConfigOCs bdbocs[] = {
	{ "( OLcfgDbOc:1.1 "
		"NAME 'olcBdbConfig' "
		"DESC 'BDB backend configuration' "
		"SUP olcDatabaseConfig "
		"MUST olcDbDirectory "
		"MAY ( olcDbCacheSize $ olcDbCheckpoint $ olcDbConfig $ "
		"olcDbNoSync $ olcDbDirtyRead $ olcDbIDLcacheSize $ "
		"olcDbIndex $ olcDbLinearIndex $ olcDbLockDetect $ "
		"olcDbMode $ olcDbSearchStack $ olcDbShmKey ) )",
		 	Cft_Database, &bdb_oc },
	{ NULL, 0, NULL }
};

static int
bdb_cf_oc(ConfigArgs *c)
{
	if ( c->op == SLAP_CONFIG_EMIT ) {
		value_add_one( &c->rvalue_vals, &bdb_oc->soc_cname );
		return 0;
	}
	return 1;
}

static slap_verbmasks bdb_lockd[] = {
	{ BER_BVC("default"), DB_LOCK_DEFAULT },
	{ BER_BVC("oldest"), DB_LOCK_OLDEST },
	{ BER_BVC("random"), DB_LOCK_RANDOM },
	{ BER_BVC("youngest"), DB_LOCK_YOUNGEST },
	{ BER_BVC("fewest"), DB_LOCK_MINLOCKS },
	{ BER_BVNULL, 0 }
};

/* reindex entries on the fly */
static void *
bdb_online_index( void *ctx, void *arg )
{
	struct re_s *rtask = arg;
	BackendDB *be = rtask->arg;
	struct bdb_info *bdb = be->be_private;

	Connection conn = {0};
	char opbuf[OPERATION_BUFFER_SIZE];
	Operation *op = (Operation *)opbuf;

	DBC *curs;
	DBT key, data;
	DB_TXN *txn;
	DB_LOCK lock;
	u_int32_t locker;
	ID id, nid;
	EntryInfo *ei;
	int rc, getnext = 1;

	connection_fake_init( &conn, op, ctx );

	op->o_bd = be;

	DBTzero( &key );
	DBTzero( &data );
	
	id = 1;
	key.data = &nid;
	key.size = key.ulen = sizeof(ID);
	key.flags = DB_DBT_USERMEM;

	data.flags = DB_DBT_USERMEM | DB_DBT_PARTIAL;
	data.dlen = data.ulen = 0;

	while ( 1 ) {
		if ( slapd_shutdown )
			break;

		rc = TXN_BEGIN( bdb->bi_dbenv, NULL, &txn, bdb->bi_db_opflags );
		if ( rc ) 
			break;
		locker = TXN_ID( txn );
		if ( getnext ) {
			getnext = 0;
			BDB_ID2DISK( id, &nid );
			rc = bdb->bi_id2entry->bdi_db->cursor(
				bdb->bi_id2entry->bdi_db, txn, &curs, bdb->bi_db_opflags );
			if ( rc ) {
				TXN_ABORT( txn );
				break;
			}
			rc = curs->c_get( curs, &key, &data, DB_SET_RANGE );
			curs->c_close( curs );
			if ( rc ) {
				TXN_ABORT( txn );
				if ( rc == DB_NOTFOUND )
					rc = 0;
				if ( rc == DB_LOCK_DEADLOCK ) {
					ldap_pvt_thread_yield();
					continue;
				}
				break;
			}
			BDB_DISK2ID( &nid, &id );
		}

		ei = NULL;
		rc = bdb_cache_find_id( op, txn, id, &ei, 0, locker, &lock );
		if ( rc ) {
			TXN_ABORT( txn );
			if ( rc == DB_LOCK_DEADLOCK ) {
				ldap_pvt_thread_yield();
				continue;
			}
			if ( rc == DB_NOTFOUND ) {
				id++
				getnext = 1;
				continue;
			}
			break;
		}
		if ( ei->bei_e ) {
			rc = bdb_index_entry( op, txn, BDB_INDEX_UPDATE_OP, ei->bei_e );
			if ( rc == DB_LOCK_DEADLOCK ) {
				TXN_ABORT( txn );
				ldap_pvt_thread_yield();
				continue;
			}
			if ( rc == 0 ) {
				rc = TXN_COMMIT( txn, 0 );
				txn = NULL;
			}
			if ( rc )
				break;
		}
		id++;
		getnext = 1;
	}
out:
	ldap_pvt_thread_mutex_lock( &slapd_rq.rq_mutex );
	ldap_pvt_runqueue_stoptask( &slapd_rq, rtask );
	ldap_pvt_runqueue_remove( &slapd_rq, rtask );
	ldap_pvt_thread_mutex_unlock( &slapd_rq.rq_mutex );

	return NULL;
}

/* Cleanup loose ends after Modify completes */
static int
bdb_cf_cleanup( ConfigArgs *c )
{
	struct bdb_info *bdb = c->be->be_private;

	if ( bdb->bi_flags & BDB_UPD_CONFIG ) {
		if ( bdb->bi_db_config ) {
			int i;
			FILE *f = fopen( bdb->bi_db_config_path, "w" );
			if ( f ) {
				for (i=0; bdb->bi_db_config[i].bv_val; i++)
					fprintf( f, "%s\n", bdb->bi_db_config[i].bv_val );
				fclose( f );
			}
		} else {
			unlink( bdb->bi_db_config_path );
		}
		bdb->bi_flags ^= BDB_UPD_CONFIG;
	}

	if ( bdb->bi_flags & BDB_DEL_INDEX ) {
		bdb_attr_flush( bdb );
		bdb->bi_flags ^= BDB_DEL_INDEX;
	}

	return 0;
}

static int
bdb_cf_gen(ConfigArgs *c)
{
	struct bdb_info *bdb = c->be->be_private;
	int rc;

	if ( c->op == SLAP_CONFIG_EMIT ) {
		rc = 0;
		switch( c->type ) {
		case BDB_CHKPT:
			if (bdb->bi_txn_cp ) {
				char buf[64];
				struct berval bv;
				bv.bv_len = sprintf( buf, "%d %d", bdb->bi_txn_cp_kbyte,
					bdb->bi_txn_cp_min );
				bv.bv_val = buf;
				value_add_one( &c->rvalue_vals, &bv );
			} else{
				rc = 1;
			}
			break;

		case BDB_DIRECTORY:
			if ( bdb->bi_dbenv_home ) {
				c->value_string = ch_strdup( bdb->bi_dbenv_home );
			} else {
				rc = 1;
			}
			break;

		case BDB_CONFIG:
			if ( bdb->bi_db_config ) {
				int i;
				struct berval bv;

				bv.bv_val = c->log;
				for (i=0; !BER_BVISNULL(&bdb->bi_db_config[i]); i++) {
					bv.bv_len = sprintf( bv.bv_val, "{%d}%s", i,
						bdb->bi_db_config[i].bv_val );
					value_add_one( &c->rvalue_vals, &bv );
				}
			}
			if ( !c->rvalue_vals ) rc = 1;
			break;

		case BDB_NOSYNC:
			if ( bdb->bi_dbenv_xflags & DB_TXN_NOSYNC )
				c->value_int = 1;
			break;
			
		case BDB_INDEX:
			bdb_attr_index_unparse( bdb, &c->rvalue_vals );
			if ( !c->rvalue_vals ) rc = 1;
			break;

		case BDB_LOCKD:
			rc = 1;
			if ( bdb->bi_lock_detect != DB_LOCK_DEFAULT ) {
				int i;
				for (i=0; !BER_BVISNULL(&bdb_lockd[i].word); i++) {
					if ( bdb->bi_lock_detect == bdb_lockd[i].mask ) {
						value_add_one( &c->rvalue_vals, &bdb_lockd[i].word );
						rc = 0;
						break;
					}
				}
			}
			break;

		case BDB_SSTACK:
			c->value_int = bdb->bi_search_stack_depth;
			break;
		}
		return rc;
	} else if ( c->op == LDAP_MOD_DELETE ) {
		rc = 0;
		switch( c->type ) {
		/* single-valued no-ops */
		case BDB_LOCKD:
		case BDB_SSTACK:
			break;

		case BDB_CHKPT:
			/* FIXME: should stop the checkpoint task too */
			bdb->bi_txn_cp = 0;
			break;
		case BDB_CONFIG:
			if ( c->valx < 0 ) {
				ber_bvarray_free( bdb->bi_db_config );
				bdb->bi_db_config = NULL;
			} else {
				int i = c->valx;
				ch_free( bdb->bi_db_config[i].bv_val );
				for (; bdb->bi_db_config[i].bv_val; i++)
					bdb->bi_db_config[i] = bdb->bi_db_config[i+1];
			}
			bdb->bi_flags |= BDB_UPD_CONFIG;
			c->cleanup = bdb_cf_cleanup;
			break;
		case BDB_DIRECTORY:
			rc = 1;
			/* FIXME: what does this mean? */
			break;
		case BDB_NOSYNC:
			bdb->bi_dbenv->set_flags( bdb->bi_dbenv, DB_TXN_NOSYNC, 0 );
			break;
		case BDB_INDEX: {
			AttributeDescription *ad = NULL;
			struct berval bv, def = BER_BVC("default");
			char *ptr;
			const char *text;
			for (ptr = c->line; !isspace( *ptr ); ptr++);
			bv.bv_val = c->line;
			bv.bv_len = ptr - bv.bv_val;
			if ( bvmatch( &bv, &def )) {
				bdb->bi_defaultmask = 0;
			} else {
				slap_bv2ad( &bv, &ad, &text );
				if ( ad ) {
					AttrInfo *ai = bdb_attr_mask( bdb, ad );
					ai->ai_indexmask |= BDB_INDEX_DELETING;
					bdb->bi_flags |= BDB_DEL_INDEX;
					c->cleanup = bdb_cf_cleanup;
				}
			}
			}
			break;
		}
		return rc;
	}

	switch( c->type ) {
	case BDB_CHKPT:
		bdb->bi_txn_cp = 1;
		bdb->bi_txn_cp_kbyte = strtol( c->argv[1], NULL, 0 );
		bdb->bi_txn_cp_min = strtol( c->argv[2], NULL, 0 );
		break;

	case BDB_CONFIG: {
		char *ptr = c->line + STRLENOF("dbconfig");
		struct berval bv;
		while (!isspace(*ptr)) ptr++;
		while (isspace(*ptr)) ptr++;
		
		if ( bdb->bi_flags & BDB_IS_OPEN ) {
			bdb->bi_flags |= BDB_UPD_CONFIG;
			c->cleanup = bdb_cf_cleanup;
		} else {
		/* If we're just starting up...
		 */
			FILE *f;
			/* If a DB_CONFIG file exists, or we don't know the path
			 * to the DB_CONFIG file, ignore these directives
			 */
			if (( bdb->bi_flags & BDB_HAS_CONFIG ) || !bdb->bi_db_config_path )
				break;
			f = fopen( bdb->bi_db_config_path, "a" );
			if ( f ) {
				/* FIXME: EBCDIC probably needs special handling */
				fprintf( f, "%s\n", ptr );
				fclose( f );
			}
		}
		ber_str2bv( ptr, 0, 1, &bv );
		ber_bvarray_add( &bdb->bi_db_config, &bv );
		}
		break;

	case BDB_DIRECTORY: {
		FILE *f;
		char *ptr;

		bdb->bi_dbenv_home = c->value_string;

		/* See if a DB_CONFIG file already exists here */
		bdb->bi_db_config_path = ch_malloc( strlen( bdb->bi_dbenv_home ) +
			STRLENOF(LDAP_DIRSEP) + STRLENOF("DB_CONFIG") + 1 );
		ptr = lutil_strcopy( bdb->bi_db_config_path, bdb->bi_dbenv_home );
		*ptr++ = LDAP_DIRSEP[0];
		strcpy( ptr, "DB_CONFIG" );

		f = fopen( bdb->bi_db_config_path, "r" );
		if ( f ) {
			bdb->bi_flags |= BDB_HAS_CONFIG;
			fclose(f);
		}
		}
		break;

	case BDB_NOSYNC:
		if ( c->value_int )
			bdb->bi_dbenv_xflags |= DB_TXN_NOSYNC;
		else
			bdb->bi_dbenv_xflags &= ~DB_TXN_NOSYNC;
		if ( bdb->bi_flags & BDB_IS_OPEN ) {
			bdb->bi_dbenv->set_flags( bdb->bi_dbenv, DB_TXN_NOSYNC,
				c->value_int );
		}
		break;

	case BDB_INDEX:
		rc = bdb_attr_index_config( bdb, c->fname, c->lineno,
			c->argc - 1, &c->argv[1] );

		if( rc != LDAP_SUCCESS ) return 1;
		if ( bdb->bi_flags & BDB_IS_OPEN ) {
			/* Start the task as soon as we finish here */
			ldap_pvt_runqueue_insert( &slapd_rq, 60,
				bdb_online_index, c->be );
		}
		break;

	case BDB_LOCKD:
		rc = verb_to_mask( c->argv[1], bdb_lockd );
		if ( BER_BVISNULL(&bdb_lockd[rc].word) ) {
			fprintf( stderr, "%s: "
				"bad policy (%s) in \"lockDetect <policy>\" line\n",
				c->log, c->argv[1] );
			return 1;
		}
		bdb->bi_lock_detect = rc;
		break;

	case BDB_SSTACK:
		if ( c->value_int < MINIMUM_SEARCH_STACK_DEPTH ) {
			fprintf( stderr,
		"%s: depth %d too small, using %d\n",
			c->log, c->value_int, MINIMUM_SEARCH_STACK_DEPTH );
			c->value_int = MINIMUM_SEARCH_STACK_DEPTH;
		}
		bdb->bi_search_stack_depth = c->value_int;
		break;
	}
	return 0;
}

int bdb_back_init_cf( BackendInfo *bi )
{
	int rc;
	bi->bi_cf_table = bdbcfg;

	rc = config_register_schema( bdbcfg, bdbocs );
	if ( rc ) return rc;
	bdbcfg[0].ad = slap_schema.si_ad_objectClass;
	return 0;
}
