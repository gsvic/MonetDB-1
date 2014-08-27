
/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 *                * Copyright August 2008-2014 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * (c)2014 author Martin Kersten
 */

#ifndef _MOSAIC_ZONE_
#define _MOSAIC_ZONE_

#include <mal.h>
#include "mal_interpreter.h"
#include "mal_client.h"

// limit the size of a zone based on count 
#define MAXZONESIZE 32*1024

mosaic_export void MOSdump_zone(Client cntxt, MOStask task);
mosaic_export void MOSadvance_zone(Client cntxt, MOStask task);
mosaic_export void MOSskip_zone(Client cntxt, MOStask task);
mosaic_export flt  MOSestimate_zone(Client cntxt, MOStask task);
mosaic_export void MOScompress_zone(Client cntxt, MOStask task);
mosaic_export void MOSdecompress_zone(Client cntxt, MOStask task);
mosaic_export str MOSsubselect_zone(Client cntxt,  MOStask task, void *low, void *hgh, bit *li, bit *hi, bit *anti);
mosaic_export str MOSthetasubselect_zone(Client cntxt,  MOStask task, void *val, str oper);
mosaic_export str MOSleftfetchjoin_zone(Client cntxt,  MOStask task);
mosaic_export str MOSjoin_zone(Client cntxt,  MOStask task);
#endif /* _MOSAIC_ZONE_ */
