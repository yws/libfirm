/* -------------------------------------------------------------------
 * $Id$
 * -------------------------------------------------------------------
 * Datentyp: Vereinfachte Map (hash-map) zum Speichern von
 * Zeigern/Adressen -> Zeigern/Adressen.
 *
 * Erstellt: Hubert Schmid, 09.06.2002
 * ---------------------------------------------------------------- */


#include "pmap.h"

#include <assert.h>
#include "set.h"


struct pmap {
  int dummy; /* dummy entry */
};


static const int INITIAL_SLOTS = 64;


static int pmap_entry_cmp(const pmap_entry * entry1, const pmap_entry * entry2, size_t size) {
  return entry1->key == entry2->key ? 0 : 1;
}


pmap * pmap_create(void) {
  return (pmap *) new_set((set_cmp_fun) pmap_entry_cmp, INITIAL_SLOTS);
}


void pmap_destroy(pmap * map) {
  del_set((set *) map);
}


void pmap_insert(pmap * map, void * key, void * value) {
  if (pmap_contains(map, key)) {
    pmap_entry * entry = pmap_find(map, key);
    entry->value = value;
  } else {
    pmap_entry entry;
    entry.key = key;
    entry.value = value;
    set_insert((set *) map, &entry, sizeof(pmap_entry), (unsigned) key);
  }
}


bool pmap_contains(pmap * map, void * key) {
  return set_find((set *) map, &key, sizeof(pmap_entry), (unsigned) key) != NULL;
}


pmap_entry * pmap_find(pmap * map, void * key) {
  return (pmap_entry *) set_find((set *) map, &key, sizeof(pmap_entry), (unsigned) key);
}


void * pmap_get(pmap * map, void * key) {
  pmap_entry * entry = pmap_find(map, key);
  return entry == NULL ? NULL : entry->value;
}


pmap_entry * pmap_first(pmap * map) {
  return (pmap_entry *) set_first((set *) map);
}


pmap_entry * pmap_next(pmap * map) {
  return (pmap_entry *) set_next((set *) map);
}
