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

#include <config.h>
#include <string.h>
#include <time.h>
#include <tap/basic.h>

#include "common/mempattern.h"
#include "common/hattrie/hat-trie.h"

static const char *alphabet = "abcdefghijklmn";
static char *randstr() {
	unsigned len = (1 + rand() % 64) + 1; /* (1-64) + '\0' */
	char *s = xmalloc(len * sizeof(char));
	for (unsigned i = 0; i < len - 1; ++i) {
		s[i] = alphabet[rand() % strlen(alphabet)];
	}
	s[len - 1] = '\0';
	return s;
}


int main(int argc, char *argv[])
{
	plan(8);

	/* Interesting intems. */
	unsigned count = 10;
	const char *items[] = {
	        "abcd",
	        "abc",
	        "ab",
	        "a",
	        "abcdefghijklmnopqrstuvw",
	        "abAcd",
	        "abcA",
	        "abA",
	        "Aab",
	        "A"
	};

	/* Dummy items. */
	srand(time(NULL));
	unsigned dummy_count = 32768;
	char **dummy = xmalloc(sizeof(char*) * dummy_count);
	for (unsigned i = 0; i < dummy_count; ++i) {
		dummy[i] = randstr();
	}

	/* Test 1: Create */
	unsigned passed = 1;
	value_t *v = NULL;
	hattrie_t *t = hattrie_create();
	ok(t != NULL, "hattrie: create");

	/* Test 2: Insert */
	passed = 1;
	for (unsigned i = 0; i < count; ++i) {
		v = hattrie_get(t, items[i], strlen(items[i]));
		if (!v) {
			passed = 0;
			break;
		}
		*v = (value_t)items[i];
	}
	ok(passed, "hattrie: insert");

	/* Test 3: Insert dummy. */
	passed = 1;
	for (unsigned i = 0; i < dummy_count; ++i) {
		v = hattrie_get(t, dummy[i], strlen(dummy[i]));
		if (!v) {
			passed = 0;
			break;
		}
		if (*v == NULL) {
			*v = dummy[i];
		}
	}
	ok(passed, "hattrie: dummy insert");

	/* Test 4: Lookup */
	passed = 1;
	for (unsigned i = 0; i < count; ++i) {
		v = hattrie_tryget(t, items[i], strlen(items[i]));
		if (!v || *v != items[i]) {
			diag("hattrie: mismatch on element '%u'", i);
			passed = 0;
			break;
		}
	}
	ok(passed, "hattrie: lookup");

	/* Test 5: LPR lookup */
	unsigned lpr_count = 5;
	const char *lpr[] = {
	        "abcdZ",
	        "abcZ",
	        "abZ",
	        "aZ",
	        "abcdefghijklmnopqrstuvw"
	};
	passed = 1;
	for (unsigned i = 0; i < lpr_count; ++i) {
		int ret = hattrie_find_lpr(t, lpr[i], strlen(lpr[i]), &v);
		if (!v || ret != 0 || *v != items[i]) {
			diag("hattrie: lpr='%s' mismatch lpr(%s) != %s",
			     (char *)(!v ? "<NULL>" : *v), lpr[i], items[i]);
			passed = 0;
			break;
		}
	}
	ok(passed, "hattrie: longest prefix match");

	/* Test 6: false LPR lookup */
	const char *false_lpr = "Z";
	int ret = hattrie_find_lpr(t, false_lpr, strlen(false_lpr), &v);
	ok(ret != 0 && v == NULL, "hattrie: non-existent prefix lookup");

	/* Unsorted iteration */
	unsigned counted = 0;
	hattrie_iter_t *it = hattrie_iter_begin(t, false);
	while (!hattrie_iter_finished(it)) {
		++counted;
		hattrie_iter_next(it);
	}
	is_int(hattrie_weight(t), counted, "hattrie: unsorted iteration");
	hattrie_iter_free(it);

	/* Sorted iteration. */
	counted = 0;
	it = hattrie_iter_begin(t, true);
	while (!hattrie_iter_finished(it)) {
		++counted;
		hattrie_iter_next(it);
	}
	is_int(hattrie_weight(t), counted, "hattrie: sorted iteration");
	hattrie_iter_free(it);

	for (unsigned i = 0; i < dummy_count; ++i) {
		free(dummy[i]);
	}
	free(dummy);
	hattrie_free(t);
	return 0;
}
