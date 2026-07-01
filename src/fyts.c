#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
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
	const char *capture;
	uint32_t capture_len;
	int priority;
} Span;

typedef struct StyleCacheEntry {
	const char *capture;
	size_t capture_len;
	fy_generic ansi;
	int priority;
	struct StyleCacheEntry *next;
} StyleCacheEntry;

typedef struct {
	fy_generic root;
	struct fy_generic_builder *builder;
} Catalogue;

typedef struct {
	fy_generic root;
	struct fy_generic_builder *builder;
	char *source;
	enum fyts_background_mode background;
	StyleCacheEntry *cache[128];
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

typedef struct {
	char **items;
	size_t count;
	size_t cap;
} StringSet;

struct fyts_ctx {
	struct fyts_config config;
	Styling styling;
	enum fyts_background_mode terminal_background;
	enum fyts_background_mode style_background;
	enum fyts_background_mode frame_background;
	const char *span_reset;
	char span_reset_storage[128];
	char *source;
	size_t source_len;
	size_t source_cap;
	char *last_output;
	size_t last_output_len;
};

static const char *RESET = "\033[0m";

static int config_color_enabled(const struct fyts_config *config);
static fy_generic styling_frame_background(Styling *styling, enum fyts_background_mode background);

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

	if (extra > SIZE_MAX - buffer->len)
		return 0;
	if (extra <= buffer->cap - buffer->len)
		return 1;

	cap = buffer->cap ? buffer->cap * 2 : 4096;
	if (cap < buffer->cap)
		return 0;
	while (extra > cap - buffer->len) {
		if (cap > SIZE_MAX / 2)
			return 0;
		cap *= 2;
	}

	data = (char *)realloc(buffer->data, cap);
	if (!data)
		return 0;
	buffer->data = data;
	buffer->cap = cap;
	return 1;
}

static int buffer_write(Buffer *buffer, const void *data, size_t len)
{
	if (!len)
		return 0;
	if (!buffer_reserve(buffer, len))
		return -1;
	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	return 0;
}

static void buffer_cleanup(Buffer *buffer)
{
	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0;
	buffer->cap = 0;
}

static void string_set_cleanup(StringSet *set)
{
	size_t i;

	for (i = 0; i < set->count; i++)
		free(set->items[i]);
	free(set->items);
	set->items = NULL;
	set->count = 0;
	set->cap = 0;
}

static int string_set_add(StringSet *set, const char *text, size_t len)
{
	char **items;
	char *copy;
	size_t cap;
	size_t i;

	for (i = 0; i < set->count; i++) {
		if (strlen(set->items[i]) == len && memcmp(set->items[i], text, len) == 0)
			return 1;
	}
	if (set->count == set->cap) {
		cap = set->cap ? set->cap * 2 : 16;
		items = (char **)realloc(set->items, cap * sizeof(*items));
		if (!items)
			return 0;
		set->items = items;
		set->cap = cap;
	}
	copy = (char *)malloc(len + 1);
	if (!copy)
		return 0;
	memcpy(copy, text, len);
	copy[len] = '\0';
	set->items[set->count++] = copy;
	return 1;
}

static int string_set_emit(StringSet *set, Buffer *out)
{
	size_t i;

	for (i = 0; i < set->count; i++) {
		if (buffer_write(out, set->items[i], strlen(set->items[i])) ||
		    buffer_write(out, "\n", 1))
			return 0;
	}
	return 1;
}

static int buffer_write_string(Buffer *buffer, const char *text)
{
	if (!text || !*text)
		return 0;
	return buffer_write(buffer, text, strlen(text));
}

static size_t line_end_for_frame(const char *data, size_t len, size_t start)
{
	size_t pos;

	for (pos = start; pos < len; pos++) {
		if (data[pos] == '\n')
			return pos + 1;
	}
	return len;
}

static enum fyts_background_mode opposite_background(enum fyts_background_mode background)
{
	return background == FYTS_BACKGROUND_LIGHT ? FYTS_BACKGROUND_DARK : FYTS_BACKGROUND_LIGHT;
}

static int apply_frame(struct fyts_ctx *ctx, Buffer *in, Buffer *out)
{
	const struct fyts_config *config = &ctx->config;
	size_t pos = 0;
	size_t end;
	size_t content_end;
	int have_line_frame;
	int have_reverse;
	const char *frame_bg = "";

	have_line_frame = (config->line_prefix && *config->line_prefix) ||
			  (config->line_suffix && *config->line_suffix);
	have_reverse = config->reverse && config_color_enabled(config);

	/* Resolve the per-line frame background once (the cast result is only valid
	 * within this frame, so it must not be returned or recomputed per line). */
	if (have_reverse)
		frame_bg =
		    fy_cast(styling_frame_background(&ctx->styling, ctx->frame_background), "");

	/* In reverse (bubble) mode the prolog is drawn as its own framed line
	 * (background + line prefix + text, extended to the end of the line), so a
	 * caller-supplied header rule sits on the same bubble as the code. Outside
	 * reverse mode it stays a raw inline string. */
	if (have_reverse && config->prolog && *config->prolog) {
		if (buffer_write_string(out, frame_bg) ||
		    buffer_write_string(out, config->line_prefix) ||
		    buffer_write_string(out, config->prolog) ||
		    buffer_write_string(out, config->line_suffix) ||
		    buffer_write_string(out, "\033[K\033[0m\n"))
			return -1;
	} else if (buffer_write_string(out, config->prolog)) {
		return -1;
	}

	if (!have_line_frame && !have_reverse) {
		if (buffer_write(out, in->data, in->len))
			return -1;
	} else {
		while (pos < in->len) {
			end = line_end_for_frame(in->data, in->len, pos);
			content_end = end;
			if (content_end > pos && in->data[content_end - 1] == '\n')
				content_end--;
			if (have_reverse && buffer_write_string(out, frame_bg))
				return -1;
			if (buffer_write_string(out, config->line_prefix))
				return -1;
			if (buffer_write(out, in->data + pos, content_end - pos))
				return -1;
			if (buffer_write_string(out, config->line_suffix))
				return -1;
			if (have_reverse && buffer_write_string(out, "\033[K\033[0m"))
				return -1;
			if (content_end < end && buffer_write(out, "\n", 1))
				return -1;
			pos = end;
		}
	}

	/* Epilog: framed footer line in reverse mode, raw otherwise. */
	if (have_reverse && config->epilog && *config->epilog) {
		if (buffer_write_string(out, frame_bg) ||
		    buffer_write_string(out, config->line_prefix) ||
		    buffer_write_string(out, config->epilog) ||
		    buffer_write_string(out, config->line_suffix) ||
		    buffer_write_string(out, "\033[K\033[0m\n"))
			return -1;
		return 0;
	}
	return buffer_write_string(out, config->epilog);
}

static char *read_file(const char *path, size_t *len_out)
{
	FILE *file;
	long len;
	char *data;

	file = fopen(path, "rb");
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
	*len_out = (size_t)len;
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

	if (!name || !*name)
		return NULL;
	for (i = 0; i < sizeof(LANGUAGES) / sizeof(LANGUAGES[0]); i++) {
		if (strcmp(LANGUAGES[i].name, name) == 0) {
			return &LANGUAGES[i];
		}
	}
	return NULL;
}

int fyts_language_supported(const char *lang)
{
	return find_language(lang) != NULL;
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
	StyleCacheEntry *entry;
	StyleCacheEntry *next;
	size_t i;

	for (i = 0; i < sizeof(styling->cache) / sizeof(styling->cache[0]); i++) {
		entry = styling->cache[i];
		while (entry) {
			next = entry->next;
			free((char *)entry->capture);
			free(entry);
			entry = next;
		}
		styling->cache[i] = NULL;
	}

	if (styling->builder)
		fy_generic_builder_destroy(styling->builder);
	free(styling->source);
	styling->builder = NULL;
	styling->source = NULL;
	styling->root = fy_invalid;
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
	size_t len;
	char *data;

	data = read_file(path, &len);
	if (!data)
		return 0;

	styling->source = data;
	source.data = data;
	source.size = len;
	return styling_parse(styling, source);
}

static int styling_load_generic(Styling *styling, fy_generic root)
{
	if (!fy_generic_is_valid(root))
		return 0;
	styling->root = root;
	return 1;
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
	Styling styling = {fy_invalid, NULL, NULL, FYTS_BACKGROUND_DARK, {NULL}};
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
	fy_generic language, extension;
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

static const char *styling_escape_string_for_style(Styling *styling, const char *style)
{
	return fy_cast(styling_escape_for_style(styling, style), "");
}

static const char *styling_background_key(enum fyts_background_mode background)
{
	return background == FYTS_BACKGROUND_LIGHT ? "light" : "dark";
}

static fy_generic styling_frame_style(Styling *styling, const char *key)
{
	fy_generic frame;
	const char *style;

	frame = fy_get(styling->root, "frame", fy_invalid);
	style = fy_get(frame, key, "");
	if (!*style)
		return fy_invalid;
	return styling_escape_for_style(styling, style);
}

static fy_generic styling_frame_background(Styling *styling, enum fyts_background_mode background)
{
	return styling_frame_style(
	    styling, background == FYTS_BACKGROUND_LIGHT ? "light-background" : "dark-background");
}

static int styling_style_matches_ansi(Styling *styling, const char *style, const char *ansi)
{
	const char *style_ansi;

	style_ansi = styling_escape_string_for_style(styling, style);
	return *style_ansi && strcmp(style_ansi, ansi) == 0;
}

static fy_generic styling_contrast_ansi(Styling *styling, enum fyts_background_mode background,
					fy_generic ansi)
{
	fy_generic frame;
	fy_generic conflicts;
	fy_generic item;
	fy_generic replacement;
	const char *conflicts_key;
	const char *replacement_key;
	const char *replacement_ansi;
	const char *ansi_str;
	const char *style;

	frame = fy_get(styling->root, "frame", fy_invalid);
	if (background == FYTS_BACKGROUND_LIGHT) {
		conflicts_key = "light-conflicts";
		replacement_key = "light-contrast";
	} else {
		conflicts_key = "dark-conflicts";
		replacement_key = "dark-contrast";
	}

	conflicts = fy_get(frame, conflicts_key, fy_invalid);
	replacement = styling_frame_style(styling, replacement_key);
	replacement_ansi = fy_cast(replacement, "");
	if (!*replacement_ansi)
		return ansi;

	ansi_str = fy_cast(ansi, "");
	fy_foreach(item, conflicts)
	{
		style = fy_cast(item, "");
		if (*style && styling_style_matches_ansi(styling, style, ansi_str))
			return replacement;
	}
	return ansi;
}

static fy_generic styling_escape_for_attribute(Styling *styling, const char *attribute)
{
	fy_generic attributes;
	fy_generic item;
	const char *style;

	attributes = fy_get(styling->root, "attributes", fy_invalid);
	item = fy_get(attributes, attribute, fy_invalid);
	if (!fy_generic_is_valid(item))
		return fy_invalid;

	style = fy_get(item, styling_background_key(styling->background), "");
	if (!*style)
		style = fy_get(item, "default", "");
	if (!*style)
		return fy_invalid;
	return styling_escape_for_style(styling, style);
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

static fy_generic capture_color_uncached(Styling *styling, const char *name, int *priority)
{
	fy_generic captures;
	fy_generic item;
	const char *capture;
	const char *attribute;
	const char *style;
	regex_t regex;
	int index;
	int matched;

	captures = fy_get(styling->root, "captures", fy_invalid);
	index = 0;
	fy_foreach(item, captures)
	{
		capture = fy_get(item, "capture", "");
		if (!*capture)
			continue;
		if (regcomp(&regex, capture, REG_EXTENDED | REG_NOSUB) != 0) {
			index++;
			continue;
		}
		matched = regexec(&regex, name, 0, NULL, 0) == 0;
		regfree(&regex);
		if (!matched) {
			index++;
			continue;
		}
		attribute = fy_get(item, "attribute", "");
		if (*attribute) {
			*priority = -index;
			return styling_escape_for_attribute(styling, attribute);
		}
		style = fy_get(item, "style", "");
		if (!*style) {
			index++;
			continue;
		}
		*priority = -index;
		return styling_escape_for_style(styling, style);
		index++;
	}

	*priority = 0;
	return fy_invalid;
}

static fy_generic capture_color(Styling *styling, const char *name, size_t name_len,
				const char *lookup_name, int *priority)
{
	size_t count = sizeof(styling->cache) / sizeof(styling->cache[0]);
	size_t index;
	StyleCacheEntry *entry;
	fy_generic ansi;
	char *capture;

	index = hash_string(name, name_len) % count;
	for (entry = styling->cache[index]; entry; entry = entry->next) {
		if (entry->capture_len == name_len && memcmp(entry->capture, name, name_len) == 0) {
			*priority = entry->priority;
			return entry->ansi;
		}
	}

	ansi = capture_color_uncached(styling, lookup_name, priority);
	entry = (StyleCacheEntry *)calloc(1, sizeof(*entry));
	capture = (char *)malloc(name_len);
	if (!entry || !capture) {
		free(entry);
		free(capture);
		return ansi;
	}

	memcpy(capture, name, name_len);
	entry->capture = capture;
	entry->capture_len = name_len;
	entry->ansi = ansi;
	entry->priority = *priority;
	entry->next = styling->cache[index];
	styling->cache[index] = entry;
	return ansi;
}

static int span_cmp(const void *a, const void *b)
{
	const Span *left = (const Span *)a;
	const Span *right = (const Span *)b;
	if (left->start != right->start)
		return left->start < right->start ? -1 : 1;
	if (left->priority != right->priority)
		return left->priority > right->priority ? -1 : 1;
	if (left->end != right->end)
		return left->end > right->end ? -1 : 1;
	return 0;
}

static int push_span(Span **spans, size_t *count, size_t *capacity, Span span)
{
	Span *next;
	size_t new_capacity;

	if (span.start >= span.end || (!fy_generic_is_valid(span.ansi) && !span.capture))
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

typedef struct {
	unsigned lo;
	unsigned hi;
} CpRange;

static size_t utf8_decode(const char *s, size_t n, unsigned *cp)
{
	unsigned char c;

	c = (unsigned char)s[0];
	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if ((c & 0xe0) == 0xc0 && n >= 2) {
		*cp = ((c & 0x1f) << 6) | ((unsigned char)s[1] & 0x3f);
		return 2;
	}
	if ((c & 0xf0) == 0xe0 && n >= 3) {
		*cp = ((c & 0x0f) << 12) | (((unsigned char)s[1] & 0x3f) << 6) |
		      ((unsigned char)s[2] & 0x3f);
		return 3;
	}
	if ((c & 0xf8) == 0xf0 && n >= 4) {
		*cp = ((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3f) << 12) |
		      (((unsigned char)s[2] & 0x3f) << 6) | ((unsigned char)s[3] & 0x3f);
		return 4;
	}
	*cp = c;
	return 1;
}

static int cp_range_contains(const CpRange *ranges, int n, unsigned cp)
{
	int lo = 0;
	int hi = n - 1;
	int mid;

	while (lo <= hi) {
		mid = (lo + hi) / 2;
		if (cp < ranges[mid].lo)
			hi = mid - 1;
		else if (cp > ranges[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

static int codepoint_width(unsigned cp)
{
	static const CpRange zero_width_ranges[] = {
	    {0x0300, 0x036f},	{0x0483, 0x0489},   {0x0591, 0x05bd},	{0x05bf, 0x05bf},
	    {0x05c1, 0x05c2},	{0x05c4, 0x05c5},   {0x05c7, 0x05c7},	{0x0610, 0x061a},
	    {0x064b, 0x065f},	{0x0670, 0x0670},   {0x06d6, 0x06dc},	{0x06df, 0x06e4},
	    {0x06e7, 0x06e8},	{0x06ea, 0x06ed},   {0x0711, 0x0711},	{0x0730, 0x074a},
	    {0x07a6, 0x07b0},	{0x07eb, 0x07f3},   {0x0816, 0x0819},	{0x081b, 0x0823},
	    {0x0825, 0x0827},	{0x0829, 0x082d},   {0x0859, 0x085b},	{0x08e3, 0x0902},
	    {0x093a, 0x093a},	{0x093c, 0x093c},   {0x0941, 0x0948},	{0x094d, 0x094d},
	    {0x0951, 0x0957},	{0x0962, 0x0963},   {0x0981, 0x0981},	{0x09bc, 0x09bc},
	    {0x09c1, 0x09c4},	{0x09cd, 0x09cd},   {0x0a01, 0x0a02},	{0x0a3c, 0x0a3c},
	    {0x0a41, 0x0a51},	{0x0a70, 0x0a71},   {0x0a75, 0x0a75},	{0x0abc, 0x0abc},
	    {0x0ac1, 0x0acd},	{0x0b01, 0x0b01},   {0x0b3c, 0x0b3c},	{0x0b3f, 0x0b3f},
	    {0x0b41, 0x0b56},	{0x0b82, 0x0b82},   {0x0bc0, 0x0bc0},	{0x0bcd, 0x0bcd},
	    {0x0c00, 0x0c00},	{0x0c3e, 0x0c40},   {0x0c46, 0x0c56},	{0x0cbc, 0x0cbc},
	    {0x0ccc, 0x0ccd},	{0x0e31, 0x0e31},   {0x0e34, 0x0e3a},	{0x0e47, 0x0e4e},
	    {0x0eb1, 0x0eb1},	{0x0eb4, 0x0ebc},   {0x0ec8, 0x0ecd},	{0x0f18, 0x0f19},
	    {0x0f35, 0x0f35},	{0x0f37, 0x0f37},   {0x0f39, 0x0f39},	{0x0f71, 0x0f7e},
	    {0x0f80, 0x0f84},	{0x0f86, 0x0f87},   {0x0f8d, 0x0fbc},	{0x0fc6, 0x0fc6},
	    {0x102d, 0x1030},	{0x1032, 0x1037},   {0x1039, 0x103a},	{0x103d, 0x103e},
	    {0x1058, 0x1059},	{0x105e, 0x1060},   {0x1071, 0x1074},	{0x1082, 0x1082},
	    {0x1085, 0x1086},	{0x108d, 0x108d},   {0x135d, 0x135f},	{0x1712, 0x1714},
	    {0x1732, 0x1734},	{0x1752, 0x1753},   {0x1772, 0x1773},	{0x17b4, 0x17b5},
	    {0x17b7, 0x17bd},	{0x17c6, 0x17c6},   {0x17c9, 0x17d3},	{0x17dd, 0x17dd},
	    {0x180b, 0x180e},	{0x1885, 0x1886},   {0x18a9, 0x18a9},	{0x1920, 0x1922},
	    {0x1927, 0x1928},	{0x1932, 0x1932},   {0x1939, 0x193b},	{0x1a17, 0x1a18},
	    {0x1a1b, 0x1a1b},	{0x1a56, 0x1a56},   {0x1a58, 0x1a60},	{0x1a62, 0x1a62},
	    {0x1a65, 0x1a6c},	{0x1a73, 0x1a7c},   {0x1a7f, 0x1a7f},	{0x1ab0, 0x1aff},
	    {0x1b00, 0x1b03},	{0x1b34, 0x1b34},   {0x1b36, 0x1b3a},	{0x1b3c, 0x1b3c},
	    {0x1b42, 0x1b42},	{0x1b6b, 0x1b73},   {0x1b80, 0x1b81},	{0x1ba2, 0x1ba5},
	    {0x1ba8, 0x1ba9},	{0x1bab, 0x1bad},   {0x1be6, 0x1be6},	{0x1be8, 0x1be9},
	    {0x1bed, 0x1bed},	{0x1bef, 0x1bf1},   {0x1c2c, 0x1c33},	{0x1c36, 0x1c37},
	    {0x1cd0, 0x1cd2},	{0x1cd4, 0x1ce0},   {0x1ce2, 0x1ce8},	{0x1ced, 0x1ced},
	    {0x1cf4, 0x1cf4},	{0x1cf8, 0x1cf9},   {0x1dc0, 0x1dff},	{0x200b, 0x200f},
	    {0x202a, 0x202e},	{0x2060, 0x2064},   {0x2066, 0x206f},	{0x20d0, 0x20f0},
	    {0x2cef, 0x2cf1},	{0x2d7f, 0x2d7f},   {0x2de0, 0x2dff},	{0x302a, 0x302d},
	    {0x3099, 0x309a},	{0xa66f, 0xa672},   {0xa674, 0xa67d},	{0xa69e, 0xa69f},
	    {0xa6f0, 0xa6f1},	{0xa802, 0xa802},   {0xa806, 0xa806},	{0xa80b, 0xa80b},
	    {0xa825, 0xa826},	{0xa8c4, 0xa8c5},   {0xa8e0, 0xa8f1},	{0xa926, 0xa92d},
	    {0xa947, 0xa951},	{0xa980, 0xa982},   {0xa9b3, 0xa9b3},	{0xa9b6, 0xa9b9},
	    {0xa9bc, 0xa9bc},	{0xa9e5, 0xa9e5},   {0xaa29, 0xaa2e},	{0xaa31, 0xaa32},
	    {0xaa35, 0xaa36},	{0xaa43, 0xaa43},   {0xaa4c, 0xaa4c},	{0xaa7c, 0xaa7c},
	    {0xaab0, 0xaab0},	{0xaab2, 0xaab4},   {0xaab7, 0xaab8},	{0xaabe, 0xaabf},
	    {0xaac1, 0xaac1},	{0xaaec, 0xaaed},   {0xaaf6, 0xaaf6},	{0xabe5, 0xabe5},
	    {0xabe8, 0xabe8},	{0xabed, 0xabed},   {0xfb1e, 0xfb1e},	{0xfe00, 0xfe0f},
	    {0xfe20, 0xfe2f},	{0xfeff, 0xfeff},   {0xfff9, 0xfffb},	{0x101fd, 0x101fd},
	    {0x102e0, 0x102e0}, {0x10376, 0x1037a}, {0x10a01, 0x10a0f}, {0x10a38, 0x10a3f},
	    {0x11000, 0x11002}, {0x11038, 0x11046}, {0x1107f, 0x11082}, {0x110b3, 0x110ba},
	    {0x11100, 0x11102}, {0x11127, 0x1112b}, {0x1112d, 0x11134}, {0x11180, 0x11181},
	    {0x111b6, 0x111be}, {0x1122f, 0x11231}, {0x11234, 0x11237}, {0x112df, 0x112ea},
	    {0x11300, 0x11301}, {0x1133c, 0x1133c}, {0x11340, 0x11340}, {0x11366, 0x11374},
	    {0x114b3, 0x114be}, {0x115b2, 0x115c0}, {0x11633, 0x1163a}, {0x1163d, 0x1163d},
	    {0x1163f, 0x11640}, {0x116ab, 0x116b7}, {0x1171d, 0x1172b}, {0x16af0, 0x16af4},
	    {0x16b30, 0x16b36}, {0x16f8f, 0x16f92}, {0x1bc9d, 0x1bc9e}, {0x1d165, 0x1d169},
	    {0x1d16d, 0x1d182}, {0x1d185, 0x1d18b}, {0x1d1aa, 0x1d1ad}, {0x1d242, 0x1d244},
	    {0x1da00, 0x1da36}, {0x1da3b, 0x1da6c}, {0x1da75, 0x1da75}, {0x1da84, 0x1da84},
	    {0x1da9b, 0x1daaf}, {0x1e000, 0x1e02a}, {0x1e8d0, 0x1e8d6}, {0x1e944, 0x1e94a},
	    {0xe0001, 0xe01ef},
	};
	static const CpRange wide_ranges[] = {
	    {0x1100, 0x115f},	{0x231a, 0x231b},   {0x2329, 0x232a},	{0x23e9, 0x23ec},
	    {0x23f0, 0x23f0},	{0x23f3, 0x23f3},   {0x25fd, 0x25fe},	{0x2614, 0x2615},
	    {0x2648, 0x2653},	{0x267f, 0x267f},   {0x2693, 0x2693},	{0x26a1, 0x26a1},
	    {0x26aa, 0x26ab},	{0x26bd, 0x26be},   {0x26c4, 0x26c5},	{0x26ce, 0x26ce},
	    {0x26d4, 0x26d4},	{0x26ea, 0x26ea},   {0x26f2, 0x26f3},	{0x26f5, 0x26f5},
	    {0x26fa, 0x26fa},	{0x26fd, 0x26fd},   {0x2705, 0x2705},	{0x270a, 0x270b},
	    {0x2728, 0x2728},	{0x274c, 0x274c},   {0x274e, 0x274e},	{0x2753, 0x2755},
	    {0x2757, 0x2757},	{0x2795, 0x2797},   {0x27b0, 0x27b0},	{0x27bf, 0x27bf},
	    {0x2b1b, 0x2b1c},	{0x2b50, 0x2b50},   {0x2b55, 0x2b55},	{0x2e80, 0x303e},
	    {0x3041, 0x33ff},	{0x3400, 0x4dbf},   {0x4e00, 0x9fff},	{0xa000, 0xa4cf},
	    {0xa960, 0xa97f},	{0xac00, 0xd7a3},   {0xf900, 0xfaff},	{0xfe10, 0xfe19},
	    {0xfe30, 0xfe6f},	{0xff00, 0xff60},   {0xffe0, 0xffe6},	{0x16fe0, 0x16fe1},
	    {0x17000, 0x18aff}, {0x1b000, 0x1b12f}, {0x1b170, 0x1b2ff}, {0x1f004, 0x1f004},
	    {0x1f0cf, 0x1f0cf}, {0x1f18e, 0x1f18e}, {0x1f191, 0x1f19a}, {0x1f1e6, 0x1f1ff},
	    {0x1f200, 0x1f320}, {0x1f32d, 0x1f335}, {0x1f337, 0x1f37c}, {0x1f37e, 0x1f393},
	    {0x1f3a0, 0x1f3ca}, {0x1f3cf, 0x1f3d3}, {0x1f3e0, 0x1f3f0}, {0x1f3f4, 0x1f3f4},
	    {0x1f3f8, 0x1f43e}, {0x1f440, 0x1f440}, {0x1f442, 0x1f4fc}, {0x1f4ff, 0x1f53d},
	    {0x1f54b, 0x1f54e}, {0x1f550, 0x1f567}, {0x1f57a, 0x1f57a}, {0x1f595, 0x1f596},
	    {0x1f5a4, 0x1f5a4}, {0x1f5fb, 0x1f64f}, {0x1f680, 0x1f6c5}, {0x1f6cc, 0x1f6cc},
	    {0x1f6d0, 0x1f6d2}, {0x1f6eb, 0x1f6ec}, {0x1f6f4, 0x1f6f9}, {0x1f910, 0x1f93e},
	    {0x1f940, 0x1f970}, {0x1f973, 0x1f976}, {0x1f97a, 0x1f97a}, {0x1f97c, 0x1f9a2},
	    {0x1f9b0, 0x1f9b9}, {0x1f9c0, 0x1f9c2}, {0x1f9d0, 0x1f9ff}, {0x1fa60, 0x1fa6d},
	    {0x20000, 0x2fffd}, {0x30000, 0x3fffd},
	};

	if (cp == 0)
		return 0;
	if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0))
		return 0;
	if (cp < 0x0300)
		return 1;
	if (cp_range_contains(zero_width_ranges,
			      (int)(sizeof(zero_width_ranges) / sizeof(zero_width_ranges[0])), cp))
		return 0;
	if (cp_range_contains(wide_ranges, (int)(sizeof(wide_ranges) / sizeof(wide_ranges[0])), cp))
		return 2;
	return 1;
}

static int source_char_width(const char *source, size_t len, size_t *char_len, int col)
{
	unsigned cp;

	if ((unsigned char)source[0] == '\t') {
		*char_len = 1;
		return 8 - (col % 8);
	}
	*char_len = utf8_decode(source, len, &cp);
	return codepoint_width(cp);
}

static int text_visible_width(const char *text)
{
	int col = 0;
	size_t i = 0;

	if (!text)
		return 0;
	while (text[i]) {
		size_t char_len;

		col += source_char_width(text + i, strlen(text + i), &char_len, col);
		i += char_len;
	}
	return col;
}

static int render_content_width(const struct fyts_config *config)
{
	int width;
	int frame_width;

	width = config->width;
	if (width <= 0)
		return width;

	if (!config->reverse && !(config->line_prefix && *config->line_prefix) &&
	    !(config->line_suffix && *config->line_suffix))
		return width;

	frame_width = text_visible_width(config->line_prefix);
	if (config->line_suffix && *config->line_suffix)
		frame_width += text_visible_width(config->line_suffix);
	if (frame_width >= width)
		return 0;
	return width - frame_width;
}

static int emit_source_range(Buffer *out, const char *source, uint32_t start, uint32_t end,
			     int *col, int width)
{
	uint32_t i;
	unsigned char c;
	size_t char_len;
	int char_width;

	for (i = start; i < end;) {
		c = (unsigned char)source[i];
		if (c == '\n') {
			if (buffer_write(out, source + i, 1))
				return 0;
			*col = 0;
			i++;
			continue;
		}

		char_width = source_char_width(source + i, end - i, &char_len, *col);
		if (width > 0 && *col + char_width > width) {
			*col = width;
			i += char_len;
			continue;
		}

		if (c == '\t') {
			while (char_width-- > 0) {
				if (buffer_write(out, " ", 1))
					return 0;
				(*col)++;
			}
			i += char_len;
			continue;
		}

		if (buffer_write(out, source + i, char_len))
			return 0;
		*col += char_width;
		i += char_len;
	}
	return 1;
}

static fy_generic contrast_ansi(struct fyts_ctx *ctx, fy_generic ansi)
{
	if (!ctx->config.reverse)
		return ansi;
	return styling_contrast_ansi(&ctx->styling, ctx->frame_background, ansi);
}

static int emit_highlighted(Buffer *out, const char *source, uint32_t source_len, Span *spans,
			    size_t count, int width, const char *reset, struct fyts_ctx *ctx)
{
	uint32_t pos = 0;
	size_t i;
	int col = 0;
	Span span;
	const char *ansi;

	if (count)
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
		if (ctx->config.debug_captures) {
			if (buffer_write(out, "<", 1) ||
			    buffer_write(out, span.capture, span.capture_len) ||
			    buffer_write(out, ">", 1))
				return 0;
		} else {
			ansi = fy_cast(contrast_ansi(ctx, span.ansi), "");
			if (buffer_write(out, ansi, strlen(ansi)))
				return 0;
		}
		if (!emit_source_range(out, source, span.start, span.end, &col, width))
			return 0;
		if (ctx->config.debug_captures) {
			if (buffer_write(out, "</>", 3))
				return 0;
		} else {
			if (buffer_write(out, reset, strlen(reset)))
				return 0;
		}
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

static void fyts_ctx_update_span_reset(struct fyts_ctx *ctx)
{
	fy_generic background_g;
	const char *background;

	ctx->span_reset = RESET;
	if (!ctx->config.reverse)
		return;

	background_g = styling_frame_background(&ctx->styling, ctx->frame_background);
	background = fy_cast(background_g, "");
	if (!*background)
		return;

	snprintf(ctx->span_reset_storage, sizeof(ctx->span_reset_storage), "%s%s", RESET,
		 background);
	ctx->span_reset = ctx->span_reset_storage;
}

static int osc11_reply_is_light(const char *s)
{
	const char *p;
	unsigned long r = 0;
	unsigned long g = 0;
	unsigned long b = 0;
	double rf;
	double gf;
	double bf;
	int nr = 0;
	int ng = 0;
	int nb = 0;

	p = strstr(s, "rgb:");
	if (!p)
		return 0;
	if (sscanf(p + 4, "%lx%n/%lx%n/%lx%n", &r, &nr, &g, &ng, &b, &nb) != 3)
		return 0;
	rf = (double)r / ((1UL << (4 * nr)) - 1);
	gf = (double)g / ((1UL << (4 * (ng - nr - 1))) - 1);
	bf = (double)b / ((1UL << (4 * (nb - ng - 1))) - 1);
	return (0.2126 * rf + 0.7152 * gf + 0.0722 * bf) > 0.5;
}

static enum fyts_background_mode terminal_detect_background(void)
{
	static const char query[] = "\033]11;?\033\\";
	const char *env;
	const char *last;
	struct termios old;
	struct termios raw;
	struct pollfd pfd;
	char buf[64];
	size_t off = 0;
	ssize_t n;
	int fd;
	int bg;

	env = getenv("COLORFGBG");
	if (env) {
		last = strrchr(env, ';');
		if (last && sscanf(last + 1, "%d", &bg) == 1)
			return bg >= 0 && bg <= 6 ? FYTS_BACKGROUND_DARK : FYTS_BACKGROUND_LIGHT;
	}

	fd = open("/dev/tty", O_RDWR | O_NOCTTY);
	if (fd < 0)
		return FYTS_BACKGROUND_DARK;
	if (!isatty(fd) || tcgetattr(fd, &old)) {
		close(fd);
		return FYTS_BACKGROUND_DARK;
	}

	raw = old;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &raw);
	(void)!write(fd, query, sizeof(query) - 1);
	pfd.fd = fd;
	pfd.events = POLLIN;
	while (off < sizeof(buf) - 1 && poll(&pfd, 1, 150) > 0) {
		n = read(fd, buf + off, sizeof(buf) - 1 - off);
		if (n <= 0)
			break;
		off += (size_t)n;
		buf[off] = '\0';
		if (strstr(buf, "rgb:") && (memchr(buf, '\\', off) || memchr(buf, '\a', off)))
			break;
	}
	buf[off] = '\0';
	tcsetattr(fd, TCSANOW, &old);
	close(fd);
	if (strstr(buf, "rgb:"))
		return osc11_reply_is_light(buf) ? FYTS_BACKGROUND_LIGHT : FYTS_BACKGROUND_DARK;
	return FYTS_BACKGROUND_DARK;
}

static enum fyts_background_mode config_background(const struct fyts_config *config)
{
	if (config->background_mode == FYTS_BACKGROUND_DARK ||
	    config->background_mode == FYTS_BACKGROUND_LIGHT)
		return config->background_mode;
	return terminal_detect_background();
}

static const TSQueryCapture *match_capture_for_id(const TSQueryMatch *match, uint32_t id)
{
	uint32_t i;

	for (i = 0; i < match->capture_count; i++) {
		if (match->captures[i].index == id)
			return &match->captures[i];
	}
	return NULL;
}

static int predicate_capture_matches_regex(const char *source, const TSQueryCapture *capture,
					   const char *pattern, uint32_t pattern_len)
{
	uint32_t start;
	uint32_t end;
	size_t len;
	char *text;
	char *regex_text;
	regex_t regex;
	int matched = 0;

	if (!capture)
		return 0;
	start = ts_node_start_byte(capture->node);
	end = ts_node_end_byte(capture->node);
	if (start > end)
		return 0;
	len = end - start;
	text = (char *)malloc(len + 1);
	regex_text = (char *)malloc((size_t)pattern_len + 1);
	if (!text || !regex_text)
		goto done;
	memcpy(text, source + start, len);
	text[len] = '\0';
	memcpy(regex_text, pattern, pattern_len);
	regex_text[pattern_len] = '\0';
	if (regcomp(&regex, regex_text, REG_EXTENDED | REG_NOSUB) != 0)
		goto done;
	matched = regexec(&regex, text, 0, NULL, 0) == 0;
	regfree(&regex);

done:
	free(text);
	free(regex_text);
	return matched;
}

static int query_match_predicates_ok(TSQuery *query, const TSQueryMatch *match, const char *source)
{
	const TSQueryPredicateStep *steps;
	const TSQueryPredicateStep *step;
	const TSQueryPredicateStep *end;
	const TSQueryCapture *capture;
	const char *operator;
	const char *pattern;
	uint32_t step_count;
	uint32_t len;
	uint32_t pattern_len;
	int negate;
	int matched;
	int ok = 1;

	steps = ts_query_predicates_for_pattern(query, match->pattern_index, &step_count);
	step = steps;
	end = steps + step_count;
	while (step < end) {
		if (step->type == TSQueryPredicateStepTypeDone) {
			step++;
			continue;
		}
		if (step->type != TSQueryPredicateStepTypeString)
			return 0;
		operator= ts_query_string_value_for_id(query, step->value_id, &len);
		step++;
		if (len == 6 && strncmp(operator, "match?", len) == 0) {
			negate = 0;
		} else if (len == 10 && strncmp(operator, "not-match?", len) == 0) {
			negate = 1;
		} else {
			while (step < end && step->type != TSQueryPredicateStepTypeDone)
				step++;
			if (step < end)
				step++;
			continue;
		}
		if (step >= end || step->type != TSQueryPredicateStepTypeCapture)
			return 0;
		capture = match_capture_for_id(match, step->value_id);
		step++;
		if (step >= end || step->type != TSQueryPredicateStepTypeString)
			return 0;
		pattern = ts_query_string_value_for_id(query, step->value_id, &pattern_len);
		step++;
		matched = predicate_capture_matches_regex(source, capture, pattern, pattern_len);
		if (negate ? matched : !matched)
			ok = 0;
		while (step < end && step->type != TSQueryPredicateStepTypeDone)
			step++;
		if (step < end)
			step++;
		if (!ok)
			return 0;
	}
	return 1;
}

static int render_source(struct fyts_ctx *ctx, const char *source, size_t source_len, Buffer *out)
{
	const LanguageSpec *spec;
	char *query_source = NULL;
	char *query_path = NULL;
	size_t query_len = 0;
	uint32_t ts_source_len;
	uint32_t ts_query_len;
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
	StringSet unmatched = {0};
	uint32_t name_len;
	const char *capture_name;
	char name[128];
	uint32_t copy_len;
	fy_generic ansi_value;
	Span span;
	int use_color;
	int use_debug;
	int use_unmatched_report;
	int priority;

	spec = find_language(ctx->config.lang);
	if (!spec) {
		fprintf(stderr, "unknown language: %s\n", ctx->config.lang);
		goto done;
	}
	if (source_len > UINT32_MAX) {
		fprintf(stderr, "source is too large for tree-sitter\n");
		goto done;
	}
	ts_source_len = (uint32_t)source_len;

	parser = ts_parser_new();
	if (!parser || !ts_parser_set_language(parser, spec->language())) {
		fprintf(stderr, "failed to initialize parser for %s\n", spec->name);
		goto done;
	}

	tree = ts_parser_parse_string(parser, NULL, source, ts_source_len);
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
	if (query_len > UINT32_MAX) {
		fprintf(stderr, "query %s is too large for tree-sitter\n", query_path);
		goto done;
	}
	ts_query_len = (uint32_t)query_len;

	query =
	    ts_query_new(spec->language(), query_source, ts_query_len, &error_offset, &error_type);
	if (!query) {
		fprintf(stderr, "invalid query %s at byte %u\n", query_path, error_offset);
		goto done;
	}

	cursor = ts_query_cursor_new();
	if (!cursor) {
		fprintf(stderr, "failed to initialize query cursor\n");
		goto done;
	}
	root = ts_tree_root_node(tree);
	ts_query_cursor_exec(cursor, query, root);
	use_color = config_color_enabled(&ctx->config);
	use_debug = ctx->config.debug_captures;
	use_unmatched_report = ctx->config.report_unmatched_captures;

	for (;;) {
		if (!ts_query_cursor_next_capture(cursor, &match, &capture_index))
			break;
		if (!query_match_predicates_ok(query, &match, source))
			continue;
		if (capture_index < match.capture_count) {
			capture = match.captures[capture_index];
			name_len = 0;
			capture_name =
			    ts_query_capture_name_for_id(query, capture.index, &name_len);
			copy_len =
			    name_len < sizeof(name) - 1 ? name_len : (uint32_t)sizeof(name) - 1;
			memcpy(name, capture_name, copy_len);
			name[copy_len] = '\0';
			priority = 0;
			ansi_value = use_color || use_debug || use_unmatched_report
					 ? capture_color(&ctx->styling, capture_name, name_len,
							 name, &priority)
					 : fy_invalid;
			if (use_unmatched_report) {
				if (!fy_generic_is_valid(ansi_value) &&
				    !string_set_add(&unmatched, capture_name, name_len)) {
					fprintf(stderr,
						"out of memory collecting unmatched captures\n");
					goto done;
				}
				continue;
			}
			if (fy_generic_is_valid(ansi_value)) {
				span.start = ts_node_start_byte(capture.node);
				span.end = ts_node_end_byte(capture.node);
				span.ansi = ansi_value;
				span.capture = use_debug ? capture_name : NULL;
				span.capture_len = use_debug ? name_len : 0;
				span.priority = priority;
				if (!push_span(&spans, &span_count, &span_capacity, span)) {
					fprintf(stderr,
						"out of memory collecting highlight spans\n");
					goto done;
				}
			}
		}
	}

	if (use_unmatched_report)
		ok = string_set_emit(&unmatched, out);
	else
		ok = emit_highlighted(out, source, ts_source_len, spans, span_count,
				      render_content_width(&ctx->config), ctx->span_reset, ctx);

done:
	string_set_cleanup(&unmatched);
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

static size_t next_line_end(const char *data, size_t len, size_t start)
{
	size_t pos;

	for (pos = start; pos < len; pos++) {
		if (data[pos] == '\n')
			return pos + 1;
	}
	return len;
}

static int ctx_collect_changed_lines(struct fyts_ctx *ctx, Buffer *rendered, Buffer *changed)
{
	size_t old_pos = 0;
	size_t new_pos = 0;
	size_t old_end;
	size_t new_end;
	size_t old_len;
	size_t new_len;
	char *last;

	while (new_pos < rendered->len) {
		new_end = next_line_end(rendered->data, rendered->len, new_pos);
		old_end = next_line_end(ctx->last_output, ctx->last_output_len, old_pos);
		new_len = new_end - new_pos;
		old_len = old_end - old_pos;

		if (old_pos >= ctx->last_output_len || old_len != new_len ||
		    memcmp(ctx->last_output + old_pos, rendered->data + new_pos, new_len) != 0) {
			if (buffer_write(changed, rendered->data + new_pos, new_len))
				return -1;
		}

		new_pos = new_end;
		if (old_pos < ctx->last_output_len)
			old_pos = old_end;
	}

	last = (char *)realloc(ctx->last_output, rendered->len ? rendered->len : 1);
	if (!last && rendered->len)
		return -1;
	ctx->last_output = last;
	if (rendered->len)
		memcpy(ctx->last_output, rendered->data, rendered->len);
	ctx->last_output_len = rendered->len;
	return 0;
}

static size_t complete_line_source_len(struct fyts_ctx *ctx)
{
	size_t pos;
	size_t complete = 0;

	for (pos = 0; pos < ctx->source_len; pos++) {
		if (ctx->source[pos] == '\n')
			complete = pos + 1;
	}
	return complete;
}

static int ctx_render_changed(struct fyts_ctx *ctx, int finish, char **out, size_t *out_len)
{
	Buffer rendered = {0};
	Buffer changed = {0};
	Buffer framed = {0};
	size_t render_len;
	int rc = -1;

	if (out)
		*out = NULL;
	if (out_len)
		*out_len = 0;
	if (!out || !out_len)
		return -1;

	render_len = finish ? ctx->source_len : complete_line_source_len(ctx);
	if (!render_len && !finish)
		return 0;

	if (!render_source(ctx, ctx->source, render_len, &rendered))
		goto done;
	if (ctx_collect_changed_lines(ctx, &rendered, &changed))
		goto done;
	if (apply_frame(ctx, &changed, &framed))
		goto done;
	*out = framed.data;
	*out_len = framed.len;
	framed.data = NULL;
	framed.len = 0;
	framed.cap = 0;
	rc = 0;

done:
	buffer_cleanup(&framed);
	buffer_cleanup(&changed);
	buffer_cleanup(&rendered);
	return rc;
}

struct fyts_ctx *fyts_ctx_create(const struct fyts_config *config)
{
	struct fyts_ctx *ctx;
	int have_styling;

	if (!config || !config->lang)
		return NULL;
	have_styling = config->styling.v != 0 && fy_generic_is_valid(config->styling);

	ctx = (struct fyts_ctx *)calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;
	ctx->config = *config;
	if (!ctx->config.write) {
		ctx->config.write = fyts_write_file;
		ctx->config.write_user = stdout;
	}
	ctx->terminal_background = FYTS_BACKGROUND_DARK;
	ctx->style_background = FYTS_BACKGROUND_DARK;
	ctx->frame_background = FYTS_BACKGROUND_DARK;
	ctx->span_reset = RESET;
	if (config_color_enabled(&ctx->config) || ctx->config.debug_captures ||
	    ctx->config.report_unmatched_captures) {
		ctx->terminal_background = config_background(&ctx->config);
		ctx->style_background = ctx->config.reverse
					    ? opposite_background(ctx->terminal_background)
					    : ctx->terminal_background;
		ctx->frame_background = ctx->style_background;
	}

	if (config_color_enabled(&ctx->config) || ctx->config.debug_captures ||
	    ctx->config.report_unmatched_captures) {
		ctx->styling.background = ctx->style_background;
		if (have_styling) {
			if (!styling_load_generic(&ctx->styling, ctx->config.styling))
				goto fail;
		} else if (ctx->config.styling_path) {
			if (!styling_load_file(&ctx->styling, ctx->config.styling_path))
				goto fail;
		} else if (!styling_load_embedded(&ctx->styling)) {
			goto fail;
		}
		fyts_ctx_update_span_reset(ctx);
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
	Buffer framed = {0};
	int rc = -1;

	ctx = fyts_ctx_create(config);
	if (!ctx)
		return -1;
	if (!render_source(ctx, source, len, &out))
		goto done;
	if (apply_frame(ctx, &out, &framed))
		goto done;
	rc = ctx->config.write(framed.data, framed.len, ctx->config.write_user);

done:
	buffer_cleanup(&framed);
	buffer_cleanup(&out);
	fyts_ctx_destroy(ctx);
	return rc;
}

int fyts_ctx_feed(struct fyts_ctx *ctx, const char *data, size_t len, char **out, size_t *out_len)
{
	char *source;
	const void *newline;
	size_t cap;

	if (out)
		*out = NULL;
	if (out_len)
		*out_len = 0;
	if (!ctx || (!data && len))
		return -1;
	if (len > SIZE_MAX - ctx->source_len)
		return -1;
	if (len > ctx->source_cap - ctx->source_len) {
		cap = ctx->source_cap ? ctx->source_cap * 2 : 4096;
		if (cap < ctx->source_cap)
			return -1;
		while (len > cap - ctx->source_len) {
			if (cap > SIZE_MAX / 2)
				return -1;
			cap *= 2;
		}
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
	return ctx_render_changed(ctx, 0, out, out_len);
}

int fyts_ctx_finish(struct fyts_ctx *ctx, char **out, size_t *out_len)
{
	if (!ctx)
		return -1;
	return ctx_render_changed(ctx, 1, out, out_len);
}
