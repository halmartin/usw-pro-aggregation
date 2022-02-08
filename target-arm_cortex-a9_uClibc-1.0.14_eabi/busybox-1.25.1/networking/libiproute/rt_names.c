/* vi: set sw=4 ts=4: */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include "libbb.h"
#include "rt_names.h"

#define CONFDIR          CONFIG_FEATURE_IP_ROUTE_DIR
#define RT_TABLE_MAX	65535

struct rtnl_tab_entry {
	unsigned int id;
	const char *name;
};

typedef struct rtnl_tab_t {
	struct rtnl_tab_entry *tab;
	size_t length;
} rtnl_tab_t;

static int tabcmp(const void *p1, const void *p2)
{
	const struct rtnl_tab_entry *e1 = p1;
	const struct rtnl_tab_entry *e2 = p2;
	return strcmp(e1->name, e2->name);
}

static void rtnl_tab_initialize(const char *file, rtnl_tab_t *tab)
{
	char *token[2];
	char fullname[sizeof(CONFDIR"/rt_dsfield") + 8];
	parser_t *parser;

	sprintf(fullname, CONFDIR"/rt_%s", file);
	parser = config_open2(fullname, fopen_for_read);
	while (config_read(parser, token, 2, 2, "# \t", PARSE_NORMAL)) {
		unsigned id = bb_strtou(token[0], NULL, 0);
		if (id > RT_TABLE_MAX) {
			bb_error_msg("database %s is corrupted at line %d",
				file, parser->lineno);
			break;
		}

		tab->tab = xrealloc(tab->tab, (tab->length + 1) * sizeof(*tab->tab));
		tab->tab[tab->length].id = id;
		tab->tab[tab->length].name = xstrdup(token[1]);
		tab->length++;
	}
	config_close(parser);
	qsort(tab->tab, tab->length, sizeof(*tab->tab), tabcmp);
}

static int rtnl_a2n(rtnl_tab_t *tab, uint32_t *id, const char *arg, int base)
{
	int delta;
	ssize_t l = 0;
	ssize_t r = tab->length - 1;
	ssize_t m;
	uint32_t i;

	while (l <= r) {
		m = l + (r - l) / 2;
		delta = strcmp(tab->tab[m].name, arg);

		if (delta == 0) {
			*id = tab->tab[m].id;
			return 0;
		} else if (delta < 0) {
			l = m + 1;
		} else {
			r = m - 1;
		}
	}

	i = bb_strtou(arg, NULL, base);
	if (i > RT_TABLE_MAX)
		return -1;

	*id = i;
	return 0;
}


static rtnl_tab_t *rtnl_rtprot_tab;

static void rtnl_rtprot_initialize(void)
{
	static const struct rtnl_tab_entry init_tab[] = {
		{  0, "none"     },
		{  1, "redirect" },
		{  2, "kernel"   },
		{  3, "boot"     },
		{  4, "static"   },
		{  8, "gated"    },
		{  9, "ra"       },
		{ 10, "mrt"      },
		{ 11, "zebra"    },
		{ 12, "bird"     }
	};

	if (rtnl_rtprot_tab)
		return;
	rtnl_rtprot_tab = xzalloc(sizeof(*rtnl_rtprot_tab));
	rtnl_rtprot_tab->tab = xzalloc(sizeof(init_tab));
	rtnl_rtprot_tab->length = sizeof(init_tab) / sizeof(init_tab[0]);
	memcpy(rtnl_rtprot_tab->tab, init_tab, sizeof(init_tab));
	rtnl_tab_initialize("protos", rtnl_rtprot_tab);
}

#if 0 /* UNUSED */
const char* FAST_FUNC rtnl_rtprot_n2a(int id)
{
	size_t i;

	rtnl_rtprot_initialize();

	for (i = 0; i < rtnl_rtprot_tab->length; i++)
		if (rtnl_rtprot_tab->tab[i].id == id)
			return rtnl_rtprot_tab->tab[i].name;

	return itoa(id);
}
#endif

int FAST_FUNC rtnl_rtprot_a2n(uint32_t *id, char *arg)
{
	rtnl_rtprot_initialize();
	return rtnl_a2n(rtnl_rtprot_tab, id, arg, 0);
}


static rtnl_tab_t *rtnl_rtscope_tab;

static void rtnl_rtscope_initialize(void)
{
	static const struct rtnl_tab_entry init_tab[] = {
		{   0, "global"  },
		{ 200, "site"    },
		{ 253, "link"    },
		{ 254, "host"    },
		{ 255, "nowhere" }
	};

	if (rtnl_rtscope_tab)
		return;
	rtnl_rtscope_tab = xzalloc(sizeof(*rtnl_rtscope_tab));
	rtnl_rtscope_tab->tab = xzalloc(sizeof(init_tab));
	rtnl_rtscope_tab->length = sizeof(init_tab) / sizeof(init_tab[0]);
	memcpy(rtnl_rtscope_tab->tab, init_tab, sizeof(init_tab));
	rtnl_tab_initialize("scopes", rtnl_rtscope_tab);
}

const char* FAST_FUNC rtnl_rtscope_n2a(int id)
{
	size_t i;

	rtnl_rtscope_initialize();

	for (i = 0; i < rtnl_rtscope_tab->length; i++)
		if (rtnl_rtscope_tab->tab[i].id == id)
			return rtnl_rtscope_tab->tab[i].name;

	return itoa(id);
}

int FAST_FUNC rtnl_rtscope_a2n(uint32_t *id, char *arg)
{
	rtnl_rtscope_initialize();
	return rtnl_a2n(rtnl_rtscope_tab, id, arg, 0);
}


static rtnl_tab_t *rtnl_rtrealm_tab;

static void rtnl_rtrealm_initialize(void)
{
	static const struct rtnl_tab_entry init_tab[] = {
		{ 0, "unknown" }
	};

	if (rtnl_rtrealm_tab)
		return;
	rtnl_rtrealm_tab = xzalloc(sizeof(*rtnl_rtrealm_tab));
	rtnl_rtrealm_tab->tab = xzalloc(sizeof(init_tab));
	rtnl_rtrealm_tab->length = sizeof(init_tab) / sizeof(init_tab[0]);
	memcpy(rtnl_rtrealm_tab->tab, init_tab, sizeof(init_tab));
	rtnl_tab_initialize("realms", rtnl_rtrealm_tab);
}

int FAST_FUNC rtnl_rtrealm_a2n(uint32_t *id, char *arg)
{
	rtnl_rtrealm_initialize();
	return rtnl_a2n(rtnl_rtrealm_tab, id, arg, 0);
}

#if ENABLE_FEATURE_IP_RULE
const char* FAST_FUNC rtnl_rtrealm_n2a(int id)
{
	size_t i;

	rtnl_rtrealm_initialize();

	for (i = 0; i < rtnl_rtrealm_tab->length; i++)
		if (rtnl_rtrealm_tab->tab[i].id == id)
			return rtnl_rtrealm_tab->tab[i].name;

	return itoa(id);
}
#endif


static rtnl_tab_t *rtnl_rtdsfield_tab;

static void rtnl_rtdsfield_initialize(void)
{
	static const struct rtnl_tab_entry init_tab[] = {
		{ 0, "0" }
	};

	if (rtnl_rtdsfield_tab)
		return;
	rtnl_rtdsfield_tab = xzalloc(sizeof(*rtnl_rtdsfield_tab));
	rtnl_rtdsfield_tab->tab = xzalloc(sizeof(init_tab));
	rtnl_rtdsfield_tab->length = sizeof(init_tab) / sizeof(init_tab[0]);
	memcpy(rtnl_rtdsfield_tab->tab, init_tab, sizeof(init_tab));
	rtnl_tab_initialize("dsfield", rtnl_rtdsfield_tab);
}

const char* FAST_FUNC rtnl_dsfield_n2a(int id)
{
	size_t i;

	rtnl_rtdsfield_initialize();

	for (i = 0; i < rtnl_rtdsfield_tab->length; i++)
		if (rtnl_rtdsfield_tab->tab[i].id == id)
			return rtnl_rtdsfield_tab->tab[i].name;

	return itoa(id);
}

int FAST_FUNC rtnl_dsfield_a2n(uint32_t *id, char *arg)
{
	rtnl_rtdsfield_initialize();
	return rtnl_a2n(rtnl_rtdsfield_tab, id, arg, 16);
}


#if ENABLE_FEATURE_IP_RULE
static rtnl_tab_t *rtnl_rttable_tab;

static void rtnl_rttable_initialize(void)
{
	static const struct rtnl_tab_entry tab_init[] = {
		{   0, "unspec"  },
		{ 253, "default" },
		{ 254, "main"    },
		{ 255, "local"   }
	};

	if (rtnl_rttable_tab)
		return;

	rtnl_rttable_tab = xzalloc(sizeof(*rtnl_rttable_tab));
	rtnl_rttable_tab->tab = xzalloc(sizeof(tab_init));
	rtnl_rttable_tab->length = sizeof(tab_init) / sizeof(tab_init[0]);
	memcpy(rtnl_rttable_tab->tab, tab_init, sizeof(tab_init));
	rtnl_tab_initialize("tables", rtnl_rttable_tab);
}

const char* FAST_FUNC rtnl_rttable_n2a(int id)
{
	size_t i;

	rtnl_rttable_initialize();

	for (i = 0; i < rtnl_rttable_tab->length; i++)
		if (rtnl_rttable_tab->tab[i].id == id)
			return rtnl_rttable_tab->tab[i].name;

	return itoa(id);
}

int FAST_FUNC rtnl_rttable_a2n(uint32_t *id, char *arg)
{
	rtnl_rttable_initialize();
	return rtnl_a2n(rtnl_rttable_tab, id, arg, 0);
}

#endif
