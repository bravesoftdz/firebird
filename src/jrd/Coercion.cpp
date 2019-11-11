/*
 *      PROGRAM:        JRD access method
 *      MODULE:         Coercion.cpp
 *      DESCRIPTION:    Automatically coercing user datatypes
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2019 Alex Peshkov <peshkoff@mail.ru>
 *  and all contributors signed below.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 *
 */

#include "firebird.h"
#include "../jrd/Coercion.h"

#include "../dsql/dsql.h"
#include "../dsql/make_proto.h"
#include "../jrd/align.h"

using namespace Jrd;
using namespace Firebird;

static const USHORT FROM_MASK = FLD_has_len | FLD_has_chset | FLD_has_scale;
static const USHORT TO_MASK = FLD_has_len | FLD_has_chset | FLD_has_scale | FLD_legacy | FLD_native;

bool CoercionArray::coerce(dsc* d) const
{
	// move down through array to ensure correct order: newer rule overrides older one
	for (unsigned n = getCount(); n--;)
	{
		if (getElement(n).coerce(d))
			return true;
	}

	return false;
}

void CoercionRule::setRule(TypeClause* from, TypeClause *to)
{
	fromMask = from->flags & FROM_MASK;
	DsqlDescMaker::fromField(&fromDsc, from);

	toMask = to->flags & TO_MASK;
	DsqlDescMaker::fromField(&toDsc, to);
}

bool CoercionRule::match(dsc* d) const
{
	bool found = false;

	// check for exact match (taking flags into an account)
	if ((!found) &&
		(d->dsc_dtype == fromDsc.dsc_dtype) &&
		((d->dsc_length == fromDsc.dsc_length) || (!(fromMask & FLD_has_len))) &&
		((d->getCharSet() == fromDsc.getCharSet()) || (!(fromMask & FLD_has_chset))) &&
		((d->dsc_scale == fromDsc.dsc_scale) || (!(fromMask & FLD_has_scale))))
	{
		found = true;
	}

	// check for inexact datatype match when FLD_has_len is not set
	if ((!found) && (!(fromMask & FLD_has_len)))
	{
		switch(fromDsc.dsc_dtype)
		{
		case dtype_dec64:
		case dtype_dec128:
			if (d->dsc_dtype == dtype_dec64 || d->dsc_dtype == dtype_dec128)
			{
				found = true;
				break;
			}
		}
	}

	return found;
}

bool CoercionRule::coerce(dsc* d) const
{
	// check does descriptor match FROM clause
	if (! match(d))
		return false;

	// native binding - do not touch descriptor at all
	if (toMask & FLD_native)
		return true;

	// process legacy case
	if (toMask & FLD_legacy)
	{
		bool found = true;

		switch(d->dsc_dtype)
		{
		case dtype_dec64:
		case dtype_dec128:
			d->dsc_dtype = dtype_double;
			d->dsc_length = 8;
			break;
		case dtype_sql_time_tz:
			d->dsc_dtype = dtype_sql_time;
			d->dsc_length = 4;
			break;
		case dtype_timestamp_tz:
			d->dsc_dtype = dtype_timestamp;
			d->dsc_length = 8;
			break;
		case dtype_int128:
			d->dsc_dtype = dtype_int64;
			d->dsc_length = 8;
			break;
		case dtype_boolean:
			d->dsc_dtype = dtype_text;
			d->dsc_length = 5;
			break;
		default:
			found = false;
			break;
		}

		return found;
	}

	// Final pass - order is important

	// length
	if (toMask & FLD_has_len)
		d->dsc_length = toDsc.dsc_length;
	else
	{
		if (!type_lengths[toDsc.dsc_dtype])
		{
			fb_assert(toDsc.isText());
			d->dsc_length = d->getStringLength();
		}
		else
			d->dsc_length = type_lengths[toDsc.dsc_dtype];
	}

	// scale
	if (toMask & FLD_has_scale)
		d->dsc_scale = toDsc.dsc_scale;
	else if (!(DTYPE_IS_EXACT(d->dsc_dtype) && DTYPE_IS_EXACT(toDsc.dsc_dtype)))
		d->dsc_scale = 0;

	// type
	d->dsc_dtype = toDsc.dsc_dtype;
	d->dsc_sub_type = toDsc.dsc_sub_type;

	// charset
	if (toMask & FLD_has_chset)
		d->setTextType(toDsc.getTextType());

	return true;
}

