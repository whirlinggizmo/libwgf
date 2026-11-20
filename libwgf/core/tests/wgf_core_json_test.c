#include <stdio.h>
#include <string.h>

#include "wgf_core_json_priv.h"

/* JSON read through cJSON: objects, arrays, members in the text's order with their keys,
 * numbers, strings with escapes, truths, and what isn't JSON. */

static int failures;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    static const char text[] = "{\"frames\": {\"b.png\": {\"x\": 2}, \"a.png\": {\"x\": 1}},"
                               " \"list\": [1, 2.5, \"three\\n\"], \"on\": true, \"off\": false}";
    wgf_core_priv_json_t *json = wgf_core_priv_json_parse(text, sizeof(text) - 1);
    const wgf_core_priv_json_t *frames = wgf_core_priv_json_get(json, "frames");
    const wgf_core_priv_json_t *list = wgf_core_priv_json_get(json, "list");
    expect(json != NULL && wgf_core_priv_json_is_object(json), "an object");
    expect(wgf_core_priv_json_size(frames) == 2 &&
               strcmp(wgf_core_priv_json_key(wgf_core_priv_json_at(frames, 0)), "b.png") == 0 &&
               wgf_core_priv_json_get_number(wgf_core_priv_json_at(frames, 1), "x", 0) == 1,
           "members in the text's order, with their keys");
    expect(wgf_core_priv_json_is_array(list) && wgf_core_priv_json_size(list) == 3 &&
               wgf_core_priv_json_number(wgf_core_priv_json_at(list, 1), 0) == 2.5 &&
               strcmp(wgf_core_priv_json_string(wgf_core_priv_json_at(list, 2)), "three\n") == 0,
           "an array's numbers and an escaped string");
    expect(wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "on")) &&
               !wgf_core_priv_json_truth(wgf_core_priv_json_get(json, "off")),
           "truths");
    expect(wgf_core_priv_json_get(json, "nothing") == NULL && wgf_core_priv_json_get_number(json, "on", -1) == -1 &&
               strcmp(wgf_core_priv_json_get_string(json, "list"), "") == 0 && wgf_core_priv_json_at(list, 9) == NULL &&
               wgf_core_priv_json_size(wgf_core_priv_json_get(json, "on")) == 0,
           "none, or the wrong kind: the fallback");
    wgf_core_priv_json_free(json);
    expect(wgf_core_priv_json_parse("{\"a\": ", 6) == NULL && wgf_core_priv_json_parse(NULL, 0) == NULL,
           "text that isn't JSON: none");
    if (failures == 0) printf("wgf_core_json_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
