/*
 * Replaces md4c's entity.c (46 KB table): mdconv always renders with
 * MD_HTML_FLAG_VERBATIM_ENTITIES, so md4c-html never looks up a name.
 */
#include "entity.h"

const ENTITY *entity_lookup(const char *name, size_t name_size)
{
    (void)name;
    (void)name_size;
    return NULL;
}
