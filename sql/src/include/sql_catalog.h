#ifndef SQL_CATALOG_H
#define SQL_CATALOG_H

#include <sql_mem.h>
#include <sql_list.h>
#include <stream.h>

#define SCALE_NONE	0
#define SCALE_FIX	1
#define SCALE_NOFIX	2
#define SCALE_ADD	3
#define SCALE_SUB	4
#define DIGITS_ADD	5
#define INOUT		6 	/* output type equals input type */

#define sql_max(i1,i2) ((i1)<(i2))?(i2):(i1)

typedef int sqlid;

typedef struct sql_base {
	int wtime;
	int rtime;
	int flag;
	sqlid id;
	char *name;
} sql_base;

typedef struct sql_type {
	sql_base base;

	char *sqlname;
	unsigned int digits;
	unsigned int scale; /* indicates how scale is used in functions */
	int localtype; 	/* localtype, need for coersions */
	unsigned char radix;
	unsigned char fixed;
} sql_type;

typedef struct sql_alias {
	char *name;
	char *alias;
} sql_alias;

typedef struct sql_subtype {
	sql_ref ref;

	sql_type *type;
	unsigned int digits;
	unsigned int scale;
	unsigned char fixed;
} sql_subtype;

typedef struct sql_aggr {
	sql_ref ref;

	char *name;
	char *imp;
	sql_subtype *tpe;
	sql_subtype *res;
	int nr;
} sql_aggr;

typedef struct sql_subaggr {
	sql_ref ref;

	sql_aggr *aggr;
	sql_subtype *res;
} sql_subaggr;

/* sql_func need type transform rules
 * types are equal if underlying types are equal +
	scale is equal
 * if types do not mach we try type conversions
 * 	which means for simple 1 arg functions
 *
 *
 */

typedef struct sql_arg {
	char *name;
	sql_subtype *type;
} sql_arg;

typedef struct sql_func {
	sql_ref ref;

	char *name;
	char *imp;
	list *ops; /* param list */
	sql_subtype *res;
		   /* res->scale
		      SCALE_NOFIX/SCALE_NONE => nothing
		      SCALE_FIX => input scale fixing,
		      SCALE_ADD => leave inputs as is and do add scales
					example numerical multiplication
		      SCALE_SUB => first input scale, fix with second scale
				   result scale is equal to first input
					example numerical division
		      DIGITS_ADD => result digits, sum of args
					example string concat
		    */
	int nr;
	int sql; /* simple sql or native implementation */
} sql_func;

typedef struct sql_subfunc {
	sql_ref ref;

	sql_func *func;
	sql_subtype *res;
} sql_subfunc;

#define TR_OLD 0
#define TR_NEW 1

/*
 * name bats using schema_table_column etc.
 * 
 * support multiple db's using multiple servers, requires a shallow connection setup
 * server, which communicates using shmem or pipes. 
 */

#define cur_user 1
#define cur_role 2

/* TODO keep a single list of sql_bats with each table 
 * also change D_* to use the sql_bat
 */
typedef struct sql_bat {
	oid bid;
	oid ibid;	/* insert bat ! */
	oid ubid;     	/* bat with updates */
} sql_bat;

typedef enum key_type {
	pkey,
	ukey,
	fkey
} key_type;

typedef struct sql_kc {
	struct sql_column *c;
	int trunc; /* 0 not truncated, >0 colum is truncated */
} sql_kc;

typedef enum idx_type {
	unique,
	join_idx,
	new_idx_types
} idx_type;

typedef struct sql_idx { 
	sql_base base;
	idx_type type; 		/* unique */
	struct list *columns; 	/* list of sql_kc */
	struct sql_table *t;
	sql_bat bat;
	struct sql_key *key; 		/* key */
} sql_idx;

/* fkey consists of two of these */
typedef struct sql_key { /* pkey, ukey, fkey */
	sql_base base;
	key_type type; 		/* pkey, ukey, fkey */
	sql_idx	*idx;		/* idx to accelerate key check */

	struct list *columns; 	/* list of sql_kc */
	struct sql_table *t;
} sql_key;

typedef struct sql_ukey { /* pkey, ukey */
	sql_key k;
	list *keys;
} sql_ukey;

typedef struct sql_fkey { /* fkey */
	sql_key k;
	struct sql_ukey *rkey; /* only set for fkey and rkey */
} sql_fkey;

typedef struct sql_column {
	sql_base base;
	sql_subtype *type;
	int colnr;
	bit null;
	char *def;

	struct sql_table *t;
	sql_bat bat;

} sql_column;

typedef struct changeset {
	fdestroy destroy;
	struct list *set;
	struct list *dset;
	node *nelm;
} changeset;

typedef struct sql_table {
	sql_base base;
	bit 	 table; 	/* table or view */
	bit	 system;	/* system or user table */
	bit	 persists;	/* persistent or temporary table */
	bit	 clear;		/* clear on transaction boundaries ? */
				/* clear on a temp table == session table! */
	char*    query;
	int      sz;

	changeset columns;
	changeset idxs;
	changeset keys;
	sql_ukey *pkey;

	oid dbid;     	 /* bat with deletes */

	struct sql_schema *s;
} sql_table;

typedef struct sql_schema {
	sql_base base;
	int auth_id;

	changeset tables;
	list *keys; 	/* the names for keys and idxs are global, but */	
	list *idxs;	/* these objects are only useful within a table */
} sql_schema;

typedef struct sql_module {
	sql_base base;
	changeset types;
} sql_module;

typedef struct res_col {
	char *tn;
	char *name;
	sql_subtype *type;
	bat b;
	int mtype;
	ptr *p;
} res_col;

typedef struct res_table {
	int id; 
	int query_type;
	int nr_cols; 
	int cur_col;
	res_col *cols;
	bat order;
	struct res_table *next;
} res_table;


#endif /* SQL_CATALOG_H */
