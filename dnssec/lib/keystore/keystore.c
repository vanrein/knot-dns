/*  Copyright (C) 2014 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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

#include <assert.h>
#include <stdlib.h>

#include "error.h"
#include "key.h"
#include "key/algorithm.h"
#include "key/dnskey.h"
#include "key/internal.h"
#include "key/privkey.h"
#include "keyid.h"
#include "keystore.h"
#include "keystore/internal.h"
#include "shared.h"
#include "wire.h"

/* -- internal API --------------------------------------------------------- */

int keystore_create(dnssec_keystore_t **store_ptr,
		    const keystore_functions_t *functions,
		    void *ctx_custom_data)
{
	assert(store_ptr);
	assert(functions);

	dnssec_keystore_t *store = calloc(1, sizeof(*store));
	if (!store) {
		return DNSSEC_ENOMEM;
	}

	store->functions = functions;

	int result = functions->ctx_new(&store->ctx, ctx_custom_data);
	if (result != DNSSEC_EOK) {
		free(store);
		return DNSSEC_ENOMEM;
	}

	*store_ptr = store;
	return DNSSEC_EOK;
}

/* -- public API ----------------------------------------------------------- */

_public_
int dnssec_keystore_deinit(dnssec_keystore_t *store)
{
	if (!store) {
		return DNSSEC_EINVAL;
	}

	dnssec_keystore_close(store);
	free(store);

	return DNSSEC_EOK;
}

_public_
int dnssec_keystore_init(dnssec_keystore_t *store, const char *config)
{
	if (!store) {
		return DNSSEC_EINVAL;
	}

	return store->functions->init(store->ctx, config);
}

_public_
int dnssec_keystore_open(dnssec_keystore_t *store, const char *config)
{
	if (!store) {
		return DNSSEC_EINVAL;
	}

	return store->functions->open(store->ctx, config);
}

_public_
int dnssec_keystore_close(dnssec_keystore_t *store)
{
	if (!store) {
		return DNSSEC_EINVAL;
	}

	return store->functions->close(store->ctx);
}

_public_
int dnssec_keystore_list_keys(dnssec_keystore_t *store, void *list)
{
	if (!store || !list) {
		return DNSSEC_EINVAL;
	}

	return store->functions->list_keys(store, list);

}

_public_
int dnssec_keystore_generate_key(dnssec_keystore_t *store,
				 dnssec_key_algorithm_t _algorithm,
				 unsigned bits, char **id_ptr)
{
	if (!store || !_algorithm || !id_ptr) {
		return DNSSEC_EINVAL;
	}

	// prepare parameters

	gnutls_pk_algorithm_t algorithm = algorithm_to_gnutls(_algorithm);
	if (algorithm == GNUTLS_PK_UNKNOWN) {
		return DNSSEC_INVALID_KEY_ALGORITHM;
	}

	if (!dnssec_algorithm_key_size_check(_algorithm, bits)) {
		return DNSSEC_INVALID_KEY_SIZE;
	}

	return store->functions->generate_key(store->ctx, algorithm, bits, id_ptr);
}

_public_
int dnssec_keystore_remove_key(dnssec_keystore_t *store, const char *key_id)
{
	if (!store || !key_id) {
		return DNSSEC_EINVAL;
	}

	return store->functions->remove_key(store, key_id);
}

static bool valid_params(dnssec_key_t *key, const char *id,
			 dnssec_key_algorithm_t algorithm)
{
	assert(key);

	// no public key, parameters must be present

	if (key->public_key == NULL) {
		return (id != NULL && algorithm != 0);
	}

	// public key present, parameters must match or be NULL

	if (algorithm != 0) {
		uint8_t current = dnssec_key_get_algorithm(key);
		if (algorithm != current) {
			return false;
		}
	}

	if (id != NULL && !dnssec_keyid_equal(key->id, id)) {
		return false;
	}

	return true;
}

_public_
int dnssec_key_import_keystore(dnssec_key_t *key, dnssec_keystore_t *keystore,
			       const char *id, dnssec_key_algorithm_t algorithm)
{
	if (!key || !keystore || !valid_params(key, id, algorithm)) {
		return DNSSEC_EINVAL;
	}

	if (key->private_key) {
		return DNSSEC_KEY_ALREADY_PRESENT;
	}

	// define search ID and algorithm

	if (id == NULL) {
		assert(key->public_key);
		id = key->id;
	}

	if (algorithm == 0) {
		assert(key->public_key);
		uint8_t algorithm8 = dnssec_key_get_algorithm(key);
		algorithm = algorithm8;
	}

	// retrieve and set the private key

	gnutls_privkey_t privkey = NULL;
	int r = keystore->functions->get_private(keystore->ctx, id, &privkey);
	if (r != DNSSEC_EOK) {
		return r;
	}

	r = key_set_private_key(key, privkey);
	if (r != DNSSEC_EOK) {
		gnutls_privkey_deinit(privkey);
		return r;
	}

	return DNSSEC_EOK;
}

_public_
int dnssec_key_import_private_keystore(dnssec_key_t *key,
				       dnssec_keystore_t *keystore)
{
	return dnssec_key_import_keystore(key, keystore, NULL, 0);
}
