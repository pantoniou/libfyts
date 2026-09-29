// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fyts/fyts.h>

/* The prefix shows two columns; its escapes take none. */
static const char styled_prefix[] = "\033[38;2;181;164;234m\033[48;2;24;22;18m| \033[0m";

static int discard_output(const void *data, size_t len, void *user)
{
	(void)data;
	(void)len;
	(void)user;
	return 0;
}

static char *highlight(int width, size_t *out_len)
{
	struct fyts_config config = {0};
	struct fyts_ctx *ctx;
	char *out = NULL;
	size_t len = 0;

	*out_len = 0;
	config.lang = "c";
	config.color_mode = FYTS_COLOR_OFF;
	config.write = discard_output;
	config.line_prefix = styled_prefix;
	config.width = width;
	ctx = fyts_ctx_create(&config);
	if (!ctx)
		return NULL;
	if (fyts_ctx_highlight_source(ctx, "abcdefghijklmnopqrstuvwxyz;\n", 28,
				      &out, &len)) {
		free(out);
		out = NULL;
	}
	fyts_ctx_destroy(ctx);
	*out_len = out ? len : 0;
	return out;
}

static int check(int width, const char *want, const char *forbid)
{
	size_t len;
	char *out = highlight(width, &len);
	int ok;

	/* The output is not NUL terminated, so search it by length. */
	ok = out && memmem(out, len, want, strlen(want)) &&
	     !memmem(out, len, forbid, strlen(forbid));
	if (!ok)
		fprintf(stderr, "width %d: want \"%s\", forbid \"%s\", got \"%.*s\"\n",
			width, want, forbid, (int)len, out ? out : "(none)");
	free(out);
	return ok;
}

int main(void)
{
	int ok = 1;

	/* Twenty columns less the two of the prefix leave eighteen. */
	ok &= check(20, "abcdefghijklmnopqr", "abcdefghijklmnopqrs");
	/* A prefix as wide as the clip leaves one column. */
	ok &= check(2, "a", "ab");
	return ok ? 0 : 1;
}
