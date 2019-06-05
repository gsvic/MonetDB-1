/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2019 MonetDB B.V.
 */

/* multi version catalog */

#include "monetdb_config.h"
#include "gdk.h"

#include "sql_mvc.h"
#include "sql_qc.h"
#include "sql_types.h"
#include "sql_env.h"
#include "sql_semantic.h"
#include "sql_partition.h"
#include "sql_privileges.h"
#include "sql_querytype.h"
#include "rel_rel.h"
#include "rel_exp.h"
#include "gdk_logger.h"
#include "wlc.h"

#include "mal_authorize.h"

static int mvc_debug = 0;

static void
sql_create_comments(mvc *m, sql_schema *s)
{
	sql_table *t;
	sql_column *c;
	sql_key *k;

	t = mvc_create_table(m, s, "comments", tt_table, 1, SQL_PERSIST, 0, -1, 0);
	c = mvc_create_column_(m, t, "id", "int", 32);
	k = sql_trans_create_ukey(m->session->tr, t, "comments_id_pkey", pkey);
	k = sql_trans_create_kc(m->session->tr, k, c);
	k = sql_trans_key_done(m->session->tr, k);
	sql_trans_create_dependency(m->session->tr, c->base.id, k->idx->base.id, INDEX_DEPENDENCY);
	c = mvc_create_column_(m, t, "remark", "varchar", 65000);
	sql_trans_alter_null(m->session->tr, c, 0);
}

#define MVC_INIT_DROP_TABLE(SQLID, TNAME)				\
	t = mvc_bind_table(m, s, TNAME);				\
	SQLID = t->base.id;						\
	if((output = mvc_drop_table(m, s, t, 0)) != MAL_SUCCEED) {	\
		mvc_destroy(m);						\
		fprintf(stderr, "!mvc_init: %s\n", output);		\
		freeException(output);					\
		return -1;						\
	}

int
mvc_init(int debug, store_type store, int ro, int su)
{
	int first = 0;
	sql_schema *s;
	sql_table *t;
	sqlid tid = 0, ntid, cid = 0, ncid;
	mvc *m;
	str msg;

	mvc_debug = debug&4;
	if (mvc_debug) {
		fprintf(stderr, "#mvc_init\n");
	}
	keyword_init();
	if(scanner_init_keywords() != 0) {
		fprintf(stderr, "!mvc_init: malloc failure\n");
		return -1;
	}

	if ((first = store_init(debug, store, ro, su)) < 0) {
		fprintf(stderr, "!mvc_init: unable to create system tables\n");
		return -1;
	}

	m = mvc_create(0, 0, NULL, NULL);
	if (!m) {
		fprintf(stderr, "!mvc_init: malloc failure\n");
		return -1;
	}

	m->eb = eb_create();
	m->sa = sa_create(m->eb);
	if (!m->sa) {
		mvc_destroy(m);
		fprintf(stderr, "!mvc_init: malloc failure\n");
		return -1;
	}

	/* disable caching */
	m->caching = 0;

	if (first || catalog_version) {
		if(mvc_trans(m) < 0) {
			mvc_destroy(m);
			fprintf(stderr, "!mvc_init: failed to start transaction\n");
			return -1;
		}

		s = m->session->schema = mvc_bind_schema(m, "sys");
		assert(m->session->schema != NULL);

		if (!first) {
			str output;
			MVC_INIT_DROP_TABLE(tid,  "tables")
			MVC_INIT_DROP_TABLE(cid,  "columns")
		}

		t = mvc_create_view(m, s, "tables", SQL_PERSIST, "SELECT \"id\", \"name\", \"schema_id\", \"query\", CAST(CASE WHEN \"system\" THEN \"type\" + 10 /* system table/view */ ELSE (CASE WHEN \"commit_action\" = 0 THEN \"type\" /* table/view */ ELSE \"type\" + 20 /* global temp table */ END) END AS SMALLINT) AS \"type\", \"system\", \"commit_action\", \"access\", CASE WHEN (NOT \"system\" AND \"commit_action\" > 0) THEN 1 ELSE 0 END AS \"temporary\" FROM \"sys\".\"_tables\" WHERE \"type\" <> 2 UNION ALL SELECT \"id\", \"name\", \"schema_id\", \"query\", CAST(\"type\" + 30 /* local temp table */ AS SMALLINT) AS \"type\", \"system\", \"commit_action\", \"access\", 1 AS \"temporary\" FROM \"tmp\".\"_tables\";", 1);
		ntid = t->base.id;
		mvc_create_column_(m, t, "id", "int", 32);
		mvc_create_column_(m, t, "name", "varchar", 1024);
		mvc_create_column_(m, t, "schema_id", "int", 32);
		mvc_create_column_(m, t, "query", "varchar", 1 << 20);
		mvc_create_column_(m, t, "type", "smallint", 16);
		mvc_create_column_(m, t, "system", "boolean", 1);
		mvc_create_column_(m, t, "commit_action", "smallint", 16);
		mvc_create_column_(m, t, "access", "smallint", 16);
		mvc_create_column_(m, t, "temporary", "smallint", 16);

		if (!first) {
			int pub = ROLE_PUBLIC;
			int p = PRIV_SELECT;
			int zero = 0;
			sql_table *privs = find_sql_table(s, "privileges");
			sql_table *deps = find_sql_table(s, "dependencies");
			sql_column *depids = find_sql_column(deps, "id");
			oid rid;
			rids *rs;

			table_funcs.table_insert(m->session->tr, privs, &t->base.id, &pub, &p, &zero, &zero);
			rs = table_funcs.rids_select(m->session->tr, depids, &tid, &tid, NULL);
			while ((rid = table_funcs.rids_next(rs)), !is_oid_nil(rid)) {
				table_funcs.column_update_value(m->session->tr, depids, rid, &ntid);
			}
			table_funcs.rids_destroy(rs);
		}

		t = mvc_create_view(m, s, "columns", SQL_PERSIST, "SELECT * FROM (SELECT p.* FROM \"sys\".\"_columns\" AS p UNION ALL SELECT t.* FROM \"tmp\".\"_columns\" AS t) AS columns;", 1);
		ncid = t->base.id;
		mvc_create_column_(m, t, "id", "int", 32);
		mvc_create_column_(m, t, "name", "varchar", 1024);
		mvc_create_column_(m, t, "type", "varchar", 1024);
		mvc_create_column_(m, t, "type_digits", "int", 32);
		mvc_create_column_(m, t, "type_scale", "int", 32);
		mvc_create_column_(m, t, "table_id", "int", 32);
		mvc_create_column_(m, t, "default", "varchar", STORAGE_MAX_VALUE_LENGTH);
		mvc_create_column_(m, t, "null", "boolean", 1);
		mvc_create_column_(m, t, "number", "int", 32);
		mvc_create_column_(m, t, "storage", "varchar", 2048);

		if (!first) {
			int pub = ROLE_PUBLIC;
			int p = PRIV_SELECT;
			int zero = 0;
			sql_table *privs = find_sql_table(s, "privileges");
			sql_table *deps = find_sql_table(s, "dependencies");
			sql_column *depids = find_sql_column(deps, "id");
			oid rid;
			rids *rs;

			table_funcs.table_insert(m->session->tr, privs, &t->base.id, &pub, &p, &zero, &zero);
			rs = table_funcs.rids_select(m->session->tr, depids, &cid, &cid, NULL);
			while ((rid = table_funcs.rids_next(rs)), !is_oid_nil(rid)) {
				table_funcs.column_update_value(m->session->tr, depids, rid, &ncid);
			}
			table_funcs.rids_destroy(rs);
		} else {
			sql_create_env(m, s);
			sql_create_comments(m, s);
			sql_create_privileges(m, s);
		}

		s = m->session->schema = mvc_bind_schema(m, "tmp");
		assert(m->session->schema != NULL);

		if ((msg = mvc_commit(m, 0, NULL, false)) != MAL_SUCCEED) {
			fprintf(stderr, "!mvc_init: unable to commit system tables: %s\n", (msg + 6));
			freeException(msg);
			return -1;
		}
	}

	if(mvc_trans(m) < 0) {
		mvc_destroy(m);
		fprintf(stderr, "!mvc_init: failed to start transaction\n");
		return -1;
	}

	//as the sql_parser is not yet initialized in the storage, we determine the sql type of the sql_parts here
	for (node *n = m->session->tr->schemas.set->h; n; n = n->next) {
		sql_schema *ss = (sql_schema*) n->data;
		if(ss->tables.set) {
			for (node *nn = ss->tables.set->h; nn; nn = nn->next) {
				sql_table *tt = (sql_table*) nn->data;
				if(isPartitionedByColumnTable(tt) || isPartitionedByExpressionTable(tt)) {
					char *err;
					if((err = initialize_sql_parts(m, tt)) != NULL) {
						fprintf(stderr, "!mvc_init: unable to start partitioned table: %s.%s: %s\n",
								ss->base.name, tt->base.name, err);
						freeException(err);
						return -1;
					}
				}
			}
		}
	}

	if ((msg = mvc_commit(m, 0, NULL, false)) != MAL_SUCCEED) {
		fprintf(stderr, "!mvc_init: unable to commit system tables: %s\n", (msg + 6));
		freeException(msg);
		return -1;
	}

	mvc_destroy(m);
	return first;
}

void
mvc_exit(void)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_exit\n");

	store_exit();
	keyword_exit();
}

void
mvc_logmanager(void)
{
	store_manager();
}

void
mvc_idlemanager(void)
{
	idle_manager();
}

int
mvc_status(mvc *m)
{
	int res = m->session->status;

	return res;
}

int
mvc_type(mvc *m)
{
	int res = m->type;

	m->type = Q_PARSE;
	return res;
}

int
mvc_debug_on(mvc *m, int flg)
{

	if (m->debug & flg)
		return 1;

	return 0;
}

void
mvc_cancel_session(mvc *m)
{
	store_lock();
	sql_trans_end(m->session);
	store_unlock();
}

int
mvc_trans(mvc *m)
{
	int schema_changed = 0, err = m->session->status;
	assert(!m->session->active);	/* can only start a new transaction */

	store_lock();
	if (GDKverbose >= 1)
		fprintf(stderr, "#%s: starting transaction\n",
			MT_thread_getname());
	schema_changed = sql_trans_begin(m->session);
	if (m->qc && (schema_changed || m->qc->nr > m->cache || err)){
		if (schema_changed || err) {
			int seqnr = m->qc->id;
			if (m->qc)
				qc_destroy(m->qc);
			m->qc = qc_create(m->clientid, seqnr);
			if (!m->qc) {
				sql_trans_end(m->session);
				store_unlock();
				return -1;
			}
		} else { /* clean all but the prepared statements */
			qc_clean(m->qc);
		}
	}
	store_unlock();
	return 0;
}

static sql_trans *
sql_trans_deref( sql_trans *tr ) 
{
	node *n, *m, *o;

	for ( n = tr->schemas.set->h; n; n = n->next) {
		sql_schema *s = n->data;

		if (s->tables.set)
		for ( m = s->tables.set->h; m; m = m->next) {
			sql_table *t = m->data;

			if (t->po) { 
				sql_table *p = t->po;

				t->po = t->po->po;
				table_destroy(p);
			}

			if (t->columns.set) {
				for ( o = t->columns.set->h; o; o = o->next) {
					sql_column *c = o->data;

					if (c->po) {
						sql_column *p = c->po;

						c->po = c->po->po;
						column_destroy(p);
					}
				}
				if(isPartitionedByColumnTable(t)) {
					t->part.pcol = t->po->part.pcol;
				} else if(isPartitionedByExpressionTable(t)) {
					t->part.pexp = t->po->part.pexp;
				}
			}
			if (t->idxs.set)
			for ( o = t->idxs.set->h; o; o = o->next) {
				sql_idx *i = o->data;

				if (i->po) {
					sql_idx *p = i->po;

					i->po = i->po->po;
					idx_destroy(p);
				}
			}
		}
	}
	return tr->parent;
}

str
mvc_commit(mvc *m, int chain, const char *name, bool enabling_auto_commit)
{
	sql_trans *cur, *tr = m->session->tr, *ctr;
	int ok = SQL_OK;//, wait = 0;
	str msg, other;
	char operation[BUFSIZ];

	assert(tr);
	assert(m->session->active);	/* only commit an active transaction */

	if (mvc_debug)
		fprintf(stderr, "#mvc_commit %s\n", (name) ? name : "");

	if(enabling_auto_commit)
		snprintf(operation, BUFSIZ, "Commit failed while enabling auto_commit: ");
	else if(name)
		snprintf(operation, BUFSIZ, "SAVEPOINT: (%s)", name);
	else
		snprintf(operation, BUFSIZ, "COMMIT:");

	if (m->session->status < 0) {
		msg = createException(SQL, "sql.commit", SQLSTATE(40000) "%s transaction is aborted, will ROLLBACK instead", operation);
		if((other = mvc_rollback(m, chain, name, false)) != MAL_SUCCEED)
			freeException(other);
		return msg;
	}

	/* savepoint then simply make a copy of the current transaction */
	if (name && name[0] != '\0') {
		sql_trans *tr = m->session->tr;
		if (mvc_debug)
			fprintf(stderr, "#mvc_savepoint\n");
		store_lock();
		m->session->tr = sql_trans_create(tr, name);
		if(!m->session->tr) {
			store_unlock();
			msg = createException(SQL, "sql.commit", SQLSTATE(HY001) "%s allocation failure while committing the transaction, will ROLLBACK instead", operation);
			if((other = mvc_rollback(m, chain, name, false)) != MAL_SUCCEED)
				freeException(other);
			return msg;
		}
		msg = WLCcommit(m->clientid);
		store_unlock();
		if(msg != MAL_SUCCEED) {
			if((other = mvc_rollback(m, chain, name, false)) != MAL_SUCCEED)
				freeException(other);
			return msg;
		}
		m->type = Q_TRANS;
		if (m->qc) /* clean query cache, protect against concurrent access on the hash tables (when functions already exists, concurrent mal will
build up the hash (not copied in the trans dup)) */
			qc_clean(m->qc);
		m->session->schema = find_sql_schema(m->session->tr, m->session->schema_name);
		if (mvc_debug)
			fprintf(stderr, "#mvc_commit %s done\n", name);
		if (GDKverbose >= 1)
			fprintf(stderr, "#%s: savepoint commit %s done\n",
				MT_thread_getname(), name);
		return msg;
	}

	/* first release all intermediate savepoints */
	ctr = cur = tr;
	tr = tr->parent;
	if (tr->parent) {
		store_lock();
		while (ctr->parent->parent != NULL && ok == SQL_OK) {
			/* first free references to tr objects, ie
			 * c->po = c->po->po etc
			 */
			ctr = sql_trans_deref(ctr);
		}
		while (tr->parent != NULL && ok == SQL_OK) 
			tr = sql_trans_destroy(tr);
		store_unlock();
	}
	cur -> parent = tr;
	tr = cur;

	store_lock();
	/* if there is nothing to commit reuse the current transaction */
	if (tr->wtime == 0) {
		if (!chain) 
			sql_trans_end(m->session);
		m->type = Q_TRANS;
		msg = WLCcommit(m->clientid);
		store_unlock();
		if(msg != MAL_SUCCEED) {
			if((other = mvc_rollback(m, chain, name, false)) != MAL_SUCCEED)
				freeException(other);
			return msg;
		}
		if (mvc_debug)
			fprintf(stderr, "#mvc_commit done\n");
		if (GDKverbose >= 1)
			fprintf(stderr, "#%s: commit done (no changes)\n",
				MT_thread_getname());
		return msg;
	}

	/*
	while (tr->schema_updates && ATOMIC_GET(&store_nr_active) > 1) {
		store_unlock();
		MT_sleep_ms(100);
		wait += 100;
		if (wait > 1000) {
			(void)sql_error(m, 010, SQLSTATE(40000) "COMMIT: transaction is aborted because of DDL concurrency conflicts, will ROLLBACK instead");
			mvc_rollback(m, chain, name, false);
			return -1;
		}
		store_lock();
	}
	 * */
	/* validation phase */
	if (sql_trans_validate(tr)) {
		if ((ok = sql_trans_commit(tr)) != SQL_OK) {
			char *err = sql_message(SQLSTATE(40000) "%s transaction commit failed (perhaps your disk is full?) exiting (kernel error: %s)", operation, GDKerrbuf);
			GDKfatal("%s", err);
			_DELETE(err);
		}
	} else {
		store_unlock();
		msg = createException(SQL, "sql.commit", SQLSTATE(40000) "%s transaction is aborted because of concurrency conflicts, will ROLLBACK instead", operation);
		if((other = mvc_rollback(m, chain, name, false)) != MAL_SUCCEED)
			freeException(other);
		return msg;
	}
	msg = WLCcommit(m->clientid);
	if(msg != MAL_SUCCEED) {
		store_unlock();
		if((other = mvc_rollback(m, chain, name, false)) != MAL_SUCCEED)
			freeException(other);
		return msg;
	}
	sql_trans_end(m->session);
	if (chain)
		sql_trans_begin(m->session);
	store_unlock();
	m->type = Q_TRANS;
	if (mvc_debug)
		fprintf(stderr, "#mvc_commit done\n");
	if (GDKverbose >= 1)
		fprintf(stderr, "#%s: commit done\n",
			MT_thread_getname());
	return msg;
}

str
mvc_rollback(mvc *m, int chain, const char *name, bool disabling_auto_commit)
{
	sql_trans *tr = m->session->tr;
	str msg;

	if (mvc_debug)
		fprintf(stderr, "#mvc_rollback %s\n", (name) ? name : "");

	assert(tr);
	assert(m->session->active);	/* only abort an active transaction */
	(void) disabling_auto_commit;

	store_lock();
	if (m->qc) 
		qc_clean(m->qc);
	if (name && name[0] != '\0') {
		while (tr && (!tr->name || strcmp(tr->name, name) != 0))
			tr = tr->parent;
		if (!tr) {
			msg = createException(SQL, "sql.rollback", SQLSTATE(42000) "ROLLBACK TO SAVEPOINT: no such savepoint: '%s'", name);
			m->session->status = -1;
			store_unlock();
			return msg;
		}
		tr = m->session->tr;
		while (!tr->name || strcmp(tr->name, name) != 0) {
			/* make sure we do not reuse changed data */
			if (tr->wtime)
				tr->status = 1;
			tr = sql_trans_destroy(tr);
		}
		m->session->tr = tr;	/* restart at savepoint */
		m->session->status = tr->status;
		if (tr->name) 
			tr->name = NULL;
		m->session->schema = find_sql_schema(m->session->tr, m->session->schema_name);
	} else if (tr->parent) {
		/* first release all intermediate savepoints */
		while (tr->parent->parent != NULL) {
			tr = sql_trans_destroy(tr);
		}
		m->session-> tr = tr;
		/* make sure we do not reuse changed data */
		if (tr->wtime)
			tr->status = 1;
		sql_trans_end(m->session);
		if (chain) 
			sql_trans_begin(m->session);
	}
	msg = WLCrollback(m->clientid);
	store_unlock();
	if (msg != MAL_SUCCEED) {
		m->session->status = -1;
		return msg;
	}
	m->type = Q_TRANS;
	if (mvc_debug)
		fprintf(stderr, "#mvc_rollback %s done\n", (name) ? name : "");
	if (GDKverbose >= 1)
		fprintf(stderr, "#%s: commit%s%s rolled back%s\n",
			MT_thread_getname(), name ? " " : "", name ? name : "",
			tr->wtime == 0 ? " (no changes)" : "");
	return msg;
}

/* release all savepoints up including the given named savepoint 
 * but keep the current changes.
 * */
str
mvc_release(mvc *m, const char *name)
{
	int ok = SQL_OK;
	int res = Q_TRANS;
	sql_trans *tr = m->session->tr;
	str msg = MAL_SUCCEED;

	assert(tr);
	assert(m->session->active);	/* only release active transactions */

	if (mvc_debug)
		fprintf(stderr, "#mvc_release %s\n", (name) ? name : "");

	if (!name && (msg = mvc_rollback(m, 0, name, false)) != MAL_SUCCEED) {
		m->session->status = -1;
		return msg;
	}

	while (tr && (!tr->name || strcmp(tr->name, name) != 0))
		tr = tr->parent;
	if (!tr || !tr->name || strcmp(tr->name, name) != 0) {
		msg = createException(SQL, "sql.release", SQLSTATE(42000) "Release savepoint %s doesn't exist", name);
		m->session->status = -1;
		return msg;
	}
	tr = m->session->tr;
	store_lock();
	while (ok == SQL_OK && (!tr->name || strcmp(tr->name, name) != 0)) {
		/* commit all intermediate savepoints */
		if (sql_trans_commit(tr) != SQL_OK)
			GDKfatal("release savepoints should not fail");
		tr = sql_trans_destroy(tr);
	}
	tr->name = NULL;
	store_unlock();
	m->session->tr = tr;
	m->session->schema = find_sql_schema(m->session->tr, m->session->schema_name);

	m->type = res;
	return msg;
}

mvc *
mvc_create(int clientid, int debug, bstream *rs, stream *ws)
{
	mvc *m;

 	m = ZNEW(mvc);
	if(!m) {
		return NULL;
	}
	if (mvc_debug)
		fprintf(stderr, "#mvc_create\n");

	m->errstr[0] = '\0';
	/* if an error exceeds the buffer we don't want garbage at the end */
	m->errstr[ERRSIZE-1] = '\0';

	m->qc = qc_create(clientid, 0);
	if(!m->qc) {
		_DELETE(m);
		return NULL;
	}
	m->eb = eb_create();
	m->sa = NULL;

	m->params = NULL;
	m->sizevars = MAXPARAMS;
	m->vars = NEW_ARRAY(sql_var, m->sizevars);

	m->topvars = 0;
	m->frame = 1;
	m->use_views = 0;
	m->argmax = MAXPARAMS;
	m->args = NEW_ARRAY(atom*,m->argmax);
	if(!m->vars || !m->args) {
		qc_destroy(m->qc);
		_DELETE(m->vars);
		_DELETE(m->args);
		_DELETE(m);
		return NULL;
	}
	m->argc = 0;
	m->sym = NULL;

	m->role_id = m->user_id = -1;
	m->timezone = 0;
	m->clientid = clientid;

	m->emode = m_normal;
	m->emod = mod_none;
	m->reply_size = 100;
	m->debug = debug;
	m->cache = DEFAULT_CACHESIZE;
	m->caching = m->cache;

	m->label = 0;
	m->cascade_action = NULL;

	store_lock();
	m->session = sql_session_create(1 /*autocommit on*/);
	store_unlock();
	if(!m->session) {
		qc_destroy(m->qc);
		_DELETE(m->vars);
		_DELETE(m->args);
		_DELETE(m);
		return NULL;
	}

	m->type = Q_PARSE;
	m->pushdown = 1;

	scanner_init(&m->scanner, rs, ws);
	return m;
}

int
mvc_reset(mvc *m, bstream *rs, stream *ws, int debug, int globalvars)
{
	int res = 1;
	sql_trans *tr;

	if (mvc_debug)
		fprintf(stderr, "#mvc_reset\n");
	tr = m->session->tr;
	if (tr && tr->parent) {
		assert(m->session->active == 0);
		store_lock();
		while (tr->parent->parent != NULL) 
			tr = sql_trans_destroy(tr);
		store_unlock();
	}
	if (tr && !sql_session_reset(m->session, 1 /*autocommit on*/))
		res = 0;

	if (m->sa)
		m->sa = sa_reset(m->sa);
	else
		m->sa = sa_create(m->eb);
	if(!m->sa)
		res = 0;

	m->errstr[0] = '\0';

	m->params = NULL;
	/* reset topvars to the set of global variables */
	stack_pop_until(m, globalvars);
	m->frame = 1;
	m->argc = 0;
	m->sym = NULL;

	m->role_id = m->user_id = -1;
	m->emode = m_normal;
	m->emod = mod_none;
	if (m->reply_size != 100)
		stack_set_number(m, "reply_size", 100);
	m->reply_size = 100;
	if (m->timezone != 0)
		stack_set_number(m, "current_timezone", 0);
	m->timezone = 0;
	if (m->debug != debug)
		stack_set_number(m, "debug", debug);
	m->debug = debug;
	if (m->cache != DEFAULT_CACHESIZE)
		stack_set_number(m, "cache", DEFAULT_CACHESIZE);
	m->cache = DEFAULT_CACHESIZE;
	m->caching = m->cache;

	m->label = 0;
	m->cascade_action = NULL;
	m->type = Q_PARSE;
	m->pushdown = 1;

	scanner_init(&m->scanner, rs, ws);
	return res;
}

void
mvc_destroy(mvc *m)
{
	sql_trans *tr;

	if (mvc_debug)
		fprintf(stderr, "#mvc_destroy\n");
	tr = m->session->tr;
	if (tr) {
		store_lock();
		if (m->session->active)
			sql_trans_end(m->session);
		while (tr->parent)
			tr = sql_trans_destroy(tr);
		m->session->tr = NULL;
		store_unlock();
	}
	sql_session_destroy(m->session);

	stack_pop_until(m, 0);
	_DELETE(m->vars);

	if (m->scanner.log) /* close and destroy stream */
		close_stream(m->scanner.log);

	if (m->sa)
		sa_destroy(m->sa);
	m->sa = NULL;
	eb_destroy(m->eb);
	if (m->qc)
		qc_destroy(m->qc);
	m->qc = NULL;

	_DELETE(m->args);
	m->args = NULL;
	_DELETE(m);
}

sql_type *
mvc_bind_type(mvc *sql, const char *name)
{
	sql_type *t = sql_trans_bind_type(sql->session->tr, NULL, name);

	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_type %s\n", name);
	return t;
}

sql_type *
schema_bind_type(mvc *sql, sql_schema *s, const char *name)
{
	sql_type *t = find_sql_type(s, name);

	(void) sql;
	if (!t)
		return NULL;
	if (mvc_debug)
		fprintf(stderr, "#schema_bind_type %s\n", name);
	return t;
}

sql_func *
mvc_bind_func(mvc *sql, const char *name)
{
	sql_func *t = sql_trans_bind_func(sql->session->tr, name);

	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_func %s\n", name);
	return t;
}

list *
schema_bind_func(mvc *sql, sql_schema * s, const char *name, int type)
{
	list *func_list = find_all_sql_func(s, name, type);

	(void) sql;
	if (!func_list)
		return NULL;
	if (mvc_debug)
		fprintf(stderr, "#schema_bind_func %s\n", name);
	return func_list;
}

sql_schema *
mvc_bind_schema(mvc *m, const char *sname)
{
	sql_trans *tr = m->session->tr;
	sql_schema *s;

	if (!tr)
		return NULL;

	/* declared tables */
	if (strcmp(sname, str_nil) == 0)
		sname = dt_schema;
 	s = find_sql_schema(tr, sname);
	if (!s)
		return NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_schema %s\n", sname);
	return s;
}

sql_table *
mvc_bind_table(mvc *m, sql_schema *s, const char *tname)
{
	sql_table *t = NULL;

	if (!s) { /* Declared tables during query compilation have no schema */
		sql_table *tpe = stack_find_table(m, tname);
		if (tpe) {
			t = tpe;
		} else { /* during exection they are in the declared table schema */
				s = mvc_bind_schema(m, dt_schema);
			return mvc_bind_table(m, s, tname);
		}
	} else {
 		t = find_sql_table(s, tname);
	}
	if (!t)
		return NULL;
	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_table %s.%s\n", s ? s->base.name : "<noschema>", tname);

	return t;
}

sql_column *
mvc_bind_column(mvc *m, sql_table *t, const char *cname)
{
	sql_column *c;

	(void)m;
	c = find_sql_column(t, cname);
	if (!c)
		return NULL;
	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_column %s.%s\n", t->base.name, cname);
	return c;
}

static sql_column *
first_column(sql_table *t)
{
	node *n = cs_first_node(&t->columns);

	if (n)
		return n->data;
	return NULL;
}


sql_column *
mvc_first_column(mvc *m, sql_table *t)
{
	sql_column *c = first_column(t);

	(void) m;
	if (!c)
		return NULL;
	if (mvc_debug)
		fprintf(stderr, "#mvc_first_column %s.%s\n", t->base.name, c->base.name);

	return c;
}

sql_key *
mvc_bind_key(mvc *m, sql_schema *s, const char *kname)
{
	node *n = list_find_name(s->keys, kname);
	sql_key *k;

	(void) m;
	if (!n)
		return NULL;
	k = n->data;

	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_key %s.%s\n", s->base.name, kname);

	return k;
}

sql_idx *
mvc_bind_idx(mvc *m, sql_schema *s, const char *iname)
{
	node *n = list_find_name(s->idxs, iname);
	sql_idx *i;

	(void) m;
	if (!n)
		return NULL;
	i = n->data;

	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_idx %s.%s\n", s->base.name, iname);

	return i;
}

static int
uniqueKey(sql_key *k)
{
	return (k->type == pkey || k->type == ukey);
}

sql_key *
mvc_bind_ukey(sql_table *t, list *colnames)
{
	node *cn;
	node *cur;
	sql_key *res = NULL;
	int len = list_length(colnames);

	if (cs_size(&t->keys))
		for (cur = t->keys.set->h; cur; cur = cur->next) {
			node *cc;
			sql_key *k = cur->data;

			if (uniqueKey(k) && list_length(k->columns) == len) {
				res = k;
				for (cc = k->columns->h, cn = colnames->h; cc && cn; cc = cc->next, cn = cn->next) {
					sql_kc *c = cc->data;
					char *n = cn->data;

					if (strcmp(c->c->base.name, n) != 0) {
						res = NULL;
						break;
					}
				}
				if (res)
					break;
			}
		}
	return res;
}

sql_trigger *
mvc_bind_trigger(mvc *m, sql_schema *s, const char *tname)
{
	node *n = list_find_name(s->triggers, tname);
	sql_trigger *trigger;

	(void) m;
	if (!n)
		return NULL;
	trigger = n->data;

	if (mvc_debug)
		fprintf(stderr, "#mvc_bind_trigger %s.%s\n", s->base.name, tname);

	return trigger;
}

sql_type *
mvc_create_type(mvc *sql, sql_schema * s, const char *name, int digits, int scale, int radix, const char *impl)
{
	sql_type *t = NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_type %s\n", name);

	t = sql_trans_create_type(sql->session->tr, s, name, digits, scale, radix, impl);
	return t;
}

int
mvc_drop_type(mvc *m, sql_schema *s, sql_type *t, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_type %s %s\n", s->base.name, t->base.name);

	if (t)
		return sql_trans_drop_type(m->session->tr, s, t->base.id, drop_action);
	return 0;
}

sql_func *
mvc_create_func(mvc *sql, sql_allocator *sa, sql_schema * s, const char *name, list *args, list *res, int type, int lang, const char *mod, const char *impl, const char *query, bit varres, bit vararg, bit system)
{
	sql_func *f = NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_func %s\n", name);
	if (sa) {
		f = create_sql_func(sa, name, args, res, type, lang, mod, impl, query, varres, vararg, system);
		f->s = s;
	} else
		f = sql_trans_create_func(sql->session->tr, s, name, args, res, type, lang, mod, impl, query, varres, vararg, system);
	return f;
}

int
mvc_drop_func(mvc *m, sql_schema *s, sql_func *f, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_func %s %s\n", s->base.name, f->base.name);

	return sql_trans_drop_func(m->session->tr, s, f->base.id, drop_action ? DROP_CASCADE_START : DROP_RESTRICT);
}

int
mvc_drop_all_func(mvc *m, sql_schema *s, list *list_func, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_all_func %s %s\n", s->base.name, ((sql_func *) list_func->h->data)->base.name);

	return sql_trans_drop_all_func(m->session->tr, s, list_func, drop_action ? DROP_CASCADE_START : DROP_RESTRICT);
}

sql_schema *
mvc_create_schema(mvc *m, const char *name, sqlid auth_id, sqlid owner)
{
	sql_schema *s = NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_schema %s %d %d\n", name, auth_id, owner);

	s = sql_trans_create_schema(m->session->tr, name, auth_id, owner);
	return s;
}

int
mvc_drop_schema(mvc *m, sql_schema * s, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_schema %s\n", s->base.name);
	return sql_trans_drop_schema(m->session->tr, s->base.id, drop_action ? DROP_CASCADE_START : DROP_RESTRICT);
}

sql_ukey *
mvc_create_ukey(mvc *m, sql_table *t, const char *name, key_type kt)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_create_ukey %s %u\n", t->base.name, kt);
	if (t->persistence == SQL_DECLARED_TABLE)
		return create_sql_ukey(m->sa, t, name, kt);	
	else
		return (sql_ukey*)sql_trans_create_ukey(m->session->tr, t, name, kt);
}

sql_key *
mvc_create_ukey_done(mvc *m, sql_key *k)
{
	if (k->t->persistence == SQL_DECLARED_TABLE)
		return key_create_done(m->sa, k);
	else
		return sql_trans_key_done(m->session->tr, k);
}

sql_fkey *
mvc_create_fkey(mvc *m, sql_table *t, const char *name, key_type kt, sql_key *rkey, int on_delete, int on_update)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_create_fkey %s %u %p\n", t->base.name, kt, rkey);
	if (t->persistence == SQL_DECLARED_TABLE)
		return create_sql_fkey(m->sa, t, name, kt, rkey, on_delete, on_update);	
	else
		return sql_trans_create_fkey(m->session->tr, t, name, kt, rkey, on_delete, on_update);
}

sql_key *
mvc_create_kc(mvc *m, sql_key *k, sql_column *c)
{
	if (k->t->persistence == SQL_DECLARED_TABLE)
		return create_sql_kc(m->sa, k, c);
	else
		return sql_trans_create_kc(m->session->tr, k, c);
}

sql_fkey *
mvc_create_fkc(mvc *m, sql_fkey *fk, sql_column *c)
{
	sql_key *k = (sql_key*)fk;

	if (k->t->persistence == SQL_DECLARED_TABLE)
		return (sql_fkey*)create_sql_kc(m->sa, k, c);
	else
		return sql_trans_create_fkc(m->session->tr, fk, c);
}

int
mvc_drop_key(mvc *m, sql_schema *s, sql_key *k, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_key %s %s\n", s->base.name, k->base.name);
	if (k->t->persistence == SQL_DECLARED_TABLE) {
		drop_sql_key(k->t, k->base.id, drop_action);
		return 0;
	} else
		return sql_trans_drop_key(m->session->tr, s, k->base.id, drop_action ? DROP_CASCADE_START : DROP_RESTRICT);
}

sql_idx *
mvc_create_idx(mvc *m, sql_table *t, const char *name, idx_type it)
{
	sql_idx *i;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_idx %s %u\n", t->base.name, it);

	if (t->persistence == SQL_DECLARED_TABLE)
		/* declared tables should not end up in the catalog */
		return create_sql_idx(m->sa, t, name, it);
	else
		i = sql_trans_create_idx(m->session->tr, t, name, it);
	return i;
}

sql_idx *
mvc_create_ic(mvc *m, sql_idx * i, sql_column *c)
{
	if (i->t->persistence == SQL_DECLARED_TABLE)
		/* declared tables should not end up in the catalog */
		return create_sql_ic(m->sa, i, c);
	else
		return sql_trans_create_ic(m->session->tr, i, c);
}

int
mvc_drop_idx(mvc *m, sql_schema *s, sql_idx *i)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_idx %s %s\n", s->base.name, i->base.name);

	if (i->t->persistence == SQL_DECLARED_TABLE) {
		/* declared tables should not end up in the catalog */
		drop_sql_idx(i->t, i->base.id);
		return 0;
	} else
		return sql_trans_drop_idx(m->session->tr, s, i->base.id, DROP_RESTRICT);
}

sql_trigger * 
mvc_create_trigger(mvc *m, sql_table *t, const char *name, sht time, sht orientation, sht event, const char *old_name, const char *new_name, const char *condition, const char *statement )
{
	sql_trigger *i;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_trigger %s %d %d %d\n", t->base.name, time, orientation, event);

	i = sql_trans_create_trigger(m->session->tr, t, name, time, orientation, 
			event, old_name, new_name, condition, statement);
	return i;
}

sql_trigger *
mvc_create_tc(mvc *m, sql_trigger * i, sql_column *c /*, extra options such as trunc */ )
{
	sql_trans_create_tc(m->session->tr, i, c);
	return i;
}

int
mvc_drop_trigger(mvc *m, sql_schema *s, sql_trigger *tri)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_trigger %s %s\n", s->base.name, tri->base.name);

	return sql_trans_drop_trigger(m->session->tr, s, tri->base.id, DROP_RESTRICT);
}


sql_table *
mvc_create_table(mvc *m, sql_schema *s, const char *name, int tt, bit system, int persistence, int commit_action, int sz, bit properties)
{
	sql_table *t = NULL;
	char *err = NULL;
	int check = 0;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_table %s %s %d %d %d %d %d\n", s->base.name, name, tt, system, persistence, commit_action, (int)properties);

	if (persistence == SQL_DECLARED_TABLE && (!s || strcmp(s->base.name, dt_schema))) {
		t = create_sql_table(m->sa, name, tt, system, persistence, commit_action, properties);
		t->s = s;
	} else {
		t = sql_trans_create_table(m->session->tr, s, name, NULL, tt, system, persistence, commit_action, sz, properties);
		if(t && isPartitionedByExpressionTable(t) && (err = bootstrap_partition_expression(m, m->session->tr->sa, t, 1))) {
			(void) sql_error(m, 02, "%s", err);
			return NULL;
		}
		check = sql_trans_set_partition_table(m->session->tr, t);
		if(check == -1) {
			(void) sql_error(m, 02, SQLSTATE(42000) "CREATE TABLE: %s_%s: the partition's expression is too long", s->base.name, t->base.name);
			return NULL;
		} else if (check) {
			(void) sql_error(m, 02, SQLSTATE(42000) "CREATE TABLE: %s_%s: an internal error occurred", s->base.name, t->base.name);
			return NULL;
		}
	}
	return t;
}

sql_table *
mvc_create_view(mvc *m, sql_schema *s, const char *name, int persistence, const char *sql, bit system)
{
	sql_table *t = NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_view %s %s %s\n", s->base.name, name, sql);

	if (persistence == SQL_DECLARED_TABLE) {
		t = create_sql_table(m->sa, name, tt_view, system, persistence, 0, 0);
		t->s = s;
		t->query = sa_strdup(m->sa, sql);
	} else {
		t = sql_trans_create_table(m->session->tr, s, name, sql, tt_view, system, SQL_PERSIST, 0, 0, 0);
	}
	return t;
}

sql_table *
mvc_create_remote(mvc *m, sql_schema *s, const char *name, int persistence, const char *loc)
{
	sql_table *t = NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_remote %s %s %s\n", s->base.name, name, loc);

	if (persistence == SQL_DECLARED_TABLE) {
		t = create_sql_table(m->sa, name, tt_remote, 0, persistence, 0, 0);
		t->s = s;
		t->query = sa_strdup(m->sa, loc);
	} else {
		t = sql_trans_create_table(m->session->tr, s, name, loc, tt_remote, 0, SQL_REMOTE, 0, 0, 0);
	}
	return t;
}

str
mvc_drop_table(mvc *m, sql_schema *s, sql_table *t, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_table %s %s\n", s->base.name, t->base.name);

	if (isRemote(t)) {
		str AUTHres;
		sql_allocator *sa = m->sa;

		m->sa = sa_create(m->eb);
		if (!m->sa)
			throw(SQL, "sql.mvc_drop_table", SQLSTATE(HY001) MAL_MALLOC_FAIL);
		char *qualified_name = sa_strconcat(m->sa, sa_strconcat(m->sa, t->s->base.name, "."), t->base.name);
		if (!qualified_name) {
			sa_destroy(m->sa);
			m->sa = sa;
			throw(SQL, "sql.mvc_drop_table", SQLSTATE(HY001) MAL_MALLOC_FAIL);
		}

		AUTHres = AUTHdeleteRemoteTableCredentials(qualified_name);
		sa_destroy(m->sa);
		m->sa = sa;

		if(AUTHres != MAL_SUCCEED)
			return AUTHres;
	}

	if(sql_trans_drop_table(m->session->tr, s, t->base.id, drop_action ? DROP_CASCADE_START : DROP_RESTRICT))
		throw(SQL, "sql.mvc_drop_table", SQLSTATE(HY001) MAL_MALLOC_FAIL);
	return MAL_SUCCEED;
}

BUN
mvc_clear_table(mvc *m, sql_table *t)
{
	return sql_trans_clear_table(m->session->tr, t);
}

sql_column *
mvc_create_column_(mvc *m, sql_table *t, const char *name, const char *type, int digits)
{
	sql_subtype tpe;

	if (!sql_find_subtype(&tpe, type, digits, 0))
		return NULL;

	return sql_trans_create_column(m->session->tr, t, name, &tpe);
}

sql_column *
mvc_create_column(mvc *m, sql_table *t, const char *name, sql_subtype *tpe)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_create_column %s %s %s\n", t->base.name, name, tpe->type->sqlname);
	if (t->persistence == SQL_DECLARED_TABLE && (!t->s || strcmp(t->s->base.name, dt_schema))) 
		/* declared tables should not end up in the catalog */
		return create_sql_column(m->sa, t, name, tpe);
	else
		return sql_trans_create_column(m->session->tr, t, name, tpe);
}

int
mvc_drop_column(mvc *m, sql_table *t, sql_column *col, int drop_action)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_column %s %s\n", t->base.name, col->base.name);
	if (col->t->persistence == SQL_DECLARED_TABLE) {
		drop_sql_column(t, col->base.id, drop_action);
		return 0;
	} else
		return sql_trans_drop_column(m->session->tr, t, col->base.id, drop_action ? DROP_CASCADE_START : DROP_RESTRICT);
}

void
mvc_create_dependency(mvc *m, sqlid id, sqlid depend_id, sht depend_type)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_create_dependency %d %d %d\n", id, depend_id, depend_type);
	if ( (id != depend_id) || (depend_type == BEDROPPED_DEPENDENCY) )
		sql_trans_create_dependency(m->session->tr, id, depend_id, depend_type);
}

void
mvc_create_dependencies(mvc *m, list *id_l, sqlid depend_id, sht dep_type)
{
	node *n = id_l->h;
	int i;

	if (mvc_debug)
		fprintf(stderr, "#mvc_create_dependencies on %d of type %d\n", depend_id, dep_type);

	for (i = 0; i < list_length(id_l); i++)
	{
		mvc_create_dependency(m, *(sqlid *) n->data, depend_id, dep_type);
		n = n->next;
	}
}

int
mvc_check_dependency(mvc * m, sqlid id, sht type, list *ignore_ids)
{
	list *dep_list = NULL;

	if (mvc_debug)
		fprintf(stderr, "#mvc_check_dependency on %d\n", id);

	switch(type) {
		case OWNER_DEPENDENCY : 
			dep_list = sql_trans_owner_schema_dependencies(m->session->tr, id);
			break;
		case SCHEMA_DEPENDENCY :
			dep_list = sql_trans_schema_user_dependencies(m->session->tr, id);
			break;
		case TABLE_DEPENDENCY : 
			dep_list = sql_trans_get_dependencies(m->session->tr, id, TABLE_DEPENDENCY, NULL);
			break;
		case VIEW_DEPENDENCY : 
			dep_list = sql_trans_get_dependencies(m->session->tr, id, TABLE_DEPENDENCY, NULL);
			break;
		case FUNC_DEPENDENCY : 
		case PROC_DEPENDENCY :
			dep_list = sql_trans_get_dependencies(m->session->tr, id, FUNC_DEPENDENCY, ignore_ids);
			break;
		default: 
			dep_list =  sql_trans_get_dependencies(m->session->tr, id, COLUMN_DEPENDENCY, NULL);
	}

	if(!dep_list)
		return DEPENDENCY_CHECK_ERROR;

	if ( list_length(dep_list) >= 2 ) {
		list_destroy(dep_list);
		return HAS_DEPENDENCY;
	}

	list_destroy(dep_list);
	return NO_DEPENDENCY;
}

sql_column *
mvc_null(mvc *m, sql_column *col, int isnull)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_null %s %d\n", col->base.name, isnull);

	if (col->t->persistence == SQL_DECLARED_TABLE) {
		col->null = isnull;
		return col;
	}
	return sql_trans_alter_null(m->session->tr, col, isnull);
}

sql_column *
mvc_default(mvc *m, sql_column *col, char *val)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_default %s %s\n", col->base.name, val);

	if (col->t->persistence == SQL_DECLARED_TABLE) {
		col->def = val?sa_strdup(m->sa, val):NULL;
		return col;
	} else {
		return sql_trans_alter_default(m->session->tr, col, val);
	}
}

sql_column *
mvc_drop_default(mvc *m, sql_column *col)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_drop_default %s\n", col->base.name);

	if (col->t->persistence == SQL_DECLARED_TABLE) {
		col->def = NULL;
		return col;
	} else {
		return sql_trans_alter_default(m->session->tr, col, NULL);
	}
}

sql_column *
mvc_storage(mvc *m, sql_column *col, char *storage)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_storage %s %s\n", col->base.name, storage);

	if (col->t->persistence == SQL_DECLARED_TABLE) {
		col->storage_type = storage?sa_strdup(m->sa, storage):NULL;
		return col;
	} else {
		return sql_trans_alter_storage(m->session->tr, col, storage);
	}
}

sql_table *
mvc_access(mvc *m, sql_table *t, sht access)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_access %s %d\n", t->base.name, access);

	if (t->persistence == SQL_DECLARED_TABLE) {
		t->access = access;
		return t;
	}
	return sql_trans_alter_access(m->session->tr, t, access);
}

int 
mvc_is_sorted(mvc *m, sql_column *col)
{
	if (mvc_debug)
		fprintf(stderr, "#mvc_is_sorted %s\n", col->base.name);

	return sql_trans_is_sorted(m->session->tr, col);
}

/* variable management */
static sql_var*
stack_set(mvc *sql, int var, const char *name, sql_subtype *type, sql_rel *rel, sql_table *t, dlist *wdef, int view, int frame)
{
	sql_var *v, *nvars;
	int nextsize = sql->sizevars;
	if (var == nextsize) {
		nextsize <<= 1;
		nvars = RENEW_ARRAY(sql_var,sql->vars,nextsize);
		if(!nvars) {
			return NULL;
		} else {
			sql->vars = nvars;
			sql->sizevars = nextsize;
		}
	}
	v = sql->vars+var;

	v->name = NULL;
	atom_init( &v->a );
	v->rel = rel;
	v->t = t;
	v->view = view;
	v->frame = frame;
	v->visited = 0;
	v->wdef = wdef;
	if (type) {
		int tpe = type->type->localtype;
		VALset(&sql->vars[var].a.data, tpe, (ptr) ATOMnilptr(tpe));
		v->a.tpe = *type;
	}
	if (name) {
		v->name = _STRDUP(name);
		if(!v->name)
			return NULL;
	}
	return v;
}

sql_var*
stack_push_var(mvc *sql, const char *name, sql_subtype *type)
{
	sql_var* res = stack_set(sql, sql->topvars, name, type, NULL, NULL, NULL, 0, 0);
	if(res)
		sql->topvars++;
	return res;
}

sql_var*
stack_push_rel_var(mvc *sql, const char *name, sql_rel *var, sql_subtype *type)
{
	sql_var* res = stack_set(sql, sql->topvars, name, type, var, NULL, NULL, 0, 0);
	if(res)
		sql->topvars++;
	return res;
}

sql_var*
stack_push_table(mvc *sql, const char *name, sql_rel *var, sql_table *t)
{
	sql_var* res = stack_set(sql, sql->topvars, name, NULL, var, t, NULL, 0, 0);
	if(res)
		sql->topvars++;
	return res;
}

sql_var*
stack_push_rel_view(mvc *sql, const char *name, sql_rel *var)
{
	sql_var* res = stack_set(sql, sql->topvars, name, NULL, var, NULL, NULL, 1, 0);
	if(res)
		sql->topvars++;
	return res;
}

sql_var*
stack_push_window_def(mvc *sql, const char *name, dlist *wdef)
{
	sql_var* res = stack_set(sql, sql->topvars, name, NULL, NULL, NULL, wdef, 0, 0);
	if(res)
		sql->topvars++;
	return res;
}

dlist *
stack_get_window_def(mvc *sql, const char *name, int *pos)
{
	for (int i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && sql->vars[i].wdef && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0) {
			if(pos)
				*pos = i;
			return sql->vars[i].wdef;
		}
	}
	return NULL;
}

/* There could a possibility that this is vulnerable to a time-of-check, time-of-use race condition.
 * However this should never happen in the SQL compiler */
char
stack_check_var_visited(mvc *sql, int i)
{
	if(i < 0 || i >= sql->topvars)
		return 0;
	return sql->vars[i].visited;
}

void
stack_set_var_visited(mvc *sql, int i)
{
	if(i < 0 || i >= sql->topvars)
		return;
	sql->vars[i].visited = 1;
}

void
stack_clear_frame_visited_flag(mvc *sql)
{
	for (int i = sql->topvars-1; i >= 0 && !sql->vars[i].frame; i--)
		sql->vars[i].visited = 0;
}

atom *
stack_set_var(mvc *sql, const char *name, ValRecord *v)
{
	int i;
	atom *res = NULL;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0) {
			VALclear(&sql->vars[i].a.data);
			if(VALcopy(&sql->vars[i].a.data, v) == NULL)
				return NULL;
			sql->vars[i].a.isnull = VALisnil(v);
			if (v->vtype == TYPE_flt)
				sql->vars[i].a.d = v->val.fval;
			else if (v->vtype == TYPE_dbl)
				sql->vars[i].a.d = v->val.dval;
			res = &sql->vars[i].a;
		}
	}
	return res;
}

atom *
stack_get_var(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0) {
			return &sql->vars[i].a;
		}
	}
	return NULL;
}

sql_var*
stack_push_frame(mvc *sql, const char *name)
{
	sql_var* res = stack_set(sql, sql->topvars, name, NULL, NULL, NULL, NULL, 0, 1);
	if(res) {
		sql->topvars++;
		sql->frame++;
	}
	return res;
}

void
stack_pop_until(mvc *sql, int top) 
{
	while(sql->topvars > top) {
		sql_var *v = &sql->vars[--sql->topvars];

		c_delete(v->name);
		VALclear(&v->a.data);
		v->a.data.vtype = 0;
		v->wdef = NULL;
	}
}

void 
stack_pop_frame(mvc *sql)
{
	while(!sql->vars[--sql->topvars].frame) {
		sql_var *v = &sql->vars[sql->topvars];

		c_delete(v->name);
		VALclear(&v->a.data);
		v->a.data.vtype = 0;
		if (v->t && v->view)
			table_destroy(v->t);
		else if (v->rel)
			rel_destroy(v->rel);
		v->wdef = NULL;
	}
	if (sql->topvars && sql->vars[sql->topvars].name)  
		c_delete(sql->vars[sql->topvars].name);
	sql->frame--;
}

sql_subtype *
stack_find_type(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && !sql->vars[i].view && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return &sql->vars[i].a.tpe;
	}
	return NULL;
}

sql_table *
stack_find_table(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && !sql->vars[i].view && sql->vars[i].t
			&& sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return sql->vars[i].t;
	}
	return NULL;
}

sql_rel *
stack_find_rel_view(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && sql->vars[i].view &&
		    sql->vars[i].rel && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return rel_dup(sql->vars[i].rel);
	}
	return NULL;
}

void 
stack_update_rel_view(mvc *sql, const char *name, sql_rel *view)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && sql->vars[i].view &&
		    sql->vars[i].rel && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0) {
			rel_destroy(sql->vars[i].rel);
			sql->vars[i].rel = view;
		}
	}
}

int 
stack_find_var(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && !sql->vars[i].view && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return 1;
	}
	return 0;
}

sql_rel *
stack_find_rel_var(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (!sql->vars[i].frame && !sql->vars[i].view &&
		    sql->vars[i].rel && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return rel_dup(sql->vars[i].rel);
	}
	return NULL;
}

int 
frame_find_var(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0 && !sql->vars[i].frame; i--) {
		if (sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return 1;
	}
	return 0;
}

int
stack_find_frame(mvc *sql, const char *name)
{
	int i, frame = sql->frame;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (sql->vars[i].frame) 
			frame--;
		else if (sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return frame;
	}
	return 0;
}

int
stack_has_frame(mvc *sql, const char *name)
{
	int i;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (sql->vars[i].frame && sql->vars[i].name && strcmp(sql->vars[i].name, name)==0)
			return 1;
	}
	return 0;
}

int
stack_nr_of_declared_tables(mvc *sql)
{
	int i, dt = 0;

	for (i = sql->topvars-1; i >= 0; i--) {
		if (sql->vars[i].rel && !sql->vars[i].view) {
			sql_var *v = &sql->vars[i];
			if (v->t)
				dt++;
		}
	}
	return dt;
}

str
stack_set_string(mvc *sql, const char *name, const char *val)
{
	atom *a = stack_get_var(sql, name);
	str new_val = _STRDUP(val);

	if (a != NULL && new_val != NULL) {
		ValRecord *v = &a->data;

		if (v->val.sval)
			_DELETE(v->val.sval);
		v->val.sval = new_val;
		return new_val;
	} else if(new_val) {
		_DELETE(new_val);
	}
	return NULL;
}

str
stack_get_string(mvc *sql, const char *name)
{
	atom *a = stack_get_var(sql, name);

	if (!a || a->data.vtype != TYPE_str)
		return NULL;
	return a->data.val.sval;
}

void
#ifdef HAVE_HGE
stack_set_number(mvc *sql, const char *name, hge val)
#else
stack_set_number(mvc *sql, const char *name, lng val)
#endif
{
	atom *a = stack_get_var(sql, name);

	if (a != NULL) {
		ValRecord *v = &a->data;
#ifdef HAVE_HGE
		if (v->vtype == TYPE_hge) 
			v->val.hval = val;
#endif
		if (v->vtype == TYPE_lng) 
			v->val.lval = val;
		if (v->vtype == TYPE_int) 
			v->val.lval = (int) val;
		if (v->vtype == TYPE_sht) 
			v->val.lval = (sht) val;
		if (v->vtype == TYPE_bte) 
			v->val.lval = (bte) val;
		if (v->vtype == TYPE_bit) {
			if (val)
				v->val.btval = 1;
			else 
				v->val.btval = 0;
		}
	}
}

#ifdef HAVE_HGE
hge
#else
lng
#endif
val_get_number(ValRecord *v) 
{
	if (v != NULL) {
#ifdef HAVE_HGE
		if (v->vtype == TYPE_hge) 
			return v->val.hval;
#endif
		if (v->vtype == TYPE_lng) 
			return v->val.lval;
		if (v->vtype == TYPE_int) 
			return v->val.ival;
		if (v->vtype == TYPE_sht) 
			return v->val.shval;
		if (v->vtype == TYPE_bte) 
			return v->val.btval;
		if (v->vtype == TYPE_bit) 
			if (v->val.btval)
				return 1;
		return 0;
	}
	return 0;
}

#ifdef HAVE_HGE
hge
#else
lng
#endif
stack_get_number(mvc *sql, const char *name)
{
	atom *a = stack_get_var(sql, name);
	return val_get_number(a?&a->data:NULL);
}

sql_column *
mvc_copy_column( mvc *m, sql_table *t, sql_column *c)
{
	return sql_trans_copy_column(m->session->tr, t, c);
}

sql_key *
mvc_copy_key(mvc *m, sql_table *t, sql_key *k)
{
	return sql_trans_copy_key(m->session->tr, t, k);
}

sql_idx *
mvc_copy_idx(mvc *m, sql_table *t, sql_idx *i)
{
	return sql_trans_copy_idx(m->session->tr, t, i);
}

sql_trigger *
mvc_copy_trigger(mvc *m, sql_table *t, sql_trigger *tr)
{
	return sql_trans_copy_trigger(m->session->tr, t, tr);
}

sql_part *
mvc_copy_part(mvc *m, sql_table *t, sql_part *pt)
{
	return sql_trans_copy_part(m->session->tr, t, pt);
}

static inline int dlist_cmp(mvc *sql, dlist *l1, dlist *l2);

static inline int
dnode_cmp(mvc *sql, dnode *d1, dnode *d2)
{
	if (d1 == d2)
		return 0;

	if (!d1 || !d2)
		return -1;

	if (d1->type == d2->type) {
		switch (d1->type) {
			case type_int:
				return (d1->data.i_val - d2->data.i_val);
			case type_lng: {
				lng c = d1->data.l_val - d2->data.l_val;
				assert((lng) GDK_int_min <= c && c <= (lng) GDK_int_max);
				return (int) c;
			}
			case type_string:
				if (d1->data.sval == d2->data.sval)
					return 0;
				if (!d1->data.sval || !d2->data.sval)
					return -1;
				return strcmp(d1->data.sval, d2->data.sval);
			case type_list:
				return dlist_cmp(sql, d1->data.lval, d2->data.lval);
			case type_symbol:
				return symbol_cmp(sql, d1->data.sym, d2->data.sym);
			case type_type:
				return subtype_cmp(&d1->data.typeval, &d2->data.typeval);
			default:
				assert(0);
		}
	}
	return -1;
}

static inline int
dlist_cmp(mvc *sql, dlist *l1, dlist *l2)
{
	int res = 0;
	dnode *d1, *d2;

	if (l1 == l2)
		return 0;

	if (!l1 || !l2 || dlist_length(l1) != dlist_length(l2))
		return -1;

	for (d1 = l1->h, d2 = l2->h; !res && d1; d1 = d1->next, d2 = d2->next) {
		res = dnode_cmp(sql, d1, d2);
	}
	return res;
}

static inline int
AtomNodeCmp(AtomNode *a1, AtomNode *a2)
{
	if (a1 == a2)
		return 0;
	if (!a1 || !a2)
		return -1;
	if (a1->a && a2->a)
		return atom_cmp(a1->a, a2->a);
	return -1;
}

static inline int
SelectNodeCmp(mvc *sql, SelectNode *s1, SelectNode *s2)
{
	if (s1 == s2)
		return 0;
	if (!s1 || !s2)
		return -1;

	if (symbol_cmp(sql, s1->limit, s2->limit) == 0 &&
		symbol_cmp(sql, s1->offset, s2->offset) == 0 &&
		symbol_cmp(sql, s1->sample, s2->sample) == 0 &&
		s1->distinct == s2->distinct &&
		s1->lateral == s2->lateral &&
		symbol_cmp(sql, s1->name, s2->name) == 0 &&
		symbol_cmp(sql, s1->orderby, s2->orderby) == 0 &&
		symbol_cmp(sql, s1->having, s2->having) == 0 &&
		symbol_cmp(sql, s1->groupby, s2->groupby) == 0 &&
		symbol_cmp(sql, s1->where, s2->where) == 0 &&
		symbol_cmp(sql, s1->from, s2->from) == 0 &&
		symbol_cmp(sql, s1->window, s2->window) == 0 &&
		dlist_cmp(sql, s1->selection, s2->selection) == 0)
		return 0;
	return -1;
}

static inline int
_symbol_cmp(mvc *sql, symbol *s1, symbol *s2)
{
	if (s1 == s2)
		return 0;
	if (!s1 || !s2)
		return -1;
	if (s1->token != s2->token || s1->type != s2->type)
		return -1;
	switch (s1->type) {
		case type_int:
			return (s1->data.i_val - s2->data.i_val);
		case type_lng: {
			lng c = s1->data.l_val - s2->data.l_val;
			assert((lng) GDK_int_min <= c && c <= (lng) GDK_int_max);
			return (int) c;
		}
		case type_string:
			if (s1->data.sval == s2->data.sval)
				return 0;
			if (!s1->data.sval || !s2->data.sval)
				return -1;
			return strcmp(s1->data.sval, s2->data.sval);
		case type_list: {
			if (s1->token == SQL_IDENT) {
				atom *at1, *at2;

				if (s2->token != SQL_IDENT)
					return -1;
				at1 = sql_bind_arg(sql, s1->data.lval->h->data.i_val);
				at2 = sql_bind_arg(sql, s2->data.lval->h->data.i_val);
				return atom_cmp(at1, at2);
			} else {
				return dlist_cmp(sql, s1->data.lval, s2->data.lval);
			}
		}
		case type_type:
			return subtype_cmp(&s1->data.typeval, &s2->data.typeval);
		case type_symbol:
			if (s1->token == SQL_SELECT) {
				if (s2->token != SQL_SELECT)
					return -1;
				return SelectNodeCmp(sql, (SelectNode *) s1, (SelectNode *) s2);
			} else if (s1->token == SQL_ATOM) {
				if (s2->token != SQL_ATOM)
					return -1;
				return AtomNodeCmp((AtomNode *) s1, (AtomNode *) s2);
			} else {
				return symbol_cmp(sql, s1->data.sym, s2->data.sym);
			}
		default:
			assert(0);
	}
	return 0;		/* never reached, just to pacify compilers */
}

int
symbol_cmp(mvc *sql, symbol *s1, symbol *s2)
{
	return _symbol_cmp(sql, s1, s2);
}
