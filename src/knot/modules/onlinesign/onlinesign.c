/*  Copyright (C) 2018 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "contrib/string.h"
#include "libdnssec/error.h"
#include "knot/include/module.h"
#include "knot/modules/onlinesign/nsec_next.h"
// Next dependencies force static module!
#include "knot/dnssec/ds_query.h"
#include "knot/dnssec/key-events.h"
#include "knot/dnssec/zone-events.h"
#include "knot/nameserver/query_module.h"
#include "knot/nameserver/process_query.h"

#define MOD_POLICY	"\x06""policy"

int policy_check(knotd_conf_check_args_t *args)
{
	int ret = knotd_conf_check_ref(args);
	if (ret != KNOT_EOK && strcmp((const char *)args->data, "default") == 0) {
		return KNOT_EOK;
	}

	return ret;
}

const yp_item_t online_sign_conf[] = {
	{ MOD_POLICY, YP_TREF, YP_VREF = { C_POLICY }, YP_FNONE, { policy_check } },
	{ NULL }
};

/*!
 * \brief RR types to force in synthesised NSEC maps.
 *
 * We cannot determine the true NSEC bitmap because of dynamic modules which
 * can synthesize some types on-the-fly. The base NSEC map will be determined
 * from zone content and this list of types.
 *
 * The types in the NSEC bitmap really don't have to exist. Only the QTYPE
 * must not be present. This will make the validation work with resolvers
 * performing negative caching.
 *
 * This list should contain all RR types, which can be potentionally
 * synthesized by other modules.
 */
static const uint16_t NSEC_FORCE_TYPES[] = {
	KNOT_RRTYPE_A,
	KNOT_RRTYPE_AAAA,
	0
};

typedef struct {
	knot_time_t event_rollover;
	knot_time_t event_parent_ds_q;
	pthread_mutex_t event_mutex;

	bool zone_doomed;
} online_sign_ctx_t;

static bool want_dnssec(knotd_qdata_t *qdata)
{
	return knot_pkt_has_dnssec(qdata->query);
}

static uint32_t dnskey_ttl(knotd_qdata_t *qdata)
{
	knot_rrset_t soa = knotd_qdata_zone_apex_rrset(qdata, KNOT_RRTYPE_SOA);
	return soa.ttl;
}

static uint32_t nsec_ttl(knotd_qdata_t *qdata)
{
	knot_rrset_t soa = knotd_qdata_zone_apex_rrset(qdata, KNOT_RRTYPE_SOA);
	return knot_soa_minimum(soa.rrs.rdata);
}

/*!
 * \brief Add bitmap records synthesized by online-signing.
 */
static void bitmap_add_synth(dnssec_nsec_bitmap_t *map, bool is_apex)
{
	dnssec_nsec_bitmap_add(map, KNOT_RRTYPE_NSEC);
	dnssec_nsec_bitmap_add(map, KNOT_RRTYPE_RRSIG);
	if (is_apex) {
		dnssec_nsec_bitmap_add(map, KNOT_RRTYPE_DNSKEY);
		//dnssec_nsec_bitmap_add(map, KNOT_RRTYPE_CDS);
	}
}

/*!
 * \brief Add bitmap records present in the zone.
 */
static void bitmap_add_zone(dnssec_nsec_bitmap_t *map, const zone_node_t *node)
{
	if (!node) {
		return;
	}

	for (int i = 0; i < node->rrset_count; i++) {
		dnssec_nsec_bitmap_add(map, node->rrs[i].type);
	}
}

/*!
 * \brief Add bitmap records which can be synthesized by other modules.
 *
 * \param qtype  Current QTYPE, will never be added into the map.
 */
static void bitmap_add_forced(dnssec_nsec_bitmap_t *map, uint16_t qtype)
{
	for (const uint16_t *type = NSEC_FORCE_TYPES; *type; type += 1) {
		if (*type != qtype) {
			dnssec_nsec_bitmap_add(map, *type);
		}
	}
}

/*!
 * \brief Add bitmap records from the actual response.
 */
static void bitmap_add_section(dnssec_nsec_bitmap_t *map, const knot_pktsection_t *answer)
{
	for (int i = 0; i < answer->count; i++) {
		const knot_rrset_t *rr = knot_pkt_rr(answer, i);
		dnssec_nsec_bitmap_add(map, rr->type);
	}
}

/*!
 * \brief Synthesize NSEC type bitmap.
 *
 * - The bitmap will contain types synthesized by this module.
 * - For ANY query, the bitmap will contain types from the actual response.
 * - For non-ANY query, the bitmap will contain types from zone and forced
 *   types which can be potentionally synthesized by other query modules.
 */
static dnssec_nsec_bitmap_t *synth_bitmap(knot_pkt_t *pkt, const knotd_qdata_t *qdata, bool add_forced)
{
	dnssec_nsec_bitmap_t *map = dnssec_nsec_bitmap_new();
	if (!map) {
		return NULL;
	}

	uint16_t qtype = knot_pkt_qtype(qdata->query);
	bool is_apex = qdata->extra->zone
	               && qdata->extra->zone->contents
	               && qdata->extra->node == qdata->extra->zone->contents->apex;

	bitmap_add_synth(map, is_apex);

	if (qtype == KNOT_RRTYPE_ANY) {
		const knot_pktsection_t *answer = knot_pkt_section(pkt, KNOT_ANSWER);
		bitmap_add_section(map, answer);
	} else {
		bitmap_add_zone(map, qdata->extra->node);
		if (add_forced && !node_rrtype_exists(qdata->extra->node, KNOT_RRTYPE_CNAME)) {
			bitmap_add_forced(map, qtype);
		}
	}

	return map;
}

static bool is_deleg(const knot_pkt_t *pkt)
{
	return !knot_wire_get_aa(pkt->wire);
}

static knot_rrset_t *synth_nsec(knot_pkt_t *pkt, knotd_qdata_t *qdata, knot_mm_t *mm)
{
	const knot_dname_t *nsec_owner = is_deleg(pkt) ? qdata->extra->encloser->owner : qdata->name;
	knot_rrset_t *nsec = knot_rrset_new(nsec_owner, KNOT_RRTYPE_NSEC,
	                                    KNOT_CLASS_IN, nsec_ttl(qdata), mm);
	if (!nsec) {
		return NULL;
	}

	knot_dname_t *next = online_nsec_next(nsec_owner, knotd_qdata_zone_name(qdata));
	if (!next) {
		knot_rrset_free(nsec, mm);
		return NULL;
	}

	dnssec_nsec_bitmap_t *bitmap = synth_bitmap(pkt, qdata, !is_deleg(pkt));
	if (!bitmap) {
		free(next);
		knot_rrset_free(nsec, mm);
		return NULL;
	}

	size_t size = knot_dname_size(next) + dnssec_nsec_bitmap_size(bitmap);
	uint8_t rdata[size];

	int written = knot_dname_to_wire(rdata, next, size);
	dnssec_nsec_bitmap_write(bitmap, rdata + written);

	knot_dname_free(next, NULL);
	dnssec_nsec_bitmap_free(bitmap);

	if (knot_rrset_add_rdata(nsec, rdata, size, mm) != KNOT_EOK) {
		knot_rrset_free(nsec, mm);
		return NULL;
	}

	return nsec;
}

static knot_rrset_t *sign_rrset(const knot_dname_t *owner,
                                const knot_rrset_t *cover,
                                knotd_mod_t *mod,
                                knot_mm_t *mm)
{
	// copy of RR set with replaced owner name

	knot_rrset_t *copy = knot_rrset_new(owner, cover->type, cover->rclass,
	                                    cover->ttl, NULL);
	if (!copy) {
		return NULL;
	}

	if (knot_rdataset_copy(&copy->rrs, &cover->rrs, NULL) != KNOT_EOK) {
		knot_rrset_free(copy, NULL);
		return NULL;
	}

	// resulting RRSIG

	knot_rrset_t *rrsig = knot_rrset_new(owner, KNOT_RRTYPE_RRSIG, copy->rclass,
	                                     copy->ttl, mm);
	if (!rrsig) {
		knot_rrset_free(copy, NULL);
		return NULL;
	}

	int ret = knotd_mod_dnssec_sign_rrset(mod, rrsig, copy, mm);
	if (ret != KNOT_EOK) {
		knot_rrset_free(copy, NULL);
		knot_rrset_free(rrsig, mm);
		return NULL;
	}

	knot_rrset_free(copy, NULL);

	return rrsig;
}

static glue_t *find_glue_for(const knot_rrset_t *rr, const knot_pkt_t *pkt)
{
	for (int i = KNOT_ANSWER; i <= KNOT_AUTHORITY; i++) {
		const knot_pktsection_t *section = knot_pkt_section(pkt, i);
		for (int j = 0; j < section->count; j++) {
			const knot_rrset_t *attempt = knot_pkt_rr(section, j);
			const additional_t *a = attempt->additional;
			for (int k = 0; a != NULL && k < a->count; k++) {
				// no need for knot_dname_cmp because the pointers are assigned
				if (a->glues[k].node->owner == rr->owner) {
					return &a->glues[k];
				}
			}
		}
	}
	return NULL;
}

static bool shall_sign_rr(const knot_rrset_t *rr, const knot_pkt_t *pkt)
{
	if (pkt->current == KNOT_ADDITIONAL) {
		glue_t *g = find_glue_for(rr, pkt);
		assert(g); // finds actually the node which is rr in
		return !(g->node->flags & NODE_FLAGS_NONAUTH);
	} else {
		return !is_deleg(pkt) || rr->type == KNOT_RRTYPE_NSEC;
	}
}

static knotd_in_state_t sign_section(knotd_in_state_t state, knot_pkt_t *pkt,
                                     knotd_qdata_t *qdata, knotd_mod_t *mod)
{
	if (!want_dnssec(qdata)) {
		return state;
	}

	const knot_pktsection_t *section = knot_pkt_section(pkt, pkt->current);
	assert(section);

	uint16_t count_unsigned = section->count;
	for (int i = 0; i < count_unsigned; i++) {
		const knot_rrset_t *rr = knot_pkt_rr(section, i);
		if (!shall_sign_rr(rr, pkt)) {
			continue;
		}

		uint16_t rr_pos = knot_pkt_rr_offset(section, i);

		uint8_t owner[KNOT_DNAME_MAXLEN] = { 0 };
		knot_dname_unpack(owner, pkt->wire + rr_pos, sizeof(owner), pkt->wire);
		knot_dname_to_lower(owner);

		knot_rrset_t *rrsig = sign_rrset(owner, rr, mod, &pkt->mm);
		if (!rrsig) {
			state = KNOTD_IN_STATE_ERROR;
			break;
		}

		int r = knot_pkt_put(pkt, KNOT_COMPR_HINT_NONE, rrsig, KNOT_PF_FREE);
		if (r != KNOT_EOK) {
			knot_rrset_free(rrsig, &pkt->mm);
			state = KNOTD_IN_STATE_ERROR;
			break;
		}
	}

	return state;
}

static knotd_in_state_t synth_authority(knotd_in_state_t state, knot_pkt_t *pkt,
                                        knotd_qdata_t *qdata, knotd_mod_t *mod)
{
	if (state == KNOTD_IN_STATE_HIT) {
		return state;
	}

	// synthesise NSEC

	if (want_dnssec(qdata)) {
		knot_rrset_t *nsec = synth_nsec(pkt, qdata, &pkt->mm);
		int r = knot_pkt_put(pkt, KNOT_COMPR_HINT_NONE, nsec, KNOT_PF_FREE);
		if (r != DNSSEC_EOK) {
			knot_rrset_free(nsec, &pkt->mm);
			return KNOTD_IN_STATE_ERROR;
		}
	}

	// promote NXDOMAIN to NODATA

	if (state == KNOTD_IN_STATE_MISS) {
		//! \todo Override RCODE set in solver_authority. Review.
		qdata->rcode = KNOT_RCODE_NOERROR;
		return KNOTD_IN_STATE_NODATA;
	}

	return state;
}

static knot_rrset_t *synth_dnskey(knotd_qdata_t *qdata, const zone_keyset_t *keyset,
                                  knot_mm_t *mm)
{
	knot_rrset_t *dnskey = knot_rrset_new(knotd_qdata_zone_name(qdata),
	                                      KNOT_RRTYPE_DNSKEY, KNOT_CLASS_IN,
	                                      dnskey_ttl(qdata), mm);
	if (!dnskey) {
		return 0;
	}

	dnssec_binary_t rdata = { 0 };
	for (size_t i = 0; i < keyset->count; i++) {
		if (!keyset->keys[i].is_public) {
			continue;
		}

		dnssec_key_get_rdata(keyset->keys[i].key, &rdata);
		assert(rdata.size > 0 && rdata.data);

		int r = knot_rrset_add_rdata(dnskey, rdata.data, rdata.size, mm);
		if (r != KNOT_EOK) {
			knot_rrset_free(dnskey, mm);
			return NULL;
		}
	}

	return dnskey;
}

/* copied from the middle of zone-sign.c */
static zone_key_t *ksk_for_cds(knotd_mod_t *mod)
{
	zone_key_t *res = NULL;
	unsigned crp = mod->dnssec->policy->child_records_publish;
	int kfc_prio = (crp == CHILD_RECORDS_ALWAYS ?
	                0 : (crp == CHILD_RECORDS_ROLLOVER ? 1 : 2));
	for (int i = 0; i < mod->keyset->count; i++) {
		zone_key_t *key = &mod->keyset->keys[i];
		if (key->is_ksk && key->cds_priority > kfc_prio) {
			res = key;
			kfc_prio = key->cds_priority;
		}
	}
	return res;
}

static knot_rrset_t *synth_cdnskey(knotd_qdata_t *qdata, knotd_mod_t *mod,
                                   knot_mm_t *mm)
{
	knot_rrset_t *dnskey = knot_rrset_new(knotd_qdata_zone_name(qdata),
	                                      KNOT_RRTYPE_CDNSKEY, KNOT_CLASS_IN,
	                                      0, mm);
	if (dnskey == NULL) {
		return 0;
	}

	dnssec_binary_t rdata = { 0 };
	zone_key_t *key = ksk_for_cds(mod);
	if (key == NULL) {
		knot_rrset_free(dnskey, mm);
		return NULL;
	}
	dnssec_key_get_rdata(key->key, &rdata);
	assert(rdata.size > 0 && rdata.data);

	int ret = knot_rrset_add_rdata(dnskey, rdata.data, rdata.size, mm);
	if (ret != KNOT_EOK) {
		knot_rrset_free(dnskey, mm);
		return NULL;
	}

	return dnskey;
}

static knot_rrset_t *synth_cds(knotd_qdata_t *qdata, knotd_mod_t *mod,
                               knot_mm_t *mm)
{
	knot_rrset_t *ds = knot_rrset_new(knotd_qdata_zone_name(qdata),
	                                  KNOT_RRTYPE_CDS, KNOT_CLASS_IN,
	                                  0, mm);
	if (ds == NULL) {
		return 0;
	}

	dnssec_binary_t rdata = { 0 };
	zone_key_t *key = ksk_for_cds(mod);
	if (key == NULL) {
		knot_rrset_free(ds, mm);
		return NULL;
	}
	zone_key_calculate_ds(key, &rdata);
	assert(rdata.size > 0 && rdata.data);

	int ret = knot_rrset_add_rdata(ds, rdata.data, rdata.size, mm);
	if (ret != KNOT_EOK) {
		knot_rrset_free(ds, mm);
		return NULL;
	}

	return ds;
}

static bool qtype_match(knotd_qdata_t *qdata, uint16_t type)
{
	uint16_t qtype = knot_pkt_qtype(qdata->query);
	return (qtype == KNOT_RRTYPE_ANY || qtype == type);
}

static bool is_apex_query(knotd_qdata_t *qdata)
{
	return knot_dname_is_equal(qdata->name, knotd_qdata_zone_name(qdata));
}

static knotd_in_state_t pre_routine(knotd_in_state_t state, knot_pkt_t *pkt,
                                    knotd_qdata_t *qdata, knotd_mod_t *mod)
{
	online_sign_ctx_t *ctx = knotd_mod_ctx(mod);
	zone_sign_reschedule_t resch = { 0 };

	(void)pkt, (void)qdata;

	pthread_mutex_lock(&ctx->event_mutex);
	if (ctx->zone_doomed) {
		pthread_mutex_unlock(&ctx->event_mutex);
		return KNOTD_IN_STATE_ERROR;
	}
	mod->dnssec->now = time(NULL);
	int ret = KNOT_ESEMCHECK;
	if (knot_time_cmp(ctx->event_parent_ds_q, mod->dnssec->now) <= 0) {
		ret = knot_parent_ds_query(mod->dnssec, mod->keyset, 1000);
		if (ret != KNOT_EOK && mod->dnssec->policy->ksk_sbm_check_interval > 0) {
			ctx->event_parent_ds_q = mod->dnssec->now + mod->dnssec->policy->ksk_sbm_check_interval;
		} else {
			ctx->event_parent_ds_q = 0;
		}
	}
	if (ret == KNOT_EOK || knot_time_cmp(ctx->event_rollover, mod->dnssec->now) <= 0) {
		ret = knot_dnssec_key_rollover(mod->dnssec, KEY_ROLL_ALLOW_KSK_ROLL | KEY_ROLL_ALLOW_ZSK_ROLL, &resch);
	}
	if (ret == KNOT_EOK) {
		if (resch.plan_ds_query && mod->dnssec->policy->ksk_sbm_check_interval > 0) {
			ctx->event_parent_ds_q = mod->dnssec->now + mod->dnssec->policy->ksk_sbm_check_interval;
		} else {
			ctx->event_parent_ds_q = 0;
		}
		ctx->event_rollover = resch.next_rollover;

		free_zone_keys(mod->keyset);
		free(mod->keyset);
		ret = knotd_mod_dnssec_load_keyset(mod, true);
		if (ret != KNOT_EOK) {
			ctx->zone_doomed = true;
			state = KNOTD_IN_STATE_ERROR;
		}
	}
	pthread_mutex_unlock(&ctx->event_mutex);

	return state;
}

static knotd_in_state_t synth_answer(knotd_in_state_t state, knot_pkt_t *pkt,
                                     knotd_qdata_t *qdata, knotd_mod_t *mod)
{
	// disallowed queries

	if (knot_pkt_qtype(pkt) == KNOT_RRTYPE_RRSIG) {
		qdata->rcode = KNOT_RCODE_REFUSED;
		return KNOTD_IN_STATE_ERROR;
	}

	// synthesized DNSSEC answers

	if (qtype_match(qdata, KNOT_RRTYPE_DNSKEY) && is_apex_query(qdata)) {
		knot_rrset_t *dnskey = synth_dnskey(qdata, mod->keyset, &pkt->mm);
		if (!dnskey) {
			return KNOTD_IN_STATE_ERROR;
		}

		int r = knot_pkt_put(pkt, KNOT_COMPR_HINT_QNAME, dnskey, KNOT_PF_FREE);
		if (r != DNSSEC_EOK) {
			knot_rrset_free(dnskey, &pkt->mm);
			return KNOTD_IN_STATE_ERROR;
		}
		state = KNOTD_IN_STATE_HIT;
	}

	if (qtype_match(qdata, KNOT_RRTYPE_CDNSKEY) && is_apex_query(qdata)) {
		knot_rrset_t *dnskey = synth_cdnskey(qdata, mod, &pkt->mm);
		if (!dnskey) {
			return KNOTD_IN_STATE_ERROR;
		}

		int r = knot_pkt_put(pkt, KNOT_COMPR_HINT_QNAME, dnskey, KNOT_PF_FREE);
		if (r != DNSSEC_EOK) {
			knot_rrset_free(dnskey, &pkt->mm);
			return KNOTD_IN_STATE_ERROR;
		}
		state = KNOTD_IN_STATE_HIT;
	}

	if (qtype_match(qdata, KNOT_RRTYPE_CDS) && is_apex_query(qdata)) {
		knot_rrset_t *ds = synth_cds(qdata, mod, &pkt->mm);
		if (!ds) {
			return KNOTD_IN_STATE_ERROR;
		}

		int r = knot_pkt_put(pkt, KNOT_COMPR_HINT_QNAME, ds, KNOT_PF_FREE);
		if (r != DNSSEC_EOK) {
			knot_rrset_free(ds, &pkt->mm);
			return KNOTD_IN_STATE_ERROR;
		}
		state = KNOTD_IN_STATE_HIT;
	}

	if (qtype_match(qdata, KNOT_RRTYPE_NSEC)) {
		knot_rrset_t *nsec = synth_nsec(pkt, qdata, &pkt->mm);
		if (!nsec) {
			return KNOTD_IN_STATE_ERROR;
		}

		int r = knot_pkt_put(pkt, KNOT_COMPR_HINT_QNAME, nsec, KNOT_PF_FREE);
		if (r != DNSSEC_EOK) {
			knot_rrset_free(nsec, &pkt->mm);
			return KNOTD_IN_STATE_ERROR;
		}

		state = KNOTD_IN_STATE_HIT;
	}

	return state;
}

static void online_sign_ctx_free(online_sign_ctx_t *ctx)
{
	pthread_mutex_destroy(&ctx->event_mutex);

	free(ctx);
}

static int online_sign_ctx_new(online_sign_ctx_t **ctx_ptr, knotd_mod_t *mod)
{
	online_sign_ctx_t *ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		return KNOT_ENOMEM;
	}

	int ret = knotd_mod_dnssec_init(mod);
	if (ret != KNOT_EOK) {
		free(ctx);
		return ret;
	}

	// Force Singe-Type signing scheme. This is only important for compatibility with older versions.
	mod->dnssec->policy->singe_type_signing = true;

	zone_sign_reschedule_t resch = { 0 };
	ret = knot_dnssec_key_rollover(mod->dnssec, KEY_ROLL_ALLOW_KSK_ROLL | KEY_ROLL_ALLOW_ZSK_ROLL, &resch);
	if (ret != KNOT_EOK) {
		free(ctx);
		return ret;
	}

	if (resch.plan_ds_query) {
		ctx->event_parent_ds_q = time(NULL);
	}
	ctx->event_rollover = resch.next_rollover;

	ret = knotd_mod_dnssec_load_keyset(mod, true);
	if (ret != KNOT_EOK) {
		free(ctx);
		return ret;
	}

	pthread_mutex_init(&ctx->event_mutex, NULL);

	*ctx_ptr = ctx;

	return KNOT_EOK;
}

int online_sign_load(knotd_mod_t *mod)
{
	knotd_conf_t conf = knotd_conf_zone(mod, C_DNSSEC_SIGNING,
	                                    knotd_mod_zone(mod));
	if (conf.single.boolean) {
		knotd_mod_log(mod, LOG_ERR, "incompatible with automatic signing");
		return KNOT_ENOTSUP;
	}

	online_sign_ctx_t *ctx = NULL;
	int r = online_sign_ctx_new(&ctx, mod);
	if (r != KNOT_EOK) {
		knotd_mod_log(mod, LOG_ERR, "failed to initialize signing key (%s)",
		              knot_strerror(r));
		return KNOT_ERROR;
	}

	knotd_mod_ctx_set(mod, ctx);

	knotd_mod_in_hook(mod, KNOTD_STAGE_ANSWER, pre_routine);

	knotd_mod_in_hook(mod, KNOTD_STAGE_ANSWER, synth_answer);
	knotd_mod_in_hook(mod, KNOTD_STAGE_ANSWER, sign_section);

	knotd_mod_in_hook(mod, KNOTD_STAGE_AUTHORITY, synth_authority);
	knotd_mod_in_hook(mod, KNOTD_STAGE_AUTHORITY, sign_section);

	knotd_mod_in_hook(mod, KNOTD_STAGE_ADDITIONAL, sign_section);

	return KNOT_EOK;
}

void online_sign_unload(knotd_mod_t *mod)
{
	online_sign_ctx_free(knotd_mod_ctx(mod));
}

KNOTD_MOD_API(onlinesign, KNOTD_MOD_FLAG_SCOPE_ZONE | KNOTD_MOD_FLAG_OPT_CONF,
              online_sign_load, online_sign_unload, online_sign_conf, NULL);
