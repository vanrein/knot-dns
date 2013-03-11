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

#include "libknot/rrset-dump.h"

#include <stdlib.h>			// free
#include <stdbool.h>			// bool
#include <string.h>			// memcpy
#include <time.h>			// strftime
#include <ctype.h>			// isprint
#include <math.h>			// pow
#include <arpa/inet.h>			// ntohs
#include <sys/socket.h>			// AF_INET (BSD)
#include <netinet/in.h>			// in_addr (BSD)

#include "common/errcode.h"		// KNOT_EOK
#include "common/base64.h"		// base64
#include "common/base32hex.h"		// base32hex
#include "common/descriptor_new.h"	// KNOT_RRTYPE
#include "libknot/util/utils.h"		// knot_wire_read_u16

#define TAB_WIDTH		8
#define BLOCK_WIDTH		40
#define BLOCK_INDENT		"\n\t\t\t\t"

#define LOC_ZERO		2147483648	// 2^31

typedef struct {
	const knot_dump_style_t *style;
	uint8_t *in;
	size_t  in_max;
	char    *out;
	size_t  out_max;
	size_t  total;
	int     ret;
} rrset_dump_params_t;

const knot_dump_style_t KNOT_DUMP_STYLE_DEFAULT = {
	.wrap = false,
	.show_class = true,
	.show_ttl = true,
	.verbose = false,
	.reduce = true,
};

static void dump_string(rrset_dump_params_t *p, const char *str)
{
	size_t in_len = strlen(str);

	p->ret = -1;

	// Check input size (+ 1 termination).
	if (in_len >= p->out_max) {
		return;
	}

	// Copy string including termination '\0'!
	if (memcpy(p->out, str, in_len + 1) == NULL) {
		return;
	}

	// Fill in output.
	p->out += in_len;
	p->out_max -= in_len;
	p->total += in_len;
	p->ret = 0;
}

static void wire_num8_to_str(rrset_dump_params_t *p)
{
	uint8_t data = *(p->in);
	size_t  in_len = sizeof(data);
	size_t  out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Write number.
	int ret = snprintf(p->out, p->out_max, "%u", data);
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_num16_to_str(rrset_dump_params_t *p)
{
	uint16_t data;
	size_t   in_len = sizeof(data);
	size_t   out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&data, p->in, in_len) == NULL) {
		return;
	}

	// Write number.
	int ret = snprintf(p->out, p->out_max, "%u", ntohs(data));
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_num32_to_str(rrset_dump_params_t *p)
{
	uint32_t data;
	size_t   in_len = sizeof(data);
	size_t   out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&data, p->in, in_len) == NULL) {
		return;
	}

	// Write number.
	int ret = snprintf(p->out, p->out_max, "%u", ntohl(data));
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_ipv4_to_str(rrset_dump_params_t *p)
{
	struct in_addr addr4;
	size_t in_len = sizeof(addr4.s_addr);
	size_t out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&(addr4.s_addr), p->in, in_len) == NULL) {
		return;
	}

	// Write address.
	if (inet_ntop(AF_INET, &addr4, p->out, p->out_max) == NULL) {
		return;
	}
	out_len = strlen(p->out);

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_ipv6_to_str(rrset_dump_params_t *p)
{
	struct in6_addr addr6;
	size_t in_len = sizeof(addr6.s6_addr);
	size_t out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&(addr6.s6_addr), p->in, in_len) == NULL) {
		return;
	}

	// Write address.
	if (inet_ntop(AF_INET6, &addr6, p->out, p->out_max) == NULL) {
		return;
	}
	out_len = strlen(p->out);

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_type_to_str(rrset_dump_params_t *p)
{
	char     type[32];
	uint16_t data;
	size_t   in_len = sizeof(data);
	size_t   out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&data, p->in, in_len) == NULL) {
		return;
	}

	// Get record type name string.
	if (knot_rrtype_to_string(ntohs(data), type, sizeof(type)) <= 0) {
		return;
	}

	// Write string.
	int ret = snprintf(p->out, p->out_max, "%s", type);
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}


static int hex_encode(const uint8_t  *in,
                      const uint32_t in_len,
                      uint8_t        *out,
                      const uint32_t out_len)
{
	static const char hex[] = "0123456789ABCDEF";

	if (out_len < 2 * in_len) {
		return -1;
	}

	for (uint32_t i = 0; i < in_len; i++) {
		out[2 * i]     = hex[in[i] / 16];
		out[2 * i + 1] = hex[in[i] % 16];
	}

	return 2 * in_len;
}

static int hex_encode_alloc(const uint8_t  *in,
                            const uint32_t in_len,
                            uint8_t        **out)
{
	uint32_t out_len = 2 * in_len;

	// Allocating output buffer.
	*out = malloc(out_len);

	if (*out == NULL) {
		return -1;
	}

	// Encoding data.
	return hex_encode(in, in_len, *out, out_len);
}

typedef int (*encode_t)(const uint8_t *in, const uint32_t in_len,
                        uint8_t *out, const uint32_t out_len);

typedef int (*encode_alloc_t)(const uint8_t *in, const uint32_t in_len,
                              uint8_t **out);

static void wire_data_encode_to_str(rrset_dump_params_t *p,
                                    encode_t enc, encode_alloc_t enc_alloc)
{
	int    ret;
	size_t in_len = p->in_max;

	p->ret = -1;

	// One-line vs multi-line mode.
	if (p->style->wrap == false) {
		// Encode data directly to the output.
		ret = enc(p->in, in_len, (uint8_t *)(p->out), p->out_max);
		if (ret <= 0) {
			return;
		}
		size_t out_len = ret;

		p->out += out_len;
		p->out_max -= out_len;
		p->total += out_len;
	} else {
		int  src_begin, src_len;
		char *buf;

		// Encode data to the temporary buffer.
		ret = enc_alloc(p->in, in_len, (uint8_t **)&buf);
		if (ret <= 0) {
			return;
		}

		// Loop which wraps base64 block in more lines.
		for (src_begin = 0; src_begin < ret; src_begin += BLOCK_WIDTH) {
			// Write indent block.
			dump_string(p, BLOCK_INDENT);
			if (p->ret != 0) {
				free(buf);
				return;
			}

			// Compute block length (the last one can be shorter).
			src_len = (ret - src_begin) < BLOCK_WIDTH ?
			          (ret - src_begin) : BLOCK_WIDTH;

			if (src_len > p->out_max) {
				free(buf);
				return;
			}

			// Write data block.
			memcpy(p->out, buf + src_begin, src_len);

			p->out += src_len;
			p->out_max -= src_len;
			p->total += src_len;
		}

		// Destroy temporary buffer.
		free(buf);
	}

	// String termination.
	if (p->out_max > 0) {
		*p->out = '\0';
	} else {
		return;
	}

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->ret = 0;
}

static void wire_len_data_encode_to_str(rrset_dump_params_t *p, encode_t enc)
{
	p->ret = -1;

	// First byte is data length.
	if (p->in_max < 1) {
		return;
	}
	size_t in_len = *(p->in);
	p->in++;
	p->in_max--;

	if (in_len > 0) {
		// Encode data directly to the output.
		int ret = enc(p->in, in_len, (uint8_t *)(p->out), p->out_max);
		if (ret <= 0) {
			return;
		}
		size_t out_len = ret;
	
		p->out += out_len;
		p->out_max -= out_len;
		p->total += out_len;
	
		// String termination.
		if (p->out_max > 0) {
			*p->out = '\0';
		} else {
			return;
		}

		// Fill in output.
		p->in += in_len;
		p->in_max -= in_len;
	} else {
		// Dump "-" if no salt.
		dump_string(p, "-");
		if (p->ret != 0) {
			return;
		}
	}

	p->ret = 0;
}

static void wire_text_to_str(rrset_dump_params_t *p)
{
	p->ret = -1;

	// First byte is string length.
	if (p->in_max < 1) {
		return;
	}
	size_t in_len = *(p->in);
	p->in++;
	p->in_max--;

	// Check if the given length makes sense.
	if (in_len > p->in_max) {
		return;
	}

	// Opening quoatition.
	dump_string(p, "\"");
	if (p->ret != 0) {
		return;
	}

	// Loop over all characters.
	for (size_t i = 0; i < in_len; i++) {
		char ch = (char)(p->in)[i];

		if (isprint(ch) != 0) {
			// For special character print leading slash.
			if (ch == '\\' || ch == '"') {
				dump_string(p, "\\");
				if (p->ret != 0) {
					return;
				}
			}

			// Print text character.
			if (p->out_max == 0) {
				return;
			}

			*p->out = ch;
			p->out++;
			p->out_max--;
			p->total++;
		} else {
			// Unprintable character encode via \ddd notation.
			int ret = snprintf(p->out, p->out_max,"\\%03u", ch);
			if (ret <= 0 || ret >= p->out_max) {
				return;
			}

			p->out += ret;
			p->out_max -= ret;
			p->total += ret;
		}
	}

	// Closing quoatition.
	dump_string(p, "\"");
	if (p->ret != 0) {
		return;
	}

	// String termination.
	if (p->out_max > 0) {
		*p->out = '\0';
	} else {
		return;
	}

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->ret = 0;
}

static void wire_timestamp_to_str(rrset_dump_params_t *p)
{
	uint32_t data;
	size_t   in_len = sizeof(data);
	size_t   out_len = 0;
	int      ret;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&data, p->in, in_len) == NULL) {
		return;
	}

	time_t timestamp = ntohl(data);

	if (p->style->verbose) {
		// Write timestamp in YYYYMMDDhhmmss format.
		ret = strftime(p->out, p->out_max, "%Y%m%d%H%M%S",
		               gmtime(&timestamp));
		if (ret == 0) {
			return;
		}
	} else {
		// Write timestamp only.
		ret = snprintf(p->out, p->out_max, "%u", ntohl(data));
		if (ret <= 0 || ret >= p->out_max) {
			return;
		}
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static int time_to_human_str(char         *out,
                             const size_t out_len,
                             uint32_t     data)
{
	size_t   total_len = 0;
	uint32_t num;
	int      ret;

	// Process days.
	num = data / 86400;
	if (num > 0) {
		ret = snprintf(out + total_len, out_len - total_len,
		               "%ud", num);
		if (ret <= 0 || ret >= out_len - total_len) {
			return -1;
		}

		total_len += ret;
		data -= num * 86400;
	}

	// Process hours.
	num = data / 3600;
	if (num > 0) {
		ret = snprintf(out + total_len, out_len - total_len,
		               "%uh", num);
		if (ret <= 0 || ret >= out_len - total_len) {
			return -1;
		}

		total_len += ret;
		data -= num * 3600;
	}

	// Process minutes.
	num = data / 60;
	if (num > 0) {
		ret = snprintf(out + total_len, out_len - total_len,
		               "%um", num);
		if (ret <= 0 || ret >= out_len - total_len) {
			return -1;
		}

		total_len += ret;
		data -= num * 60;
	}

	// Process seconds.
	num = data;
	if (num > 0 || total_len == 0) {
		ret = snprintf(out + total_len, out_len - total_len,
		               "%us", num);
		if (ret <= 0 || ret >= out_len - total_len) {
			return -1;
		}

		total_len += ret;
	}

	return total_len;
}

static void wire_ttl_to_str(rrset_dump_params_t *p)
{
	uint32_t data;
	size_t   in_len = sizeof(data);
	size_t   out_len = 0;
	int      ret;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&data, p->in, in_len) == NULL) {
		return;
	}

	if (p->style->verbose) {
		// Write time in human readable format.
		ret = time_to_human_str(p->out, p->out_max, ntohl(data));
		if (ret <= 0) {
			return;
		}
	} else {
		// Write timestamp only.
		ret = snprintf(p->out, p->out_max, "%u", ntohl(data));
		if (ret <= 0 || ret >= p->out_max) {
			return;
		}
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_bitmap_to_str(rrset_dump_params_t *p)
{
	int    ret;
	char   type[32];
	size_t i = 0;
	size_t in_len = p->in_max;
	size_t out_len = 0;

	p->ret = -1;

	// Loop over bitmap window array (can be empty).
	while (i < in_len) {
		// First byte is window number.
		uint8_t win = p->in[i++];

		// Check window length (length must follow).
		if (i >= in_len) {
			return;
		}

		// Second byte is window length.
		uint8_t bitmap_len = p->in[i++];

		// Check window length (len bytes must follow).
		if (i + bitmap_len > in_len) {
			return;
		}

		// Bitmap processing.
		for (size_t j = 0; j < (bitmap_len * 8); j++) {
			if ((p->in[i + j / 8] & (128 >> (j % 8))) != 0) {
				uint16_t type_num = win * 256 + j;

				if (knot_rrtype_to_string(type_num, type,
				                          sizeof(type)) <= 0) {
					return;
				}

				// Print type name to type list.
				if (out_len > 0) {
					ret = snprintf(p->out, p->out_max,
					               " %s", type);
				} else {
					ret = snprintf(p->out, p->out_max,
					               "%s", type);
				}
				if (ret <= 0 || ret >= p->out_max) {
					return;
				}
				out_len += ret;
				p->out += ret;
				p->out_max -= ret;
			}
		}

		i += bitmap_len;
	}

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_dname_to_str(rrset_dump_params_t *p)
{
	knot_dname_t *dname;
	uint8_t label_len;
	size_t  in_len = 0;
	size_t  out_len = 0;

	p->ret = -1;

	// Compute dname length.
	do {
		// Read label length.
		if (p->in_max < 1) {
			return;
		}
		label_len = *(p->in);
		in_len++;
		p->in++;
		p->in_max--;

		if (label_len > p->in_max) {
			return;
		}
		in_len += label_len;
		p->in += label_len;
		p->in_max -= label_len;
	} while (label_len > 0);

	// Create dname.
	dname = knot_dname_new_from_wire(p->in - in_len, in_len, NULL);
	if (dname == NULL) {
		return;
	}

	// Write dname string.
	char *dname_str = knot_dname_to_str(dname);
	free(dname);
	int ret = snprintf(p->out, p->out_max, "%s", dname_str);
	free(dname_str);
	if (ret < 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void ptr_dname_to_str(rrset_dump_params_t *p)
{
	knot_dname_t *dname;
	size_t in_len = sizeof(knot_dname_t *);
	size_t out_len = 0;

	p->ret = -1;

	// Check input size.
	if (in_len > p->in_max) {
		return;
	}

	// Fill in input data.
	if (memcpy(&dname, p->in, in_len) == NULL) {
		return;
	}

	// Write dname string.
	char *dname_str = knot_dname_to_str(dname);
	int ret = snprintf(p->out, p->out_max, "%s", dname_str);
	free(dname_str);
	if (ret < 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->in += in_len;
	p->in_max -= in_len;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;
	p->ret = 0;
}

static void wire_apl_to_str(rrset_dump_params_t *p)
{
	struct in_addr addr4;
	struct in6_addr addr6;
	int    ret;
	size_t out_len = 0;

	p->ret = -1;

	// Input check: family(2B) + prefix(1B) + afdlen(1B).
	if (p->in_max < 4) {
		return;
	}

	// Read fixed size values.
	uint16_t family   = knot_wire_read_u16(p->in);
	uint8_t  prefix   = *(p->in + 2);
	uint8_t  negation = *(p->in + 3) >> 7;
	uint8_t  afdlen   = *(p->in + 3) & 0x7F;
	p->in += 4;
	p->in_max -= 4;

	// Write negation mark.
	if (negation != 0) {
		dump_string(p, "!");
		if (p->ret != 0) {
			return;
		}
	}

	// Write address family with colon.
	ret = snprintf(p->out, p->out_max, "%u:", family);
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	p->out += ret;
	p->out_max -= ret;
	p->total += ret;

	// Write address.
	switch (family) {
	case 1:
		memset(&addr4, 0, sizeof(addr4));

		if (afdlen > sizeof(addr4.s_addr) || afdlen > p->in_max) {
			return;
		}

		if (memcpy(&(addr4.s_addr), p->in, afdlen) == NULL) {
			return;
		}

		// Write address.
		if (inet_ntop(AF_INET, &addr4, p->out, p->out_max) == NULL) {
			return;
		}
		out_len = strlen(p->out);

		break;
	case 2:
		memset(&addr6, 0, sizeof(addr6));

		if (afdlen > sizeof(addr6.s6_addr) || afdlen > p->in_max) {
			return;
		}

		if (memcpy(&(addr6.s6_addr), p->in, afdlen) == NULL) {
			return;
		}

		// Write address.
		if (inet_ntop(AF_INET6, &addr6, p->out, p->out_max) == NULL) {
			return;
		}
		out_len = strlen(p->out);

		break;
	default:
		return;
	}
	p->in += afdlen;
	p->in_max -= afdlen;
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;

	// Write prefix length with forward slash.
	ret = snprintf(p->out, p->out_max, "/%u", prefix);
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	p->out += ret;
	p->out_max -= ret;
	p->total += ret;

	p->ret = 0;
}

static void wire_loc_to_str(rrset_dump_params_t *p)
{
	int    ret;
	size_t in_len = 16;

	p->ret = -1;

	// Check input size (1 LOC = 16 B).
	if (in_len > p->in_max) {
		return;
	}

	// Read values.
	uint8_t version = *p->in;
	p->in++;
	uint8_t size_w = *p->in;
	p->in++;
	uint8_t hpre_w = *p->in;
	p->in++;
	uint8_t vpre_w = *p->in;
	p->in++;
	uint32_t lat_w = knot_wire_read_u32(p->in);
	p->in += 4;
	uint32_t lon_w = knot_wire_read_u32(p->in);
	p->in += 4;
	uint32_t alt_w = knot_wire_read_u32(p->in);
	p->in += 4;

	p->in_max -= in_len;

	// Version check.
	if (version != 0) {
		return;
	}

	// Latitude calculation.
	char lat_mark;
	uint32_t lat;
	if (lat_w >= LOC_ZERO) {
		lat_mark = 'N';
		lat = lat_w - LOC_ZERO;
	} else {
		lat_mark = 'S';
		lat = LOC_ZERO - lat_w;
	}

	uint32_t d1 = lat / 3600000;
	uint32_t m1 = (lat - 3600000 * d1) / 60000;
	double s1 = 0.001 * (lat - 3600000 * d1 - 60000 * m1);

	// Longitude calculation.
	char lon_mark;
	uint32_t lon;
	if (lon_w >= LOC_ZERO) {
		lon_mark = 'E';
		lon = lon_w - LOC_ZERO;
	} else {
		lon_mark = 'W';
		lon = LOC_ZERO - lon_w; 
	}

	uint32_t d2 = lon / 3600000;
	uint32_t m2 = (lon - 3600000 * d2) / 60000;
	double s2 = 0.001 * (lon - 3600000 * d2 - 60000 * m2);

	// Write latitude and longitude. 
	ret = snprintf(p->out, p->out_max, "%u %u %.*f %c  %u %u %.*f %c",
	               d1, m1, (uint32_t)s1 != s1 ? 3 : 0, s1, lat_mark,
	               d2, m2, (uint32_t)s2 != s2 ? 3 : 0, s2, lon_mark);
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	p->out += ret;
	p->out_max -= ret;
	p->total += ret;

	// Altitude calculation.
	double alt = 0.01 * alt_w - 100000.0;

	// Compute mantisa and exponent for each size.
	uint8_t size_m = size_w >> 4;
	uint8_t size_e = size_w & 0xF;
	uint8_t hpre_m = hpre_w >> 4;
	uint8_t hpre_e = hpre_w & 0xF;
	uint8_t vpre_m = vpre_w >> 4;
	uint8_t vpre_e = vpre_w & 0xF;

	// Sizes check.
	if (size_m > 9 || size_e > 9 || hpre_m > 9 || hpre_e > 9 ||
	    vpre_m > 9 || vpre_e > 9) {
		return;
	}

	// Size and precisions calculation.
	double size = 0.01 * size_m * pow(10, size_e);
	double hpre = 0.01 * hpre_m * pow(10, hpre_e);
	double vpre = 0.01 * vpre_m * pow(10, vpre_e);

	// Write altitude and precisions.
	ret = snprintf(p->out, p->out_max, "  %.*fm  %.*fm %.*fm %.*fm",
	               (int32_t)alt != alt ? 2 : 0, alt,
	               (uint32_t)size != size ? 2 : 0, size,
	               (uint32_t)hpre != hpre ? 2 : 0, hpre,
	               (uint32_t)vpre != vpre ? 2 : 0, vpre);
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	p->out += ret;
	p->out_max -= ret;
	p->total += ret;

	p->ret = 0;
}

static void wire_gateway_to_str(rrset_dump_params_t *p)
{
	p->ret = -1;

	// Input check: type(1B) + algo(1B).
	if (p->in_max < 2) {
		return;
	}

	uint8_t type = *p->in;
	uint8_t alg = *(p->in + 1);

	// Write gateway type.
	wire_num8_to_str(p);
	if (p->ret != 0) {
		return;
	}

	// Write space.
	dump_string(p, " ");
	if (p->ret != 0) {
		return;
	}

	// Write algorithm number.
	wire_num8_to_str(p);
	if (p->ret != 0) {
		return;
	}

	// Write space.
	dump_string(p, " ");
	if (p->ret != 0) {
		return;
	}

	// Write appropriate gateway.
	switch (type) {
	case 0:
		dump_string(p, ".");
		break;
	case 1:
		wire_ipv4_to_str(p);
		break;
	case 2:
		wire_ipv6_to_str(p);
		break;
	case 3:
		wire_dname_to_str(p);
		break;
	default:
		return;
	}
	if (p->ret != 0) {
		return;
	}

	if (alg > 0) {
		// Write space.
		dump_string(p, " ");
		if (p->ret != 0) {
			return;
		}

		// Write ipsec key.
		wire_data_encode_to_str(p, &base64_encode, &base64_encode_alloc);
		if (p->ret != 0) {
			return;
		}
	}

	p->ret = 0;
}

static void wire_unknown_to_str(rrset_dump_params_t *p)
{
	int    ret;
	size_t in_len = p->in_max;
	size_t out_len = 0;

	p->ret = -1;

	// Write unknown length header.
	if (in_len > 0) {
		ret = snprintf(p->out, p->out_max, "\\# %zu ", in_len);
	} else {
		ret = snprintf(p->out, p->out_max, "\\# 0");
	}
	if (ret <= 0 || ret >= p->out_max) {
		return;
	}
	out_len = ret;

	// Fill in output.
	p->out += out_len;
	p->out_max -= out_len;
	p->total += out_len;

	// Write hex data if any.
	if (in_len > 0) {
		wire_data_encode_to_str(p, &hex_encode, &hex_encode_alloc);
		if (p->ret != 0) {
			return;
		}
	}

	p->ret = 0;
}

#define DUMP_PARAMS	uint8_t *in, const size_t in_len, char *out, \
			const size_t out_max, const knot_dump_style_t *style
#define DUMP_INIT	rrset_dump_params_t p = { .style = style, .in = in, \
			.in_max = in_len, .out = out, .out_max = out_max };
//#define	DUMP_END	return (p.in_max == 0 ? p.total : -1);
#define	DUMP_END	return p.total;

#define CHECK_RET(p)	if (p.ret != 0) return -1;

#define DUMP_SPACE	dump_string(&p, " "); CHECK_RET(p);
#define DUMP_NUM8	wire_num8_to_str(&p); CHECK_RET(p);
#define DUMP_NUM16	wire_num16_to_str(&p); CHECK_RET(p);
#define DUMP_NUM32	wire_num32_to_str(&p); CHECK_RET(p);
#define DUMP_DNAME	ptr_dname_to_str(&p); CHECK_RET(p);
#define DUMP_TIME	wire_ttl_to_str(&p); CHECK_RET(p);
#define DUMP_TIMESTAMP	wire_timestamp_to_str(&p); CHECK_RET(p);
#define DUMP_IPV4	wire_ipv4_to_str(&p); CHECK_RET(p);
#define DUMP_IPV6	wire_ipv6_to_str(&p); CHECK_RET(p);
#define DUMP_TYPE	wire_type_to_str(&p); CHECK_RET(p);
#define DUMP_HEX	wire_data_encode_to_str(&p, &hex_encode, \
				&hex_encode_alloc); CHECK_RET(p);
#define DUMP_BASE64	wire_data_encode_to_str(&p, &base64_encode, \
				&base64_encode_alloc); CHECK_RET(p);
#define DUMP_HASH	wire_len_data_encode_to_str(&p, &base32hex_encode); \
				CHECK_RET(p);
#define DUMP_SALT	wire_len_data_encode_to_str(&p, &hex_encode); \
				CHECK_RET(p);
#define DUMP_TEXT	wire_text_to_str(&p); CHECK_RET(p);
#define DUMP_BITMAP	wire_bitmap_to_str(&p); CHECK_RET(p);
#define DUMP_APL	wire_apl_to_str(&p); CHECK_RET(p);
#define DUMP_LOC	wire_loc_to_str(&p); CHECK_RET(p);
#define DUMP_GATEWAY	wire_gateway_to_str(&p); CHECK_RET(p);
#define DUMP_UNKNOWN	wire_unknown_to_str(&p); CHECK_RET(p);

static int dump_a(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_IPV4;

	DUMP_END;
}

static int dump_ns(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_DNAME;

	DUMP_END;
}

static int dump_soa(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_DNAME; DUMP_SPACE;
	DUMP_DNAME; DUMP_SPACE;
	DUMP_NUM32; DUMP_SPACE;
	DUMP_TIME; DUMP_SPACE;
	DUMP_TIME; DUMP_SPACE;
	DUMP_TIME; DUMP_SPACE;
	DUMP_TIME;

	DUMP_END;
}

static int dump_hinfo(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_TEXT; DUMP_SPACE;
	DUMP_TEXT;

	DUMP_END;
}

static int dump_minfo(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_DNAME; DUMP_SPACE;
	DUMP_DNAME;

	DUMP_END;
}

static int dump_mx(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM16; DUMP_SPACE;
	DUMP_DNAME;

	DUMP_END;
}

static int dump_txt(DUMP_PARAMS)
{
	DUMP_INIT;

	// First text string.
	DUMP_TEXT;

	// Other text strings if any.
	while (p.in_max > 0) {
		DUMP_SPACE; DUMP_TEXT; 
	}

	DUMP_END;
}

static int dump_dnskey(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_BASE64;

	DUMP_END;
}

static int dump_aaaa(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_IPV6;

	DUMP_END;
}

static int dump_loc(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_LOC;

	DUMP_END;
}

static int dump_srv(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_DNAME;

	DUMP_END;
}

static int dump_naptr(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_TEXT; DUMP_SPACE;
	DUMP_TEXT; DUMP_SPACE;
	DUMP_TEXT; DUMP_SPACE;
	DUMP_DNAME;

	DUMP_END;
}

static int dump_cert(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_BASE64;

	DUMP_END;
}

static int dump_apl(DUMP_PARAMS)
{
	DUMP_INIT;

	// Print list of APLs (empty list is allowed).
	while (p.in_max > 0) {
		if (p.total > 0) {
			DUMP_SPACE;
		}
		DUMP_APL;
	}

	DUMP_END;
}

static int dump_ds(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM16; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_HEX;

	DUMP_END;
}

static int dump_sshfp(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_HEX;

	DUMP_END;
}

static int dump_ipseckey(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM8; DUMP_SPACE;
	DUMP_GATEWAY;

	DUMP_END;
}

static int dump_rrsig(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_TYPE; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM32; DUMP_SPACE;
	DUMP_TIMESTAMP; DUMP_SPACE;
	DUMP_TIMESTAMP; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_DNAME; DUMP_SPACE;
	DUMP_BASE64;

	DUMP_END;
}

static int dump_nsec(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_DNAME; DUMP_SPACE;
	DUMP_BITMAP;

	DUMP_END;
}

static int dump_dhcid(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_BASE64;

	DUMP_END;
}

static int dump_nsec3(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_SALT; DUMP_SPACE;
	DUMP_HASH; DUMP_SPACE;
	DUMP_BITMAP;

	DUMP_END;
}

static int dump_nsec3param(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM16; DUMP_SPACE;
	DUMP_SALT;

	DUMP_END;
}

static int dump_tlsa(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_NUM8; DUMP_SPACE;
	DUMP_HEX;

	DUMP_END;
}

static int dump_unknown(DUMP_PARAMS)
{
	DUMP_INIT;

	DUMP_UNKNOWN;

	DUMP_END;
}

int knot_rrset_txt_dump_data(const knot_rrset_t      *rrset,
                             const size_t            pos,
                             char                    *dst,
                             const size_t            maxlen,
                             const knot_dump_style_t *style)
{
	if (rrset == NULL || dst == NULL || style == NULL) {
		return KNOT_EINVAL;
	}

	uint8_t *data = knot_rrset_get_rdata(rrset, pos);
	size_t  data_len = rrset_rdata_item_size(rrset, pos);

	int ret = 0;

	switch (knot_rrset_type(rrset)) {
		case KNOT_RRTYPE_A:
			ret = dump_a(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_NS:
		case KNOT_RRTYPE_CNAME:
		case KNOT_RRTYPE_PTR:
		case KNOT_RRTYPE_DNAME:
			ret = dump_ns(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_SOA:
			ret = dump_soa(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_HINFO:
			ret = dump_hinfo(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_MINFO:
		case KNOT_RRTYPE_RP:
			ret = dump_minfo(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_MX:
		case KNOT_RRTYPE_AFSDB:
		case KNOT_RRTYPE_RT:
		case KNOT_RRTYPE_KX:
			ret = dump_mx(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_TXT:
		case KNOT_RRTYPE_SPF:
			ret = dump_txt(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_KEY:
		case KNOT_RRTYPE_DNSKEY:
			ret = dump_dnskey(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_AAAA:
			ret = dump_aaaa(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_LOC:
			ret = dump_loc(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_SRV:
			ret = dump_srv(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_NAPTR:
			ret = dump_naptr(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_CERT:
			ret = dump_cert(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_APL:
			ret = dump_apl(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_DS:
			ret = dump_ds(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_SSHFP:
			ret = dump_sshfp(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_IPSECKEY:
			ret = dump_ipseckey(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_RRSIG:
			ret = dump_rrsig(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_NSEC:
			ret = dump_nsec(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_DHCID:
			ret = dump_dhcid(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_NSEC3:
			ret = dump_nsec3(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_NSEC3PARAM:
			ret = dump_nsec3param(data, data_len, dst, maxlen, style);
			break;
		case KNOT_RRTYPE_TLSA:
			ret = dump_tlsa(data, data_len, dst, maxlen, style);
			break;
		default:
			ret = dump_unknown(data, data_len, dst, maxlen, style);
			break;
	}

	return ret;
}

int knot_rrset_txt_dump_header(const knot_rrset_t      *rrset,
                               const size_t            pos,
                               char                    *dst,
                               const size_t            maxlen,
                               const knot_dump_style_t *style)
{
	if (rrset == NULL || dst == NULL || style == NULL) {
		return KNOT_EINVAL;
	}

	size_t len = 0;
	char   buf[32];
	int    ret;

	// Dump rrset owner.
	char *name = knot_dname_to_str(rrset->owner);
	// If reduced style don't print non-first records in rrset or rrsigs.
	if (style->reduce && (pos > 0 || rrset->type == KNOT_RRTYPE_RRSIG)) {
		// Fill buffer with tabs.
		memset(buf, '\t', sizeof(buf));
		// Compute leading number of tabs.
		size_t tabs = (strlen(name) + TAB_WIDTH - 1) / TAB_WIDTH;
		// Check number of tabs.
		tabs = tabs < sizeof(buf) ? tabs : (sizeof(buf) - 1);
		// Terminate tabs string.
		buf[tabs] = '\0';

		ret = snprintf(dst + len, maxlen - len, "%s", buf);
	} else {
		// Get rrset owner.
		if (style->reduce) {
			// If reduced style don't print extra spaces.
			ret = snprintf(dst + len, maxlen - len, "%s\t", name);
		} else {
			ret = snprintf(dst + len, maxlen - len, "%-20s\t", name);
		}
	}
	free(name);
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;

	// Dump rrset ttl.
	if (style->show_ttl) {
		if (style->verbose) {
			// Create human readable ttl string.
			ret = time_to_human_str(buf, sizeof(buf), rrset->ttl);
			if (ret < 0) {
				return KNOT_ESPACE;
			}
			ret = snprintf(dst + len, maxlen - len, "%s\t", buf);
		} else {
			ret = snprintf(dst + len, maxlen - len, "%6u\t",
			               rrset->ttl);
		}
	} else {
		ret = snprintf(dst + len, maxlen - len, "     \t");
	}
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;

	// Dump rrset class.
	if (style->show_class) {
		if (knot_rrclass_to_string(rrset->rclass, buf, sizeof(buf)) < 0)
		{
			return KNOT_ESPACE;
		}
		ret = snprintf(dst + len, maxlen - len, "%-2s\t", buf);
	} else {
		ret = snprintf(dst + len, maxlen - len, "  \t");
	}
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;

	// Dump rrset type.
	if (knot_rrtype_to_string(rrset->type, buf, sizeof(buf)) < 0) {
		return KNOT_ESPACE;
	}
	ret = snprintf(dst + len, maxlen - len, "%-5s\t", buf);
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;
	
	return len;
}

int knot_rrset_txt_dump(const knot_rrset_t      *rrset,
                        char                    *dst,
                        const size_t            maxlen,
                        const knot_dump_style_t *style)
{
	if (rrset == NULL || dst == NULL || style == NULL) {
		return KNOT_EINVAL;
	}

	size_t len = 0;
	int    ret;

	// Loop over rdata in rrset.
	for (size_t i = 0; i < rrset->rdata_count; i++) {
		// Dump rdata owner, class, ttl and type.
		ret = knot_rrset_txt_dump_header(rrset, i, dst + len,
		                                 maxlen - len, style);
		if (ret < 0) {
			return KNOT_ESPACE;
		}
		len += ret;

		// Dump rdata as such.
		ret = knot_rrset_txt_dump_data(rrset, i, dst + len,
		                               maxlen - len, style);
		if (ret < 0) {
			return KNOT_ESPACE;
		}
		len += ret;

		// Terminate line.
		if (len >= maxlen) {
			return KNOT_ESPACE;
		}
		dst[len++] = '\n';
		dst[len] = '\0';
	}

	// Dump RRSIG records if any via recursion call.
	if (rrset->rrsigs != NULL) {
		ret = knot_rrset_txt_dump(rrset->rrsigs, dst + len,
		                          maxlen - len, style);
		if (ret < 0) {
			return KNOT_ESPACE;
		}
		len += ret;
	}

	return len;
}
