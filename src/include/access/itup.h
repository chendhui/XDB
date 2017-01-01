/*-------------------------------------------------------------------------
 *
 * itup.h
 *	  POSTGRES index tuple definitions.
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/itup.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef ITUP_H
#define ITUP_H

#include "access/tupdesc.h"
#include "access/tupmacs.h"
#include "storage/bufpage.h"
#include "storage/itemptr.h"

/*
 * Index tuple header structure
 *
 * All index tuples start with IndexTupleData.  If the HasNulls bit is set,
 * this is followed by an IndexAttributeBitMapData.  The index attribute
 * values follow, beginning at a MAXALIGN boundary.
 *
 * Note that the space allocated for the bitmap does not vary with the number
 * of attributes; that is because we don't have room to store the number of
 * attributes in the header.  Given the MAXALIGN constraint there's no space
 * savings to be had anyway, for usual values of INDEX_MAX_KEYS.
 */

typedef struct IndexTupleData
{
	ItemPointerData t_tid;		/* reference TID to heap tuple */

	/* ---------------
	 * t_info is laid out in the following fashion:
	 *
	 * 15th (high) bit: has nulls
	 * 14th bit: has var-width attributes
	 * 13th bit: has tuple count (for btree)
	 * 12-0 bit: size of tuple
	 * ---------------
	 */

	unsigned short t_info;		/* various info about tuple */

} IndexTupleData;				/* MORE DATA FOLLOWS AT END OF STRUCT */

typedef IndexTupleData *IndexTuple;

typedef struct IndexAttributeBitMapData
{
	bits8		bits[(INDEX_MAX_KEYS + 8 - 1) / 8];
}	IndexAttributeBitMapData;

typedef IndexAttributeBitMapData *IndexAttributeBitMap;

typedef struct IndexTupleCountData {
	uint64		tuple_count;	
}	IndexTupleCountData;

typedef IndexTupleCountData *IndexTupleCount;

/*
 * t_info manipulation macros
 */
#define INDEX_SIZE_MASK 0x1FFF
#define INDEX_HAS_TUPLE_COUNT 0x2000
#define INDEX_VAR_MASK	0x4000
#define INDEX_NULL_MASK 0x8000

#define IndexTupleSize(itup)		((Size) (((IndexTuple) (itup))->t_info & INDEX_SIZE_MASK))
#define IndexTupleDSize(itup)		((Size) ((itup).t_info & INDEX_SIZE_MASK))
#define IndexTupleHasTupleCount(itup)	((((IndexTuple) (itup))->t_info & INDEX_HAS_TUPLE_COUNT))
#define IndexTupleHasNulls(itup)	((((IndexTuple) (itup))->t_info & INDEX_NULL_MASK))
#define IndexTupleHasVarwidths(itup) ((((IndexTuple) (itup))->t_info & INDEX_VAR_MASK))


/*
 * Takes an infomask as argument (primarily because this needs to be usable
 * at index_form_tuple time so enough space is allocated).
 */
#define IndexInfoFindDataOffset(t_info) \
((Size) MAXALIGN(	\
	MAXALIGN64(	\
		sizeof(IndexTupleData) +	\
		(((t_info) & INDEX_NULL_MASK) ? sizeof(IndexAttributeBitMapData) : 0)) +	\
	(((t_info) & INDEX_HAS_TUPLE_COUNT) ? sizeof(IndexTupleCountData) : 0) \
))

#define IndexInfoFindTupleCountOffset(t_info) \
(	\
	(!((t_info) & INDEX_NULL_MASK)) ?	\
	(	\
		MAXALIGN64(sizeof(IndexTupleData))	\
	) :	\
	(	\
		MAXALIGN64(sizeof(IndexTupleData) + sizeof(IndexAttributeBitMapData))	\
	)	\
)

/* ----------------
 *		index_getattr
 *
 *		This gets called many times, so we macro the cacheable and NULL
 *		lookups, and call nocache_index_getattr() for the rest.
 *
 * ----------------
 */
#define index_getattr(tup, attnum, tupleDesc, isnull) \
( \
	AssertMacro(PointerIsValid(isnull) && (attnum) > 0), \
	*(isnull) = false, \
	!IndexTupleHasNulls(tup) ? \
	( \
		(tupleDesc)->attrs[(attnum)-1]->attcacheoff >= 0 ? \
		( \
			fetchatt((tupleDesc)->attrs[(attnum)-1], \
			(char *) (tup) + IndexInfoFindDataOffset((tup)->t_info) \
			+ (tupleDesc)->attrs[(attnum)-1]->attcacheoff) \
		) \
		: \
			nocache_index_getattr((tup), (attnum), (tupleDesc)) \
	) \
	: \
	( \
		(att_isnull((attnum)-1, (char *)(tup) + sizeof(IndexTupleData))) ? \
		( \
			*(isnull) = true, \
			(Datum)NULL \
		) \
		: \
		( \
			nocache_index_getattr((tup), (attnum), (tupleDesc)) \
		) \
	) \
)

/*
 * MaxIndexTuplesPerPage is an upper bound on the number of tuples that can
 * fit on one index page.  An index tuple must have either data or a null
 * bitmap or a tuple count, so we can safely assume it's at least 1 byte 
 * bigger than a bare IndexTupleData struct.  We arrive at the divisor 
 * because each tuple must be maxaligned, and it must have an associated 
 * item pointer.
 */
#define MaxIndexTuplesPerPage	\
	((int) ((BLCKSZ - SizeOfPageHeaderData) / \
			(MAXALIGN(sizeof(IndexTupleData) + 1) + sizeof(ItemIdData))))


/* routines in indextuple.c */
extern IndexTuple index_form_tuple(TupleDesc tupleDescriptor,
				 Datum *values, bool *isnull);
extern IndexTuple index_form_tuple_with_tuple_count(TupleDesc tupleDescriptor,
				 Datum *values, bool *isnull, uint64 tuple_count);
extern Datum nocache_index_getattr(IndexTuple tup, int attnum,
					  TupleDesc tupleDesc);
extern void index_deform_tuple(IndexTuple tup, TupleDesc tupleDescriptor,
				   Datum *values, bool *isnull);
extern IndexTuple CopyIndexTuple(IndexTuple source);

extern IndexTuple CopyIndexTupleAndSetTupleCount(IndexTuple source, uint64 tuple_count);

#ifndef PG_USE_INLINE
extern uint64 index_tuple_get_count(IndexTuple tup);
extern void index_tuple_set_count(IndexTuple tup, uint64 tuple_count);
extern void index_tuple_add_count(IndexTuple tup, uint64 delta_tuple_count);
#endif

#if defined(PG_USE_INLINE) || defined(INDEX_TUPLE_INCLUDE_DEFINITIONS)
STATIC_IF_INLINE uint64 index_tuple_get_count(IndexTuple tup) {
	Assert(IndexTupleHasTupleCount(tup));
	return *(uint64*)((char*) tup + IndexInfoFindTupleCountOffset(tup->t_info));
}

STATIC_IF_INLINE void index_tuple_set_count(IndexTuple tup, uint64 tuple_count) {
	Assert(IndexTupleHasTupleCount(tup));
	*(uint64*)((char*) tup + IndexInfoFindTupleCountOffset(tup->t_info)) = tuple_count;
}

STATIC_IF_INLINE void index_tuple_add_count(IndexTuple tup, uint64 delta_tuple_count) {
	Assert(IndexTupleHasTupleCount(tup));
	*(uint64*)((char*) tup + IndexInfoFindTupleCountOffset(tup->t_info)) += delta_tuple_count;
}
#endif

#endif   /* ITUP_H */
