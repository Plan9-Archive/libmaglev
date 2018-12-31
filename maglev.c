#include <u.h>
#include <libc.h>

#include "maglev.h"

/*
 * consistent hashing from:
 * Maglev: A Fast and Reliable Software Network Load Balancer
 */

/* thanks bob */
static u32int
one_at_a_time(u32int hash, char *key, int len)
{
	int i;

	for(i=0; i<len; ++i){
		hash += key[i];
		hash += hash << 10;
		hash ^= hash >> 6;
	}
	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;

	return hash;
}

typedef struct Backend Backend;
struct Backend {
	char *name;
	int namelen;

	int weight;
};

struct Maglev {
	u32int (*hash)(u32int, char*, int); /* hash function */

	u32int N;	/* size of backend pool */
	u32int M;	/* size of the lookup table */

	Backend *backends;
	int *lookup;
};

enum {
	SmallM	= 65537,
};

Maglev*
maglev_new(int M, u32int (*hash)(u32int init, char *key, int keylen))
{
	Maglev *mag;

	if(M == 0)
		M = SmallM;

	if(hash == nil)
		hash = one_at_a_time;

	mag = malloc(sizeof(*mag));
	if(mag == nil)
		return nil;

	mag->hash = hash;
	mag->N = 0;
	mag->M = M;
	mag->backends = nil;
	mag->lookup = nil;

	return mag;
}

void
maglev_free(Maglev *m)
{
	int i;

	for(i = 0; i < m->N; i++)
		free(m->backends[i].name);
	free(m->backends);
	free(m->lookup);
}

static u32int**
generate_permutations(Maglev *m)
{
	int i, j;
	u32int offset, skip, idx;
	u32int **permutations;
	Backend *b;

	if(m->N == 0)
		return nil;

	assert(m->M > (m->N * 100));

	permutations = malloc(sizeof(u32int*) * m->N);
	if(permutations == nil)
		return nil;

	for(i = 0; i < m->N; i++){
		permutations[i] = malloc(sizeof(u32int) * m->M);
		if(permutations[i] == nil)
			goto fail;
	}

	for(i = 0; i < m->N; i++){
		b = &m->backends[i];
		offset = m->hash(0xCAFE, b->name, b->namelen) % m->M;
		skip = m->hash(0xBABE, b->name, b->namelen) % (m->M - 1) + 1;

		idx = offset;
		for(j = 0; j < m->M; j++){
			permutations[i][j] = idx;
			idx += skip;
			if(idx >= m->M)
				idx -= m->M;
		}
	}

	return permutations;

fail:
	for(i = 0; i < m->N; i++)
		free(permutations[i]);

	free(permutations);
	return nil;
}

static int
maglev_populate(Maglev *m)
{
	int i, j, n, *entry;
	u32int c, *next, **permutations;

	if(m->N == 0)
		return -1;

	permutations = generate_permutations(m);
	if(permutations == nil)
		return -1;

	next = mallocz(sizeof(u32int) * m->N, 1);
	entry = malloc(sizeof(int) * m->M);
	if(next == nil || entry == nil){
		free(permutations);
		free(next);
		free(entry);
		return -1;
	}

	for(j = 0; j < m->M; j++){
		entry[j] = -1;
	}

	n = 0;

	for(;;){
		for(i = 0; i < m->N; i++){
			for(j = 0; j < m->backends[i].weight; j++){
				c = permutations[i][next[i]];
				while(entry[c] >= 0){
					next[i]++;
					c = permutations[i][next[i]];
				}
	
				entry[c] = i;
				next[i]++;
				n++;
	
				if(n == m->M)
					goto out;
			}
		}
	}

out:
	for(i = 0; i < m->N; i++)
		free(permutations[i]);
	free(permutations);
	free(next);
	free(m->lookup);
	m->lookup = entry;

	return 0;
}

int
maglev_add_weight(Maglev *m, char *backend, int weight)
{
	int i;
	char *dup;
	Backend *backends;

	for(i = 0; i < m->N; i++){
		if(strcmp(backend, m->backends[i].name) == 0){
			werrstr("backend already exists");
			return -1;
		}
	}

	dup = strdup(backend);
	if(dup == nil)
		return -1;

	backends = realloc(m->backends, sizeof(Backend) * (m->N+1));
	if(backends == nil){
		free(dup);
		return -1;
	}

	backends[m->N++] = (Backend){dup, strlen(dup), weight};

	m->backends = backends;

	/* XXX: if this fails the state is inconsistent */
	return maglev_populate(m);
}

int
maglev_add(Maglev *m, char *backend)
{
	return maglev_add_weight(m, backend, 1);
}

int
maglev_remove(Maglev *m, char *backend)
{
	int i, idx;

	idx = 0;

	for(i = 0; i < m->N; i++){
		if(strcmp(backend, m->backends[i].name) == 0){
			idx = i;
			break;
		}
	}

	if(i == m->N){
		werrstr("backend not found");
		return -1;
	}

	memmove(m->backends + idx, m->backends + idx + 1, (m->N - (idx + 1)) * sizeof(Backend));
	m->N--;

	m->backends = realloc(m->backends, sizeof(Backend*) * m->N);

	assert(m->backends != nil);

	return maglev_populate(m);
}

static int
maglev_lookup(Maglev *m, char *key)
{
	u32int h;
	h = m->hash(0xCAFE, key, strlen(key));

	return m->lookup[h % m->M];
}

char*
maglev_get(Maglev *m, char *key)
{
	int idx;

	if(m->N == 0){
		werrstr("maglev is empty");
		return nil;
	}

	idx = maglev_lookup(m, key);

	return m->backends[idx].name;
}

int
maglev_get_multi(Maglev *m, char *key, char **result, int nresult)
{
	int i, idx;

	if(nresult > m->N){
		werrstr("not enough backends");
		return -1;
	}

	idx = maglev_lookup(m, key);

	for(i = 0; i < nresult; i++)
		result[i] = m->backends[idx++ % m->N].name;

	return nresult;
}
