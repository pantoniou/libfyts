#include <string.h>
#include <stdlib.h>
#include <fyts/fyts.h>

struct output {
	char *data;
	size_t len;
	size_t cap;
};

static int append_output(struct output *out, char **datap, size_t len)
{
	char *next;
	size_t cap;
	char *data;

	data = *datap;
	if (!data)
		return 0;
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
	free(data);
	*datap = NULL;
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
	struct output out = {0};
	struct fyts_config config = {0};
	struct fyts_ctx *ctx;
	char *chunk = NULL;
	size_t chunk_len = 0;
	int rc = 1;

	config.lang = "c";
	config.color_mode = FYTS_COLOR_ON;

	ctx = fyts_ctx_create(&config);
	if (!ctx)
		goto done;
	if (fyts_ctx_feed(ctx, "int main", 8, &chunk, &chunk_len))
		goto done;
	if (append_output(&out, &chunk, chunk_len))
		goto done;
	if (out.len != 0)
		goto done;
	if (fyts_ctx_feed(ctx, "(void) {\n", 9, &chunk, &chunk_len))
		goto done;
	if (append_output(&out, &chunk, chunk_len))
		goto done;
	if (!out.data || !has_text(out.data, out.len, "main"))
		goto done;
	if (fyts_ctx_feed(ctx, "  return 0;\n", 12, &chunk, &chunk_len))
		goto done;
	if (append_output(&out, &chunk, chunk_len))
		goto done;
	if (fyts_ctx_feed(ctx, "}", 1, &chunk, &chunk_len))
		goto done;
	if (append_output(&out, &chunk, chunk_len))
		goto done;
	if (!has_text(out.data, out.len, "return") || has_text(out.data, out.len, "}"))
		goto done;
	if (fyts_ctx_finish(ctx, &chunk, &chunk_len))
		goto done;
	if (append_output(&out, &chunk, chunk_len))
		goto done;
	if (!out.data || !has_text(out.data, out.len, "return"))
		goto done;
	if (!has_text(out.data, out.len, "}"))
		goto done;
	if (!has_text(out.data, out.len, "\033["))
		goto done;
	rc = 0;

done:
	fyts_ctx_destroy(ctx);
	free(chunk);
	free(out.data);
	return rc;
}
