typedef struct Maglev Maglev;

#pragma incomplete Maglev

Maglev* maglev_new(int M, u32int (*hash)(u32int init, char *key, int keylen));
void maglev_free(Maglev *m);
int maglev_add(Maglev *m, char *backend);
int maglev_add_weight(Maglev *m, char *backend, int weight);
int maglev_remove(Maglev *m, char *backend);
char *maglev_get(Maglev *m, char *key);
int maglev_get_multi(Maglev *m, char *key, char **result, int nresult);