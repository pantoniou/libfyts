#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fyts/fyts.h>
#include <libfyaml/libfyaml-generic.h>
#include <tree_sitter/api.h>

typedef struct {
	const char *name;
	const TSLanguage *(*language)(void);
	const char *query_path;
} LanguageSpec;

typedef struct {
	uint32_t start;
	uint32_t end;
	fy_generic ansi;
} Span;

typedef struct {
	const char *capture;
	size_t capture_len;
	fy_generic ansi;
	int occupied;
} StyleCacheEntry;

typedef struct {
	fy_generic root;
	struct fy_generic_builder *builder;
} Catalogue;

typedef struct {
	fy_generic root;
	struct fy_generic_builder *builder;
	char *source;
	StyleCacheEntry cache[128];
} Styling;

typedef enum {
	RENDER_OK,
	RENDER_ERROR,
} RenderStatus;

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} Buffer;

struct fyts_ctx {
	struct fyts_config config;
	Styling styling;
	char *source;
	size_t source_len;
	size_t source_cap;
	char *last_output;
	size_t last_output_len;
};

static const char *RESET = "\033[0m";

#include "language_catalogue.inc"
#include "embedded_catalogue.inc"
#include "embedded_styling.inc"

int fyts_write_file(const void *data, size_t len, void *user)
{
	FILE *file = (FILE *)user;

	return fwrite(data, 1, len, file) == len ? 0 : -1;
}

static int buffer_reserve(Buffer *buffer, size_t extra)
{
	char *data;
	size_t cap;

	if (extra <= buffer->cap - buffer->len)
		return 1;

	cap = buffer->cap ? buffer->cap * 2 : 4096;
	while (extra > cap - buffer->len)
		cap *= 2;

	data = (char *)realloc(buffer->data, cap);
	if (!data)
		return 0;
	buffer->data = data;
	buffer->cap = cap;
	return 1;
}

static int buffer_write(Buffer *buffer, const void *data, size_t len)
{
	if (!buffer_reserve(buffer, len))
		return -1;
	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	return 0;
}

static int buffer_write_fn(const void *data, size_t len, void *user)
{
	return buffer_write((Buffer *)user, data, len);
}

static void buffer_cleanup(Buffer *buffer)
{
	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0;
	buffer->cap = 0;
}

static char *read_file(const char *path, uint32_t *len_out)
{
	FILE *file = fopen(path, "rb");
	long len;
	char *data;

	if (!file) {
		perror(path);
		return NULL;
	}
	if (fseek(file, 0, SEEK_END) != 0) {
		perror(path);
		fclose(file);
		return NULL;
	}
	len = ftell(file);
	if (len < 0) {
		perror(path);
		fclose(file);
		return NULL;
	}
	rewind(file);
	data = (char *)malloc((size_t)len + 1);
	if (!data) {
		fprintf(stderr, "out of memory reading %s\n", path);
		fclose(file);
		return NULL;
	}
	if (fread(data, 1, (size_t)len, file) != (size_t)len) {
		perror(path);
		free(data);
		fclose(file);
		return NULL;
	}
	data[len] = '\0';
	fclose(file);
	*len_out = (uint32_t)len;
	return data;
}

static char *copy_string(const char *text)
{
	size_t len = strlen(text);
	char *copy = (char *)malloc(len + 1);
	if (!copy)
		return NULL;
	memcpy(copy, text, len + 1);
	return copy;
}

static const LanguageSpec *find_language(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof(LANGUAGES) / sizeof(LANGUAGES[0]); i++) {
		if (strcmp(LANGUAGES[i].name, name) == 0) {
			return &LANGUAGES[i];
		}
	}
	return NULL;
}

static void catalogue_cleanup(Catalogue *catalogue)
{
	if (catalogue->builder)
		fy_generic_builder_destroy(catalogue->builder);
	catalogue->builder = NULL;
	catalogue->root = fy_invalid;
}

static int catalogue_load(Catalogue *catalogue)
{
	fy_generic_sized_string source;
	int ok = 0;

	catalogue->root = fy_invalid;
	catalogue->builder = fy_generic_builder_create(NULL);
	if (!catalogue->builder)
		goto done;

	source.data = (const char *)EMBEDDED_CATALOGUE;
	source.size = EMBEDDED_CATALOGUE_LEN;
	catalogue->root = fy_parse(catalogue->builder, source, FYOPPF_DEFAULT, NULL);
	ok = fy_generic_is_valid(catalogue->root);

done:
	if (!ok)
		catalogue_cleanup(catalogue);
	return ok;
}

static void styling_cleanup(Styling *styling)
{
	if (styling->builder)
		fy_generic_builder_destroy(styling->builder);
	free(styling->source);
	styling->builder = NULL;
	styling->source = NULL;
	styling->root = fy_invalid;
	memset(styling->cache, 0, sizeof(styling->cache));
}

static int styling_parse(Styling *styling, fy_generic_sized_string source)
{
	int ok = 0;

	styling->root = fy_invalid;
	styling->builder = fy_generic_builder_create(NULL);
	if (!styling->builder)
		goto done;

	styling->root = fy_parse(styling->builder, source, FYOPPF_DEFAULT, NULL);
	ok = fy_generic_is_valid(styling->root);

done:
	if (!ok)
		styling_cleanup(styling);
	return ok;
}

static int styling_load_embedded(Styling *styling)
{
	fy_generic_sized_string source;

	source.data = (const char *)EMBEDDED_STYLING;
	source.size = EMBEDDED_STYLING_LEN;
	return styling_parse(styling, source);
}

static int styling_load_file(Styling *styling, const char *path)
{
	fy_generic_sized_string source;
	uint32_t len;
	char *data;

	data = read_file(path, &len);
	if (!data)
		return 0;

	styling->source = data;
	source.data = data;
	source.size = len;
	return styling_parse(styling, source);
}

char *fyts_list_languages(size_t *lenp)
{
	Catalogue catalogue;
	Buffer out = {0};
	fy_generic language;
	const char *name;
	char *result = NULL;

	if (!catalogue_load(&catalogue)) {
		fprintf(stderr, "failed to parse embedded language catalogue\n");
		return NULL;
	}

	fy_foreach(language, catalogue.root)
	{
		name = fy_get(language, "name", "");
		if (*name &&
		    (buffer_write(&out, name, strlen(name)) || buffer_write(&out, "\n", 1)))
			goto done;
	}

	if (!buffer_reserve(&out, 1))
		goto done;
	out.data[out.len] = '\0';
	if (lenp)
		*lenp = out.len;
	result = out.data;
	out.data = NULL;

done:
	buffer_cleanup(&out);
	catalogue_cleanup(&catalogue);
	return result;
}

char *fyts_output_catalogue(size_t *lenp)
{
	Catalogue catalogue;
	fy_generic emitted;
	const char *text;
	char *output = NULL;

	if (!catalogue_load(&catalogue)) {
		fprintf(stderr, "failed to parse embedded language catalogue\n");
		return NULL;
	}

	emitted = fy_emit(
	    catalogue.builder, catalogue.root,
	    FYOPEF_DISABLE_DIRECTORY | FYOPEF_OUTPUT_TYPE_STRING | FYOPEF_STYLE_PRETTY, NULL);
	if (!fy_generic_is_valid(emitted))
		goto done;

	text = fy_castp(&emitted, "");
	output = copy_string(text);
	if (output && lenp)
		*lenp = strlen(output);

done:
	catalogue_cleanup(&catalogue);
	return output;
}

char *fyts_output_styling(size_t *lenp)
{
	Styling styling = {fy_invalid, NULL, NULL};
	fy_generic emitted;
	const char *text;
	char *output = NULL;

	if (!styling_load_embedded(&styling)) {
		fprintf(stderr, "failed to parse embedded styling\n");
		return NULL;
	}

	emitted = fy_emit(
	    styling.builder, styling.root,
	    FYOPEF_DISABLE_DIRECTORY | FYOPEF_OUTPUT_TYPE_STRING | FYOPEF_STYLE_PRETTY, NULL);
	if (!fy_generic_is_valid(emitted))
		goto done;

	text = fy_castp(&emitted, "");
	output = copy_string(text);
	if (output && lenp)
		*lenp = strlen(output);

done:
	styling_cleanup(&styling);
	return output;
}

static const char *path_extension(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *base = slash ? slash + 1 : path;
	const char *dot = strrchr(base, '.');
	if (!dot || dot == base)
		return NULL;
	return dot;
}

char *fyts_detect_language_for_path(const char *path)
{
	const char *wanted_ext = path_extension(path);
	Catalogue catalogue;
	fy_generic language, extensions, extension;
	const char *name;
	char *detected = NULL;

	if (!wanted_ext)
		return NULL;

	if (!catalogue_load(&catalogue))
		return NULL;

	detected = NULL;
	fy_foreach(language, catalogue.root)
	{
		name = fy_generic_get_default(language, "name", "");
		if (!*name)
			continue;
		fy_foreach(extension, fy_get(language, "extensions", fy_invalid))
		{
			if (strcmp(fy_cast(extension, ""), wanted_ext) == 0) {
				detected = find_language(name) ? copy_string(name) : NULL;
				goto out;
			}
		}
	}
out:
	catalogue_cleanup(&catalogue);
	return detected;
}

static fy_generic styling_escape_for_style(Styling *styling, const char *style)
{
	fy_generic styles;

	styles = fy_get(styling->root, "styles", fy_invalid);
	return fy_get(styles, style, fy_invalid);
}

static unsigned long hash_string(const char *text, size_t len)
{
	unsigned long hash = 5381;
	size_t i;
	unsigned char c;

	for (i = 0; i < len; i++) {
		c = (unsigned char)text[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

static fy_generic capture_color_uncached(Styling *styling, const char *name)
{
	fy_generic captures;
	fy_generic item;
	const char *capture;
	const char *style;

	captures = fy_get(styling->root, "captures", fy_invalid);
	fy_foreach(item, captures)
	{
		capture = fy_get(item, "capture", "");
		if (!*capture || !strstr(name, capture))
			continue;
		style = fy_get(item, "style", "");
		if (!*style)
			continue;
		return styling_escape_for_style(styling, style);
	}

	return fy_invalid;
}

static fy_generic capture_color(Styling *styling, const char *name, size_t name_len,
				const char *lookup_name)
{
	size_t count = sizeof(styling->cache) / sizeof(styling->cache[0]);
	size_t start = hash_string(name, name_len) % count;
	size_t i;
	size_t index;
	StyleCacheEntry *entry;
	fy_generic ansi;

	for (i = 0; i < count; i++) {
		index = (start + i) % count;
		entry = &styling->cache[index];
		if (entry->occupied && entry->capture_len == name_len &&
		    memcmp(entry->capture, name, name_len) == 0)
			return entry->ansi;
		if (!entry->occupied) {
			ansi = capture_color_uncached(styling, lookup_name);
			entry->capture = name;
			entry->capture_len = name_len;
			entry->ansi = ansi;
			entry->occupied = 1;
			return ansi;
		}
	}

	return capture_color_uncached(styling, lookup_name);
}

static int span_cmp(const void *a, const void *b)
{
	const Span *left = (const Span *)a;
	const Span *right = (const Span *)b;
	if (left->start != right->start)
		return left->start < right->start ? -1 : 1;
	if (left->end != right->end)
		return left->end > right->end ? -1 : 1;
	return 0;
}

static int push_span(Span **spans, size_t *count, size_t *capacity, Span span)
{
	Span *next;
	size_t new_capacity;

	if (span.start >= span.end || !fy_generic_is_valid(span.ansi))
		return 1;
	if (*count == *capacity) {
		new_capacity = *capacity ? *capacity * 2 : 128;
		next = (Span *)realloc(*spans, new_capacity * sizeof(Span));
		if (!next)
			return 0;
		*spans = next;
		*capacity = new_capacity;
	}
	(*spans)[(*count)++] = span;
	return 1;
}

static int source_char_width(unsigned char c, int col)
{
	if (c == '\t')
		return 8 - (col % 8);
	if (c < ' ' || c == 0x7f)
		return 0;
	return 1;
}

static int emit_source_range(Buffer *out, const char *source, uint32_t start, uint32_t end,
			     int *col, int width)
{
	uint32_t i;
	unsigned char c;
	int char_width;

	for (i = start; i < end; i++) {
		c = (unsigned char)source[i];
		if (c == '\n') {
			if (buffer_write(out, source + i, 1))
				return 0;
			*col = 0;
			continue;
		}

		char_width = source_char_width(c, *col);
		if (width > 0 && *col + char_width > width) {
			*col = width;
			continue;
		}

		if (buffer_write(out, source + i, 1))
			return 0;
		*col += char_width;
	}
	return 1;
}

static int emit_highlighted(Buffer *out, const char *source, uint32_t source_len, Span *spans,
			    size_t count, int width)
{
	uint32_t pos = 0;
	size_t i;
	int col = 0;
	Span span;
	const char *ansi;

	qsort(spans, count, sizeof(Span), span_cmp);
	for (i = 0; i < count; i++) {
		span = spans[i];
		if (span.end <= pos)
			continue;
		if (span.start < pos)
			span.start = pos;
		if (span.start > source_len)
			break;
		if (span.end > source_len)
			span.end = source_len;

		if (!emit_source_range(out, source, pos, span.start, &col, width))
			return 0;
		ansi = fy_cast(span.ansi, "");
		if (buffer_write(out, ansi, strlen(ansi)))
			return 0;
		if (!emit_source_range(out, source, span.start, span.end, &col, width))
			return 0;
		if (buffer_write(out, RESET, strlen(RESET)))
			return 0;
		pos = span.end;
	}
	return emit_source_range(out, source, pos, source_len, &col, width);
}

static int config_color_enabled(const struct fyts_config *config)
{
	if (config->color_mode == FYTS_COLOR_ON)
		return 1;
	if (config->color_mode == FYTS_COLOR_OFF)
		return 0;
	if (config->write == fyts_write_file && config->write_user)
		return isatty(fileno((FILE *)config->write_user));
	return 0;
}

static int render_source(struct fyts_ctx *ctx, const char *source, size_t source_len, Buffer *out)
{
	const LanguageSpec *spec;
	char *query_source = NULL;
	char *query_path = NULL;
	uint32_t query_len = 0;
	TSParser *parser = NULL;
	TSTree *tree = NULL;
	TSQuery *query = NULL;
	TSQueryCursor *cursor = NULL;
	TSNode root;
	TSQueryError error_type;
	uint32_t error_offset;
	Span *spans = NULL;
	size_t span_count = 0;
	size_t span_capacity = 0;
	int ok = 0;
	TSQueryMatch match;
	uint32_t capture_index;
	TSQueryCapture capture;
	uint32_t name_len;
	const char *capture_name;
	char name[128];
	uint32_t copy_len;
	fy_generic ansi_value;
	Span span;
	int use_color;

	spec = find_language(ctx->config.lang);
	if (!spec) {
		fprintf(stderr, "unknown language: %s\n", ctx->config.lang);
		goto done;
	}

	parser = ts_parser_new();
	if (!parser || !ts_parser_set_language(parser, spec->language())) {
		fprintf(stderr, "failed to initialize parser for %s\n", spec->name);
		goto done;
	}

	tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
	if (!tree) {
		fprintf(stderr, "failed to parse source\n");
		goto done;
	}

	if (ctx->config.query_path) {
		query_path = copy_string(ctx->config.query_path);
	} else {
		query_path = copy_string(spec->query_path);
	}
	if (!query_path) {
		fprintf(stderr, "out of memory building query path\n");
		goto done;
	}

	query_source = read_file(query_path, &query_len);
	if (!query_source)
		goto done;

	query = ts_query_new(spec->language(), query_source, query_len, &error_offset, &error_type);
	if (!query) {
		fprintf(stderr, "invalid query %s at byte %u\n", query_path, error_offset);
		goto done;
	}

	cursor = ts_query_cursor_new();
	root = ts_tree_root_node(tree);
	ts_query_cursor_exec(cursor, query, root);
	use_color = config_color_enabled(&ctx->config);

	for (;;) {
		if (!ts_query_cursor_next_capture(cursor, &match, &capture_index))
			break;
		if (capture_index < match.capture_count) {
			capture = match.captures[capture_index];
			name_len = 0;
			capture_name =
			    ts_query_capture_name_for_id(query, capture.index, &name_len);
			copy_len =
			    name_len < sizeof(name) - 1 ? name_len : (uint32_t)sizeof(name) - 1;
			memcpy(name, capture_name, copy_len);
			name[copy_len] = '\0';
			ansi_value =
			    use_color ? capture_color(&ctx->styling, capture_name, name_len, name)
				      : fy_invalid;
			if (fy_generic_is_valid(ansi_value)) {
				span.start = ts_node_start_byte(capture.node);
				span.end = ts_node_end_byte(capture.node);
				span.ansi = ansi_value;
				if (!push_span(&spans, &span_count, &span_capacity, span)) {
					fprintf(stderr,
						"out of memory collecting highlight spans\n");
					goto done;
				}
			}
		}
	}

	ok = emit_highlighted(out, source, (uint32_t)source_len, spans, span_count,
			      ctx->config.width);

done:
	free(spans);
	if (cursor)
		ts_query_cursor_delete(cursor);
	if (query)
		ts_query_delete(query);
	if (tree)
		ts_tree_delete(tree);
	if (parser)
		ts_parser_delete(parser);
	free(query_path);
	free(query_source);
	return ok;
}

static int ctx_emit_diff(struct fyts_ctx *ctx, Buffer *out)
{
	size_t prefix = 0;
	char *last;

	while (prefix < ctx->last_output_len && prefix < out->len &&
	       ctx->last_output[prefix] == out->data[prefix])
		prefix++;

	if (out->len > prefix &&
	    ctx->config.write(out->data + prefix, out->len - prefix, ctx->config.write_user))
		return -1;

	last = (char *)realloc(ctx->last_output, out->len ? out->len : 1);
	if (!last && out->len)
		return -1;
	ctx->last_output = last;
	if (out->len)
		memcpy(ctx->last_output, out->data, out->len);
	ctx->last_output_len = out->len;
	return 0;
}

static int ctx_render_and_emit(struct fyts_ctx *ctx)
{
	Buffer out = {0};
	int rc = -1;

	if (!render_source(ctx, ctx->source, ctx->source_len, &out))
		goto done;
	rc = ctx_emit_diff(ctx, &out);

done:
	buffer_cleanup(&out);
	return rc;
}

struct fyts_ctx *fyts_ctx_create(const struct fyts_config *config)
{
	struct fyts_ctx *ctx;

	if (!config || !config->lang)
		return NULL;

	ctx = (struct fyts_ctx *)calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;
	ctx->config = *config;
	if (!ctx->config.write) {
		ctx->config.write = fyts_write_file;
		ctx->config.write_user = stdout;
	}

	if (config_color_enabled(&ctx->config)) {
		if (ctx->config.styling_path) {
			if (!styling_load_file(&ctx->styling, ctx->config.styling_path))
				goto fail;
		} else if (!styling_load_embedded(&ctx->styling)) {
			goto fail;
		}
	}

	return ctx;

fail:
	fyts_ctx_destroy(ctx);
	return NULL;
}

void fyts_ctx_destroy(struct fyts_ctx *ctx)
{
	if (!ctx)
		return;
	styling_cleanup(&ctx->styling);
	free(ctx->source);
	free(ctx->last_output);
	free(ctx);
}

int fyts_highlight_source(const struct fyts_config *config, const char *source, size_t len)
{
	struct fyts_ctx *ctx;
	Buffer out = {0};
	int rc = -1;

	ctx = fyts_ctx_create(config);
	if (!ctx)
		return -1;
	if (!render_source(ctx, source, len, &out))
		goto done;
	rc = ctx->config.write(out.data, out.len, ctx->config.write_user);

done:
	buffer_cleanup(&out);
	fyts_ctx_destroy(ctx);
	return rc;
}

int fyts_ctx_feed(struct fyts_ctx *ctx, const char *data, size_t len)
{
	char *source;
	const void *newline;

	if (!ctx || (!data && len))
		return -1;
	if (len > ctx->source_cap - ctx->source_len) {
		size_t cap = ctx->source_cap ? ctx->source_cap * 2 : 4096;
		while (len > cap - ctx->source_len)
			cap *= 2;
		source = (char *)realloc(ctx->source, cap);
		if (!source)
			return -1;
		ctx->source = source;
		ctx->source_cap = cap;
	}
	memcpy(ctx->source + ctx->source_len, data, len);
	ctx->source_len += len;

	newline = memchr(data, '\n', len);
	if (!newline)
		return 0;
	return ctx_render_and_emit(ctx);
}

int fyts_ctx_finish(struct fyts_ctx *ctx)
{
	if (!ctx)
		return -1;
	return ctx_render_and_emit(ctx);
}
