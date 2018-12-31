#include "maglev.c"

static void
dump_counts(Maglev *m)
{
	int i;
	int counts[10];

	assert(m->N <= nelem(counts));

	memset(counts, 0, sizeof(counts));

	for(i = 0; i < m->M; i++)
		counts[m->lookup[i]]++;

	print(">> counts:\n");
	for(i = 0; i < m->N; i++)
		print("%s = %d\n", m->backends[i].name, counts[i]);
}

static void
dump_backends(Maglev *m)
{
	int i;

	print(">> backend set:\n");
	for(i = 0; i < m->N; i++){
		print("%s\n", m->backends[i].name);
	}
}

static void
dump_tab(Maglev *m)
{
	int i;

	print(">> lookup table:\n");
	for(i = 0; i < m->M; i++){
		print("lookup[%d] = %d\n", i, m->lookup[i]);
	}
}

void
main(int argc, char *argv[])
{
	int i, rv;
	char *addr, buf[128];
	char *multi[2];
	Maglev *m;

	ARGBEGIN{
	}ARGEND

	m = maglev_new(0, nil);
	assert(m != nil);

	rv = maglev_remove(m, "tcp!127.0.0.1!80");
	assert(rv == -1);

	print(">> add tcp!127.0.0.1!80\n");
	rv = maglev_add_weight(m, "tcp!127.0.0.1!80", 2);
	assert(rv == 0);

	dump_backends(m);
	dump_counts(m);

	print(">> add tcp!127.0.0.2!80\n");
	rv = maglev_add(m, "tcp!127.0.0.2!80");
	assert(rv == 0);

	dump_backends(m);
	dump_counts(m);

	for(i = 0; i < 16; i++){
		snprint(buf, sizeof(buf), "object_%d_key", i*37);
		addr = maglev_get(m, buf);
		assert(addr != nil);

		print("backends[%s] = %s\n", buf, addr);
	}

	print(">> add tcp!127.0.0.3!80\n");
	rv = maglev_add(m, "tcp!127.0.0.3!80");
	assert(rv == 0);

	dump_backends(m);
	dump_counts(m);

	for(i = 0; i < 16; i++){
		snprint(buf, sizeof(buf), "object_%d_key", i*37);
		addr = maglev_get(m, buf);
		assert(addr != nil);

		print("backends[%s] = %s\n", buf, addr);
	}

//	dump_tab(m);

	print(">> get multi:\n");
	for(i = 0; i < 16; i++){
		snprint(buf, sizeof(buf), "object_%d_key", i*37);

		rv = maglev_get_multi(m, buf, multi, nelem(multi));
		assert(rv == nelem(multi));

		print("backends[%s] = %s, %s\n", buf, multi[0], multi[1]);
	}

	maglev_free(m);

	exits(nil);
}
