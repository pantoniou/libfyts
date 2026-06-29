#ifndef FYTS_H
#define FYTS_H

#include <stddef.h>
#include <stdio.h>
#include <libfyaml/libfyaml-generic.h>

#ifndef FYTS_EXPORT
#if defined(__GNUC__) && __GNUC__ >= 4
#define FYTS_EXPORT __attribute__((visibility("default")))
#else
#define FYTS_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct fyts_ctx;

typedef int (*fyts_write_fn)(const void *data, size_t len, void *user);

enum fyts_color_mode {
	FYTS_COLOR_AUTO,
	FYTS_COLOR_OFF,
	FYTS_COLOR_ON,
};

enum fyts_background_mode {
	FYTS_BACKGROUND_AUTO,
	FYTS_BACKGROUND_DARK,
	FYTS_BACKGROUND_LIGHT,
};

struct fyts_config {
	const char *lang;
	const char *query_path;
	const char *styling_path;
	fy_generic styling;
	enum fyts_color_mode color_mode;
	enum fyts_background_mode background_mode;
	int reverse;
	int debug_captures;
	int report_unmatched_captures;
	int width;
	const char *prolog;
	const char *epilog;
	const char *line_prefix;
	const char *line_suffix;
	fyts_write_fn write;
	void *write_user;
};

FYTS_EXPORT int fyts_write_file(const void *data, size_t len, void *user);

FYTS_EXPORT struct fyts_ctx *fyts_ctx_create(const struct fyts_config *config);
FYTS_EXPORT void fyts_ctx_destroy(struct fyts_ctx *ctx);

FYTS_EXPORT int fyts_highlight_source(const struct fyts_config *config, const char *source, size_t len);
FYTS_EXPORT int fyts_ctx_feed(struct fyts_ctx *ctx, const char *data, size_t len, char **out, size_t *out_len);
FYTS_EXPORT int fyts_ctx_finish(struct fyts_ctx *ctx, char **out, size_t *out_len);

FYTS_EXPORT char *fyts_list_languages(size_t *lenp);
FYTS_EXPORT int fyts_language_supported(const char *lang);
FYTS_EXPORT char *fyts_output_catalogue(size_t *lenp);
FYTS_EXPORT char *fyts_output_styling(size_t *lenp);
FYTS_EXPORT char *fyts_detect_language_for_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif
