#include "spools.h"
#include "bambuddy_parse.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

void spools_filter(JsonDocument &filter)
{
  for (const char *k : {"id", "material", "subtype", "color_name", "rgba", "brand", "label_weight", "weight_used"})
    filter[0][k] = true;
}

uint32_t parse_rgb(const char *hex)
{
  if (!hex) return 0;
  if (*hex == '#') hex++;
  size_t n = strlen(hex);
  if (n != 6 && n != 8) return 0;
  uint32_t v = 0;
  for (int i = 0; i < 6; i++) {
    char c = hex[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return 0;
    v = v << 4 | (uint32_t)d;
  }
  return v;
}

void spool_name(char *out, size_t size, const char *brand, const char *subtype, const char *color)
{
  char raw[96];
  auto has = [](const char *s) { return s && *s; };
  // The colour's name tells spools of one brand apart; the subtype stands in when there is none.
  const char *second = has(color) ? color : has(subtype) ? subtype : "";
  snprintf(raw, sizeof(raw), "%s%s%s", has(brand) ? brand : "", has(brand) && *second ? " " : "", second);
  copy_ascii(out, size, raw[0] ? raw : "Spool");
}

bool parse_spools(JsonDocument &doc, SpoolStore &st)
{
  if (!st.items) st.items = (Spool *)calloc(MAX_SPOOLS, sizeof(Spool));
  if (!st.items) return false;
  st.count = st.total = 0;
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    int id = o["id"] | 0;
    if (id <= 0) continue;
    st.total++;
    if (st.count >= MAX_SPOOLS) continue;
    Spool &s = st.items[st.count++];
    s = Spool();
    s.id = id;
    copy_ascii(s.material, sizeof(s.material), o["material"] | "");
    spool_name(s.name, sizeof(s.name), o["brand"], o["subtype"], o["color_name"]);
    s.rgb = parse_rgb(o["rgba"]);
    if (o["label_weight"].is<float>() && o["weight_used"].is<float>()) {
      float left = o["label_weight"].as<float>() - o["weight_used"].as<float>();
      s.remainG = left < 0 ? 0 : left > 100000 ? 100000 : (int)(left + 0.5f);
    }
  }
  st.state = SpoolState::Ready;
  st.code = 200;
  return true;
}

bool spools_copy(const SpoolStore &from, SpoolStore &to)
{
  to.state = from.state;
  to.code = from.code;
  to.total = from.total;
  if (!from.items || from.count == 0) {
    to.count = 0;
    return true;
  }
  if (!to.items) to.items = (Spool *)calloc(MAX_SPOOLS, sizeof(Spool));
  if (!to.items) {
    to.count = 0;
    return false;
  }
  memcpy(to.items, from.items, sizeof(Spool) * (size_t)from.count);
  to.count = from.count;
  return true;
}

static bool is_of(const Spool &s, const char *material) { return !material || !*material || strcasecmp(s.material, material) == 0; }

int spools_page(const SpoolStore &st, const char *material, int offset, Spool *out, int max, int &total)
{
  total = 0;
  int given = 0;
  for (int i = 0; i < st.count; i++) {
    if (!is_of(st.items[i], material)) continue;
    if (total >= offset && given < max) out[given++] = st.items[i];
    total++;
  }
  return given;
}

int spools_materials(const SpoolStore &st, char out[][SPOOL_MATERIAL], int max)
{
  int n = 0;
  for (int i = 0; i < st.count; i++) {
    const char *m = st.items[i].material;
    if (!*m) continue;
    bool seen = false;
    for (int j = 0; j < n; j++) seen = seen || strcasecmp(out[j], m) == 0;
    if (!seen && n < max) snprintf(out[n++], SPOOL_MATERIAL, "%s", m);
  }
  return n;
}
