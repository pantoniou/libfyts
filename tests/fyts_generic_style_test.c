#include <string.h>
#include <stdlib.h>
#include <fyts/fyts.h>

struct output {
	char *data;
	size_t len;
	size_t cap;
};

static int write_output(const void *data, size_t len, void *user)
{
	struct output *out = user;
	char *next;
	size_t cap;

	if (len > out->cap - out->len) {
		cap = out->cap ? out->cap * 2 : 128;
		while (len > cap - out->len)
			cap *= 2;
		next = realloc(out->data, cap);
		if (!next)
			return -1;
		out->data = next;
		out->cap = cap;
	}
	memcpy(out->data + out->len, data, len);
	out->len += len;
	return 0;
}

static int has_text(const char *data, size_t len, const char *needle)
{
	size_t needle_len = strlen(needle);
	size_t i;

	if (needle_len > len)
		return 0;
	for (i = 0; i <= len - needle_len; i++) {
		if (memcmp(data + i, needle, needle_len) == 0)
			return 1;
	}
	return 0;
}

int main(void)
{
	static const char style_yaml[] = "captures:\n"
					 "  - capture: ^string$\n"
					 "    attribute: string\n"
					 "attributes:\n"
					 "  string:\n"
					 "    default: red\n"
					 "styles:\n"
					 "  red: \"\\e[31m\"\n";
	static const char source[] = "const char *s = \"generic\";\n";
	struct fy_generic_builder *gb = NULL;
	fy_generic_sized_string input;
	struct fyts_config config = {0};
	struct output out = {0};
	fy_generic styling;
	int rc = 1;

	gb = fy_generic_builder_create(NULL);
	if (!gb)
		goto done;
	input.data = style_yaml;
	input.size = sizeof(style_yaml) - 1;
	styling = fy_parse(gb, input, FYOPPF_DEFAULT, NULL);
	if (!fy_generic_is_valid(styling))
		goto done;

	config.lang = "c";
	config.styling = styling;
	config.color_mode = FYTS_COLOR_ON;
	config.write = write_output;
	config.write_user = &out;
	if (fyts_highlight_source(&config, source, sizeof(source) - 1))
		goto done;
	if (!has_text(out.data, out.len, "\033[31m\"generic\""))
		goto done;
	rc = 0;

done:
	if (gb)
		fy_generic_builder_destroy(gb);
	free(out.data);
	return rc;
}
