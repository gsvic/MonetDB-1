/* -*-C-*- */

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2020 MonetDB B.V.
 */

#include "monetdb_config.h"
#include "mal.h"
#include "monet_version.h"
#include "mutils.h"
#ifdef HAVE_LIBPCRE
#include <pcre.h>
#endif
#ifdef HAVE_OPENSSL
#include <openssl/opensslconf.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#endif
#ifdef HAVE_LIBXML
#include <libxml/xmlversion.h>
#endif

#define STRING(a)  # a
#define XSTRING(s) STRING(s)

#if defined(HAVE_LIBPCRE) || defined(HAVE_OPENSSL)
static void
print_libversion(const char *lib, const char *rtvers, const char *cmvers)
{
	printf("  %s: %s", lib, rtvers);
	if (strcmp(rtvers, cmvers) != 0)
		printf(" (compiled with %s)", cmvers);
	printf("\n");
}
#endif

void
monet_version(void)
{
	dbl sz_mem_gb;
	int cores;

	MT_init();  /* for MT_pagesize */
	sz_mem_gb = (dbl)(MT_npages() * MT_pagesize()) / (1024.0 * 1024.0 * 1024.0);
	cores = MT_check_nr_cores();

	printf("MonetDB 5 server %s", GDKversion());
#ifdef MONETDB_RELEASE
	printf(" (%s)", MONETDB_RELEASE);
#else
	const char *rev = mercurial_revision();
	if (strcmp(rev, "Unknown") != 0)
		printf(" (hg id: %s)", rev);
#endif
	printf(" (%zu-bit%s)\n",
			(size_t) (sizeof(ptr) * 8),
#ifdef HAVE_HGE
			", 128-bit integers"
#else
			""
#endif
	);
#ifndef MONETDB_RELEASE
	printf("This is an unreleased version\n");
#endif
	printf("Copyright (c) 1993 - July 2008 CWI\n"
	       "Copyright (c) August 2008 - 2020 MonetDB B.V., all rights reserved\n");
	printf("Visit https://www.monetdb.org/ for further information\n");
	printf("Found %.1fGiB available memory, %d available cpu core%s\n",
			sz_mem_gb, cores, cores != 1 ? "s" : "");
	/* don't want to GDKinit just for this
			"using %d thread%s\n",
			GDKnr_threads, GDKnr_threads != 1 ? "s" : ""); */
	printf("Libraries:\n");
#ifdef HAVE_LIBPCRE
	/* PCRE_PRERELEASE may be defined as an empty value.  In order
	 * to get the proper amount of white space between various
	 * parts of the version string on different compilers (none
	 * between minor and prerelease, a single one between that
	 * combination and the date), we need to resort to some
	 * run-time trickery since we can't do it with the
	 * preprocessor */
	print_libversion("libpcre",
			 pcre_version(),
			 XSTRING(Z PCRE_PRERELEASE)[1] == 0
			 ? XSTRING(PCRE_MAJOR.PCRE_MINOR PCRE_DATE)
			 : XSTRING(PCRE_MAJOR.PCRE_MINOR)
			   XSTRING(PCRE_PRERELEASE PCRE_DATE));
#endif
#ifdef HAVE_OPENSSL
	print_libversion("openssl",
#if OPENSSL_VERSION_NUMBER < 0x10100000
			 OPENSSL_VERSION_TEXT,
#else
			 OpenSSL_version(OPENSSL_VERSION),
#endif
			 OPENSSL_VERSION_TEXT);
#endif
#ifdef HAVE_LIBXML
	/* no run-time version available, so only compile time */
	printf("  libxml2: %s\n", LIBXML_DOTTED_VERSION);
#endif
	printf("Compiled by: %s (%s)\n", "victor@Victors-MBP", HOST);
	printf("Compilation: %s\n", "/Library/Developer/CommandLineTools/usr/bin/cc  -Werror -Wall -Wextra -Werror-implicit-function-declaration -Wpointer-arith -Wundef -Wformat=2 -Wno-format-nonliteral -Winit-self -Winvalid-pch -Wmissing-declarations -Wmissing-format-attribute -Wmissing-prototypes -Wno-missing-field-initializers -Wold-style-definition -Wpacked -Wunknown-pragmas -Wvariadic-macros -Wstack-protector -fstack-protector-all -Wmissing-include-dirs -Wnested-externs -Wno-char-subscripts -Wunreachable-code");
	printf("Linking    : %s\n", "/Library/Developer/CommandLineTools/usr/bin/ld");
}
