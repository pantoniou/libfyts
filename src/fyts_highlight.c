#include <stdint.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fyts/fyts.h>

static int terminal_wrap_disabled;

static void restore_terminal_wrap(void)
{
	if (!terminal_wrap_disabled)
		return;
	fputs("\033[?7h", stdout);
	fflush(stdout);
	terminal_wrap_disabled = 0;
}

static void disable_terminal_wrap(void)
{
	if (terminal_wrap_disabled || !isatty(STDOUT_FILENO))
		return;
	fputs("\033[?7l", stdout);
	fflush(stdout);
	terminal_wrap_disabled = 1;
	atexit(restore_terminal_wrap);
}

static void usage(FILE *out)
{
	fprintf(out, "usage: fyts-highlight [-b auto|dark|light] [-c auto|off|on] [-l language] "
		     "[--list-languages] [--output-catalog] [--output-styling] [-q file.scm] "
		     "[-s file.yaml] [-w auto|0|columns] [--prolog text] [--epilog text] "
		     "[--line-prefix text] [--line-suffix text] [--debug-captures] "
		     "[--report-unmatched-captures] [--reverse] [--stream] "
		     "<source>\n");
}

static int parse_color_mode(const char *arg, enum fyts_color_mode *mode)
{
	if (strcmp(arg, "auto") == 0) {
		*mode = FYTS_COLOR_AUTO;
		return 1;
	}
	if (strcmp(arg, "off") == 0) {
		*mode = FYTS_COLOR_OFF;
		return 1;
	}
	if (strcmp(arg, "on") == 0) {
		*mode = FYTS_COLOR_ON;
		return 1;
	}
	return 0;
}

static int parse_background_mode(const char *arg, enum fyts_background_mode *mode)
{
	if (strcmp(arg, "auto") == 0) {
		*mode = FYTS_BACKGROUND_AUTO;
		return 1;
	}
	if (strcmp(arg, "dark") == 0) {
		*mode = FYTS_BACKGROUND_DARK;
		return 1;
	}
	if (strcmp(arg, "light") == 0) {
		*mode = FYTS_BACKGROUND_LIGHT;
		return 1;
	}
	return 0;
}

static int parse_width(const char *arg, int *width)
{
	char *end;
	long value;

	if (strcmp(arg, "auto") == 0) {
		*width = 0;
		return 1;
	}

	value = strtol(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || value < 0 || value > INT32_MAX)
		return 0;
	*width = (int)value;
	return 1;
}

static int terminal_width(void)
{
	struct winsize ws;

	if (!isatty(STDOUT_FILENO))
		return 0;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
		return 0;
	return ws.ws_col > 0 ? ws.ws_col : 0;
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

static int output_buffer(char *data, size_t len)
{
	int rc;

	if (!len) {
		free(data);
		return 0;
	}
	if (!data)
		return 1;
	rc = fyts_write_file(data, len, stdout);
	free(data);
	return rc ? 1 : 0;
}

static int output_chunk(char **data, size_t len)
{
	int rc;

	rc = output_buffer(*data, len);
	*data = NULL;
	return rc;
}

static int stream_file(const char *path, const struct fyts_config *config)
{
	FILE *file;
	struct fyts_ctx *ctx;
	char input[4096];
	char *output = NULL;
	size_t output_len = 0;
	size_t len;
	int rc = 1;

	file = fopen(path, "rb");
	if (!file) {
		perror(path);
		return 1;
	}

	ctx = fyts_ctx_create(config);
	if (!ctx)
		goto done;

	for (;;) {
		len = fread(input, 1, sizeof(input), file);
		if (len > 0) {
			if (fyts_ctx_feed(ctx, input, len, &output, &output_len))
				goto done;
			if (output_chunk(&output, output_len))
				goto done;
		}
		if (len < sizeof(input)) {
			if (ferror(file)) {
				perror(path);
				goto done;
			}
			break;
		}
	}

	if (fyts_ctx_finish(ctx, &output, &output_len))
		goto done;
	if (output_chunk(&output, output_len))
		goto done;
	rc = 0;

done:
	free(output);
	fyts_ctx_destroy(ctx);
	fclose(file);
	return rc;
}

int main(int argc, char **argv)
{
	enum {
		OPT_OUTPUT_STYLING = 256,
		OPT_STREAM,
		OPT_PROLOG,
		OPT_EPILOG,
		OPT_LINE_PREFIX,
		OPT_LINE_SUFFIX,
		OPT_REVERSE,
		OPT_DEBUG_CAPTURES,
		OPT_REPORT_UNMATCHED_CAPTURES,
	};
	static const struct option options[] = {
	    {"background", required_argument, NULL, 'b'},
	    {"color", required_argument, NULL, 'c'},
	    {"lang", required_argument, NULL, 'l'},
	    {"query", required_argument, NULL, 'q'},
	    {"width", required_argument, NULL, 'w'},
	    {"style", required_argument, NULL, 's'},
	    {"list-languages", no_argument, NULL, 'L'},
	    {"output-catalog", no_argument, NULL, 'o'},
	    {"output-styling", no_argument, NULL, OPT_OUTPUT_STYLING},
	    {"stream", no_argument, NULL, OPT_STREAM},
	    {"prolog", required_argument, NULL, OPT_PROLOG},
	    {"epilog", required_argument, NULL, OPT_EPILOG},
	    {"line-prefix", required_argument, NULL, OPT_LINE_PREFIX},
	    {"line-suffix", required_argument, NULL, OPT_LINE_SUFFIX},
	    {"debug-captures", no_argument, NULL, OPT_DEBUG_CAPTURES},
	    {"report-unmatched-captures", no_argument, NULL, OPT_REPORT_UNMATCHED_CAPTURES},
	    {"reverse", no_argument, NULL, OPT_REVERSE},
	    {"help", no_argument, NULL, 'h'},
	    {NULL, 0, NULL, 0},
	};
	struct fyts_config config = {0};
	const char *source_path = NULL;
	char *detected_lang_name = NULL;
	char *output = NULL;
	char *source = NULL;
	size_t source_len = 0;
	size_t output_len = 0;
	int should_list_languages = 0;
	int should_output_catalogue = 0;
	int should_output_styling = 0;
	int should_stream = 0;
	int requested_width = -1;
	int opt;
	int rc;

	config.color_mode = FYTS_COLOR_AUTO;
	config.write = fyts_write_file;
	config.write_user = stdout;

	while ((opt = getopt_long(argc, argv, "b:c:l:q:s:w:Loh", options, NULL)) != -1) {
		switch (opt) {
		case 'b':
			if (!parse_background_mode(optarg, &config.background_mode)) {
				fprintf(stderr, "invalid background mode: %s\n", optarg);
				usage(stderr);
				return 2;
			}
			break;
		case 'c':
			if (!parse_color_mode(optarg, &config.color_mode)) {
				fprintf(stderr, "invalid color mode: %s\n", optarg);
				usage(stderr);
				return 2;
			}
			break;
		case 'l':
			config.lang = optarg;
			break;
		case 'q':
			config.query_path = optarg;
			break;
		case 's':
			config.styling_path = optarg;
			break;
		case 'w':
			if (!parse_width(optarg, &requested_width)) {
				fprintf(stderr, "invalid width: %s\n", optarg);
				usage(stderr);
				return 2;
			}
			break;
		case 'L':
			should_list_languages = 1;
			break;
		case 'o':
			should_output_catalogue = 1;
			break;
		case OPT_OUTPUT_STYLING:
			should_output_styling = 1;
			break;
		case OPT_STREAM:
			should_stream = 1;
			break;
		case OPT_PROLOG:
			config.prolog = optarg;
			break;
		case OPT_EPILOG:
			config.epilog = optarg;
			break;
		case OPT_LINE_PREFIX:
			config.line_prefix = optarg;
			break;
		case OPT_LINE_SUFFIX:
			config.line_suffix = optarg;
			break;
		case OPT_DEBUG_CAPTURES:
			config.debug_captures = 1;
			break;
		case OPT_REPORT_UNMATCHED_CAPTURES:
			config.report_unmatched_captures = 1;
			break;
		case OPT_REVERSE:
			config.reverse = 1;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}

	if (optind < argc)
		source_path = argv[optind++];
	if (optind < argc) {
		usage(stderr);
		return 2;
	}

	if (should_list_languages) {
		output = fyts_list_languages(&output_len);
		return output_buffer(output, output_len);
	}
	if (should_output_catalogue) {
		output = fyts_output_catalogue(&output_len);
		return output_buffer(output, output_len);
	}
	if (should_output_styling) {
		output = fyts_output_styling(&output_len);
		return output_buffer(output, output_len);
	}

	if (!source_path) {
		usage(stderr);
		return 2;
	}

	if (requested_width == 0)
		config.width = terminal_width();
	else if (requested_width > 0)
		config.width = requested_width;

	if (!config.lang) {
		detected_lang_name = fyts_detect_language_for_path(source_path);
		config.lang = detected_lang_name;
		if (!config.lang) {
			fprintf(stderr, "could not detect language for %s; pass --lang\n",
				source_path);
			return 2;
		}
	}

	if (config.color_mode != FYTS_COLOR_OFF)
		disable_terminal_wrap();

	if (should_stream) {
		rc = stream_file(source_path, &config);
	} else {
		source = read_file(source_path, &source_len);
		if (!source) {
			free(detected_lang_name);
			return 1;
		}
		rc = fyts_highlight_source(&config, source, source_len);
	}

	free(source);
	free(detected_lang_name);
	return rc ? 1 : 0;
}
