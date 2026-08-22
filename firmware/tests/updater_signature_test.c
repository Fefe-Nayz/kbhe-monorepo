#include "firmware_public_key.h"
#include "monocypher-ed25519.h"
#include "updater_shared.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RELEASE_SIGNING_VECTOR_PATH
#error "RELEASE_SIGNING_VECTOR_PATH must name the shared JSON test vector"
#endif

static uint8_t hex_nibble(char value) {
  if (value >= '0' && value <= '9') {
    return (uint8_t)(value - '0');
  }
  value = (char)tolower((unsigned char)value);
  assert(value >= 'a' && value <= 'f');
  return (uint8_t)(value - 'a' + 10);
}

static const char *decode_named_hex(const char *json, const char *name,
                                    uint8_t *out, size_t out_len) {
  const char *cursor = strstr(json, name);
  assert(cursor != NULL);
  cursor = strchr(cursor + strlen(name), ':');
  assert(cursor != NULL);
  cursor = strchr(cursor, '"');
  assert(cursor != NULL);
  ++cursor;
  for (size_t i = 0; i < out_len; ++i) {
    assert(isxdigit((unsigned char)cursor[i * 2u]));
    assert(isxdigit((unsigned char)cursor[i * 2u + 1u]));
    out[i] = (uint8_t)((hex_nibble(cursor[i * 2u]) << 4) |
                       hex_nibble(cursor[i * 2u + 1u]));
  }
  assert(cursor[out_len * 2u] == '"');
  return cursor + out_len * 2u + 1u;
}

int main(void) {
  FILE *stream = fopen(RELEASE_SIGNING_VECTOR_PATH, "rb");
  char json[4096];
  uint8_t public_key[32];
  uint8_t manifest[UPDATER_SIGNATURE_MANIFEST_SIZE];
  uint8_t signature[UPDATER_SIGNATURE_SIZE];
  size_t json_len;

  assert(stream != NULL);
  json_len = fread(json, 1u, sizeof(json) - 1u, stream);
  assert(!ferror(stream));
  assert(feof(stream));
  assert(fclose(stream) == 0);
  json[json_len] = '\0';

  _Static_assert(sizeof(updater_signature_manifest_t) == 84u,
                 "signature manifest must remain canonical");
  const char *firmware_section = strstr(json, "\"firmware\"");
  assert(firmware_section != NULL);
  (void)decode_named_hex(json, "\"publicKeyHex\"", public_key,
                         sizeof(public_key));
  (void)decode_named_hex(firmware_section, "\"manifestHex\"", manifest,
                         sizeof(manifest));
  (void)decode_named_hex(firmware_section, "\"signatureHex\"", signature,
                         sizeof(signature));

  assert(memcmp(public_key, KBHE_FIRMWARE_RELEASE_PUBLIC_KEY,
                sizeof(public_key)) == 0);
  assert(crypto_ed25519_check(signature, public_key, manifest,
                              sizeof(manifest)) == 0);
  manifest[20] ^= 1u;
  assert(crypto_ed25519_check(signature, public_key, manifest,
                              sizeof(manifest)) != 0);

  puts("updater_signature_test: ok");
  return 0;
}
