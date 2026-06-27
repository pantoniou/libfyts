#ifndef FYTS_H
#define FYTS_H

#include <stddef.h>
#include <stdio.h>

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

struct fyts_config {
	const char *lang;
	const char *query_path;
	const char *styling_path;
	enum fyts_color_mode color_mode;
	int width;
	fyts_write_fn write;
	void *write_user;
};

int fyts_write_file(const void *data, size_t len, void *user);

struct fyts_ctx *fyts_ctx_create(const struct fyts_config *config);
void fyts_ctx_destroy(struct fyts_ctx *ctx);

int fyts_highlight_source(const struct fyts_config *config, const char *source, size_t len);
int fyts_ctx_feed(struct fyts_ctx *ctx, const char *data, size_t len);
int fyts_ctx_finish(struct fyts_ctx *ctx);

char *fyts_list_languages(size_t *lenp);
char *fyts_output_catalogue(size_t *lenp);
char *fyts_output_styling(size_t *lenp);
char *fyts_detect_language_for_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif
