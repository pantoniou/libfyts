// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fyts/fyts.h>
#include <libfypalette.h>

/* Every role has a colour of its own, so the output says which role styled a
 * capture. */
static const char theme[] = "colors:\n"
			    "  ink: '#010101'\n"
			    "  kw: '#020202'\n"
			    "  str: '#030303'\n"
			    "  cstr: '#040404'\n"
			    "  blk: '#050505'\n"
			    "roles:\n"
			    "  code:\n"
			    "    fg: ink\n"
			    "    block: {bg: blk}\n"
			    "    keyword: {fg: kw}\n"
			    "    string: {fg: str}\n"
			    "    c:\n"
			    "      string: {fg: cstr}\n";

static int failures;

#define CHECK(cond)                                                                                \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);   \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

/* Report a highlighted output that lacks want or holds forbid, with escapes
 * made visible. */
static void expect(int line, const char *out, const char *want, const char *forbid)
{
	const char *p;

	if (out && (!want || strstr(out, want)) && (!forbid || !strstr(out, forbid)))
		return;
	fprintf(stderr, "%s:%d: output %s \"%s\"%s%s%s:\n", __FILE__, line,
		out ? "lacks" : "missing", want ? want + (*want == '\033') : "",
		forbid ? " or holds \"" : "", forbid ? forbid : "", forbid ? "\"" : "");
	for (p = out ? out : ""; *p; p++)
		fputs(*p == '\033' ? "\\e" : (char[]){*p, '\0'}, stderr);
	fputc('\n', stderr);
	failures++;
}

/* The output of fyts_ctx_highlight_source() is counted, not terminated;
 * terminate it so that it can be searched. */
static char *terminate(char *out, size_t out_len)
{
	char *s;

	if (!out)
		return NULL;
	s = realloc(out, out_len + 1);
	if (!s) {
		free(out);
		return NULL;
	}
	s[out_len] = '\0';
	return s;
}

/* Highlight source with a context that uses the palette; a NULL palette
 * uses the styling. The result is heap allocated and terminated. */
static char *highlight(struct fypal_ctx *palette, const char *lang, int reverse, const char *source)
{
	struct fyts_config config = {0};
	struct fyts_ctx *ctx;
	char *out = NULL;
	size_t out_len = 0;

	config.lang = lang;
	config.color_mode = FYTS_COLOR_ON;
	config.background_mode = FYTS_BACKGROUND_DARK;
	config.reverse = reverse;
	ctx = fyts_ctx_create(&config);
	if (!ctx)
		return NULL;
	if (fyts_ctx_set_palette(ctx, palette) ||
	    fyts_ctx_highlight_source(ctx, source, strlen(source), &out, &out_len)) {
		free(out);
		out = NULL;
	}
	fyts_ctx_destroy(ctx);
	return terminate(out, out_len);
}

int main(void)
{
	static const char c_source[] = "int f(void) { return \"generic\"; }\n";
	static const char py_source[] = "x = \"generic\"\n";
	struct fyts_config config = {0};
	struct fypal_caps caps = {
	    .depth = FYPAL_DEPTH_TRUECOLOR,
	    .attrs = FYPAL_ATTR_ALL,
	    .underline_color = 1,
	};
	struct fypal_ctx *palette;
	struct fyts_ctx *ctx;
	char *out;
	size_t out_len;

	palette = fypal_ctx_create(&caps);
	if (!palette || fypal_ctx_load(palette, theme, "test")) {
		fprintf(stderr, "theme: %s\n", palette ? fypal_ctx_error(palette) : "no context");
		return 1;
	}

	/* a language role wins over the general one */
	out = highlight(palette, "c", 0, c_source);
	expect(__LINE__, out, "\033[38;2;4;4;4m\"generic\"", "38;2;3;3;3");
	/* a capture below a defined role falls back to it: keyword.return */
	expect(__LINE__, out, "\033[38;2;2;2;2mreturn", NULL);
	free(out);

	/* a language without its own roles takes the general ones */
	out = highlight(palette, "python", 0, py_source);
	expect(__LINE__, out, "\033[38;2;3;3;3m\"generic\"", NULL);
	free(out);

	/* reverse mode frames the code on the background of code.block */
	out = highlight(palette, "c", 1, c_source);
	expect(__LINE__, out, "48;2;5;5;5", NULL);
	free(out);

	/* the palette follows its capabilities */
	caps.depth = FYPAL_DEPTH_256;
	fypal_ctx_set_caps(palette, &caps);
	out = highlight(palette, "c", 0, c_source);
	expect(__LINE__, out, "\033[38;5;", "38;2;");
	free(out);
	caps.depth = FYPAL_DEPTH_TRUECOLOR;
	fypal_ctx_set_caps(palette, &caps);

	/* without a palette the styling is used, as before */
	out = highlight(NULL, "c", 0, c_source);
	expect(__LINE__, out, NULL, "38;2;4;4;4");
	expect(__LINE__, out, NULL, "38;2;2;2;2");
	free(out);

	/* a retained context changes palette and back */
	config.lang = "c";
	config.color_mode = FYTS_COLOR_ON;
	config.background_mode = FYTS_BACKGROUND_DARK;
	ctx = fyts_ctx_create(&config);
	CHECK(ctx != NULL);
	if (ctx) {
		out = NULL;
		CHECK(!fyts_ctx_highlight_source(ctx, c_source, strlen(c_source), &out, &out_len));
		out = terminate(out, out_len);
		CHECK(out && !strstr(out, "38;2;4;4;4"));
		free(out);
		out = NULL;
		CHECK(!fyts_ctx_set_palette(ctx, palette));
		CHECK(!fyts_ctx_highlight_source(ctx, c_source, strlen(c_source), &out, &out_len));
		out = terminate(out, out_len);
		CHECK(out && strstr(out, "38;2;4;4;4"));
		free(out);
		out = NULL;
		CHECK(!fyts_ctx_set_palette(ctx, NULL));
		CHECK(!fyts_ctx_highlight_source(ctx, c_source, strlen(c_source), &out, &out_len));
		out = terminate(out, out_len);
		CHECK(out && !strstr(out, "38;2;4;4;4"));
		free(out);
		fyts_ctx_destroy(ctx);
	}

	CHECK(fyts_ctx_set_palette(NULL, palette) == -1);

	fypal_ctx_destroy(palette);
	return failures ? 1 : 0;
}
