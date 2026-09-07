/*
 * A compiled query is shared by every context that asks for the same
 * language and query file. Check that the sharing is invisible: two live
 * contexts render the same, a context outlives the one that compiled the
 * query, and an edited query file takes effect.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fyts/fyts.h>

static const char source[] = "int main(void) { return 0; }\n";
static const char query_type[] = "(primitive_type) @type\n";
static const char query_ident[] = "(identifier) @variable\n";

static int discard_output(const void *data, size_t len, void *user)
{
	(void)data;
	(void)len;
	(void)user;
	return 0;
}

static struct fyts_ctx *make_ctx(const char *path)
{
	struct fyts_config config = {0};

	config.lang = "c";
	config.color_mode = FYTS_COLOR_ON;
	config.write = discard_output;
	config.query_path = path;
	return fyts_ctx_create(&config);
}

static char *render(struct fyts_ctx *ctx, size_t *len)
{
	char *out = NULL;

	if (!ctx || fyts_ctx_highlight_source(ctx, source, sizeof(source) - 1, &out, len)) {
		free(out);
		return NULL;
	}
	return out;
}

static int write_query(const char *path, const char *text)
{
	FILE *file;

	file = fopen(path, "wb");
	if (!file)
		return -1;
	if (fwrite(text, 1, strlen(text), file) != strlen(text)) {
		fclose(file);
		return -1;
	}
	return fclose(file) ? -1 : 0;
}

int main(void)
{
	char path[] = "/tmp/fyts-query-cache-XXXXXX";
	struct fyts_ctx *first = NULL;
	struct fyts_ctx *second = NULL;
	struct fyts_ctx *third = NULL;
	char *a = NULL, *b = NULL, *c = NULL, *d = NULL, *e = NULL;
	size_t a_len = 0, b_len = 0, c_len = 0, d_len = 0, e_len = 0;
	int fd;
	int rc = 1;

	fd = mkstemp(path);
	if (fd < 0)
		return 1;
	if (close(fd) || write_query(path, query_type))
		goto done;

	/* Two contexts on one query file render the same. */
	first = make_ctx(path);
	second = make_ctx(path);
	a = render(first, &a_len);
	b = render(second, &b_len);
	if (!a || !b || a_len != b_len || memcmp(a, b, a_len))
		goto done;

	/* The query outlives the context that compiled it. */
	fyts_ctx_destroy(first);
	first = NULL;
	c = render(second, &c_len);
	if (!c || c_len != a_len || memcmp(c, a, a_len))
		goto done;

	/* An edited query file takes effect. The two queries are the same
	 * size and the edit lands in the same second, so only the text
	 * separates them. */
	if (write_query(path, query_ident))
		goto done;
	third = make_ctx(path);
	d = render(third, &d_len);
	if (!d || (d_len == a_len && !memcmp(d, a, a_len)))
		goto done;

	/* The retired query stays valid for the context still holding it. */
	e = render(second, &e_len);
	if (!e || e_len != a_len || memcmp(e, a, a_len))
		goto done;
	rc = 0;

done:
	fyts_ctx_destroy(first);
	fyts_ctx_destroy(second);
	fyts_ctx_destroy(third);
	free(a);
	free(b);
	free(c);
	free(d);
	free(e);
	unlink(path);
	return rc;
}
