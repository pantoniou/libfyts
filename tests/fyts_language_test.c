#include <fyts/fyts.h>

int main(void)
{
	if (!fyts_language_supported("c"))
		return 1;
	if (!fyts_language_supported("python"))
		return 1;
	if (!fyts_language_supported("sh"))
		return 1;
	if (!fyts_language_supported("c++"))
		return 1;
#ifndef FYTS_TEST_MINIMAL_CATALOGUE
	/* csharp/javascript are not part of the minimal catalogue */
	if (!fyts_language_supported("c#"))
		return 1;
	if (!fyts_language_supported("jsx"))
		return 1;
#endif
	if (!fyts_language_supported("yml"))
		return 1;
	if (fyts_language_supported("not-a-language"))
		return 1;
	if (fyts_language_supported(""))
		return 1;
	if (fyts_language_supported(NULL))
		return 1;
	if (!fyts_language_progressive_safe("c"))
		return 1;
	if (!fyts_language_progressive_safe("sh"))
		return 1;
	if (fyts_language_progressive_safe("markdown"))
		return 1;
	if (fyts_language_progressive_safe("not-a-language"))
		return 1;
	return 0;
}
