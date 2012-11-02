/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "updates/changesets.h"
#include "libknot/common.h"
#include "rrset.h"

static const size_t KNOT_CHANGESET_COUNT = 5;
static const size_t KNOT_CHANGESET_STEP = 5;
static const size_t KNOT_CHANGESET_RRSET_COUNT = 5;
static const size_t KNOT_CHANGESET_RRSET_STEP = 5;

/*----------------------------------------------------------------------------*/

static int knot_changeset_check_count(knot_rrset_t ***rrsets, size_t count,
                                        size_t *allocated)
{
	/* Check if allocated is sufficient. */
	if (count <= *allocated) {
		return KNOT_EOK;
	}

	/* How many steps is needed to content count? */
	size_t extra = (count - *allocated) % KNOT_CHANGESET_RRSET_STEP;
	extra = (extra + 1) * KNOT_CHANGESET_RRSET_STEP;

	/* Allocate new memory block. */
	const size_t item_len = sizeof(knot_rrset_t *);
	const size_t new_count = *allocated + extra;
	knot_rrset_t **rrsets_new = malloc(new_count * item_len);
	if (rrsets_new == NULL) {
		return KNOT_ENOMEM;
	}

	/* Clear old memory block and copy old data. */
	memset(rrsets_new, 0, new_count * item_len);
	memcpy(rrsets_new, *rrsets, (*allocated) * item_len);

	/* Replace old rrsets. */
	free(*rrsets);
	*rrsets = rrsets_new;
	*allocated = new_count;

	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/

static int knot_changeset_rrsets_match(const knot_rrset_t *rrset1,
                                         const knot_rrset_t *rrset2)
{
	return knot_rrset_compare(rrset1, rrset2, KNOT_RRSET_COMPARE_HEADER)
	       && (knot_rrset_type(rrset1) != KNOT_RRTYPE_RRSIG
	           || knot_rdata_rrsig_type_covered(
	                    knot_rrset_rdata(rrset1))
	              == knot_rdata_rrsig_type_covered(
	                    knot_rrset_rdata(rrset2)));
}

/*----------------------------------------------------------------------------*/

int knot_changeset_allocate(knot_changesets_t **changesets,
                            uint32_t flags)
{
	// create new changesets
	*changesets = (knot_changesets_t *)(malloc(sizeof(knot_changesets_t)));
	if (*changesets == NULL) {
		return KNOT_ENOMEM;
	}

	memset(*changesets, 0, sizeof(knot_changesets_t));
	(*changesets)->flags = flags;

	if (knot_changesets_check_size(*changesets) != KNOT_EOK) {
		free(*changesets);
		*changesets = NULL;
		return KNOT_ENOMEM;
	}
	
	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/

int knot_changeset_add_rrset(knot_rrset_t ***rrsets,
                              size_t *count, size_t *allocated,
                              knot_rrset_t *rrset)
{
	int ret = knot_changeset_check_count(rrsets, *count + 1, allocated);
	if (ret != KNOT_EOK) {
		return ret;
	}

	(*rrsets)[*count] = rrset;
	*count = *count + 1;

	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/

int knot_changeset_add_rr(knot_rrset_t ***rrsets, size_t *count,
                           size_t *allocated, knot_rrset_t *rr)
{
	// try to find the RRSet in the list of RRSets, but search backwards
	// as it is probable that the last RRSet is the one to which the RR
	// belongs

	// Just check the last RRSet. If the RR belongs to it, merge it,
	// otherwise just add the RR to the end of the list

	if (*count > 0
	    && knot_changeset_rrsets_match((*rrsets)[*count - 1], rr)) {
		// Create changesets exactly as they came, with possibly
		// duplicate records
		if (knot_rrset_merge((void **)&(*rrsets)[*count - 1],
		                     (void **)&rr) != KNOT_EOK) {
			return KNOT_ERROR;
		}

		knot_rrset_free(&rr);
		return KNOT_EOK;
	} else {
		return knot_changeset_add_rrset(rrsets, count, allocated, rr);
	}
}

/*----------------------------------------------------------------------------*/

int knot_changeset_add_new_rr(knot_changeset_t *changeset,
                               knot_rrset_t *rrset,
                               knot_changeset_part_t part)
{
	knot_rrset_t ***rrsets = NULL;
	size_t *count = NULL;
	size_t *allocated = NULL;

	switch (part) {
	case KNOT_CHANGESET_ADD:
		rrsets = &changeset->add;
		count = &changeset->add_count;
		allocated = &changeset->add_allocated;
		break;
	case KNOT_CHANGESET_REMOVE:
		rrsets = &changeset->remove;
		count = &changeset->remove_count;
		allocated = &changeset->remove_allocated;
		break;
	default:
		assert(0);
	}

	assert(rrsets != NULL);
	assert(count != NULL);
	assert(allocated != NULL);

	int ret = knot_changeset_add_rr(rrsets, count, allocated, rrset);
	if (ret != KNOT_EOK) {
		return ret;
	}

	return ret;
}

/*----------------------------------------------------------------------------*/

knot_rrset_t *knot_changeset_remove_rr(knot_rrset_t **rrsets, size_t *count,
                                       int pos)
{
	if (pos >= *count || *count == 0) {
		return NULL;
	}

	knot_rrset_t *removed = rrsets[pos];

	// shift all RRSets from pos+1 one cell to the left
	for (int i = pos; i < *count - 1; ++i) {
		rrsets[i] = rrsets[i + 1];
	}

	// just to be sure, set the last previously occupied position to NULL
	rrsets[*count - 1] = NULL;
	*count -= 1;

	return removed;
}

/*----------------------------------------------------------------------------*/

void knot_changeset_store_soa(knot_rrset_t **chg_soa,
                               uint32_t *chg_serial, knot_rrset_t *soa)
{
	*chg_soa = soa;
	*chg_serial = knot_rdata_soa_serial(knot_rrset_rdata(soa));
}

/*----------------------------------------------------------------------------*/

int knot_changeset_add_soa(knot_changeset_t *changeset, knot_rrset_t *soa,
                            knot_changeset_part_t part)
{
	switch (part) {
	case KNOT_CHANGESET_ADD:
		knot_changeset_store_soa(&changeset->soa_to,
		                          &changeset->serial_to, soa);
		break;
	case KNOT_CHANGESET_REMOVE:
		knot_changeset_store_soa(&changeset->soa_from,
		                          &changeset->serial_from, soa);
		break;
	default:
		assert(0);
	}

	/*! \todo Remove return value? */
	return KNOT_EOK;
}

/*---------------------------------------------------------------------------*/

int knot_changesets_check_size(knot_changesets_t *changesets)
{
	/* Check if allocated is sufficient. */
	if (changesets->count < changesets->allocated) {
		return KNOT_EOK;
	}

	/* How many steps is needed to content count? */
	size_t extra = (changesets->count - changesets->allocated)
	                % KNOT_CHANGESET_STEP;
	extra = (extra + 1) * KNOT_CHANGESET_STEP;

	/* Allocate new memory block. */
	const size_t item_len = sizeof(knot_changeset_t);
	size_t new_count = (changesets->allocated + extra);
	knot_changeset_t *sets = malloc(new_count * item_len);
	if (sets == NULL) {
		return KNOT_ENOMEM;
	}

	/* Clear new memory block and copy old data. */
	memset(sets, 0, new_count * item_len);
	memcpy(sets, changesets->sets, changesets->allocated * item_len);

	/* Set type to all newly allocated changesets. */
	for (int i = changesets->allocated; i < new_count; ++i) {
		sets[i].flags = changesets->flags;
	}

	/* Replace old changesets. */
	free(changesets->sets);
	changesets->sets = sets;
	changesets->allocated = new_count;

	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/

void knot_changeset_set_flags(knot_changeset_t *changeset,
                             uint32_t flags)
{
	changeset->flags = flags;
}

/*----------------------------------------------------------------------------*/

uint32_t knot_changeset_flags(knot_changeset_t *changeset)
{
	return changeset->flags;
}

/*----------------------------------------------------------------------------*/

void knot_free_changeset(knot_changeset_t **changeset)
{
	assert((*changeset)->add_allocated >= (*changeset)->add_count);
	assert((*changeset)->remove_allocated >= (*changeset)->remove_count);
	assert((*changeset)->allocated >= (*changeset)->size);

	int j;
	for (j = 0; j < (*changeset)->add_count; ++j) {
		knot_rrset_deep_free(&(*changeset)->add[j], 1, 1, 1);
	}
	free((*changeset)->add);

	for (j = 0; j < (*changeset)->remove_count; ++j) {
		knot_rrset_deep_free(&(*changeset)->remove[j], 1, 1, 1);
	}
	free((*changeset)->remove);

	knot_rrset_deep_free(&(*changeset)->soa_from, 1, 1, 1);
	knot_rrset_deep_free(&(*changeset)->soa_to, 1, 1, 1);

	free((*changeset)->data);


	*changeset = NULL;
}

/*----------------------------------------------------------------------------*/

void knot_free_changesets(knot_changesets_t **changesets)
{
	if (changesets == NULL || *changesets == NULL) {
		return;
	}

	assert((*changesets)->allocated >= (*changesets)->count);

	for (int i = 0; i < (*changesets)->count; ++i) {
		knot_changeset_t *ch = &(*changesets)->sets[i];
		knot_free_changeset(&ch);
	}

	free((*changesets)->sets);
	
	knot_rrset_deep_free(&(*changesets)->first_soa, 1, 1, 1);
	
	assert((*changesets)->changes == NULL);

	free(*changesets);
	*changesets = NULL;
}
